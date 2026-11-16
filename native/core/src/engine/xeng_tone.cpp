// U28: ToneDrum (XENG tone): a drum tone with a pitch bend, a filtered noise layer, a click, a distortion and its own tail. index.html ToneDrum.render, line for line. No allocation.
#include "engine/xeng_util.h"

namespace thunder {
using namespace xe;

size_t xeng_tone(const ToneP& p, double sr, float* out, size_t cap) {
  const double len = mn(6, mx(p.tdec, p.nDec) / 1000 * 7 + 0.02), Nd = mx(1, std::ceil(len * sr));
  if (!okN(Nd)) return 0;
  const size_t N = (size_t)Nd;
  if (!out) return N;
  if (N > cap) return 0;
  const double f0 = p.pitch * js_pow(2, orz(p.tune) / 12); const int32_t wv = i32(p.wave);
  static const double MR[8] = {1, 1.594, 2.136, 2.296, 2.653, 2.918, 3.156, 3.501};
  const double td = mx(1, p.tdec) / 1000, bt = mx(1, p.bendT) / 1000, nd = mx(1, p.nDec) / 1000;
  double ph = 0, phs[8] = {0, 0, 0, 0, 0, 0, 0, 0}, s0 = 0, s1 = 0, dcx = 0, dc = 0; uint32_t rnd = 1234;
  const int32_t nt = i32(p.nType); const double q = 0.5 + p.nRes * p.nRes * (nt == 3 ? 40 : 8);
  for (size_t i = 0; i < N; i++) {
    const double t = (double)i / sr, fe = js_exp(-t / bt), f = f0 * js_pow(2, p.bend * fe / 12), inc = f / sr, aT = js_exp(-t / td * 1.0);
    ph += inc; if (ph >= 1) ph -= 1;
    double tone;
    if (wv == 0) tone = js_sin(2 * kPi * ph);
    else if (wv == 1) tone = 4 * jabs(ph - 0.5) - 1;
    else if (wv == 2) tone = (2 * ph - 1) * 0.7;
    else if (wv == 3) tone = (ph < 0.5 ? 1 : -1) * 0.6;
    else { tone = 0; for (int k = 0; k < 8; k++) { phs[k] += inc * MR[k]; if (phs[k] >= 1) phs[k] -= 1; tone += js_sin(2 * kPi * phs[k]) / (1 + k * (1.5 - p.spectra)); } tone *= 0.45; }
    if (p.spectra > 0 && wv != 4) { for (int k = 1; k < 4; k++) { phs[k] += inc * MR[k]; if (phs[k] >= 1) phs[k] -= 1; tone += js_sin(2 * kPi * phs[k]) * p.spectra * 0.35 / k; } }
    rnd = rnd * 1664525u + 1013904223u; const double w = (double)rnd / 2147483648.0 - 1;
    const double nf = mn(sr * 0.45, p.nFreq * js_pow(2, p.nSweep * 4 * js_exp(-t / 0.05))), g = js_tan(kPi * nf / sr), r = 1 / q;
    const double hh = (w - (r + g) * s0 - s1) / (1 + r * g + g * g), bp = g * hh + s0; s0 = g * hh + bp; const double lp = g * bp + s1; s1 = g * bp + lp;
    const double nz = nt == 0 ? lp : nt == 2 ? hh : nt == 3 ? bp * 2 / std::sqrt(q) : bp * 1.5;
    const double cl = t < 0.004 ? (1 - t / 0.004) * (i % 2 ? -1 : 1) * 0.3 + (t < 0.0008 ? 1 : 0) : 0;
    double y = tone * aT * p.tLev + nz * js_exp(-t / nd) * p.nLev + cl * p.click;
    if (p.dist > 0) { const double d = 1 + p.dist * 12; y = js_tanh(y * d) / js_tanh(d) * 0.9; }
    const double o = y - dcx + 0.995 * dc; dcx = y; dc = o; out[i] = (float)o;
  }
  level_drive(out, N, p.level, p.drive, 0.5);
  return trim_fade(out, N, sr, 0.002);
}

}  // namespace thunder
