// U28: BreathCore (XENG breath): coloured noise through a bank of resonant band-passes, grains, a wind drift, a high-pass and a low-pass. index.html BreathCore.render + KeysUtil. No allocation (at most 8 bands).
#include "engine/xeng_util.h"

namespace thunder {
using namespace xe;
namespace {
struct Bq { double r, b0, a1, a2, x1, x2, y1, y2; };
}

size_t xeng_breath(const BreathP& p, double sr, double hold, float* out, size_t cap) {
  const double h = mx(0.01, orv(hold, 0.3)), atk = mx(0.0005, p.atk / 1000), dec = mx(0.001, p.dec / 1000), rel = mx(0.001, p.rel / 1000);
  const double len = mn(10, (p.sus > 0.01 ? h : mn(h, atk + dec * 6)) + rel * 6 + 0.02), Nd = mx(1, std::ceil(len * sr));
  if (!okN(Nd)) return 0;
  const size_t N = (size_t)Nd;
  if (!out) return N;
  if (N > cap) return 0;
  KeysRnd rnd(0xb4ea7); const double kf = js_pow(2, orz(p.tune) / 12);
  const int nb = cidx(js_round(p.bands), 1, 8); static const double skew[8] = {0, 0.27, -0.19, 0.41, -0.33, 0.52, -0.12, 0.61};
  Bq bq[8]; for (int b = 0; b < nb; b++) bq[b] = Bq{js_pow(1 + p.spread * 1.5, b) * (1 + p.metal * skew[b] * 0.9), 0, 0, 0, 0, 0, 0, 0};
  const double fkey = js_pow(kf, p.key), Q = 0.7 + p.q * p.q * 60, wet = mx(0, mn(1, p.body));
  double br = 0, bl = 0, hp1 = 0, lp1 = 0, lp2 = 0, env = 0, gEnv = 0, nextG = 0, wind = 0, wtarget = 0, wn = 0; int st = 0;
  const size_t CR = 32; const double col = p.color;
  for (size_t i0 = 0; i0 < N; i0 += CR) {
    const double t = (double)i0 / sr, fe = js_exp(-t / mx(0.003, p.fdec / 1000));
    if (--wn <= 0) { wtarget = rnd() * 2 - 1; wn = js_round(sr * (0.05 + rnd() * 0.25) / (double)CR); } wind += (wtarget - wind) * 0.04;
    const double swp = js_pow(2, p.sweep * 3 * mn(1, t / h)), fmul = fkey * swp * (1 + p.wind * 0.25 * wind);
    for (int b = 0; b < nb; b++) { Bq& q = bq[b]; const double f = mn(sr * 0.45, mx(20, p.freq * q.r * fmul)), w = 2 * kPi * f / sr, al = js_sin(w) / (2 * Q), a0 = 1 + al; q.b0 = al / a0; q.a1 = -2 * js_cos(w) / a0; q.a2 = (1 - al) / a0; }
    const double lpf = mn(sr * 0.45, mx(20, p.lp * js_pow(2, p.fenv * 4 * fe) * (1 + p.wind * 0.4 * wind))), gl = js_tan(kPi * lpf / sr), kl = 1.3, al1 = 1 / (1 + gl * (gl + kl)), al2 = gl * al1;
    const double hpc = js_exp(-2 * kPi * mx(10, p.hp) / sr);
    const size_t i1 = (size_t)mn((double)N, (double)(i0 + CR));
    for (size_t i = i0; i < i1; i++) {
      const double tt = (double)i / sr;
      if (tt < h) { if (st == 0) { env += 1 / (atk * sr); if (env >= 1) { env = 1; st = 1; } } else env += (p.sus - env) * (1 / (dec * sr)) * 2.2; }
      else env -= env * (1 / (rel * sr)) * 3;
      const double w = rnd() * 2 - 1; br = br * 0.995 + w * 0.05; const double blue = w - bl; bl = w;
      double n = col < 0.5 ? br * (1 - col * 2) * 4 + w * col * 2 : w * (2 - col * 2) + blue * (col * 2 - 1) * 0.7;
      if (p.grain > 0) { if (--nextG <= 0) { gEnv = 0.6 + rnd() * 0.8; nextG = mx(1, js_round(sr / p.grate * (0.3 + rnd() * 1.4))); } gEnv *= 1 - 8 * p.grate / sr; n *= (1 - p.grain) + p.grain * gEnv * 2; }
      double y = 0; for (int b = 0; b < nb; b++) { Bq& q = bq[b]; const double v = q.b0 * (n - q.x2) - q.a1 * q.y1 - q.a2 * q.y2; q.x2 = q.x1; q.x1 = n; q.y2 = q.y1; q.y1 = v; y += v; }
      y = n * (1 - wet) + y * wet * (1 + p.q * 4) / std::sqrt((double)nb);
      hp1 = hp1 * hpc + y * (1 - hpc); y -= hp1;
      const double v1 = al1 * lp1 + al2 * (y - lp2), v2 = lp2 + gl * v1; lp1 = 2 * v1 - lp1; lp2 = 2 * v2 - lp2; y = v2;
      out[i] = (float)(y * env * (1 + p.wind * 0.6 * wind));
    }
  }
  return finish(out, N, p.level, p.drive, sr, 0.003);
}

}  // namespace thunder
