// Number helpers for values that come from a song file, a message or the host (N1 review C1 / C3). Header only, no allocation.
#ifndef THUNDER_RT_NUM_H
#define THUNDER_RT_NUM_H
#include <cstddef>
#include <cstdint>

namespace thunder {

// A double as an int in [lo, hi]: NaN gives lo, anything beyond saturates (the plain C cast is undefined for NaN and out-of-range
// values: INT_MIN on x86, 0 or saturation on arm64). Truncates toward zero like the cast. Use ONLY where the value is not already
// proven to be in range.
inline int to_int_sat(double v, int lo, int hi) {
  if (!(v == v)) return lo;
  if (v <= (double)lo) return lo;
  if (v >= (double)hi) return hi;
  return (int)v;
}
inline int64_t to_i64_sat(double v, int64_t lo, int64_t hi) {
  if (!(v == v)) return lo;
  if (v <= (double)lo) return lo;
  if (v >= 9.2e18 || v >= (double)hi) return hi;   // (double)INT64_MAX rounds up to 2^63: compare below it
  return (int64_t)v;
}
inline size_t to_size_sat(double v, size_t hi) { return (size_t)to_i64_sat(v, 0, (int64_t)hi); }

// Bit t of a 32-bit mask, false for a t outside 0..31 (a shift by 32 or more is undefined: the preview bus is track 32, the master column 32).
inline bool bit32(uint32_t m, int t) { return t >= 0 && t < 32 && ((m >> t) & 1u) != 0; }

}  // namespace thunder
#endif
