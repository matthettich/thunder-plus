// U23: the wavetable layer (wavetableOsc): four band-limited spectra morphed (sine, triangle, saw, square), a formant tilt, a phase bend, up to 8 detuned unison voices, a sub and noise.
// The single cycle is rebuilt only when the pitch rises past the harmonic limit already built (blk).
#include "engine/layers.h"
#include "rt/jsmath.h"

namespace thunder {
using l23::mn; using l23::mx; using l23::orv;

void WavetableOsc::init(const EngLayer& L, double sr_, double A_, double B_, EngRng& R) {
  const EngU23& U = L.u23;
  sr = sr_; A = A_; B = B_;
  for (int i = 0; i <= TN; i++) tab[i] = 0;
  uni = (int)mx(1, mn(8, js_round(orv(U.wtUni, 1))));
  const double spread = orv(U.wtSpread, 0); sub = orv(U.wtSub, 0); noise = orv(U.wtNoise, 0); bend = orv(U.wtBend, 0);
  for (int v = 0; v < 8; v++) { phases[v] = 0; dets[v] = 0; }
  for (int v = 0; v < uni; v++) { const double o = uni == 1 ? 0 : ((double)v / (uni - 1)) * 2 - 1; dets[v] = js_pow(2, o * spread * 0.5 / 12); phases[v] = R.next(); }
  builtH = -1; subPh = 0;
}

void WavetableOsc::build(double hmax) {
  double W[4] = {0, 0, 0, 0};
  const double seg = mx(0, mn(1, A)) * 3; const int i0 = (int)mn(2, (double)js_to_int32(seg)); const double tf = seg - i0;
  W[i0] += 1 - tf; W[(int)mn(3, i0 + 1)] += tf;
  for (int i = 0; i <= TN; i++) {
    const double p = (double)i / TN; double y = 0;
    for (int k = 1; (double)k <= hmax; k++) {
      const int odd = k & 1, sine = k == 1 ? 1 : 0; const double tri = odd ? 1.0 / ((double)k * k) : 0, saw = 1.0 / k, sq = odd ? 1.0 / k : 0;
      y += (W[0] * sine + W[1] * tri + W[2] * saw + W[3] * sq) * js_pow((double)k, -B * 1.5) * js_sin(2 * l23::kPi * k * p);
    }
    tab[i] = (float)y;
  }
  double pk = 0; for (int i = 0; i < TN; i++) pk = mx(pk, std::fabs((double)tab[i]));
  if (pk > 0) for (int i = 0; i <= TN; i++) tab[i] = (float)((double)tab[i] / pk);
  builtH = hmax;
}

void WavetableOsc::blk(double inc) {
  const double f = mx(inc * sr, 1), h = mx(1, mn(64, std::floor(sr * 0.5 / f) - 1));
  if (builtH < 0 || h < builtH) build(h);
}

double WavetableOsc::s(double inc, EngRng& R) {
  auto tb = [&](int i) { return (i >= 0 && i <= TN) ? (double)tab[i] : (double)NAN; };   // tab[i] past the table is undefined
  double y = 0;
  for (int v = 0; v < uni; v++) {
    const double p = phases[v];
    const double w = (bend == 0 || bend != bend) ? p : (bend > 0 ? js_pow(p, 1 + bend * 3) : 1 - js_pow(1 - p, 1 - bend * 3));   // warp
    const double x = w * TN; const int32_t i = js_to_int32(x); const double fr = x - i;
    y += tb(i) + (tb(i + 1) - tb(i)) * fr;
    phases[v] += inc * dets[v]; if (phases[v] >= 1) phases[v] -= std::floor(phases[v]);
  }
  y /= uni;
  if (sub > 0) { y += sub * eng_sinT(subPh); subPh += inc * 0.5; }
  if (noise > 0) y += noise * (R.next() * 2 - 1);
  return y;
}
}  // namespace thunder
