// U28: OrganCore (XENG organ): tonewheel drawbars, percussion, key click, a rotary speaker, pluck. index.html OrganCore.render + KeysUtil. No allocation (at most 9 drawbars and the percussion).
#include "engine/xeng_util.h"

namespace thunder {
using namespace xe;
namespace {
struct Bar { int k; double a; bool hi; };
}

size_t xeng_organ(const OrganP& p, double sr, double hold, float* out, size_t cap) {
  const double f0 = 65.406 * js_pow(2, p.oct + orz(p.tune) / 12), h = mx(0.03, orv(hold, 1.5)), rel = mx(0.001, p.rel / 1000), atk = mx(0.0005, p.atk / 1000);
  const double len = mn(30, h + rel * 7 + 0.02), Nd = mx(1, std::ceil(len * sr));
  if (!okN(Nd)) return 0;
  const size_t N = (size_t)Nd;
  if (!out) return N;
  if (N > cap) return 0;
  KeysRnd rnd(0x0a9a);
  KeysBank bank(sr); Bar bars[9]; int nbars = 0;
  static const double RATIOS[9] = {0.5, 1.5, 1, 2, 3, 4, 5, 6, 8};
  const double dv[9] = {p.d1, p.d2, p.d3, p.d4, p.d5, p.d6, p.d7, p.d8, p.d9};
  for (int i = 0; i < 9; i++) {
    const double lv = js_round(orz(dv[i])); if (lv <= 0) continue;
    double f = f0 * RATIOS[i]; while (f > 5900 && f > f0) f /= 2; if (f > sr * 0.45) continue;
    Bar b; b.k = bank.add(f, rnd() * 6.28); b.a = js_pow(10, -3 * (8 - lv) / 20); b.hi = f > 800; bars[nbars++] = b;
  }
  const int32_t pc = i32(p.perc); const double pr = pc == 1 ? 2 : pc == 2 ? 3 : 0; const int perc = pr != 0 ? bank.add(f0 * pr, 0) : -1;
  const int32_t rot = i32(p.rotary); const double hornHz = rot == 2 ? 6.8 : rot == 1 ? 0.83 : 0, drumHz = rot == 2 ? 5.9 : rot == 1 ? 0.67 : 0;
  const double lpw = 1 - js_exp(-2 * kPi * (1500 + p.bright * p.bright * 14000) / sr), nb0 = nbars != 0 ? nbars : 1;
  uint8_t HI[kBankMax] = {0}; for (int i = 0; i < nbars; i++) HI[bars[i].k] = bars[i].hi ? 1 : 0;
  const size_t CR = 16; double lp = 0, cl = 0, hp = 0, hph = rnd(), dph = rnd();
  for (size_t i0 = 0; i0 < N; i0 += CR) {
    const double t = (double)i0 / sr, env = (t < atk ? t / atk : 1) * (t < h ? 1 : js_exp(-(t - h) / rel));
    const double pl = p.pluck > 0 ? 1 - p.pluck * (1 - js_exp(-t / mx(0.01, p.pluckDec / 1000))) : 1;
    hph += hornHz * (double)CR / sr; dph += drumHz * (double)CR / sr;
    const double hornAm = rot ? 1 - p.rotDepth * 0.35 * (0.5 + 0.5 * js_sin(2 * kPi * hph)) : 1, drumAm = rot ? 1 - p.rotDepth * 0.2 * (0.5 + 0.5 * js_sin(2 * kPi * dph)) : 1;
    const double dop = rot ? p.rotDepth * 0.0025 * js_cos(2 * kPi * hph) : 0;
    for (int j = 0; j < nbars; j++) { const Bar& b = bars[j]; bank.a[b.k] = b.a * env * pl * (b.hi ? hornAm : drumAm) / std::sqrt(nb0); }
    if (perc >= 0) bank.a[perc] = p.percLv * js_exp(-t / mx(0.02, p.percDec / 1000)) * (t < h ? 1 : js_exp(-(t - h) / rel)) * 0.7;
    const size_t i1 = (size_t)mn((double)N, (double)(i0 + CR)); double* C = bank.c; double* Sn = bank.s; const double* WC = bank.wc; const double* WS = bank.ws; const double* A = bank.a; const int nb = bank.n;
    for (size_t i = i0; i < i1; i++) {
      double y = 0;
      for (int k = 0; k < nb; k++) {
        const double c = C[k], s = Sn[k]; double wc = WC[k], ws = WS[k];
        if (dop != 0 && HI[k]) { const double dw = dop * ws; wc -= ws * dw; ws += WC[k] * dw; }
        C[k] = c * wc - s * ws; Sn[k] = s * wc + c * ws; y += s * A[k];
      }
      const double tt = (double)i / sr; const double ck = tt < 0.006 ? (1 - tt / 0.006) : tt >= h && tt < h + 0.004 ? 0.4 * (1 - (tt - h) / 0.004) : 0;
      if (ck > 0) { const double nz = rnd() * 2 - 1; hp = nz - cl; cl = nz; y += hp * ck * p.click * 0.6; }
      lp += (y - lp) * lpw; out[i] = (float)lp;
    }
    if ((i0 & 4095) == 0) for (int k = 0; k < nb; k++) { const double m = 1 / js_hypot2(C[k], Sn[k]); C[k] *= m; Sn[k] *= m; }
  }
  return finish(out, N, p.level, p.drive, sr, 0.003);
}

}  // namespace thunder
