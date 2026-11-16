// U28: PhaseCore (XENG phase): phase distortion, two oscillators, ring modulation, rendered at twice the rate. index.html PhaseCore.render / osc, line for line. No allocation.
#include "engine/xeng_util.h"

namespace thunder {
using namespace xe;
namespace {
const double TAU = 6.283185307;
inline double bend(double u, double k) { return u < k ? u * 0.5 / k : 0.5 + (u - k) * 0.5 / (1 - k); }
double osc(int wv, double x, double d) {
  switch (wv) {
    case 0: return js_cos(TAU * bend(x, 0.5 - d * 0.49));
    case 1: { const double hh = x < 0.5 ? 0 : 0.5, u = (x - hh) * 2, k = 1 - d * 0.97, ph = u < k ? u * 0.5 / k : 0.5; return js_cos(TAU * (hh + ph)); }
    case 2: { const double k = 1 - d * 0.9; return x < k ? js_cos(TAU * x / k) : 1; }
    case 3: return js_cos(2 * TAU * bend(x, 0.5 - d * 0.49));
    case 4: { if (x < 0.5) return js_cos(TAU * bend(x * 2, 0.5 - d * 0.49)); const double u = (x - 0.5) * 2, k = 1 - d * 0.9; return u < k ? js_cos(TAU * u / k) : 1; }
    case 5: return 1 - (1 - x) * (1 - js_cos(TAU * x * (1 + d * 15)));
    case 6: return 1 - (1 - jabs(2 * x - 1)) * (1 - js_cos(TAU * x * (1 + d * 15)));
    default: return 1 - mn(1, 2 * (1 - x)) * (1 - js_cos(TAU * x * (1 + d * 15)));
  }
}
}  // namespace

size_t xeng_phase(const PhaseP& p, double sr, double hold, float* out, size_t cap) {
  const double h = mx(0.02, orv(hold, 1));
  const double atk = mx(0.0005, p.atk / 1000), dec = mx(0.001, p.dec / 1000), rel = mx(0.001, p.rel / 1000);
  const double da = mx(0.0005, p.datk / 1000), dd = mx(0.005, p.ddec / 1000), dr = mx(0.005, p.drel / 1000);
  const double len = mn(12, h + mx(rel, dr) * 6 + 0.05), Nd = mx(1, std::ceil(len * sr));
  if (!okN(Nd)) return 0;
  const size_t N = (size_t)Nd;
  if (!out) return N;
  if (N > cap) return 0;
  KeysRnd rnd(0x9d15);
  const double f0 = 65.406 * js_pow(2, p.oct + orz(p.tune) / 12);
  const int OS = 2; const double sr2 = sr * OS;
  const int w1 = (int)mx(0, mn(7, js_round(p.wave))), w2 = (int)mx(0, mn(7, js_round(p.wave2))); const double mix = p.mix;
  const double inc1 = f0 / sr2, inc2 = f0 * js_pow(2, p.det * p.det * 50 / 1200) / sr2, kf = js_pow(261.6 / f0, p.key * 0.7);
  double x1 = 0, x2 = 0.25, env = 0, denv = 0; int st = 0, dst = 0;
  for (size_t i = 0; i < N; i++) {
    const double t = (double)i / sr;
    if (t < h) {
      if (st == 0) { env += 1 / (atk * sr); if (env >= 1) { env = 1; st = 1; } } else env += (p.sus - env) * 2.2 / (dec * sr);
      if (dst == 0) { denv += 1 / (da * sr); if (denv >= 1) { denv = 1; dst = 1; } } else denv += (p.dsus - denv) * 2.2 / (dd * sr);
    } else { env -= env * 3 / (rel * sr); denv -= denv * 3 / (dr * sr); }
    const double d = mx(0, mn(1, p.dcw * denv * kf));
    double y = 0;
    for (int k = 0; k < OS; k++) {
      x1 += inc1; if (x1 >= 1) x1 -= 1; x2 += inc2; if (x2 >= 1) x2 -= 1;
      const double a = osc(w1, x1, d), b = osc(w2, x2, d);
      y += a * (1 - mix) + b * mix + p.ring * a * b;
    }
    y /= OS;
    if (p.noise > 0) y += p.noise * 0.5 * (rnd() * 2 - 1);
    out[i] = (float)(y * env);
  }
  return finish(out, N, p.level, p.drive, sr, 0.003);
}

}  // namespace thunder
