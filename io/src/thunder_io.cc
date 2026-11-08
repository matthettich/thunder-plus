// thunder_io.node: Thunder Plus's native audio and MIDI I/O helper: devices(), now(), and (A1) the audio input:
// openInput() / read() / closeInput(). Plain N-API (node_api.h, no node-addon-api), no C++ exceptions. The only thread is
// the OS audio callback, which never calls into JS: it copies into a lock-free ring (ring.h) that read() drains.
// See docs/NATIVE-IO-HELPER-PLAN.md.
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <node_api.h>

#include "ring.h"
#include <atomic>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
  #include <mach/mach_time.h>
  #include <CoreAudio/CoreAudio.h>
#elif defined(_WIN32)
  // windows.h is already pulled in by miniaudio.h's implementation (QueryPerformanceCounter lives there)
#else
  #include <time.h>
#endif

// ---------------------------------------------------------------------------------------------------------------------
// now()

// The OS clock in nanoseconds, the clock the audio callback's timestamps will use later: macOS mach_absolute_time (the
// same clock as CoreAudio's mHostTime), Windows QueryPerformanceCounter, elsewhere CLOCK_MONOTONIC. Whole seconds and the
// remainder are converted separately, so the 64-bit integer math never overflows; only the final value is made a double.
static uint64_t HostNowNs(void) {
#if defined(__APPLE__)
  static mach_timebase_info_data_t tb = {0, 0};
  if (tb.denom == 0) mach_timebase_info(&tb);
  uint64_t t = mach_absolute_time();
  uint64_t whole = t / tb.denom, rem = t % tb.denom;  // ns = t * numer / denom
  return whole * tb.numer + (rem * tb.numer) / tb.denom;
#elif defined(_WIN32)
  static LONGLONG freq = 0;
  LARGE_INTEGER c;
  if (freq == 0) { LARGE_INTEGER f; QueryPerformanceFrequency(&f); freq = f.QuadPart; }
  QueryPerformanceCounter(&c);
  uint64_t t = (uint64_t)c.QuadPart, f = (uint64_t)freq;
  return (t / f) * 1000000000ull + ((t % f) * 1000000000ull) / f;
#else
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
#endif
}

// now() -> Number (nanoseconds)
static napi_value Now(napi_env env, napi_callback_info info) {
  (void)info;
  napi_value out;
  napi_create_double(env, (double)HostNowNs(), &out);
  return out;
}

// ---------------------------------------------------------------------------------------------------------------------
// devices()

// One shared context, created on the first devices() call and kept until the environment goes away.
static ma_context g_ctx;
static bool g_ctxUp = false;

static void CloseInputNow(void);
static void CleanupCtx(void* arg) {
  (void)arg;
  CloseInputNow();   // the input uses the context: stop it first
  if (g_ctxUp) { ma_context_uninit(&g_ctx); g_ctxUp = false; }
}
static bool g_hookAdded = false;
static ma_result SharedCtx(napi_env env) {
  if (g_ctxUp) return MA_SUCCESS;
  ma_result r = ma_context_init(NULL, 0, NULL, &g_ctx);
  if (r != MA_SUCCESS) return r;
  g_ctxUp = true;
  if (!g_hookAdded) { napi_add_env_cleanup_hook(env, CleanupCtx, NULL); g_hookAdded = true; }
  return MA_SUCCESS;
}

struct Rec {
  bool hasIn, hasOut;
  ma_device_info in, out;  // the enumeration entries (ids, names, defaults)
  ma_uint32 inCh, outCh;   // widest channel count seen (2 = unknown)
  ma_uint32 rates[64 * 2];
  int nRates;
};

static void AddRate(Rec* r, ma_uint32 hz) {
  if (hz == 0) return;
  for (int i = 0; i < r->nRates; i++) if (r->rates[i] == hz) return;
  if (r->nRates < (int)(sizeof(r->rates) / sizeof(r->rates[0]))) r->rates[r->nRates++] = hz;
}

// Reads nativeDataFormats of one side. Channel count 0 means "any" in miniaudio: unknown, reported as 2.
static ma_uint32 ReadSide(ma_context* ctx, ma_device_type type, const ma_device_info* e, Rec* r) {
  ma_device_info info;
  memset(&info, 0, sizeof(info));
  if (ma_context_get_device_info(ctx, type, &e->id, &info) != MA_SUCCESS) return 2;
  ma_uint32 ch = 0;
  bool any = false;
  for (ma_uint32 i = 0; i < info.nativeDataFormatCount && i < 64; i++) {
    ma_uint32 c = info.nativeDataFormats[i].channels;
    if (c == 0) any = true; else if (c > ch) ch = c;
    AddRate(r, info.nativeDataFormats[i].sampleRate);
  }
  if (ch == 0) ch = (any || info.nativeDataFormatCount == 0) ? 2 : ch;
  return ch;
}

// A device id as a string: the raw ma_device_id bytes in hex, trailing zero bytes dropped (pad with zeros to read it back),
// behind the "in:" or "out:" prefix.
static napi_value IdString(napi_env env, const char* prefix, const ma_device_id* id) {
  const uint8_t* b = (const uint8_t*)id;
  size_t n = sizeof(ma_device_id);
  while (n > 1 && b[n - 1] == 0) n--;
  size_t pl = strlen(prefix);
  char* s = (char*)malloc(pl + n * 2 + 1);
  napi_value out = NULL;
  if (!s) { napi_get_null(env, &out); return out; }
  memcpy(s, prefix, pl);
  static const char hex[] = "0123456789abcdef";
  for (size_t i = 0; i < n; i++) { s[pl + i * 2] = hex[b[i] >> 4]; s[pl + i * 2 + 1] = hex[b[i] & 15]; }
  napi_create_string_utf8(env, s, pl + n * 2, &out);
  free(s);
  return out;
}

static int CmpU32(const void* a, const void* b) {
  ma_uint32 x = *(const ma_uint32*)a, y = *(const ma_uint32*)b;
  return x < y ? -1 : x > y;
}

static void SetNum(napi_env env, napi_value o, const char* k, double v) {
  napi_value n; napi_create_double(env, v, &n); napi_set_named_property(env, o, k, n);
}
static void SetBool(napi_env env, napi_value o, const char* k, bool v) {
  napi_value n; napi_get_boolean(env, v, &n); napi_set_named_property(env, o, k, n);
}

static napi_value Fail(napi_env env, const char* what, ma_result res) {
  char msg[256];
  snprintf(msg, sizeof(msg), "%s: %s", what, ma_result_description(res));
  napi_throw_error(env, NULL, msg);
  return NULL;
}

// devices([{backend:'null'}]) -> [{id, inId, outId, name, backend, inputs, outputs, rates, isDefaultIn, isDefaultOut}]
//   inputs / outputs: the widest channel count the device reports on that side, 0 when it has no such side, 2 when
//   miniaudio says "any" or cannot tell. A capture and a playback entry are one record only when both the name and the raw
//   device id bytes are equal; if unsure they stay separate. id is inId when there is a capture side, else outId.
static napi_value Devices(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value argv[1];
  napi_get_cb_info(env, info, &argc, argv, NULL, NULL);

  bool useNull = false;
  if (argc >= 1) {
    napi_valuetype vt;
    napi_typeof(env, argv[0], &vt);
    if (vt == napi_object) {
      napi_value be;
      napi_get_named_property(env, argv[0], "backend", &be);
      napi_typeof(env, be, &vt);
      if (vt == napi_string) {
        char b[16];
        size_t len = 0;
        napi_get_value_string_utf8(env, be, b, sizeof(b), &len);
        if (len == 4 && memcmp(b, "null", 4) == 0) useNull = true;
        else { napi_throw_type_error(env, NULL, "devices: backend must be 'null' or omitted"); return NULL; }
      }
    }
  }

  ma_context local;
  ma_context* ctx;
  if (useNull) {
    ma_backend nb[1] = {ma_backend_null};
    ma_result r = ma_context_init(nb, 1, NULL, &local);
    if (r != MA_SUCCESS) return Fail(env, "audio context (null backend)", r);
    ctx = &local;
  } else {
    ma_result r0 = SharedCtx(env);
    if (r0 != MA_SUCCESS) return Fail(env, "audio context", r0);
    ctx = &g_ctx;
  }

  ma_device_info *pPlay = NULL, *pCap = NULL;
  ma_uint32 nPlay = 0, nCap = 0;
  ma_result r = ma_context_get_devices(ctx, &pPlay, &nPlay, &pCap, &nCap);
  if (r != MA_SUCCESS) {
    if (useNull) ma_context_uninit(&local);
    return Fail(env, "device enumeration", r);
  }

  Rec* recs = (Rec*)calloc((size_t)nPlay + nCap + 1, sizeof(Rec));
  bool* used = (bool*)calloc((size_t)nPlay + 1, sizeof(bool));
  napi_value arr;
  napi_create_array(env, &arr);
  if (!recs || !used) {
    free(recs); free(used);
    if (useNull) ma_context_uninit(&local);
    napi_throw_error(env, NULL, "out of memory");
    return NULL;
  }

  size_t n = 0;
  for (ma_uint32 c = 0; c < nCap; c++) {  // each capture entry takes the first unused playback entry that is the same device
    Rec* rc = &recs[n++];
    rc->hasIn = true;
    rc->in = pCap[c];
    for (ma_uint32 p = 0; p < nPlay; p++) {
      if (used[p]) continue;
      if (strcmp(pCap[c].name, pPlay[p].name) == 0 && memcmp(&pCap[c].id, &pPlay[p].id, sizeof(ma_device_id)) == 0) {
        used[p] = true; rc->hasOut = true; rc->out = pPlay[p]; break;
      }
    }
  }
  for (ma_uint32 p = 0; p < nPlay; p++) {
    if (used[p]) continue;
    Rec* rc = &recs[n++];
    rc->hasOut = true;
    rc->out = pPlay[p];
  }

  const char* backendName = ma_get_backend_name(ctx->backend);
  for (size_t i = 0; i < n; i++) {
    Rec* rc = &recs[i];
    rc->inCh = rc->hasIn ? ReadSide(ctx, ma_device_type_capture, &rc->in, rc) : 0;
    rc->outCh = rc->hasOut ? ReadSide(ctx, ma_device_type_playback, &rc->out, rc) : 0;
    qsort(rc->rates, (size_t)rc->nRates, sizeof(ma_uint32), CmpU32);

    napi_value o, v, rates;
    napi_create_object(env, &o);
    napi_value inId = NULL, outId = NULL;
    if (rc->hasIn) inId = IdString(env, "in:", &rc->in.id); else napi_get_null(env, &inId);
    if (rc->hasOut) outId = IdString(env, "out:", &rc->out.id); else napi_get_null(env, &outId);
    napi_set_named_property(env, o, "id", rc->hasIn ? inId : outId);
    napi_set_named_property(env, o, "inId", inId);
    napi_set_named_property(env, o, "outId", outId);
    const char* nm = rc->hasIn ? rc->in.name : rc->out.name;
    napi_create_string_utf8(env, nm, NAPI_AUTO_LENGTH, &v);
    napi_set_named_property(env, o, "name", v);
    napi_create_string_utf8(env, backendName ? backendName : "", NAPI_AUTO_LENGTH, &v);
    napi_set_named_property(env, o, "backend", v);
    SetNum(env, o, "inputs", rc->inCh);
    SetNum(env, o, "outputs", rc->outCh);
    napi_create_array_with_length(env, (size_t)rc->nRates, &rates);
    for (int k = 0; k < rc->nRates; k++) {
      napi_create_double(env, rc->rates[k], &v);
      napi_set_element(env, rates, (uint32_t)k, v);
    }
    napi_set_named_property(env, o, "rates", rates);
    SetBool(env, o, "isDefaultIn", rc->hasIn && rc->in.isDefault);
    SetBool(env, o, "isDefaultOut", rc->hasOut && rc->out.isDefault);
    napi_set_element(env, arr, (uint32_t)i, o);
  }

  free(recs);
  free(used);
  if (useNull) ma_context_uninit(&local);
  return arr;
}


// ---------------------------------------------------------------------------------------------------------------------
// The audio input (A1): openInput(id, {rate, block, backend}) / read() / closeInput()
//
// One input at a time, with ALL of the device's input channels (the page picks the ones it keeps, so changing the pair
// never reopens the device). Samples are float32, interleaved, at the rate asked for (miniaudio resamples if the device
// runs at another; it does not change the device's own rate).
//
// TIME (plan §4). Each callback is stamped on the OS clock (now(), the same clock as the page's mapping). A callback runs
// a little after its frames were captured, and a late callback only ever makes it later, so the stream's start is tracked
// as a LOWER ENVELOPE: start = min(start + slack, now - frames so far / rate), where slack lets it rise by 100 ppm of the
// time passed, enough to follow the crystals' drift (tens of ppm) but not a late wake-up. A frame f's time is then
// start + f / rate. This is when the frames reach the computer; the converter and driver latency before that is what
// latencyFrames reports (when the system says) and what Measure… finds.

namespace {
struct Input {
  ma_device dev;
  ma_context nullCtx;          // only for {backend:'null'} (tests and CI)
  bool usesNullCtx = false;
  bool up = false;
  tio::InputRing ring;
  uint32_t channels = 0;
  double rate = 0;
  std::atomic<uint64_t> frames{0};      // device frames delivered so far (audio thread only writes)
  std::atomic<int64_t> startNs{0};      // the lower envelope of the stream's frame-0 time, ns
  std::atomic<bool> stamped{false};
  int64_t lastNow = 0;                  // audio thread only
};
Input* g_in = nullptr;
}

static void OnCapture(ma_device* d, void* out, const void* in, ma_uint32 n) {
  (void)out;
  Input* I = (Input*)d->pUserData;
  if (!I || !in || n == 0) return;
  const int64_t now = (int64_t)HostNowNs();
  const uint64_t f0 = I->frames.load(std::memory_order_relaxed);
  const int64_t measured = now - (int64_t)((double)(f0 + n) * 1e9 / I->rate);   // frame 0's time, if this callback were on time
  if (!I->stamped.load(std::memory_order_relaxed)) { I->startNs.store(measured, std::memory_order_relaxed); I->stamped.store(true, std::memory_order_release); }
  else {
    int64_t est = I->startNs.load(std::memory_order_relaxed) + (now - I->lastNow) / 10000;   // + 100 ppm of the time since
    I->startNs.store(measured < est ? measured : est, std::memory_order_release);
  }
  I->lastNow = now;
  I->ring.put(f0, (const float*)in, n);   // dropped (and counted) if the reader has fallen a second behind
  I->frames.store(f0 + n, std::memory_order_release);
}

static void CloseInputNow(void) {
  Input* I = g_in;
  if (!I) return;
  g_in = nullptr;
  if (I->up) ma_device_uninit(&I->dev);   // waits for the callback to finish: nothing touches the ring after this
  if (I->usesNullCtx) ma_context_uninit(&I->nullCtx);
  I->ring.free_();
  delete I;
}

static bool HexId(const char* s, size_t len, ma_device_id* id) {
  memset(id, 0, sizeof(*id));
  if (len >= 3 && memcmp(s, "in:", 3) == 0) { s += 3; len -= 3; } else return false;
  if (len % 2 || len / 2 > sizeof(*id)) return false;
  uint8_t* b = (uint8_t*)id;
  for (size_t i = 0; i < len / 2; i++) {
    int v = 0;
    for (int k = 0; k < 2; k++) {
      char c = s[i * 2 + k]; v <<= 4;
      if (c >= '0' && c <= '9') v |= c - '0'; else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10; else return false;
    }
    b[i] = (uint8_t)v;
  }
  return true;
}

#if defined(__APPLE__)
// LEAVE THE INTERFACE AS IT IS (owner, 7 Nov: "the audio interface gets reset"): miniaudio sets the device's I/O buffer size
// (kAudioDevicePropertyBufferFrameSize) to the period it asks for, and on CoreAudio that is the whole device's setting, so the
// interface reconfigured under the app's own output (and anything else using it). Asking for the size it already has makes
// that a no-op. Returns 0 when it can't tell (then the system picks, as before).
static unsigned CurrentBufferFrames(const ma_device_id* id) {
  AudioObjectID dev = kAudioObjectUnknown;
  UInt32 sz = sizeof(dev);
  if (id) {
    CFStringRef uid = CFStringCreateWithCString(NULL, id->coreaudio, kCFStringEncodingUTF8);
    if (!uid) return 0;
    AudioValueTranslation t = { &uid, sizeof(uid), &dev, sizeof(dev) };
    AudioObjectPropertyAddress a = { kAudioHardwarePropertyDeviceForUID, kAudioObjectPropertyScopeGlobal, 0 };
    sz = sizeof(t);
    OSStatus st = AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, NULL, &sz, &t);
    CFRelease(uid);
    if (st != noErr) return 0;
  } else {
    AudioObjectPropertyAddress a = { kAudioHardwarePropertyDefaultInputDevice, kAudioObjectPropertyScopeGlobal, 0 };
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, NULL, &sz, &dev) != noErr) return 0;
  }
  if (dev == kAudioObjectUnknown) return 0;
  UInt32 frames = 0; sz = sizeof(frames);
  AudioObjectPropertyAddress b = { kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeInput, 0 };
  if (AudioObjectGetPropertyData(dev, &b, 0, NULL, &sz, &frames) != noErr) return 0;
  return frames;
}
#endif

static napi_value Result(napi_env env, bool ok, const char* why) {
  napi_value o, v;
  napi_create_object(env, &o);
  SetBool(env, o, "ok", ok);
  if (why) { napi_create_string_utf8(env, why, NAPI_AUTO_LENGTH, &v); napi_set_named_property(env, o, "why", v); }
  return o;
}
static bool NumProp(napi_env env, napi_value o, const char* k, double* out) {
  bool has = false; napi_has_named_property(env, o, k, &has); if (!has) return false;
  napi_value v; napi_valuetype t; napi_get_named_property(env, o, k, &v); napi_typeof(env, v, &t);
  if (t != napi_number) return false;
  napi_get_value_double(env, v, out); return true;
}

// openInput(id, {rate, block, backend}) -> {ok, rate, channels, block, latencyFrames, name} | {ok:false, why}
//   id: a device's inId from devices() ('' or missing: the system's default input). rate: what the page's audio runs at
//   (0 / missing: the device's own). block: the callback size asked for, in frames (0: the system's choice).
static napi_value OpenInput(napi_env env, napi_callback_info info) {
  size_t argc = 2; napi_value argv[2];
  napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
  CloseInputNow();

  char idbuf[600]; size_t idlen = 0; bool haveId = false;
  if (argc >= 1) { napi_valuetype t; napi_typeof(env, argv[0], &t);
    if (t == napi_string) { napi_get_value_string_utf8(env, argv[0], idbuf, sizeof(idbuf), &idlen); haveId = idlen > 0; } }
  double rate = 0, block = 0; bool useNull = false;
  if (argc >= 2) { napi_valuetype t; napi_typeof(env, argv[1], &t);
    if (t == napi_object) {
      NumProp(env, argv[1], "rate", &rate); NumProp(env, argv[1], "block", &block);
      bool has = false; napi_has_named_property(env, argv[1], "backend", &has);
      if (has) { napi_value v; char b[16]; size_t l = 0; napi_get_named_property(env, argv[1], "backend", &v);
        if (napi_get_value_string_utf8(env, v, b, sizeof(b), &l) == napi_ok && l == 4 && memcmp(b, "null", 4) == 0) useNull = true; }
    } }
  if (rate < 0 || rate > 768000) rate = 0;
  if (block < 0 || block > 8192) block = 0;

  ma_device_id id;
  if (haveId && !HexId(idbuf, idlen, &id)) return Result(env, false, "not an input id");

  Input* I = new Input();
  ma_context* ctx;
  if (useNull) {
    ma_backend nb[1] = {ma_backend_null};
    ma_result r = ma_context_init(nb, 1, NULL, &I->nullCtx);
    if (r != MA_SUCCESS) { delete I; return Result(env, false, ma_result_description(r)); }
    I->usesNullCtx = true; ctx = &I->nullCtx;
  } else {
    ma_result r = SharedCtx(env);
    if (r != MA_SUCCESS) { delete I; return Result(env, false, ma_result_description(r)); }
    ctx = &g_ctx;
  }

  ma_device_config cfg = ma_device_config_init(ma_device_type_capture);
  cfg.capture.pDeviceID = haveId ? &id : NULL;
  cfg.capture.format = ma_format_f32;
  cfg.capture.channels = 0;                    // every channel the device has
  cfg.capture.shareMode = ma_share_mode_shared;
  cfg.sampleRate = (ma_uint32)rate;            // 0: the device's own
  cfg.periodSizeInFrames = (ma_uint32)block;
#if defined(__APPLE__)
  if (!useNull) { unsigned cur = CurrentBufferFrames(haveId ? &id : NULL); if (cur) cfg.periodSizeInFrames = cur; }   // never resize the interface
#endif
  cfg.performanceProfile = ma_performance_profile_low_latency;
  cfg.noPreSilencedOutputBuffer = MA_TRUE;
  cfg.noClip = MA_TRUE;
  cfg.dataCallback = OnCapture;
  cfg.pUserData = I;

  ma_result r = ma_device_init(ctx, &cfg, &I->dev);
  if (r != MA_SUCCESS) { if (I->usesNullCtx) ma_context_uninit(&I->nullCtx); delete I; return Result(env, false, ma_result_description(r)); }
  I->up = true;
  I->channels = I->dev.capture.channels;
  I->rate = (double)I->dev.sampleRate;
  // one second of every channel (A1 plan §3.1: sized to what was opened, not to the 32-channel worst case)
  if (I->channels == 0 || I->rate <= 0 || !I->ring.init(I->channels, (size_t)I->rate)) {
    ma_device_uninit(&I->dev); if (I->usesNullCtx) ma_context_uninit(&I->nullCtx); delete I;
    return Result(env, false, "could not set up the input buffer");
  }
  g_in = I;
  r = ma_device_start(&I->dev);
  if (r != MA_SUCCESS) { CloseInputNow(); return Result(env, false, ma_result_description(r)); }

  napi_value o = Result(env, true, NULL), v;
  SetNum(env, o, "rate", I->rate);
  SetNum(env, o, "channels", I->channels);
  SetNum(env, o, "deviceRate", I->dev.capture.internalSampleRate);   // the device's own rate (differs: miniaudio resamples)
  SetNum(env, o, "block", I->dev.capture.internalPeriodSizeInFrames);
  // what the system buffers before the callback, as far as miniaudio knows (the driver / converter latency is not
  // exposed: Measure… finds the rest)
  SetNum(env, o, "latencyFrames", (double)I->dev.capture.internalPeriodSizeInFrames * (I->dev.capture.internalPeriods ? I->dev.capture.internalPeriods : 1));
  napi_create_string_utf8(env, I->dev.capture.name, NAPI_AUTO_LENGTH, &v);
  napi_set_named_property(env, o, "name", v);
  return o;
}

// read() -> null | {frame0, hostNs, channels, frames, data: Float32Array, overruns}
//   Everything that has arrived since the last read, as one contiguous run (a dropped callback ends a run: the next read
//   starts after the gap, at its true frame). data is a new buffer each time (the helper transfers it to the page).
static napi_value Read(napi_env env, napi_callback_info info) {
  (void)info;
  napi_value nul; napi_get_null(env, &nul);
  Input* I = g_in;
  if (!I || !I->stamped.load(std::memory_order_acquire)) return nul;
  size_t waiting = I->ring.framesWaiting();
  if (waiting == 0) return nul;
  void* mem = NULL; napi_value ab;
  if (napi_create_arraybuffer(env, waiting * I->channels * sizeof(float), &mem, &ab) != napi_ok) return nul;
  uint64_t frame0 = 0;
  uint32_t got = I->ring.take((float*)mem, (uint32_t)waiting, &frame0);
  if (got == 0) return nul;
  napi_value o, data;
  napi_create_typedarray(env, napi_float32_array, (size_t)got * I->channels, ab, 0, &data);
  napi_create_object(env, &o);
  SetNum(env, o, "frame0", (double)frame0);
  SetNum(env, o, "hostNs", (double)I->startNs.load(std::memory_order_acquire) + (double)frame0 * 1e9 / I->rate);
  SetNum(env, o, "channels", I->channels);
  SetNum(env, o, "frames", got);
  SetNum(env, o, "rate", I->rate);
  SetNum(env, o, "overruns", (double)I->ring.lost());
  napi_set_named_property(env, o, "data", data);
  return o;
}

// closeInput(): stops and frees the input (safe when none is open)
static napi_value CloseInput(napi_env env, napi_callback_info info) {
  (void)info;
  CloseInputNow();
  napi_value u; napi_get_undefined(env, &u); return u;
}

// ---------------------------------------------------------------------------------------------------------------------

// version() -> "thunder_io 2 / miniaudio 0.11.26"
static napi_value Version(napi_env env, napi_callback_info info) {
  (void)info;
  napi_value out;
  napi_create_string_utf8(env, "thunder_io 2 / miniaudio " MA_VERSION_STRING, NAPI_AUTO_LENGTH, &out);
  return out;
}

static void Export(napi_env env, napi_value exports, const char* name, napi_callback cb) {
  napi_value fn;
  napi_create_function(env, name, NAPI_AUTO_LENGTH, cb, NULL, &fn);
  napi_set_named_property(env, exports, name, fn);
}

static napi_value Init(napi_env env, napi_value exports) {
  Export(env, exports, "version", Version);
  Export(env, exports, "devices", Devices);
  Export(env, exports, "now", Now);
  Export(env, exports, "openInput", OpenInput);
  Export(env, exports, "read", Read);
  Export(env, exports, "closeInput", CloseInput);
  return exports;
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, Init)
