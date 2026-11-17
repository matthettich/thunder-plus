// U28: SkinCore (XENG skin): a drum head, the modes of a circular membrane (Bessel weights), a shell resonance. index.html SkinCore.render, line for line. No allocation (at most 24 modes).
#include "engine/xeng_util.h"

namespace thunder {
using namespace xe;
namespace {
const double kModes[24][3] = {{0, 1, 1}, {1, 1, 1.594}, {2, 1, 2.136}, {0, 2, 2.296}, {3, 1, 2.653}, {1, 2, 2.918}, {4, 1, 3.156}, {2, 2, 3.501}, {0, 3, 3.600}, {5, 1, 3.652}, {3, 2, 4.060}, {6, 1, 4.154},
  {1, 3, 4.230}, {4, 2, 4.601}, {7, 1, 4.651}, {2, 3, 4.832}, {0, 4, 4.903}, {8, 1, 5.144}, {5, 2, 5.131}, {3, 3, 5.412}, {6, 2, 5.654}, {1, 4, 5.540}, {9, 1, 5.634}, {4, 3, 5.976}};
double fact(double n) { double f = 1; for (double i = 2; i <= n; i++) f *= i; return f; }
double bessel(double m, double x) {
  double s = 0, term = js_pow(x / 2, m) / fact(m);
  for (int k = 0; k < 24; k++) { s += term; term *= -(x * x / 4) / ((k + 1) * (k + 1 + m)); if (jabs(term) < 1e-12) break; }
  return s;
}
struct Mode { double ratio, w, tau, y1, y2, c1, c2, g; };
}  // namespace

size_t xeng_skin(const SkinP& p, double sr, float* out, size_t cap) {
  const double dec = mx(0.003, p.dec / 1000);
  const double len = mn(8, dec * 5 + 0.05), Nd = mx(1, std::ceil(len * sr));
  if (!okN(Nd)) return 0;
  const size_t N = (size_t)Nd;
  if (!out) return N;
  if (N > cap) return 0;
  KeysRnd rnd(0x5c1a);
  const double f0 = p.pitch * js_pow(2, orz(p.tune) / 12);
  const double r = mx(0.02, mn(0.98, p.pos)); Mode modes[24]; int nmodes = 0; const double nm = js_round(6 + p.size * 18), hard = 0.15 + p.hard * p.hard * 0.85;
  static const double z0[4] = {2.405, 5.520, 8.654, 11.792}, z1[4] = {3.832, 7.016, 10.173, 13.324}, z2[3] = {5.136, 8.417, 11.620}, z3[3] = {6.380, 9.761, 13.015}, z4[2] = {7.588, 11.065}, z5[2] = {8.771, 12.339},
    z6[1] = {9.936}, z7[1] = {11.086}, z8[1] = {12.225}, z9[1] = {13.354};
  const double kmax = mn(nm, 24);
  for (int k = 0; (double)k < kmax; k++) {
    const double m = kModes[k][0], n = kModes[k][1], ratio = kModes[k][2];
    // z = (zeros[m] || [J0z[n - 1]])[n - 1] || 3: every m of the table has a zeros row; a missing entry (undefined or 0) is 3
    const double* zr = nullptr; int zn = 0;
    switch ((int)m) { case 0: zr = z0; zn = 4; break; case 1: zr = z1; zn = 4; break; case 2: zr = z2; zn = 3; break; case 3: zr = z3; zn = 3; break; case 4: zr = z4; zn = 2; break;
      case 5: zr = z5; zn = 2; break; case 6: zr = z6; zn = 1; break; case 7: zr = z7; zn = 1; break; case 8: zr = z8; zn = 1; break; default: zr = z9; zn = 1; break; }
    const int ni = (int)n - 1; const double z = (ni >= 0 && ni < zn && zr[ni] != 0) ? zr[ni] : 3;
    double w = jabs(bessel(m, z * r)) * (m == 0 ? 1 : 1.4);
    w *= 1 / (1 + js_pow(ratio / (1 + hard * 6), 2));
    const double tau = dec / (1 + (ratio - 1) * (0.3 + p.damp * 2)) / (1 + p.muffle * 3 * ratio * ratio * 0.3);
    modes[nmodes++] = Mode{ratio, w, tau, 0, 0, 0, 0, 0};
  }
  const double sf = f0 * (0.5 + p.shellF * 1.5), sw = 2 * kPi * mn(sr * 0.4, sf) / sr, srr = js_exp(-1 / (0.15 * sr));
  const double shc1 = 2 * srr * js_cos(sw), shc2 = -srr * srr; double shy1 = 0, shy2 = 0;
  const double tT = mx(0.005, p.tenT / 1000), bend = p.tension * p.tension * 24; const size_t CR = 16; const double hitLenD = mx(2, js_round(sr * (0.0003 + (1 - hard) * 0.004)));
  const double hitLen = hitLenD;
  for (size_t i0 = 0; i0 < N; i0 += CR) {
    const double t = (double)i0 / sr, fm = js_pow(2, bend / 12 * js_exp(-t / tT));
    for (int k = 0; k < nmodes; k++) { Mode& q = modes[k]; const double f = mn(sr * 0.45, f0 * q.ratio * fm), w = 2 * kPi * f / sr, rr = js_exp(-1 / (q.tau * sr)); q.c1 = 2 * rr * js_cos(w); q.c2 = -rr * rr; q.g = q.w * (1 - rr) * 2; }
    const size_t i1 = (size_t)mn((double)N, (double)(i0 + CR));
    for (size_t i = i0; i < i1; i++) {
      const double di = (double)i;
      const double x = di < hitLen ? 0.5 - 0.5 * js_cos(2 * kPi * di / hitLen) + p.noise * (rnd() * 2 - 1) * 0.5 : (di < hitLen * 6 ? p.noise * (rnd() * 2 - 1) * 0.15 * (1 - di / (hitLen * 6)) : 0);
      double y = 0; for (int k = 0; k < nmodes; k++) { Mode& q = modes[k]; const double v = q.g * x + q.c1 * q.y1 + q.c2 * q.y2; q.y2 = q.y1; q.y1 = v; y += v; }
      const double sv = 0.02 * y + shc1 * shy1 + shc2 * shy2; shy2 = shy1; shy1 = sv;
      out[i] = (float)(y + sv * p.shell * 3);
    }
  }
  return finish(out, N, p.level, p.drive, sr, 0.003);
}

}  // namespace thunder
