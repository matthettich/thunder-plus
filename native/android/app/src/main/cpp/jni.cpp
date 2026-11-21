// Android glue: miniaudio (AAudio) opens the output and calls thunder_process; JNI entry points for Kotlin.
#include <jni.h>
#include <android/log.h>
#include <dlfcn.h>
#include <time.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#include "miniaudio.h"
#include "thunder/core.h"
#include "thunder/rtlog.h"
#include "midi_out.h"   // U36 T9: the MIDI out (the AMidi ports and the JNI entry points are in midi_jni.cpp)
#if defined(__x86_64__)
#include <xmmintrin.h>
#endif

#define LOG_TAG "ThunderCore"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

namespace {

constexpr int kMaxBlock = 4096;

struct Engine {
  thunder_core* core = nullptr;
  ma_device device;
  bool deviceInit = false;
  float l[kMaxBlock], r[kMaxBlock];   // de-interleave scratch, owned by the callback
  std::atomic<bool> probeRun{false};
  std::thread probe;
  char info[256] = {0};
  std::atomic<bool> infoReady{false};
  std::atomic<bool> resetReq{false};      // the callback performs rtlog_reset (the logger is written by the audio thread only)
  std::atomic<int> xrunLast{0}, xrunBase{0}, latFrames{0};
  // U36 T9: MIDI out. `midi` is set before the stream starts and cleared after it stopped, so the callback reads it plainly. libaaudio's two counters, resolved BEFORE the stream starts
  // (the callback calls them through these pointers; no dlsym in the callback).
  tpa::MidiHost* midi = nullptr;
  void* aaLib = nullptr;
  int32_t (*aaTs)(void*, int32_t, int64_t*, int64_t*) = nullptr;
  int64_t (*aaWritten)(void*) = nullptr;
  int lastLatSentMs = -1;
  // The AAudio timestamp is read by ONE thread (AAudio does not list getTimestamp among its thread-safe calls): while MIDI out runs it is the audio callback (tsRead, every 20 ms),
  // and probeLoop reads this snapshot instead. A seqlock: tsSeq odd = being written. pos / ns / written as tsRead got them, tsMono = our clock at that moment.
  std::atomic<uint32_t> tsSeq{0};
  std::atomic<int64_t> tsPos{0}, tsNs{0}, tsWritten{0}, tsMono{0};
};
Engine g;
rtlog g_rt;   // A5's clock-drift logger (native/core/include/thunder/rtlog.h)

inline int64_t monoNs() { timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (int64_t)t.tv_sec * 1000000000LL + t.tv_nsec; }

// Denormals: on arm64 FPCR.FZ (bit 24) flushes denormal inputs and results to zero for this thread; x86_64 gets FTZ + DAZ.
// Set at the top of every callback (the audio thread is AAudio's, not ours, and this is cheap). Does not affect other threads.
inline void setFlushToZero() {
#if defined(__aarch64__)
  uint64_t fpcr; asm volatile("mrs %0, fpcr" : "=r"(fpcr)); fpcr |= (1ull << 24); asm volatile("msr fpcr, %0" : : "r"(fpcr));
#elif defined(__x86_64__)
  _mm_setcsr(_mm_getcsr() | 0x8040);
#endif
}

// THE AUDIO CALLBACK (reviewed by the lead, 15 Nov). Only: flush-to-zero, the logger's begin / end, thunder_process, interleave.
// No allocation (scratch is in the Engine, fixed size), no locks, no logging, no JNI. A block larger than kMaxBlock is
// rendered in pieces.
void dataCallback(ma_device* dev, void* out, const void* /*in*/, ma_uint32 frames) {
  const int64_t cbStart = monoNs();   // the clock map's host time is the callback's start (spec 3.4)
  Engine* e = static_cast<Engine*>(dev->pUserData);
  float* o = static_cast<float*>(out);
  setFlushToZero();
  if (e->resetReq.load(std::memory_order_acquire)) { rtlog_reset(&g_rt, (int)dev->sampleRate); e->resetReq.store(false, std::memory_order_release); }
  rtlog_begin(&g_rt, monoNs());
  ma_uint32 done = 0;
  while (done < frames) {
    ma_uint32 n = frames - done;
    if (n > (ma_uint32)kMaxBlock) n = (ma_uint32)kMaxBlock;
    thunder_process(e->core, e->l, e->r, (int)n);
    for (ma_uint32 i = 0; i < n; i++) {
      o[2 * (done + i)] = e->l[i];
      o[2 * (done + i) + 1] = e->r[i];
    }
    done += n;
  }
  if (e->midi) e->midi->audioBlock((int)frames, cbStart);   // U36 T9: the scheduler's block and the clock map, after the block (midi_out.h AUDIO)
  rtlog_end(&g_rt, monoNs(), (int)frames);
}

// AAudio's own counters (not exposed by miniaudio): the stream pointer is miniaudio's, the functions come from libaaudio.
typedef int32_t (*fnI32)(void*);
typedef int64_t (*fnI64)(void*);
typedef int32_t (*fnTs)(void*, int32_t, int64_t*, int64_t*);

// The clock map's AAudio pair (spec 3.4). Called by MidiHost::audioBlock on the audio thread at most every 20 ms. AAudio's getTimestamp and getFramesWritten read the stream's
// shared counters (UNVERIFIED on the Fold: that getTimestamp is cheap in the callback; the rate limit is the guard). false: no timestamp (AAudio refuses for a while after start).
bool tsRead(void*, tpa::TsPair* p) {
  void* stream = (void*)g.device.aaudio.pStreamPlayback;
  if (!stream || !g.aaTs || !g.aaWritten) return false;
  int64_t pos = 0, ns = 0;
  if (g.aaTs(stream, CLOCK_MONOTONIC, &pos, &ns) != 0) return false;
  p->pos = pos; p->ns = ns; p->written = g.aaWritten(stream);
  const uint32_t q = g.tsSeq.load(std::memory_order_relaxed);
  g.tsSeq.store(q + 1, std::memory_order_release);
  g.tsPos.store(p->pos, std::memory_order_release); g.tsNs.store(p->ns, std::memory_order_release); g.tsWritten.store(p->written, std::memory_order_release); g.tsMono.store(monoNs(), std::memory_order_release);
  g.tsSeq.store(q + 2, std::memory_order_release);
  return true;
}

void probeLoop() {
  void* lib = dlopen("libaaudio.so", RTLD_NOW);
  if (!lib) return;
  auto xrun = (fnI32)dlsym(lib, "AAudioStream_getXRunCount");
  auto written = (fnI64)dlsym(lib, "AAudioStream_getFramesWritten");
  auto ts = (fnTs)dlsym(lib, "AAudioStream_getTimestamp");
  auto burst = (fnI32)dlsym(lib, "AAudioStream_getFramesPerBurst");
  auto bufsz = (fnI32)dlsym(lib, "AAudioStream_getBufferSizeInFrames");
  auto sharing = (fnI32)dlsym(lib, "AAudioStream_getSharingMode");
  auto perf = (fnI32)dlsym(lib, "AAudioStream_getPerformanceMode");
  bool infoDone = false;
  while (g.probeRun.load()) {
    void* stream = (void*)g.device.aaudio.pStreamPlayback;
    if (stream && g.core) {
      int under = xrun ? xrun(stream) : 0;
      if (under < 0) under = 0;
      g.xrunLast = under;
      rtlog_host_underruns(&g_rt, under - g.xrunBase.load());
      int latFrames = 0;
      int64_t pos = 0, tns = 0;
      bool have = false; int64_t wr = 0;
      if (g.midi) {   // the callback owns getTimestamp: read its snapshot (up to 20 ms old; the written count is carried forward at the stream's rate)
        for (int tries = 0; tries < 4 && !have; tries++) {
          const uint32_t a = g.tsSeq.load(std::memory_order_acquire);
          if (a == 0 || (a & 1)) continue;
          pos = g.tsPos.load(std::memory_order_acquire); tns = g.tsNs.load(std::memory_order_acquire); const int64_t w = g.tsWritten.load(std::memory_order_acquire), m = g.tsMono.load(std::memory_order_acquire);
          if (g.tsSeq.load(std::memory_order_acquire) != a) continue;
          wr = w + (int64_t)((double)(monoNs() - m) * 1e-9 * g.device.sampleRate); have = true;
        }
      } else if (ts && written && ts(stream, CLOCK_MONOTONIC, &pos, &tns) == 0) { wr = written(stream); have = true; }   // no MIDI out: this thread is the only reader
      if (have) {
        const int64_t now = monoNs();
        // frames still to be played = written - (position presented at `tns`, advanced to now)
        double presented = (double)pos + (double)(now - tns) * 1e-9 * g.device.sampleRate;
        double lat = (double)wr - presented;
        if (lat > 0 && lat < g.device.sampleRate) latFrames = (int)lat;
      }
      g.latFrames = latFrames;
      if (latFrames > 0 && g.device.sampleRate > 0) {   // the clock map's floor (set_out_latency); a control call, serialised inside MidiHost
        const int ms = (int)(latFrames * 1000.0 / g.device.sampleRate);
        if (ms != g.lastLatSentMs) { g.lastLatSentMs = ms; tpa::host().setOutLatencyNs((int64_t)(latFrames * 1e9 / g.device.sampleRate)); }
      }
      thunder_host_stats(g.core, latFrames, under);
      if (!infoDone && burst && bufsz) {
        std::snprintf(g.info, sizeof g.info, "sr=%u burstFrames=%d bufferFrames=%d sharing=%d (0 exclusive, 1 shared) perfMode=%d (12 low latency)", g.device.sampleRate,
                      burst(stream), bufsz(stream), sharing ? sharing(stream) : -1, perf ? perf(stream) : -1);
        g.infoReady = true; infoDone = true;
      }
    }
    for (int i = 0; i < 10 && g.probeRun.load(); i++) { timespec s{0, 10 * 1000 * 1000}; nanosleep(&s, nullptr); }
  }
  dlclose(lib);
}

}  // namespace

extern "C" {

JNIEXPORT jint JNICALL Java_app_thunderplus_nativetest_NativeEngine_nativeStart(JNIEnv*, jobject) {
  if (g.deviceInit) return (jint)g.device.sampleRate;
  ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
  cfg.playback.format = ma_format_f32;
  cfg.playback.channels = 2;
  cfg.sampleRate = 0;   // the device's native rate
  cfg.dataCallback = dataCallback;
  cfg.pUserData = &g;
  cfg.performanceProfile = ma_performance_profile_low_latency;
  cfg.aaudio.usage = ma_aaudio_usage_media;
  if (ma_device_init(nullptr, &cfg, &g.device) != MA_SUCCESS) { LOGI("ma_device_init failed"); return -1; }
  g.deviceInit = true;
  g.core = thunder_create((int)g.device.sampleRate, kMaxBlock);
  if (!g.core) { ma_device_uninit(&g.device); g.deviceInit = false; return -1; }
  g.infoReady = false;
  // U36 T9: AAudio's counters for the clock map, then the MIDI out (a failure here is not fatal: audio goes on without MIDI)
  g.aaLib = dlopen("libaaudio.so", RTLD_NOW);
  if (g.aaLib) { g.aaTs = (int32_t(*)(void*, int32_t, int64_t*, int64_t*))dlsym(g.aaLib, "AAudioStream_getTimestamp"); g.aaWritten = (int64_t(*)(void*))dlsym(g.aaLib, "AAudioStream_getFramesWritten"); }
  g.lastLatSentMs = -1;
  if (tpa::host().engineStart((double)g.device.sampleRate, &tsRead, nullptr)) g.midi = &tpa::host(); else LOGI("MIDI out did not start");
  rtlog_reset(&g_rt, (int)g.device.sampleRate);
  g.resetReq = false; g.xrunBase = 0; g.xrunLast = 0; g.latFrames = 0;
  g.probeRun = true;
  g.probe = std::thread(probeLoop);
  if (ma_device_start(&g.device) != MA_SUCCESS) {
    g.probeRun = false; g.probe.join();
    ma_device_uninit(&g.device); g.deviceInit = false; g.midi = nullptr; tpa::host().engineStop(); if (g.aaLib) { dlclose(g.aaLib); g.aaLib = nullptr; }
    thunder_destroy(g.core); g.core = nullptr; return -1;
  }
  LOGI("audio started at %u Hz", g.device.sampleRate);
  return (jint)g.device.sampleRate;
}

JNIEXPORT void JNICALL Java_app_thunderplus_nativetest_NativeEngine_nativeStop(JNIEnv*, jobject) {
  if (!g.deviceInit) return;
  // N1 review A4 (UNVERIFIED at run time: no Android here): the probe thread reads the AAudio stream, so it is stopped and joined
  // BEFORE ma_device_uninit closes that stream (the old order let it call into a closed stream for up to 10 ms). Then the
  // callback is stopped, and only then is the core destroyed.
  g.probeRun = false;
  if (g.probe.joinable()) g.probe.join();
  ma_device_uninit(&g.device);   // stops the callback
  g.deviceInit = false;
  g.midi = nullptr;
  tpa::host().engineStop();      // U36 T9: AFTER the callback stopped (no more clock_map / process): joins the sender, frees thunder_midiout, then thunder_midi
  if (g.aaLib) { dlclose(g.aaLib); g.aaLib = nullptr; g.aaTs = nullptr; g.aaWritten = nullptr; }
  thunder_destroy(g.core);
  g.core = nullptr;
}

// N1 review B2 (UNVERIFIED at run time: no Android here). The command arrives as a UTF-8 BYTE ARRAY (Kotlin: String.toByteArray(UTF_8)),
// not a jstring: GetStringUTFChars gives MODIFIED UTF-8 (a character above U+FFFF as two 3-byte surrogates, U+0000 as C0 80), which the
// core's strict reader refuses, so a song with an emoji in a name did not load. Bytes also carry their own length (no strlen, no int cast
// of a size_t). Copied out (not pinned): a song.load parses for a while and a pinned array stops the Java GC.
JNIEXPORT jboolean JNICALL Java_app_thunderplus_nativetest_NativeEngine_nativePostBytes(JNIEnv* env, jobject, jbyteArray bytes) {
  if (!g.core || !bytes) return JNI_FALSE;
  const jsize n = env->GetArrayLength(bytes);
  if (n <= 0) return JNI_FALSE;
  std::vector<char> buf((size_t)n);
  env->GetByteArrayRegion(bytes, 0, n, reinterpret_cast<jbyte*>(buf.data()));
  if (env->ExceptionCheck()) { env->ExceptionClear(); return JNI_FALSE; }
  return thunder_post(g.core, buf.data(), (int)n) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL Java_app_thunderplus_nativetest_NativeEngine_nativePutBytes(JNIEnv* env, jobject, jint id, jstring meta, jbyteArray bytes, jint off, jint len) {
  // N1 review A2: the range is checked against the Java array here (off + len in 64 bits), and a null meta or a failed
  // GetStringUTFChars is refused instead of dereferenced. UNVERIFIED at run time: no Android here.
  if (!g.core || !bytes || !meta || off < 0 || len < 0 || (len & 3) || (off & 3)) return JNI_FALSE;
  if ((jlong)off + (jlong)len > (jlong)env->GetArrayLength(bytes)) return JNI_FALSE;
  const char* m = env->GetStringUTFChars(meta, nullptr);
  if (!m) return JNI_FALSE;
  void* p = env->GetPrimitiveArrayCritical(bytes, nullptr);
  int ok = p ? thunder_put(g.core, id, m, reinterpret_cast<const float*>(static_cast<char*>(p) + off), len / 4) : 0;
  if (p) env->ReleasePrimitiveArrayCritical(bytes, p, JNI_ABORT);
  env->ReleaseStringUTFChars(meta, m);
  return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL Java_app_thunderplus_nativetest_NativeEngine_nativePutInfo(JNIEnv* env, jobject, jint id) {
  if (!g.core) return nullptr;
  int64_t count = 0; double sum = 0;
  if (!thunder_put_info(g.core, id, &count, &sum)) return nullptr;
  char b[96]; std::snprintf(b, sizeof b, "%lld,%.17g", (long long)count, sum);
  return env->NewStringUTF(b);
}

JNIEXPORT void JNICALL Java_app_thunderplus_nativetest_NativeEngine_nativeRtReset(JNIEnv*, jobject) {
  g.xrunBase = g.xrunLast.load();
  if (g.deviceInit) g.resetReq.store(true, std::memory_order_release);   // the callback does it; a stopped engine is reset at its next start
  else { rtlog_reset(&g_rt, g_rt.sample_rate ? g_rt.sample_rate : 48000); }
}

JNIEXPORT jstring JNICALL Java_app_thunderplus_nativetest_NativeEngine_nativeRtSnapshot(JNIEnv* env, jobject) {
  char b[400];
  if (g.resetReq.load(std::memory_order_acquire)) {
    std::snprintf(b, sizeof b, "{\"pending\":1,\"sr\":%d}", g_rt.sample_rate);
    return env->NewStringUTF(b);
  }
  rtlog_snapshot sn = rtlog_read(&g_rt);
  const int sr = g_rt.sample_rate;
  std::snprintf(b, sizeof b,
    "{\"pending\":0,\"sr\":%d,\"frames\":%lld,\"calls\":%lld,\"late\":%lld,\"under\":%lld,\"audioS\":%.6f,\"wallS\":%.6f,"
    "\"ratioPpm\":%.2f,\"maxGapMs\":%.3f,\"maxWorkMs\":%.3f,\"pass\":%d,\"outLatMs\":%.2f}",
    sr, (long long)sn.frames, (long long)sn.calls, (long long)sn.late, (long long)sn.underruns, sn.audio_s, sn.wall_s,
    sn.ratio_ppm, sn.max_gap_ms, sn.max_work_ms, sn.pass, sr > 0 ? g.latFrames.load() * 1000.0 / sr : 0.0);
  return env->NewStringUTF(b);
}

JNIEXPORT jstring JNICALL Java_app_thunderplus_nativetest_NativeEngine_nativeInfo(JNIEnv* env, jobject) {
  return env->NewStringUTF(g.infoReady.load() ? g.info : "");
}

}  // extern "C"
