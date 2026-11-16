// Host builds only: rand() of the effect sources (Airwindows seeds its dither state with it, Cloud Seed its random buffers) as wasi-libc's
// (musl's) generator: a 64-bit LCG starting from seed 0, so a native effect starts from the same numbers as the same effect in tfx.wasm
// (glibc's rand() gives other numbers, and the Airwindows reverbs and the tape then differ from the wasm at -20 to -40 dB).
// The wasm worklet has one module (one state) per node; the host selects the state of the FxHost it is working for.
#include <cstdint>
namespace { uint64_t g_default = 0; thread_local uint64_t* g_cur = nullptr; }
extern "C" void thunder_wasi_rand_use(uint64_t* state) { g_cur = state; }
extern "C" int thunder_wasi_rand(void) { uint64_t& s = g_cur ? *g_cur : g_default; s = 6364136223846793005ULL * s + 1; return (int)(s >> 33); }
