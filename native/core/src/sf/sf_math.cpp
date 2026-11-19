#include "sf/sf_math.h"

#include <cstdint>
#include <cstring>

#include "rt/jsmath.h"

namespace thunder {
namespace sf {

double sf_log10(double x) {
  static const double two54 = 1.80143985094819840000e+16, ivln10 = 4.34294481903251816668e-01, log10_2hi = 3.01029995663611771306e-01, log10_2lo = 3.69423907715893078616e-13;
  uint64_t bits; std::memcpy(&bits, &x, 8);
  int32_t hx = (int32_t)(bits >> 32); uint32_t lx = (uint32_t)bits;
  int32_t k = 0;
  if (hx < 0x00100000) {   // x < 2**-1022
    if (((hx & 0x7fffffff) | (int32_t)lx) == 0) return -INFINITY;   // log(+-0) = -inf
    if (hx < 0) return NAN;                                          // log(-#) = NaN
    k -= 54; x *= two54; std::memcpy(&bits, &x, 8); hx = (int32_t)(bits >> 32);
  }
  if (hx >= 0x7ff00000) return x + x;
  if (hx == 0x3ff00000 && lx == 0) return 0.0;   // log(1) = +0
  k += (hx >> 20) - 1023;
  const int32_t i = (int32_t)(((uint32_t)k & 0x80000000u) >> 31);
  hx = (hx & 0x000fffff) | ((0x3ff - i) << 20);
  const double y = (double)(k + i);
  std::memcpy(&bits, &x, 8); bits = ((uint64_t)(uint32_t)hx << 32) | (bits & 0xffffffffu); std::memcpy(&x, &bits, 8);
  const double z = y * log10_2lo + ivln10 * js_log(x);
  return z + y * log10_2hi;
}

}  // namespace sf
}  // namespace thunder
