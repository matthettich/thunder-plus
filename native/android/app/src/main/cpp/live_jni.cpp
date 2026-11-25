// N2 H1a: the Android glue of the LIVE host: miniaudio (AAudio) opens the output and calls tpa::LiveEngine::render; JNI entry points for Kotlin's LiveNative (LiveNative.kt).
// Beside the N0 glue (jni.cpp, its own device and its own thunder_core): the two are never started together (Kotlin stops the one before starting the other). Everything with a decision in it
// is in live_engine.cpp / pad_filler.cpp, which a Linux test runs; this file is the part only a phone can run (UNVERIFIED on a device: CI compiles it).
#include <android/log.h>
#include <dlfcn.h>
#include <jni.h>
#include <time.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "engine_slot.h"
#include "live_engine.h"
#include "miniaudio.h"
#include "pad_filler.h"
#include "rt/json.h"
#include "thunder/rtlog.h"
#ifdef THUNDER_HAVE_LXR
#include "thunder/lxr_module.h"   // the optional drum module (native/lxr): registered once at start
#endif

#define LOG_TAG "ThunderLive"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

namespace {

struct Live {
  tpa::Slot<tpa::LiveEngine> eng;                    // ANY thread reads it through get() (a copy for the whole call); only start / shutdown (g_life) replace it
  std::atomic<tpa::LiveEngine*> audioEng{nullptr};   // the audio callback's raw pointer: set before the stream starts, cleared after it stopped (the callback takes no lock and copies no shared_ptr)
  ma_device device;
  std::atomic<bool> deviceInit{false};
  void* aaLib = nullptr;
  int32_t (*aaXrun)(void*) = nullptr;
  int32_t (*aaBuf)(void*) = nullptr;
  int64_t rev = 0;
  std::vector<int> lastSlots;   // pads the previous project filled (to remove the ones a reload does not fill)
};
Live g;
std::mutex g_dev;    // the ma_device fields other threads read (sample rate, the AAudio stream pointer): held briefly by readers, and by start / shutdown around init / uninit
std::mutex g_life;   // start / shutdown / load: one at a time (the JNI threads are Kotlin's; the audio thread never takes it)
rtlog g_lrt;         // A5's clock-drift logger on the live callback

inline int64_t monoNs() { timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (int64_t)t.tv_sec * 1000000000LL + t.tv_nsec; }

// THE AUDIO CALLBACK: the logger, then LiveEngine::render (flush-to-zero, thunder_live_process, interleave). No allocation, no lock, no JNI, no log.
void dataCallback(ma_device* dev, void* out, const void*, ma_uint32 frames) {
  const int64_t t0 = monoNs();
  tpa::LiveEngine* e = static_cast<Live*>(dev->pUserData)->audioEng.load(std::memory_order_acquire);
  if (!e) { std::memset(out, 0, sizeof(float) * 2 * (size_t)frames); return; }
  rtlog_begin(&g_lrt, t0);
  e->render(static_cast<float*>(out), (int)frames, t0);
  rtlog_end(&g_lrt, monoNs(), (int)frames);
}

std::string jsonEscape(const std::string& s) { std::string o; for (char c : s) { if (c == '"' || c == '\\') { o += '\\'; o += c; } else if ((unsigned char)c < 0x20) o += ' '; else o += c; } return o; }

}  // namespace

extern "C" {

JNIEXPORT jint JNICALL Java_app_thunderplus_nativetest_LiveNative_nativeCaps(JNIEnv*, jobject) { return (jint)thunder_live_caps(); }

// Opens the output, creates the live host at the stream's rate, starts the control and poller threads, then the stream. Returns the sample rate, or a negative code.
JNIEXPORT jint JNICALL Java_app_thunderplus_nativetest_LiveNative_nativeStart(JNIEnv*, jobject, jboolean chains) {
  std::lock_guard<std::mutex> lk(g_life);
  if (g.deviceInit) return (jint)g.device.sampleRate;
#ifdef THUNDER_HAVE_LXR
  thunder_lxr_module_register();   // idempotent registry of one pointer (lxr_seam.h); pads of the LXR engines are rendered through it
#endif
  ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
  cfg.playback.format = ma_format_f32;
  cfg.playback.channels = 2;
  cfg.sampleRate = 0;   // the device's native rate
  cfg.dataCallback = dataCallback;
  cfg.pUserData = &g;
  cfg.performanceProfile = ma_performance_profile_low_latency;
  cfg.aaudio.usage = ma_aaudio_usage_media;
  auto eng = std::make_shared<tpa::LiveEngine>();
  { std::lock_guard<std::mutex> dl(g_dev);
    if (ma_device_init(nullptr, &cfg, &g.device) != MA_SUCCESS) { LOGI("ma_device_init failed"); return -1; }
    g.deviceInit = true; }
  tpa::LiveEngine::Config ec; ec.sampleRate = (double)g.device.sampleRate; ec.maxBlock = 4096; ec.chains = chains == JNI_TRUE;
  std::string err;
  if (!eng->create(ec, &err)) { LOGI("live create failed: %s", err.c_str()); { std::lock_guard<std::mutex> dl(g_dev); ma_device_uninit(&g.device); g.deviceInit = false; } return -2; }
  eng->startThreads();
  g.aaLib = dlopen("libaaudio.so", RTLD_NOW);   // resolved before the stream runs: the status call reads the xrun count and buffer size through these
  if (g.aaLib) { g.aaXrun = (int32_t(*)(void*))dlsym(g.aaLib, "AAudioStream_getXRunCount"); g.aaBuf = (int32_t(*)(void*))dlsym(g.aaLib, "AAudioStream_getBufferSizeInFrames"); }
  rtlog_reset(&g_lrt, (int)g.device.sampleRate);
  g.eng.set(eng);
  g.audioEng.store(eng.get(), std::memory_order_release);   // before the stream starts: the callback never sees a half-made engine
  int started;
  { std::lock_guard<std::mutex> dl(g_dev); started = ma_device_start(&g.device); }
  if (started != MA_SUCCESS) {
    g.audioEng.store(nullptr, std::memory_order_release);
    g.eng.take();
    eng->destroy();
    { std::lock_guard<std::mutex> dl(g_dev); ma_device_uninit(&g.device); g.deviceInit = false; }
    if (g.aaLib) { dlclose(g.aaLib); g.aaLib = nullptr; }
    return -3;
  }
  LOGI("live engine started at %u Hz", g.device.sampleRate);
  return (jint)g.device.sampleRate;
}

// The destroy order (live.h destroy, N0's nativeStop): stop the transport, let two callbacks run it, stop the stream (no more process), join the threads, destroy.
// The slot is emptied FIRST: from then on no new JNI call gets the engine; a call already inside it holds its own copy, and destroy() below waits for / refuses it (live_engine.h).
JNIEXPORT void JNICALL Java_app_thunderplus_nativetest_LiveNative_nativeShutdown(JNIEnv*, jobject) {
  std::lock_guard<std::mutex> lk(g_life);
  if (!g.deviceInit) return;
  std::shared_ptr<tpa::LiveEngine> eng = g.eng.take();
  if (eng) {
    eng->call([](thunder_live* L) { return thunder_live_stop(L, THUNDER_LIVE_NOW); }, 500);
    const int64_t c0 = eng->audioCalls();
    for (int i = 0; i < 100 && eng->audioCalls() < c0 + 2; i++) std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  { std::lock_guard<std::mutex> dl(g_dev); ma_device_uninit(&g.device); g.deviceInit = false; }   // stops the callback
  g.audioEng.store(nullptr, std::memory_order_release);
  if (eng) eng->destroy();       // joins the control and poller threads, then thunder_live_destroy
  eng.reset();
  g.lastSlots.clear();
  if (g.aaLib) { dlclose(g.aaLib); g.aaLib = nullptr; g.aaXrun = nullptr; g.aaBuf = nullptr; }
}

// A .thunder project's bytes (UTF-8): the song and the kit. The pads are rendered HERE (the caller's thread, not the control thread, not the audio thread), then ONE control call publishes the
// song and the pads. Returns a JSON object {ok, rc, err, pads, skipped, dry, renderMs, notes[]}.
JNIEXPORT jstring JNICALL Java_app_thunderplus_nativetest_LiveNative_nativeLoadProject(JNIEnv* env, jobject, jbyteArray bytes) {
  std::lock_guard<std::mutex> lk(g_life);
  auto fail = [&](const char* why, int rc) { char b[300]; std::snprintf(b, sizeof b, "{\"ok\":false,\"rc\":%d,\"err\":\"%s\"}", rc, why); return env->NewStringUTF(b); };
  auto eng = g.eng.get(); if (!eng) return fail("engine not started", -2);
  if (!bytes) return fail("no bytes", -1);
  const jsize n = env->GetArrayLength(bytes);
  if (n <= 0) return fail("empty file", -1);
  // The task below may outlive this call if the wait times out (LiveEngine::call): everything it touches is owned by shared_ptrs it captures.
  auto textP = std::make_shared<std::string>((size_t)n, '\0');
  std::string& text = *textP;
  env->GetByteArrayRegion(bytes, 0, n, reinterpret_cast<jbyte*>(&text[0]));
  if (env->ExceptionCheck()) { env->ExceptionClear(); return fail("could not read the bytes", -1); }
  const std::string_view songT = tpa::extractMember(text, "song"), kitT = tpa::extractMember(text, "kit");
  if (songT.empty()) return fail("not a .thunder project: no song member", -3);
  double bpm = 120, lpb = 4;
  { thunder::JsonDoc sd; if (sd.parse(songT)) { bpm = sd.root().num("bpm", 120); lpb = sd.root().num("lpb", 4); } }
  auto psP = std::make_shared<tpa::PadSet>();
  tpa::PadSet& ps = *psP;
  if (!kitT.empty() && !tpa::renderPads(kitT, bpm, lpb, (double)g.device.sampleRate, &ps)) return fail("the kit is unreadable", -3);
  const int64_t rev = ++g.rev;
  auto serrP = std::make_shared<std::array<char, 256>>(); serrP->fill(0);
  std::vector<int> slots; for (auto& p : ps.pads) slots.push_back(p.slot);
  auto goneP = std::make_shared<std::vector<int>>(); std::vector<int>& gone = *goneP; for (int s : g.lastSlots) { bool keep = false; for (int t : slots) if (t == s) keep = true; if (!keep) gone.push_back(s); }
  const double sr = (double)g.device.sampleRate;
  const int rc = eng->call([=](thunder_live* L) -> int {
    tpa::PadSet& ps = *psP; char* serr = serrP->data();
    const std::string_view songT = tpa::extractMember(*textP, "song");
    int r = thunder_live_song_publish(L, (const uint8_t*)songT.data(), songT.size(), nullptr, 0, rev, serr, serrP->size());
    if (r) return r;
    for (int s : *goneP) { r = thunder_live_pad_set(L, s, nullptr, nullptr, 0, sr); if (r) return r; }
    for (auto& p : ps.pads) {
      if (!p.block) continue;   // taken by an earlier attempt of this lambda (an E_BUSY retry repeats it from the start: the song publish is idempotent, a taken pad is already in)
      r = thunder_live_pad_take(L, p.slot, p.block, 0, p.frames, sr); if (r) return r;
      p.block = nullptr;
      r = thunder_live_pad_kit(L, p.slot, p.choke, p.syn ? 1 : 0, p.rel); if (r) return r;
    }
    return thunder_live_pads_publish(L);
  }, 20000);
  if (rc == 0) g.lastSlots = slots;
  std::string notes;
  for (size_t i = 0; i < ps.notes.size() && i < 8; i++) { if (i) notes += ","; notes += "\"" + jsonEscape(ps.notes[i]) + "\""; }
  char head[360];
  std::snprintf(head, sizeof head, "{\"ok\":%s,\"rc\":%d,\"err\":\"%s\",\"pads\":%zu,\"skipped\":%d,\"dry\":%d,\"renderMs\":%.0f,\"bpm\":%.1f,\"notes\":[", rc == 0 ? "true" : "false", rc, jsonEscape(serrP->data()).c_str(), ps.pads.size(), ps.skipped, ps.dry, ps.ms, bpm);
  return env->NewStringUTF((std::string(head) + notes + "]}").c_str());
}

JNIEXPORT jint JNICALL Java_app_thunderplus_nativetest_LiveNative_nativePlay(JNIEnv*, jobject, jint row) {
  auto eng = g.eng.get(); if (!eng) return -2;
  const uint32_t seed = (uint32_t)(monoNs() & 0x7fffffff) | 1u;
  return eng->call([=](thunder_live* L) {
    thunder_live_play_opts po; std::memset(&po, 0, sizeof po);
    po.size = (uint32_t)sizeof po; po.t0 = THUNDER_LIVE_NOW; po.mode = THUNDER_LIVE_MODE_SONG; po.row = row; po.pdc_mode = 1; po.random_seed = seed; po.spp = -1;
    int r = thunder_live_meters_want(L, 10.0); (void)r;
    return thunder_live_play(L, &po);
  });
}

JNIEXPORT jint JNICALL Java_app_thunderplus_nativetest_LiveNative_nativeTransportStop(JNIEnv*, jobject) {
  auto eng = g.eng.get(); if (!eng) return -2;
  return eng->call([](thunder_live* L) { return thunder_live_stop(L, THUNDER_LIVE_NOW); });
}
JNIEXPORT jint JNICALL Java_app_thunderplus_nativetest_LiveNative_nativeBpm(JNIEnv*, jobject, jdouble bpm) {
  auto eng = g.eng.get(); if (!eng) return -2;
  return eng->call([=](thunder_live* L) { return thunder_live_bpm(L, bpm, THUNDER_LIVE_NOW); });
}
JNIEXPORT jint JNICALL Java_app_thunderplus_nativetest_LiveNative_nativeLoop(JNIEnv*, jobject, jint has, jint y0, jint y1) {
  auto eng = g.eng.get(); if (!eng) return -2;
  return eng->call([=](thunder_live* L) { return thunder_live_loop(L, has, y0, y1, THUNDER_LIVE_NOW); });
}
JNIEXPORT jint JNICALL Java_app_thunderplus_nativetest_LiveNative_nativeMixSet(JNIEnv*, jobject, jint track, jint key, jdouble value, jdouble ramp) {
  auto eng = g.eng.get(); if (!eng) return -2;
  return eng->call([=](thunder_live* L) { return thunder_live_mix_set(L, track, key, value, ramp, THUNDER_LIVE_NOW); });
}

// The poller's latest state as JSON. Also hands AAudio's xrun count and buffer size to the host (the poller thread passes them to thunder_live_host_stats every 100 ms).
JNIEXPORT jstring JNICALL Java_app_thunderplus_nativetest_LiveNative_nativeStatus(JNIEnv* env, jobject) {
  auto eng = g.eng.get(); if (!eng) return env->NewStringUTF("{}");
  { std::lock_guard<std::mutex> dl(g_dev);   // the stream pointer and the two AAudio calls on it: shutdown / reopen change them only under g_dev
    void* stream = g.deviceInit ? (void*)g.device.aaudio.pStreamPlayback : nullptr;
    if (stream) {
      const int x = g.aaXrun ? g.aaXrun(stream) : 0, b = g.aaBuf ? g.aaBuf(stream) : 0;
      eng->setHostStats(b > 0 ? b : 0, x > 0 ? x : 0);   // the buffer size stands for the output latency (GUESS: an upper bound; H1c reads the AAudio timestamp as N0's probe does)
    } }
  return env->NewStringUTF(eng->statusJson().c_str());
}
JNIEXPORT jstring JNICALL Java_app_thunderplus_nativetest_LiveNative_nativeStats(JNIEnv* env, jobject) {
  auto eng = g.eng.get(); if (!eng) return env->NewStringUTF("{}");
  return env->NewStringUTF(eng->statsJson().c_str());
}

JNIEXPORT void JNICALL Java_app_thunderplus_nativetest_LiveNative_nativeRtReset(JNIEnv*, jobject) { std::lock_guard<std::mutex> dl(g_dev); if (g.deviceInit) rtlog_reset(&g_lrt, (int)g.device.sampleRate); }
JNIEXPORT jstring JNICALL Java_app_thunderplus_nativetest_LiveNative_nativeRtSnapshot(JNIEnv* env, jobject) {
  char b[400];
  rtlog_snapshot sn = rtlog_read(&g_lrt);
  const int sr = g_lrt.sample_rate;
  std::snprintf(b, sizeof b,
    "{\"pending\":0,\"sr\":%d,\"frames\":%lld,\"calls\":%lld,\"late\":%lld,\"under\":%lld,\"audioS\":%.6f,\"wallS\":%.6f,\"ratioPpm\":%.2f,\"maxGapMs\":%.3f,\"maxWorkMs\":%.3f,\"pass\":%d,\"outLatMs\":0}",
    sr, (long long)sn.frames, (long long)sn.calls, (long long)sn.late, (long long)sn.underruns, sn.audio_s, sn.wall_s, sn.ratio_ppm, sn.max_gap_ms, sn.max_work_ms, sn.pass);
  return env->NewStringUTF(b);
}

}  // extern "C"
