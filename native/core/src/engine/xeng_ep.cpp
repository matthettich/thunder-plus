// U28: TineEPCore (XENG ep): a hammered tine and tone bar through an electromagnetic pickup (the 1/(gap - x)^2 curve), tremolo, dampers. index.html TineEPCore.render + KeysUtil. No allocation.
#include "engine/xeng_util.h"

namespace thunder {
using namespace xe;

size_t xeng_ep(const EpP& p, double sr, double hold, float* out, size_t cap) {
  const double f0 = 65.406 * js_pow(2, p.oct + orz(p.tune) / 12), h = mx(0.05, orv(hold, 2)), reg = js_pow(261.6 / f0, 0.5);
  const double tau = mx(0.2, 4.5 * p.dec * reg), rel = mx(0.002, p.rel / 1000);
  const double len = mn(20, mn(h, tau * 3) + rel * 7), Nd = mx(1, std::ceil(len * sr));
  if (!okN(Nd)) return 0;
  const size_t N = (size_t)Nd;
  if (!out) return N;
  if (N > cap) return 0;
  KeysRnd rnd(0x7e11);
  KeysBank bank(sr); const double amp = 0.45 + p.hard * 0.55;
  const int fund = bank.add(f0, 0), oct = bank.add(f0 * 2.0, 0), bar = bank.add(f0 * 0.5 * 1.0006, 0);
  static const double pr[3][2] = {{7.11, 1}, {14.3, 0.5}, {21.6, 0.25}}; int pk[3]; double pa[3]; int np = 0;
  for (int i = 0; i < 3; i++) if (f0 * pr[i][0] < sr * 0.45) { pk[np] = bank.add(f0 * pr[i][0], rnd() * 6.28); pa[np] = pr[i][1]; np++; }
  const double k = 0.15 + p.bark * 0.75, d = 1;
  const double lpw = 1 - js_exp(-2 * kPi * (1200 + p.tone * p.tone * 9000) / sr);
  const size_t CR = 32; double lp = 0, lp2 = 0, dc = 0, dcy = 0, ph = 0;
  for (size_t i0 = 0; i0 < N; i0 += CR) {
    const double t = (double)i0 / sr, damp = t < h ? 1 : js_exp(-(t - h) / rel), e = amp * js_exp(-t / tau) * damp;
    bank.a[fund] = e * 0.9; bank.a[oct] = e * 0.06 * (1 + p.hard); bank.a[bar] = e * 0.08 * js_exp(-t / (tau * 0.6));
    for (int j = 0; j < np; j++) bank.a[pk[j]] = pa[j] * p.bell * (0.25 + p.hard * 0.75) * js_exp(-t / (0.012 + 0.02 * (1 - p.hard))) * 0.35;
    const size_t i1 = (size_t)mn((double)N, (double)(i0 + CR)); double* C = bank.c; double* Sn = bank.s; const double* WC = bank.wc; const double* WS = bank.ws; const double* A = bank.a; const int nb = bank.n;
    for (size_t i = i0; i < i1; i++) {
      double x = 0;
      for (int j = 0; j < nb; j++) { const double c = C[j], s = Sn[j]; C[j] = c * WC[j] - s * WS[j]; Sn[j] = s * WC[j] + c * WS[j]; x += s * A[j]; }
      x = mx(-0.95, mn(0.95, x));
      const double y = (1 / ((d - k * x) * (d - k * x)) - 1) / (2 * k);
      lp += (y - lp) * lpw; lp2 += (lp - lp2) * lpw;
      dcy = lp2 - dc + 0.997 * dcy; dc = lp2;
      double o2 = dcy;
      if (p.trem > 0.001) { ph += p.tremRate / sr; o2 *= 1 - p.trem * 0.5 * (1 - js_cos(2 * kPi * ph)); }
      out[i] = (float)o2;
    }
    if ((i0 & 4095) == 0) for (int j = 0; j < nb; j++) { const double m = 1 / js_hypot2(C[j], Sn[j]); C[j] *= m; Sn[j] *= m; }
  }
  return finish(out, N, p.level, p.drive, sr, 0.004);
}

}  // namespace thunder
