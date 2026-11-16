// U28: KeysUtil (bank / add / finish / rnd) and the helpers every XENG engine uses. Internal to core/src/engine/xeng_*.cpp.
#ifndef THUNDER_ENGINE_XENG_UTIL_H
#define THUNDER_ENGINE_XENG_UTIL_H
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "engine/layers.h"
#include "engine/xeng.h"
#include "rt/jsmath.h"

namespace thunder {
namespace xe {
using l23::mx;   // Math.max(a, b): NaN wins, +0 beats -0
using l23::mn;   // Math.min(a, b)
constexpr double kPi = 3.141592653589793;
inline double c01(double v) { return mx(0, mn(1, v)); }     // Math.max(0, Math.min(1, v))
inline double orz(double x) { return (x != 0 && x == x) ? x : 0; }   // x || 0
inline double orv(double x, double d) { return (x != 0 && x == x) ? x : d; }   // x || d
inline int32_t i32(double x) { return js_to_int32(x); }     // x | 0
inline double jabs(double x) { return std::fabs(x); }
// `new Float32Array(N)` size check shared by every engine: N must be representable; (size_t)N is returned, 0 if not
inline bool okN(double N) { return N >= 1 && N < 4e9; }

// KeysUtil.rnd(seed): s = (s * 1664525 + 1013904223) >>> 0; s / 4294967296 (the product is below 2^53: exact in doubles, so uint32 arithmetic gives the same bits)
struct KeysRnd { uint32_t s; explicit KeysRnd(double seed) : s(js_to_uint32(seed)) {} double operator()() { s = s * 1664525u + 1013904223u; return (double)s / 4294967296.0; } };

// KeysUtil.bank / add: rotating phasors, arrays of doubles (JS Arrays, not typed arrays: no float32 rounding)
constexpr int kBankMax = 256;   // partials count is clamped to 256, the piano makes at most 64 partials x 3 strings
struct KeysBank {
  double sr, c[kBankMax], s[kBankMax], wc[kBankMax], ws[kBankMax], a[kBankMax]; int n = 0;
  explicit KeysBank(double sr_) : sr(sr_) {}
  int add(double f, double phase) { const double w = 2 * kPi * f / sr; c[n] = js_cos(phase); s[n] = js_sin(phase); wc[n] = js_cos(w); ws[n] = js_sin(w); a[n] = 0; return n++; }
};

// The tail every engine ends with: end = last sample with |x| >= 1e-4 (at least 1), a fade of round(sr * fade) samples into the new end. Returns the new length.
size_t trim_fade(float* out, size_t N, double sr, double fade);
// KeysUtil.finish(out, p, sr, tailFade): peak-normalise to 0.8 * level, drive (tanh), then trim_fade. Returns the length.
size_t finish(float* out, size_t N, double level, double drive, double sr, double tailFade);
// The peak / level / drive pass alone (the engines that mix their own tail in between: Modal, Swarm, Drift use the same lines inline)
inline double peak_of(const float* out, size_t N) { double pk = 0; for (size_t i = 0; i < N; i++) pk = mx(pk, jabs((double)out[i])); return pk; }
void level_drive(float* out, size_t N, double level, double drive, double scale);   // lv = Math.pow(10, (level || 0) / 20) * scale, then the tanh drive: scale is (pk > 0 ? 0.8 / pk : 1), Tone's is 0.5
}  // namespace xe
}  // namespace thunder
#endif
