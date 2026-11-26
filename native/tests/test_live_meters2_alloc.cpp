// M1 (ctest `live_meters2_alloc`): the audio path with FXLVL / FXDYN posting allocates nothing. A host with effects on two tracks and the master, every kind watched, is warmed up, then thunder process
// runs 400 quanta with malloc / calloc / realloc / free / operator new counted (the records are polled outside the count). Linux (glibc), not under a sanitizer: elsewhere skipped (exit 77).
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>
#if defined(__GLIBC__) && !defined(__SANITIZE_ADDRESS__)
#define ALLOC_AUDIT 1
#include <execinfo.h>
#include <dlfcn.h>
#endif
#include "rt/json.h"
using namespace thunder;
#ifdef ALLOC_AUDIT
static thread_local int g_on = 0; static thread_local unsigned long g_cnt = 0; static thread_local char g_first[2048]; static thread_local bool g_have = false;
static void note(const char* w) { g_cnt++; if (!g_have) { g_have = true; void* bt[14]; int n = backtrace(bt, 14); char** sy = backtrace_symbols(bt, n); std::snprintf(g_first, sizeof g_first, "%s", w); for (int i = 2; i < n && std::strlen(g_first) < 1700; i++) { std::strcat(g_first, " <- "); std::strncat(g_first, sy[i], 110); } std::free(sy); } }
extern "C" { void* __libc_malloc(size_t); void* __libc_calloc(size_t, size_t); void* __libc_realloc(void*, size_t); void __libc_free(void*);
void* malloc(size_t n) { if (g_on) { g_on = 0; note("malloc"); g_on = 1; } return __libc_malloc(n); }
void* calloc(size_t a, size_t b) { if (g_on) { g_on = 0; note("calloc"); g_on = 1; } return __libc_calloc(a, b); }
void* realloc(void* p, size_t n) { if (g_on) { g_on = 0; note("realloc"); g_on = 1; } return __libc_realloc(p, n); }
void free(void* p) { if (g_on && p) { g_on = 0; note("free"); g_on = 1; } __libc_free(p); }
// The aligned family (verifier D7): posix_memalign, aligned_alloc, memalign (and with them libstdc++'s aligned and nothrow operator new, which end in these or in malloc). The real ones are found with dlsym once.
static void* nextSym(const char* n) { return dlsym(RTLD_NEXT, n); }
int posix_memalign(void** r, size_t a, size_t n) { const int on = g_on; g_on = 0; static int (*f)(void**, size_t, size_t) = (int (*)(void**, size_t, size_t))nextSym("posix_memalign"); if (on) note("posix_memalign"); g_on = on; return f(r, a, n); }
void* aligned_alloc(size_t a, size_t n) { const int on = g_on; g_on = 0; static void* (*f)(size_t, size_t) = (void* (*)(size_t, size_t))nextSym("aligned_alloc"); if (on) note("aligned_alloc"); g_on = on; return f(a, n); }
void* memalign(size_t a, size_t n) { const int on = g_on; g_on = 0; static void* (*f)(size_t, size_t) = (void* (*)(size_t, size_t))nextSym("memalign"); if (on) note("memalign"); g_on = on; return f(a, n); } }
#else
static int g_on = 0; static unsigned long g_cnt = 0; static char g_first[1]; static bool g_have = false;
#endif

#include "live_common.h"
using namespace lt;
static void tapFn(void*, Mixer& m, int64_t q) {
  for (int t = 0; t < 2; t++) for (int i = 0; i < 128; i++) { const double x = (double)(q + i); m.inputsL()[t][i] = (float)(0.5 * std::sin(0.0575 * x)); m.inputsR()[t][i] = (float)(0.4 * std::sin(0.0431 * x)); }
}
int main() {
#ifndef ALLOC_AUDIT
  std::printf("live_meters2_alloc: skipped (needs glibc without a sanitizer)\n"); return 77;
#else
  Host H(opts(THUNDER_LIVE_F_EFFECTS | THUNDER_LIVE_F_METERS | THUNDER_LIVE_F_MASTER, 4096)); if (!H.h) return 1;
  const std::string a = "[{\"type\":\"tape\",\"on\":true},{\"type\":\"comp\",\"on\":true},{\"type\":\"pcomp\",\"on\":true,\"pump\":{\"threshold\":-40,\"ratio\":8}}]";
  for (int t : {0, 1, 32}) { if (H->fxPlan(t, (const uint8_t*)a.data(), a.size(), 120) || H->fxCommit(t, -1)) { std::printf("plan failed\n"); return 1; } }
  thunder_live_meters_spec s; std::memset(&s, 0, sizeof s); s.size = sizeof s; s.kinds = THUNDER_LIVE_MK_FXLVL | THUNDER_LIVE_MK_FXDYN; s.fx_tracks = 3ull | (1ull << 32); s.rate_hz = 30;
  if (H->metersSet(s)) return 1;
  H->setTestTap(tapFn, nullptr);
  static float L[128], R[128]; int64_t fr = 0; size_t recs = 0, fx = 0;
  auto quanta = [&](int n) { for (int i = 0; i < n; i++) { if (H->process(L, R, 128, fr, 0)) { std::printf("process failed\n"); std::exit(1); } fr += 128; } };
  quanta(200);
  for (auto& r : H.poll()) (void)r;
  g_cnt = 0; g_on = 1; quanta(400); g_on = 0;
  const unsigned long n = g_cnt;
  for (auto& r : H.poll()) { recs++; const int t = evType(r); if (t == THUNDER_LIVE_EV_FXLVL || t == THUNDER_LIVE_EV_FXDYN) fx++; }
  std::printf("live_meters2_alloc: %zu records (%zu FX) in 400 quanta, %lu allocator calls on the audio path\n", recs, fx, n);
  if (fx < 50) { std::printf("FAIL: too few FX records\n"); return 1; }
  if (n) { std::printf("FAIL: allocation on the audio path; first: %s\n", g_first); return 1; }
  return 0;
#endif
}
