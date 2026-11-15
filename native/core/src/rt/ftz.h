// Flush-to-zero / denormals-are-zero for the length of one audio callback (U05). A decaying filter or reverb tail reaches
// denormal numbers, which cost 10 to 100 times a normal operation on x86 and stall the callback. Set on entry, restored on
// exit, so the host's own floating-point mode is left as it was. No effect on parity: it only changes values below 1e-308
// (doubles) / 1e-38 (floats).
#ifndef THUNDER_RT_FTZ_H
#define THUNDER_RT_FTZ_H
#include <cstdint>

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <xmmintrin.h>
#define THUNDER_FTZ_X86 1
#elif defined(__aarch64__) && (defined(__GNUC__) || defined(__clang__))
#define THUNDER_FTZ_A64 1
#endif

namespace thunder {

struct ScopedFtz {
#if defined(THUNDER_FTZ_X86)
  unsigned int saved;
  ScopedFtz() : saved(_mm_getcsr()) { _mm_setcsr(saved | 0x8040u); }   // FTZ (bit 15) + DAZ (bit 6)
  ~ScopedFtz() { _mm_setcsr(saved); }
#elif defined(THUNDER_FTZ_A64)
  uint64_t saved;
  ScopedFtz() { __asm__ __volatile__("mrs %0, fpcr" : "=r"(saved)); const uint64_t v = saved | (1ull << 24); __asm__ __volatile__("msr fpcr, %0" : : "r"(v)); }   // FPCR.FZ
  ~ScopedFtz() { __asm__ __volatile__("msr fpcr, %0" : : "r"(saved)); }
#else
  ScopedFtz() {}   // wasm and others: no denormal penalty to avoid, or no control over it
#endif
  ScopedFtz(const ScopedFtz&) = delete;
  ScopedFtz& operator=(const ScopedFtz&) = delete;
};

}  // namespace thunder
#endif
