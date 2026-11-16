// U28: SwarmCore (XENG swarm): a cloud of up to 48 detuned oscillators, each on its own smooth random path, a resonant low-pass, ADSR. index.html SwarmCore.render, line for line. No allocation (at most 48 voices).
#include "engine/xeng_util.h"

namespace thunder {
using namespace xe;
namespace {
const int kIvlN[7] = {3, 4, 4, 4, 4, 5, 5};
const double kIvl[7][5] = {{12, -12, 24, 0, 0}, {7, 12, -5, 19, 0}, {4, 7, 12, 16, 0}, {3, 7, 12, 15, 0}, {5, 7, 12, 17, 0}, {1, 2, -1, -2, 3}, {12, 19, 24, 28, 31}};
struct Path { double hz, a, b, ph; };
struct Vc { double ph, det; Path dp, sp; double on, g, dt, lv; };
inline double blep(double t, double dt) { return t < dt ? (t /= dt, t + t - t * t - 1) : t > 1 - dt ? (t = (t - 1) / dt, t * t + t + t + 1) : 0; }
double shape(int k, double ph, double dt) {
  if (k == 0) return 2 * ph - 1 - blep(ph, dt);
  if (k == 1) return (ph < 0.5 ? 1 : -1) + blep(ph, dt) - blep(std::fmod(ph + 0.5, 1.0), dt);
  if (k == 2) return ph < 0.5 ? 4 * ph - 1 : 3 - 4 * ph;
  return js_sin(2 * kPi * ph);
}
}

size_t xeng_swarm(const SwarmP& p, double sr, double hold, float* out, size_t cap) {
  const double atk = mx(0.5, p.atk) / 1000, dec = mx(1, p.dec) / 1000, rel = mx(1, p.rel) / 1000, sus = mx(0, mn(1, p.sus));
  const double h = mx(atk + 0.02, orv(hold, 1)), bloom = mx(0, p.bloom) / 1000;
  const double len = mn(32, h + rel * 1.1 + 0.05), Nd = mx(1, std::ceil(len * sr));
  if (!okN(Nd)) return 0;
  const size_t N = (size_t)Nd;
  if (!out) return N;
  if (N > cap) return 0;
  KeysRnd rnd(0x5eed1234);
  const int nv = (int)mx(1, mn(48, js_round(p.voices))); const double f0 = 65.406 * js_pow(2, p.oct + orz(p.tune) / 12);
  const int ivi = (int)mx(0, mn(6, (double)i32(p.ivl))); const double* IV = kIvl[ivi]; const int ivn = kIvlN[ivi]; const double spreadC = p.spread * p.spread * 120, driftC = p.drift * p.drift * 80;
  auto path = [&](double hz) { Path q; q.hz = hz * (0.6 + rnd() * 0.8); q.a = rnd() * 2 - 1; q.b = rnd() * 2 - 1; q.ph = rnd(); return q; };
  auto step = [&](Path& q, double dt) { q.ph += q.hz * dt; while (q.ph >= 1) { q.ph -= 1; q.a = q.b; q.b = rnd() * 2 - 1; } const double m = (1 - js_cos(kPi * q.ph)) * 0.5; return q.a + (q.b - q.a) * m; };
  Path shared = path(mx(0.01, p.rate) * 0.7); Vc V[48];
  for (int j = 0; j < nv; j++) {
    const double pos = nv > 1 ? (double)j / (nv - 1) * 2 - 1 : 0; const double scat = rnd() < p.scatter ? IV[(int)std::floor(rnd() * ivn)] : 0;
    Vc& v = V[j]; v.ph = rnd(); v.det = (pos + (rnd() - 0.5) * 0.3) * spreadC + scat * 100; v.dp = path(mx(0.01, p.rate)); v.sp = path(2 + p.rate * 3);
    v.on = bloom != 0 ? rnd() * bloom : 0; v.g = scat != 0 ? 0.7 : 1; v.dt = 0; v.lv = 0;
  }
  const double w = mx(0, mn(3, p.wave)), wiD = mn(2, std::floor(w)), wf = w - wiD; const int wi = (int)wiD;
  const double kf = js_pow(f0 / 261.6, p.key); const size_t CR = 32; const double cdt = (double)CR / sr, sdt = 1 / sr, norm = 1 / std::sqrt((double)nv);
  const double ah = h > atk ? sus + (1 - sus) * js_exp(-6.9 * (h - atk) / dec) : 1;
  double ic1 = 0, ic2 = 0, subPh = 0, a1 = 1, a2 = 0, a3 = 0;
  for (size_t i0 = 0; i0 < N; i0 += CR) {
    const double t0 = (double)i0 * sdt, sh = step(shared, cdt) * driftC * p.cohes;
    for (int j = 0; j < nv; j++) {
      Vc& v = V[j];
      v.dt = f0 * js_pow(2, (v.det + step(v.dp, cdt) * driftC * (1 - p.cohes) + sh) / 1200) * sdt;
      v.lv = v.g * (p.shimmer != 0 ? 1 - p.shimmer * (0.5 + 0.5 * step(v.sp, cdt)) : 1);
    }
    const double fe = js_exp(-6.9 * t0 / mx(0.005, p.fdec / 1000)), fc = mx(30, mn(sr * 0.45, p.cut * kf * js_pow(2, p.fenv * 5 * fe)));
    { const double g = js_tan(kPi * fc / sr), k = 2 - 1.96 * mn(1, p.res); a1 = 1 / (1 + g * (g + k)); a2 = g * a1; a3 = g * a2; }
    const size_t i1 = (size_t)mn((double)N, (double)(i0 + CR));
    for (size_t i = i0; i < i1; i++) {
      const double t = (double)i * sdt; double mix = 0;
      for (int j = 0; j < nv; j++) {
        Vc& v = V[j]; if (t < v.on) continue;
        const double dt = v.dt; double ph = v.ph + dt; if (ph >= 1) ph -= 1; v.ph = ph;
        double y = shape(wi, ph, dt); if (wf > 0.001) y += (shape(wi + 1, ph, dt) - y) * wf;
        mix += y * v.lv * (bloom != 0 && t - v.on < atk ? (t - v.on) / atk : 1);
      }
      mix *= norm;
      if (p.sub > 0.001) { subPh += f0 * 0.5 * sdt; if (subPh >= 1) subPh -= 1; mix += js_sin(2 * kPi * subPh) * p.sub * 0.7; }
      const double v3 = mix - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3; ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2;
      out[i] = (float)(v2 * (t < atk ? t / atk : t < h ? sus + (1 - sus) * js_exp(-6.9 * (t - atk) / dec) : ah * js_exp(-6.9 * (t - h) / rel)));
    }
  }
  return finish(out, N, p.level, p.drive, sr, 0.003);
}

}  // namespace thunder
