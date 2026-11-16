// U21: the helpers the "basic studio effects" and the Oct 2026 voice effects share in chains-worklet.js (fxGuard, fxSat, fxClean, fxHermAt, bfK, bfStep, bfTidy, soft, the Source key),
// as the JS has them (doubles, the JS's arithmetic order). Kept apart from util.h so the units that add modules in parallel do not touch the same file.
#ifndef THUNDER_MODFX_BFUTIL_H
#define THUNDER_MODFX_BFUTIL_H
#include "modfx/util.h"

namespace thunder {
namespace modfx {

// soft = x => { const a = Math.abs(x); return a < .8 ? x : Math.sign(x) * (.8 + .2 * Math.tanh((a - .8) / .2)); }
inline double soft(double x) { const double a = std::fabs(x); return a < .8 ? x : jsign(x) * (.8 + .2 * js_tanh((a - .8) / .2)); }
// fxGuard: untouched below +-2, rounds off into +-4 above, a NaN becomes silence
inline double fxGuard(double y) { const double a = y < 0 ? -y : y; return a < 2 ? y : a < 1e9 ? (y < 0 ? -1 : 1) * (2 + 2 * js_tanh((a - 2) * .5)) : 0; }
inline double fxSat(double v) { return 2 * soft(v * .5); }
inline double fxClean(double v) { return (v > 1e-20 || v < -1e-20) && v > -64 && v < 64 ? v : 0; }
// bfTempo = ctx => Math.max((ctx.transport && ctx.transport.tempo) || 120, 20)
inline double bfTempo(const Ctx& c) { return jmax(truthy(c.tempo) ? c.tempo : 120, 20); }
// bfK: one-pole coefficient for a time constant in seconds
inline double bfK(double t, double sr) { return 1 - js_exp(-1 / (sr * jmax(t, 1e-5))); }
// an index into a table of n from a Number the JS clamps (a NaN would be a TypeError in the JS: 0 here)
inline int tidx(double v, int n) { return v == v ? (int)clamp(v, 0, n - 1) : 0; }

struct Svf { double z1 = 0, z2 = 0, lp = 0, bp = 0, hp = 0; };
inline void bfStep(Svf& s, double x, double g, double k) {
  const double a1 = 1 / (1 + g * (g + k)), a2 = g * a1, a3 = g * a2, v3 = x - s.z2, v1 = a1 * s.z1 + a2 * v3, v2 = s.z2 + a2 * s.z1 + a3 * v3;
  s.z1 = 2 * v1 - s.z1; s.z2 = 2 * v2 - s.z2; s.lp = v2; s.bp = v1; s.hp = x - k * v1 - v2;
}
inline void bfTidy(Svf& s) {
  if (!(s.z1 > -1e4 && s.z1 < 1e4 && s.z2 > -1e4 && s.z2 < 1e4)) { s.z1 = s.z2 = 0; }
  s.z1 = fxClean(s.z1); s.z2 = fxClean(s.z2);
}

// fxHerm4 / fxHermAt: 4-point Hermite read of a power-of-two float32 buffer (mask m) at a fractional index
inline double fxHerm4(double a, double b, double c, double d, double f) {
  const double c1 = .5 * (c - a), c2 = a - 2.5 * b + 2 * c - .5 * d, c3 = .5 * (d - a) + 1.5 * (b - c);
  return ((c3 * f + c2) * f + c1) * f + b;
}
inline double fxHermAt(const float* X, int32_t m, double pos) {
  const double i = std::floor(pos), f = pos - i; const int32_t ii = i32(i);
  return fxHerm4((double)X[(ii - 1) & m], (double)X[ii & m], (double)X[(ii + 1) & m], (double)X[(ii + 2) & m], f);
}

// ctx.key(sck): the Source's mono buffer (null: Self). The host sets Ctx::key; none set: null, as `ctx.key` undefined in the JS.
inline const float* keyOf(const Ctx& c, double sck) { return c.key ? c.key(c.keyUser, sck) : nullptr; }

}  // namespace modfx
}  // namespace thunder
#endif
