// U28: ModalCore (XENG modal): a bank of decaying resonant modes, struck, plucked, rubbed or bowed. index.html ModalCore.render / ratios / MAT, line for line. No allocation (at most 24 modes).
#include "engine/xeng_util.h"

namespace thunder {
using namespace xe;
namespace {
struct Rat { int n; double r[16]; };
const Rat kRat[12] = {
  {12, {1, 2.756, 5.404, 8.933, 13.344, 18.638, 24.81, 31.87, 39.81, 48.63, 58.34, 68.93}},
  {8, {1, 3.99, 10.07, 18.9, 29.3, 41.4, 55.1, 70.6}},
  {7, {1, 4.0, 10.0, 17.6, 27.2, 38.9, 52.5}},
  {8, {1, 2.71, 5.15, 8.43, 12.5, 17.4, 23.1, 29.6}},
  {12, {0.5, 1, 1.183, 1.506, 2, 2.514, 2.662, 3.011, 4.166, 5.433, 6.796, 8.215}},
  {16, {1, 1.42, 1.78, 2.11, 2.27, 2.62, 2.84, 3.31, 3.48, 3.89, 4.25, 4.6, 5.03, 5.37, 5.91, 6.32}},
  {16, {1, 1.594, 2.136, 2.296, 2.653, 2.918, 3.156, 3.501, 3.6, 3.652, 4.06, 4.154, 4.601, 4.832, 4.903, 5.131}},
  {7, {1, 2.32, 4.25, 6.63, 9.38, 12.5, 16.0}},
  {16, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}},
  {12, {1, 3, 5, 7, 9, 11, 13, 15, 17, 19, 21, 23}},
  {6, {1, 2.57, 4.1, 6.2, 8.6, 11.3}},
  {16, {1, 1.68, 2.43, 2.73, 3.42, 4.05, 4.58, 5.11, 5.7, 6.29, 6.88, 7.5, 8.07, 8.71, 9.31, 9.87}}};
const double kMat[12][2] = {{1, 1}, {0.8, 1.2}, {3, 0.5}, {2.2, 0.5}, {3.5, 0.35}, {4, 0.3}, {0.6, 1.3}, {2.8, 0.4}, {1.6, 0.8}, {1.2, 0.9}, {0.18, 1.6}, {2.5, 0.6}};
struct Mode { double c1, c2, g, y1, y2, tau; };
// the modes (the part of render before the exciter): returns how many, and the largest tau; they are needed for the length
int make_modes(const ModalP& p, double sr, double f0, Mode* modes, double* maxT) {
  const int m = (int)mx(0, mn(11, (double)i32(p.mat))); const Rat& R0 = kRat[m]; const double* M = kMat[m];
  const double baseT = mx(0.005, p.dec) * M[0];
  const int nm = (int)mx(1, mn(24, (double)i32(p.modes))); int cnt = 0; double mt = 0.05;
  for (int k = 0; k < nm; k++) {
    double r = k < R0.n ? R0.r[k] : R0.r[R0.n - 1] + (k - R0.n + 1) * (R0.r[R0.n - 1] - R0.r[R0.n - 2]);
    r *= 1 + p.inharm * 0.004 * k * k;
    const double f = f0 * r; if (f > sr * 0.45 || f < 20) continue;
    const double shape = jabs(js_sin(kPi * (k + 1) * mx(0.01, mn(0.99, p.pos)))) * 0.85 + 0.15;
    const double amp = js_pow(k + 1, -1.8 + p.bright * 1.9) * shape * (m == 4 && k == 0 ? 0.6 : 1);
    const double tau = baseT / (1 + (r - 1) * p.dtilt * M[1] * 0.6);
    const double w = 2 * kPi * f / sr, rr = js_exp(-1 / (tau * sr));
    modes[cnt++] = Mode{2 * rr * js_cos(w), -rr * rr, amp * js_sin(w), 0, 0, tau};
  }
  for (int k = 0; k < cnt; k++) mt = mx(mt, modes[k].tau);
  *maxT = mt; return cnt;
}
}  // namespace

size_t xeng_modal(const ModalP& p, double sr, double hold, float* out, size_t cap) {
  const double f0 = 65.406 * js_pow(2, p.oct + orz(p.tune) / 12); const int32_t exc = i32(p.exc); const bool sus = exc == 2 || exc == 4;
  const double h = sus ? mx(0.08, orv(hold, 1)) : 0;
  Mode modes[24]; double maxT = 0; const int nmodes = make_modes(p, sr, f0, modes, &maxT);
  const double len = mn(10, h + maxT * 6.9 + 0.05), Nd = mx(1, std::ceil(len * sr));
  if (!okN(Nd)) return 0;
  const size_t N = (size_t)Nd;
  if (!out) return N;
  if (N > cap) return 0;
  const double pw = mx(2, js_round(sr * (0.0002 + (1 - p.hard) * 0.006))); uint32_t rnd = 777; double lpn = 0;
  for (size_t i = 0; i < N; i++) {
    const double t = (double)i / sr; rnd = rnd * 1664525u + 1013904223u; const double w = (double)rnd / 2147483648.0 - 1;
    lpn += (w - lpn) * (0.05 + p.hard * 0.9);
    double x = 0;
    if (exc == 0) { if ((double)i < pw) x = 0.5 - 0.5 * js_cos(2 * kPi * (double)i / pw); x += p.noise * lpn * js_exp(-t / 0.01); }
    else if (exc == 1) x = lpn * js_exp(-t / (0.004 + (1 - p.hard) * 0.03)) * 1.5;
    else if (exc == 3) x = (i < 3 ? 1 : 0) + lpn * js_exp(-t / 0.006) * 0.8;
    else { const double g = t < 0.04 ? t / 0.04 : t < h ? 1 : js_exp(-(t - h) / 0.03); x = lpn * g * p.bow * (exc == 4 ? 0.06 : 0.035) * (1 + p.noise * 2); }
    double y = 0;
    for (int k = 0; k < nmodes; k++) { Mode& q = modes[k]; const double v = q.c1 * q.y1 + q.c2 * q.y2 + q.g * x; q.y2 = q.y1; q.y1 = v; y += v; }
    out[i] = (float)(y + (exc == 4 ? lpn * p.noise * 0.05 * (t < h ? 1 : 0) : 0));
  }
  return finish(out, N, p.level, p.drive, sr, 0.003);
}

}  // namespace thunder
