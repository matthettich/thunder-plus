// U14: "every effect starts in zeroed memory". dsp/tfx/tfx.h's Fx::operator new does malloc + memset(0) before the constructor, and members the
// effects never initialise rely on it. A compiler may delete a store that comes before the constructor (GCC's -flifetime-dse: the reason for
// -fno-lifetime-dse in core/dsp/CMakeLists.txt, GCC only), so every compiler in CI has to prove the zero-fill took place.
// The effect sources' malloc is thunder_fx_malloc (core/dsp/fx_alloc.h, force-included): here it fills every allocation with a pattern
// first. The host creates each of the 34 effects by its own path (FxHost::plan); the output of the first blocks must be the same bits
// as with allocations filled with zeros. If the zero-fill were gone, the pattern would be the starting state and the output would differ.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "fx/fxhost.h"

#ifndef THUNDER_HAVE_TFX
int main() { std::printf("fxzero: not run on this platform: no DSP library (the effects are not built here)\n"); return 77; }
#else
extern "C" void thunder_fx_poison(int byte);
extern "C" void* thunder_fx_malloc(size_t n);
using namespace thunder;

static std::vector<float> runOne(int id, int poison) {
  thunder_fx_poison(poison);
  FxHost H(48000.0); FxEntry e; e.t = id; e.slot = 0; e.on = true; std::snprintf(e.key, kFxKey, "0:%s", fx_key_of(id)); e.nvals = fx_param_count(id);
  for (int k = 0; k < e.nvals; k++) e.vals[k] = fx_param_default(id, k);
  fx_map(id, e.vals, 48000.0, 120.0, 0.0, e.p);
  FxPlan* p = H.plan(&e, 1);   // the effect is made here, with memory from the poisoned allocator
  thunder_fx_poison(-1);
  H.commit(p); delete p;
  std::vector<float> out; uint32_t sd = 99;
  for (int b = 0; b < 400; b++) {
    float L[128], R[128];
    for (int i = 0; i < 128; i++) { sd = sd * 1664525u + 1013904223u; L[i] = (float)(sd >> 8) / 16777216.f - 0.5f; sd = sd * 1664525u + 1013904223u; R[i] = (float)(sd >> 8) / 16777216.f - 0.5f; }
    H.process(L, R, 128, b * 128 / 48000.0);
    out.insert(out.end(), L, L + 128); out.insert(out.end(), R, R + 128);
  }
  return out;
}

int main() {
  int fails = 0;
  {   // the poison reaches the allocation the effects use
    thunder_fx_poison(0xA5); unsigned char* q = (unsigned char*)thunder_fx_malloc(64); thunder_fx_poison(-1);
    bool ok = q != nullptr; for (int i = 0; ok && i < 64; i++) ok = q[i] == 0xA5; std::free(q);
    std::printf("poisoned allocator fills with the pattern  %s\n", ok ? "ok" : "FAIL"); if (!ok) fails++;
  }
  int n = 0;
  for (int id = 0; id < 34; id++) {
    const std::vector<float> z = runOne(id, 0x00);
    bool same = true;
    for (int poison : {0xA5, 0xFF, 0x5A}) { const std::vector<float> a = runOne(id, poison); if (a.size() != z.size() || std::memcmp(a.data(), z.data(), z.size() * sizeof(float)) != 0) same = false; }
    double sum = 0; for (float v : z) sum += std::fabs(v);
    if (!same) { std::printf("%-12s starts from the allocator's pattern, not from zeros  FAIL\n", fx_key_of(id)); fails++; }
    else if (!(sum > 0)) { std::printf("%-12s output is silent: nothing was compared  FAIL\n", fx_key_of(id)); fails++; }
    else n++;
  }
  std::printf("fxzero: %d of 34 effects start from zeroed memory whatever malloc returned  %s\n", n, fails ? "FAIL" : "ok");
  return fails ? 1 : 0;
}
#endif
