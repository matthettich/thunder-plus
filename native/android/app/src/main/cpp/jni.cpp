// Android glue: miniaudio (AAudio) opens the output and calls thunder_process; JNI entry points for Kotlin.
#include <jni.h>
#include <android/log.h>
#include <dlfcn.h>
#include <time.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>

#include "miniaudio.h"
#include "thunder/core.h"

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
};
Engine g;

// F-REVIEW (A3): THE AUDIO CALLBACK. It must do nothing but call thunder_process and interleave. Check: no allocation
// (scratch is in the Engine, fixed size), no locks, no logging, no JNI. A block larger than kMaxBlock is rendered in
// pieces. thunder_process itself is reviewed in native/core/src/core.cpp.
void dataCallback(ma_device* dev, void* out, const void* /*in*/, ma_uint32 frames) {
  Engine* e = static_cast<Engine*>(dev->pUserData);
  float* o = static_cast<float*>(out);
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
}

// AAudio's own counters (not exposed by miniaudio): the stream pointer is miniaudio's, the functions come from libaaudio.
typedef int32_t (*fnI32)(void*);
typedef int64_t (*fnI64)(void*);
typedef int32_t (*fnTs)(void*, int32_t, int64_t*, int64_t*);

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
      int latFrames = 0;
      int64_t pos = 0, tns = 0;
      if (ts && written && ts(stream, CLOCK_MONOTONIC, &pos, &tns) == 0) {
        timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
        int64_t now = (int64_t)t.tv_sec * 1000000000LL + t.tv_nsec;
        // frames still to be played = written - (position presented at `tns`, advanced to now)
        double presented = (double)pos + (double)(now - tns) * 1e-9 * g.device.sampleRate;
        double lat = (double)written(stream) - presented;
        if (lat > 0 && lat < g.device.sampleRate) latFrames = (int)lat;
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
  g.probeRun = true;
  g.probe = std::thread(probeLoop);
  if (ma_device_start(&g.device) != MA_SUCCESS) {
    g.probeRun = false; g.probe.join();
    ma_device_uninit(&g.device); g.deviceInit = false; thunder_destroy(g.core); g.core = nullptr; return -1;
  }
  LOGI("audio started at %u Hz", g.device.sampleRate);
  return (jint)g.device.sampleRate;
}

JNIEXPORT void JNICALL Java_app_thunderplus_nativetest_NativeEngine_nativeStop(JNIEnv*, jobject) {
  if (!g.deviceInit) return;
  ma_device_uninit(&g.device);   // stops the callback
  g.deviceInit = false;
  g.probeRun = false;
  if (g.probe.joinable()) g.probe.join();
  thunder_destroy(g.core);
  g.core = nullptr;
}

JNIEXPORT jboolean JNICALL Java_app_thunderplus_nativetest_NativeEngine_nativePost(JNIEnv* env, jobject, jstring s) {
  if (!g.core || !s) return JNI_FALSE;
  const char* c = env->GetStringUTFChars(s, nullptr);
  int ok = thunder_post(g.core, c, (int)std::strlen(c));
  env->ReleaseStringUTFChars(s, c);
  return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL Java_app_thunderplus_nativetest_NativeEngine_nativePoll(JNIEnv* env, jobject) {
  if (!g.core) return nullptr;
  char buf[512];
  int n = thunder_poll(g.core, buf, (int)sizeof buf);
  if (n <= 0) return nullptr;
  return env->NewStringUTF(buf);
}

JNIEXPORT jboolean JNICALL Java_app_thunderplus_nativetest_NativeEngine_nativePutBytes(JNIEnv* env, jobject, jint id, jstring meta, jbyteArray bytes, jint off, jint len) {
  if (!g.core || !bytes || (len & 3) || (off & 3)) return JNI_FALSE;
  const char* m = env->GetStringUTFChars(meta, nullptr);
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

JNIEXPORT jstring JNICALL Java_app_thunderplus_nativetest_NativeEngine_nativeInfo(JNIEnv* env, jobject) {
  return env->NewStringUTF(g.infoReady.load() ? g.info : "");
}

}  // extern "C"
