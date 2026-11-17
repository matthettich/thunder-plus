// U28: FmDrumCore (XENG fmdrum): three-operator FM percussion. index.html FmDrumCore.render, line for line. No allocation.
#include "engine/xeng_util.h"

namespace thunder {
using namespace xe;

size_t xeng_fmdrum(const FmDrumP& p, double sr, float* out, size_t cap) {
  const double dec = mx(0.001, p.dec / 1000), nd = mx(0.001, p.ndec / 1000);
  const double len = mn(6, mx(dec * (4 + p.curve * 4), p.noise > 0 ? nd * 7 : 0) + 0.01), Nd = mx(1, std::ceil(len * sr));
  if (!okN(Nd)) return 0;
  const size_t N = (size_t)Nd;
  if (!out) return N;
  if (N > cap) return 0;
  KeysRnd rnd(0xfd0d);
  const double f0 = p.pitch * js_pow(2, orz(p.tune) / 12);
  const double alg = mx(0, mn(2, js_round(p.alg))), I1 = p.i1 * p.i1 * 14, I2 = p.i2 * p.i2 * 14, bT = mx(0.002, p.bendT / 1000);
  const double d1 = mx(0.002, p.idec1 / 1000), d2 = mx(0.002, p.idec2 / 1000), fbk = p.fb * p.fb * 2.5;
  double pc = 0, p1 = 0, p2 = 0, m2p = 0, lp = 0, lpn = 0, lpn2 = 0;
  const double nk = 1 - js_exp(-2 * kPi * mn(sr * 0.45, p.ncut) / sr), tk = 1 - js_exp(-2 * kPi * mn(sr * 0.45, 400 + p.tone * p.tone * 18000) / sr);
  for (size_t i = 0; i < N; i++) {
    const double t = (double)i / sr, f = mn(sr * 0.4, f0 * js_pow(2, p.bend / 12 * js_exp(-t / bT))), w = 6.283185307 * f / sr;
    const double env = js_exp(-t / dec) * (1 - p.curve) + p.curve * (t < dec ? 1 - js_pow(t / dec, 2) * 0.5 : 0.5 * js_exp(-(t - dec) / (dec * 0.4)));
    const double e1 = I1 * js_exp(-t / d1), e2 = I2 * js_exp(-t / d2);
    p2 += w * p.r2; p1 += w * p.r1; pc += w;
    const double m2 = js_sin(p2 + fbk * m2p); m2p = m2;
    double m1, c;
    if (alg == 0) { m1 = js_sin(p1 + e2 * m2); c = js_sin(pc + e1 * m1); }
    else if (alg == 1) { m1 = js_sin(p1); c = js_sin(pc + e1 * m1 + e2 * m2); }
    else { m1 = js_sin(p1 + e2 * m2); c = js_sin(pc + e1 * m1 + e2 * 0.5 * m2); }
    lp += (c - lp) * tk; double y = lp * env;
    if (p.noise > 0) { const double wn = rnd() * 2 - 1; lpn += (wn - lpn) * nk; lpn2 += (lpn - lpn2) * nk; y += lpn2 * p.noise * 1.6 * js_exp(-t / nd); }
    if (p.click > 0 && t < 0.004) y += p.click * (rnd() * 2 - 1) * (1 - t / 0.004) * 0.8;
    out[i] = (float)y;
  }
  return finish(out, N, p.level, p.drive, sr, 0.003);
}

}  // namespace thunder
