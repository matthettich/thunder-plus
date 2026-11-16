// U28: PartialsCore (XENG partials): one note of up to 256 harmonics, two spectra morphed over the note. index.html PartialsCore.render / spectrum + KeysUtil. No allocation (the bank holds 256).
#include "engine/xeng_util.h"

namespace thunder {
using namespace xe;
namespace {
double spectrum(int n, double f, double tilt, double comb, double fmt, double fw, double famt) {
  double a = js_pow(n, -(0.3 + tilt * 2.2));
  if (comb > 0) a *= n % 2 == 1 ? 1 : 1 - comb; else if (comb < 0) a *= n % 2 == 0 || n == 1 ? 1 : 1 + comb;
  if (famt > 0) { const double lf = js_log2(f / fmt), g = js_exp(-lf * lf / (0.05 + fw * fw * 2)); a *= 1 - famt + famt * (0.15 + g * 6); }
  return a;
}
struct Part { double f, a1, a2, tau, ph, rt; int id; };
}

size_t xeng_partials(const PartialsP& p, double sr, double hold, float* out, size_t cap) {
  const double f0 = 65.406 * js_pow(2, p.oct + orz(p.tune) / 12), h = mx(0.03, orv(hold, 2)), T = mx(0.01, p.decay), rel = mx(0.001, p.rel / 1000), atk = mx(0.0005, p.atk / 1000);
  const double len = mn(12, (p.sus > 0.01 ? mn(h, T * 3) : mn(h, T * 3)) + rel * 6 + 0.05), Nd = mx(1, std::ceil(len * sr));
  if (!okN(Nd)) return 0;
  const size_t N = (size_t)Nd;
  if (!out) return N;
  if (N > cap) return 0;
  KeysRnd rnd(0x9a27);
  const double cntD = mx(1, mn(256, js_round(p.count))); const int cnt = (int)cntD; KeysBank bank(sr); Part parts[256]; int np = 0;
  for (int n = 1; n <= cnt; n++) {
    const double f = n * f0 * js_pow(1 + p.stretch * p.stretch * 0.08 * (n - 1), 0.5); if (f > sr * 0.45) break;
    const double a1 = spectrum(n, f, p.tilt, p.comb, p.fmt, p.fwidth, p.famt), a2 = spectrum(n, f, p.tilt2, p.comb2, p.fmt, p.fwidth, p.famt);
    const double tau = T / (1 + (n - 1) * p.dtilt * p.dtilt * 0.6); const int id = bank.add(f, rnd() * 6.283);
    Part q; q.f = f; q.a1 = a1; q.a2 = a2; q.tau = tau; q.id = id; q.ph = rnd() * 6.283; q.rt = 0.5 + rnd(); parts[np++] = q;
  }
  const size_t CR = 32; const double mt = mx(0.01, p.mtime / 1000), ba = mx(0.0005, p.batk / 1000), bd = mx(0.005, p.bdec / 1000);
  for (size_t i0 = 0; i0 < N; i0 += CR) {
    const double t = (double)i0 / sr, env = (t < h ? mn(1, t / atk) * (p.sus + (1 - p.sus) * js_exp(-t / T)) : js_exp(-(t - h) / rel));
    const double m = p.morph * mn(1, t / mt), be = mn(1, t / ba) * js_exp(-t / bd);
    const double top = f0 * (2 + js_pow(2, (p.bright + be * 0.6) * 8));
    for (int j = 0; j < np; j++) {
      const Part& q = parts[j];
      const double d = p.drift > 0 ? 1 + p.drift * 0.8 * js_sin(q.ph + 6.2832 * p.drate * q.rt * t) : 1;
      const double roll = q.f > top ? js_exp(-(q.f - top) / top) : 1;
      bank.a[q.id] = (q.a1 * (1 - m) + q.a2 * m) * js_exp(-t / q.tau) * roll * d * env;
    }
    const size_t i1 = (size_t)mn((double)N, (double)(i0 + CR)); double* C = bank.c; double* S = bank.s; const double* WC = bank.wc; const double* WS = bank.ws; const double* A = bank.a; const int nb = bank.n;
    for (size_t i = i0; i < i1; i++) {
      double y = 0;
      for (int k = 0; k < nb; k++) { const double c = C[k], s = S[k]; C[k] = c * WC[k] - s * WS[k]; S[k] = s * WC[k] + c * WS[k]; y += s * A[k]; }
      out[i] = (float)y;
    }
  }
  return finish(out, N, p.level, p.drive, sr, 0.003);
}

}  // namespace thunder
