// PFX: bad_alloc sweep over a pad render with library effects (as test_chains_diff does): the n-th allocation of eng_render_voice fails (operator new throws std::bad_alloc), for every n until the
// call succeeds. A failing render must return cleanly (kEngNoMemory, or kEngOk where the failed allocation was the one of an effect that then passed the sound: counted by pad_fx_oom_count()),
// never terminate, and give back everything it made (live allocations back to the start). glibc only (the allocation hooks).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>
#if defined(__GLIBC__) && !defined(__SANITIZE_ADDRESS__) && !defined(__SANITIZE_THREAD__)
#define ALLOC_AUDIT 1
static long g_live = 0, g_failAt = 0, g_failN = 0;
extern "C" { void* __libc_malloc(size_t); void* __libc_calloc(size_t, size_t); void* __libc_realloc(void*, size_t); void __libc_free(void*);
void* malloc(size_t n) { if (g_failAt && ++g_failN == g_failAt) return nullptr; g_live++; return __libc_malloc(n); }
void* calloc(size_t a, size_t b) { if (g_failAt && ++g_failN == g_failAt) return nullptr; g_live++; return __libc_calloc(a, b); }
void* realloc(void* p, size_t n) { if (!p) g_live++; return __libc_realloc(p, n); }
void free(void* p) { if (p) g_live--; __libc_free(p); } }
#endif
#include "engine/engine.h"
#include "engine/engine_json.h"
#include "engine/padfx.h"
#include "rt/json.h"
using namespace thunder;

int main() {
#ifndef ALLOC_AUDIT
  std::printf("SKIP: no allocation hooks on this platform\n"); return 77;
#else
  const char* fxs[] = {"[{\"type\":\"crush\",\"down\":0.3,\"bits\":8,\"mix\":0.6,\"smooth\":false},{\"type\":\"spring\",\"decay\":0.6,\"mix\":0.4},{\"type\":\"tape\",\"input\":0.5,\"soften\":0.5,\"bump\":0.5,\"flutter\":0.5,\"output\":0.5,\"mix\":1},{\"type\":\"rings\",\"model\":0,\"poly\":1,\"note\":48,\"structure\":0.4,\"brightness\":0.5,\"damping\":0.6,\"position\":0.3,\"input\":1,\"mix\":0.5},{\"type\":\"clouds\",\"mode\":0,\"lofi\":true,\"freeze\":false,\"position\":0.1,\"size\":0.5,\"pitch\":0,\"density\":0.6,\"texture\":0.5,\"blend\":0.5,\"feedback\":0.2,\"reverb\":0.5}]"};
  int bad = 0; long total = 0, failing = 0;
  for (const char* js : fxs) {
    std::string doc = std::string("{\"fx\":") + js + "}";
    JsonDoc d; if (!d.parse(doc)) { std::printf("FAIL: json\n"); return 1; }
    EngVoice* V = new EngVoice; V->nFx = (int)d.root().arr("fx").size(); V->len = 0.05; V->tail = 0.05; V->layers[0].on = true; V->layers[0].level = 0.8; V->layers[0].type = LType::Sine;
    for (int i = 0; i < V->nFx; i++) { eng_fx_from_json(V->fx[i], d.root().arr("fx").at((size_t)i)); if (V->fx[i].kind != EngFx::Kind::Lib) { std::printf("FAIL: fx %d not hosted\n", i); return 1; } }
    EngRenderOpts o; o.sr = 16000; o.hold = 0;
    const size_t bound = eng_render_bound(*V, o); std::vector<float> buf(bound + 8);
    const long live0 = g_live; const unsigned long oom0 = pad_fx_oom_count();
    size_t n = 0; int ok = -99;
    for (long k = 1; k < 200000; k++) {
      const long l0 = g_live; g_failN = 0; g_failAt = k; size_t len = 0;
      const int st = eng_render_voice(*V, o, buf.data(), buf.size(), &len);
      const bool hit = g_failN >= k; g_failAt = 0;
      if (g_live != l0) { std::printf("FAIL: allocation %ld failing leaves %+ld block(s) behind (status %d)\n", k, g_live - l0, st); bad++; }
      if (!hit) { ok = st; n = k; break; }   // the call finished before reaching the k-th allocation: no more to sweep
      failing++;
      if (st != kEngNoMemory && st != kEngOk && st != kEngTooSmall) { std::printf("FAIL: allocation %ld failing gives status %d\n", k, st); bad++; }
    }
    total += (long)n;
    if (ok != kEngOk) { std::printf("FAIL: the unfailed render gave status %d\n", ok); bad++; }
    if (g_live != live0) { std::printf("FAIL: live allocations %+ld after the sweep\n", g_live - live0); bad++; }
    std::printf("padfx oom sweep: %ld allocation points, %ld failing renders, pad effects that passed the sound after an out-of-memory: %lu, live left %+ld\n", n, failing, pad_fx_oom_count() - oom0, g_live - live0);
    delete V;
  }
  std::printf("padfx_oom: %d bad\n", bad);
  return bad ? 1 : 0;
#endif
}
