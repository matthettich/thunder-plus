// U28: PianoCore (XENG piano): a struck stiff string, two or three strings a note, a soundboard of three resonances. index.html PianoCore.render + KeysUtil. No allocation (the bank is fixed at 64 partials x 3 strings).
#include "engine/xeng_util.h"

namespace thunder {
using namespace xe;
namespace {
struct Part { double a, tau; int ids[3]; int nid; };
struct Res { double c1, c2, y1, y2; };
}

size_t xeng_piano(const PianoP& p, double sr, double hold, float* out, size_t cap) {
  const double f0 = 65.406 * js_pow(2, p.oct + orz(p.tune) / 12), h = mx(0.05, orv(hold, 2)), reg = js_pow(261.6 / f0, 0.55);
  const double T0 = mx(0.15, 7 * p.dec * reg), rel = mx(0.002, p.rel / 1000); const bool ped = i32(p.pedal) == 1;
  const double len = mn(12, ped ? T0 * 2.5 : mn(h, T0 * 2.5) + rel * 7), Nd = mx(1, std::ceil(len * sr));
  if (!okN(Nd)) return 0;
  const size_t N = (size_t)Nd;
  if (!out) return N;
  if (N > cap) return 0;
  KeysRnd rnd(0x91a2);
  const double B = 0.00008 * (1 + p.inharm * 9) * js_pow(f0 / 261.6, 0.6); const int S = (int)mx(1, mn(3, js_round(p.strings)));
  static const double d0[3] = {0, 1, -0.73}; double dets[3]; for (int i = 0; i < S; i++) dets[i] = d0[i] * p.det * p.det * 6;
  const double fc = 600 + p.hard * p.hard * 9000 + p.bright * 3000;
  KeysBank bank(sr); Part parts[64]; int np = 0;
  for (int n = 1; n <= 64; n++) {
    const double fn = n * f0 * std::sqrt(1 + B * n * n); if (fn > sr * 0.45) break;
    const double shape = jabs(js_sin(kPi * n * mx(0.03, p.pos))) * 0.8 + 0.2, roll = 1 / (1 + js_pow(fn / fc, 2));
    const double a = js_pow(n, -0.9 - (1 - p.bright) * 0.6) * shape * roll; if (a < 1e-4) continue;
    const double tau = T0 / (1 + (n - 1) * (0.12 + (1 - p.bright) * 0.18));
    Part q; q.nid = 0;
    for (int d = 0; d < S; d++) q.ids[q.nid++] = bank.add(fn * js_pow(2, dets[d] / 1200), rnd() * 6.283);
    q.a = a / S; q.tau = tau; parts[np++] = q;
  }
  Res res[3]; { static const double rf[3][2] = {{95, 0.04}, {230, 0.025}, {620, 0.012}};
    for (int i = 0; i < 3; i++) { const double w = 2 * kPi * rf[i][0] / sr, r = js_exp(-1 / (rf[i][1] * sr)); res[i] = Res{2 * r * js_cos(w), -r * r, 0, 0}; } }
  const size_t CR = 32; double lpn = 0, dc = 0, dcy = 0;
  for (size_t i0 = 0; i0 < N; i0 += CR) {
    const double t = (double)i0 / sr, damp = ped || t < h ? 1 : js_exp(-(t - h) / rel);
    for (int j = 0; j < np; j++) { const Part& q = parts[j]; const double e = q.a * (0.68 * js_exp(-t / q.tau) + 0.32 * js_exp(-t / (q.tau * 4.5))) * damp; for (int k = 0; k < q.nid; k++) bank.a[q.ids[k]] = e; }
    const size_t i1 = (size_t)mn((double)N, (double)(i0 + CR)); double* C = bank.c; double* Sn = bank.s; const double* WC = bank.wc; const double* WS = bank.ws; const double* A = bank.a; const int nb = bank.n;
    for (size_t i = i0; i < i1; i++) {
      double y = 0;
      for (int k = 0; k < nb; k++) { const double c = C[k], s = Sn[k]; C[k] = c * WC[k] - s * WS[k]; Sn[k] = s * WC[k] + c * WS[k]; y += s * A[k]; }
      const double tt = (double)i / sr; lpn += ((rnd() * 2 - 1) - lpn) * 0.08;
      const double th = p.thump * (lpn * js_exp(-tt / 0.006) * 0.6 + js_sin(2 * kPi * 70 * tt) * js_exp(-tt / 0.03) * 0.4);
      double bd = 0; for (int k = 0; k < 3; k++) { Res& r = res[k]; const double v = r.c1 * r.y1 + r.c2 * r.y2 + (y * 0.02 + th * 0.2); r.y2 = r.y1; r.y1 = v; bd += v; }
      const double x = y + th * 0.15 + bd * p.body * 0.5;
      dcy = x - dc + 0.995 * dcy; dc = x; out[i] = (float)dcy;
    }
    if ((i0 & 4095) == 0) for (int k = 0; k < nb; k++) { const double m = 1 / js_hypot2(C[k], Sn[k]); C[k] *= m; Sn[k] *= m; }
  }
  return finish(out, N, p.level, p.drive, sr, 0.004);
}

}  // namespace thunder
