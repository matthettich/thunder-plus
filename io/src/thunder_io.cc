// thunder_io.node: Thunder Plus's native audio and MIDI I/O helper (phase A0.2: devices() and now()).
// Plain N-API (node_api.h, no node-addon-api), no C++ exceptions, no threads. See docs/NATIVE-IO-HELPER-PLAN.md.
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <node_api.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
  #include <mach/mach_time.h>
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

static void CleanupCtx(void* arg) {
  (void)arg;
  if (g_ctxUp) { ma_context_uninit(&g_ctx); g_ctxUp = false; }
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
    if (!g_ctxUp) {
      ma_result r = ma_context_init(NULL, 0, NULL, &g_ctx);
      if (r != MA_SUCCESS) return Fail(env, "audio context", r);
      g_ctxUp = true;
      napi_add_env_cleanup_hook(env, CleanupCtx, NULL);
    }
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

// version() -> "thunder_io 1 / miniaudio 0.11.26"
static napi_value Version(napi_env env, napi_callback_info info) {
  (void)info;
  napi_value out;
  napi_create_string_utf8(env, "thunder_io 1 / miniaudio " MA_VERSION_STRING, NAPI_AUTO_LENGTH, &out);
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
  return exports;
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, Init)
