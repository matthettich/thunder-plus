// U31 (lead review, item I): setParam(..., "track" / "_bn", ...) runs on the audio thread (ChainsLive::apply) and calls prepareDerived, which works out the run list. The worst case: kMaxModules modules, about kMaxCables
// cables, many Sidechain Outs (the part that searched every cable for every Sidechain Out). Measures the time of one such setParam (worst and mean over many), counts allocator calls inside it (glibc, no sanitizer), and
// fails when the 99th percentile is above 2 ms (a regression guard; the measured Release number is in docs/port/U31-NOTES.md) or on any allocation.
#include <algorithm>
#include <ctime>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <set>
#include <string>
#include <tuple>
#include <vector>
#if defined(__GLIBC__) && !defined(__SANITIZE_ADDRESS__)
#define ALLOC_COUNT 1
static thread_local int g_on = 0; static thread_local unsigned long g_cnt = 0;
extern "C" { void* __libc_malloc(size_t); void* __libc_calloc(size_t, size_t); void* __libc_realloc(void*, size_t); void __libc_free(void*);
void* malloc(size_t n) { if (g_on) g_cnt++; return __libc_malloc(n); } void* calloc(size_t a, size_t b) { if (g_on) g_cnt++; return __libc_calloc(a, b); }
void* realloc(void* p, size_t n) { if (g_on) g_cnt++; return __libc_realloc(p, n); } void free(void* p) { if (g_on && p) g_cnt++; __libc_free(p); } }
#else
static int g_on = 0; static unsigned long g_cnt = 0;
#endif
#include "chains/chains.h"
#include "rt/json.h"

// Thread CPU time in milliseconds (POSIX: clock_gettime on the thread clock; Windows: GetThreadTimes, kernel + user in 100 ns units).
static double threadCpuMs() {
#ifdef _WIN32
  FILETIME c, e, k, u; GetThreadTimes(GetCurrentThread(), &c, &e, &k, &u);
  const unsigned long long t = (((unsigned long long)k.dwHighDateTime << 32) | k.dwLowDateTime) + (((unsigned long long)u.dwHighDateTime << 32) | u.dwLowDateTime);
  return (double)t * 1e-4;
#else
  timespec t; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t); return (double)t.tv_sec * 1e3 + (double)t.tv_nsec * 1e-6;
#endif
}
using namespace thunder; using namespace thunder::chains;
int main(int argc, char** argv) {
  const int NCAB = argc > 1 ? std::atoi(argv[1]) : 16000;   // (loading is quadratic in the cables: compile() after each one; 16000 takes about 35 s, so ctest uses 4000)
  const int NV = 510, NS = 510; std::string mods = "{\"modules\":[{\"id\":\"a\",\"type\":\"trackIn\",\"params\":{\"track\":1}},{\"id\":\"c\",\"type\":\"chainOut\",\"params\":{\"track\":1}},{\"id\":\"d\",\"type\":\"chainOut\",\"params\":{\"track\":2}}";
  for (int i = 0; i < NV; i++) mods += ",{\"id\":\"v" + std::to_string(i) + "\",\"type\":\"vca\"}";
  for (int i = 0; i < NS; i++) mods += ",{\"id\":\"s" + std::to_string(i) + "\",\"type\":\"scout\",\"params\":{\"snd\":" + std::to_string(1 + i % 8) + "}}";
  mods += "],\"cables\":[";
  std::set<std::tuple<int, int, int, int>> seen; std::string cab; int nc = 0; uint32_t r = 12345; auto rnd = [&](int n) { r = r * 1664525u + 1013904223u; return (int)((r >> 8) % (uint32_t)n); };
  auto name = [&](int m) { return m == 0 ? std::string("a") : m <= NV ? "v" + std::to_string(m - 1) : "s" + std::to_string(m - NV - 1); };
  // module index space: 0 = a, 1..NV = vca, NV+1..NV+NS = scouts. From: a.out or a vca's out; to: a vca's in / cv or a scout's in / inR.
  while (nc < NCAB) {
    const int f = rnd(NV + 1), t = 1 + rnd(NV + NS); const bool tv = t <= NV; const int tp = rnd(2); const int key = f * 2;
    if (!seen.insert({f, 0, t, tp}).second) continue; (void)key;
    cab += (nc ? "," : "") + std::string("{\"from\":[\"") + name(f) + "\",\"out\"],\"to\":[\"" + name(t) + "\",\"" + (tv ? (tp ? "cv" : "in") : (tp ? "inR" : "in")) + "\"]}"; nc++;
  }
  cab += ",{\"from\":[\"v0\",\"out\"],\"to\":[\"c\",\"in\"]},{\"from\":[\"s0\",\"in\"],\"to\":[\"d\",\"in\"]}";   // (a Chain Out in the component; the last cable is odd on purpose and may be refused: the patch is rebuilt below without it if so)
  Json dummy; JsonDoc doc; std::string text = mods + cab + "]}";
  if (!doc.parse(text)) { std::printf("test document does not parse\n"); return 1; }
  EngineOptions o; o.focus = 0; waves::Host host; ChainsEngine e(48000, o, &host);
  if (!e.loadPatch(doc.root())) { std::printf("patch refused: %s\n", e.lastError().c_str()); return 1; }
  std::vector<double> ts; double worst = 0, sum = 0; const int N = 300; unsigned long allocs = 0;
  for (int i = 0; i < N; i++) {
    const double v = 1 + (i & 1); const double t0 = threadCpuMs(); g_cnt = 0; g_on = 1; e.setParam("c", "track", v); g_on = 0; allocs += g_cnt;
    const double ms = threadCpuMs() - t0;  worst = ms > worst ? ms : worst; sum += ms; ts.push_back(ms); if (ms > 1.0) std::printf("  slow call %d: %.2f ms\n", i, ms);
  }
  for (int i = 0; i < N; i++) { g_cnt = 0; g_on = 1; e.setParam("v3", "_bn", 1 + (i % 15)); g_on = 0; allocs += g_cnt; }
  std::sort(ts.begin(), ts.end()); const double med = ts[ts.size() / 2], p99 = ts[ts.size() * 99 / 100];
  std::printf("chains_param_cost: %d modules, %d cables, %d Sidechain Outs: setParam(track) median %.3f ms, mean %.3f ms, p99 %.3f ms, max %.3f ms over %d calls (thread CPU time); allocator calls inside setParam: %lu\n", (int)(NV + NS + 3), nc + 2, NS, med, sum / N, p99, worst, N, allocs);
  int bad = 0;
#if defined(ALLOC_COUNT)
  if (allocs) { std::printf("FAIL: setParam allocated\n"); bad++; }
#endif
#if !defined(__SANITIZE_ADDRESS__) && !defined(__SANITIZE_THREAD__)
  if (p99 > 2.0) { std::printf("FAIL: the 99th percentile is above 2 ms\n"); bad++; }
#endif
  return bad ? 1 : 0;
}
