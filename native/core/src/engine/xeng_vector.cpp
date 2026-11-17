// U29: VectorCore (XENG vector): vector synthesis and wave sequencing. Four sources (or the steps of a sequence), each one of 18 waves rebuilt band-limited for the note with an inverse FFT; a joystick (envelope, LFO) or a
// step sequence crossfades them; unison, a two-pole low-pass with an envelope, an ADSR. index.html VectorCore (dft, vowel, spec, cycle, render), line for line.
// The song tempo (songBpm()) is the caller's `bpm` argument: note-value steps read it.
#include "engine/xeng_util.h"

namespace thunder {
using namespace xe;

namespace {
constexpr int kL = 2048, kNW = 18, kNZ = 17, kH = 256;
struct Spec { double re[kH + 1], im[kH + 1]; };

// VectorCore.dft(fn): the harmonics of a wave measured from its time-domain shape (4096 mid-point samples)
template <class Fn> bool dft(XArena& ar, Fn fn, Spec& S) {
  const int M = 4096, H = kH; double* y = ar.get<double>(M); if (!y) return false;
  for (int n = 0; n <= H; n++) { S.re[n] = 0; S.im[n] = 0; }
  for (int i = 0; i < M; i++) y[i] = fn(((double)i + 0.5) / M);
  for (int n = 1; n <= H; n++) {
    const double w = 2 * kPi * n / M, wc = js_cos(w), ws = js_sin(w); double c = js_cos(w * 0.5), s = js_sin(w * 0.5), a = 0, b = 0;
    for (int i = 0; i < M; i++) { a += y[i] * c; b += y[i] * s; const double c2 = c * wc - s * ws; s = s * wc + c * ws; c = c2; }
    S.re[n] = 2 * a / M; S.im[n] = 2 * b / M;
  }
  return true;
}
// VectorCore.vowel(v, f0): a buzz falling 6 dB an octave through four formants (Hz, bandwidth, level), the formants fixed in Hz
void vowel(int v, double f0, Spec& S) {
  static const double F[3][4][3] = {{{730, 90, 0}, {1090, 110, -5}, {2440, 160, -18}, {3400, 250, -26}}, {{500, 80, 0}, {840, 100, -7}, {2400, 160, -24}, {3300, 250, -30}}, {{280, 70, 0}, {2250, 120, -10}, {3000, 170, -14}, {3700, 250, -20}}};
  for (int n = 0; n <= kH; n++) { S.re[n] = 0; S.im[n] = 0; }
  for (int n = 1; n <= kH; n++) {
    const double f = n * f0; double g = 0.004;
    for (int q = 0; q < 4; q++) { const double fc = F[v][q][0], bw = F[v][q][1], db = F[v][q][2], d = (f - fc) / (bw * 0.5); g += js_pow(10, db / 20) / std::sqrt(1 + d * d); }
    S.im[n] = g / n;
  }
}
struct PN { int n; double a; };
void list(Spec& S, const PN* L, int k) { for (int i = 0; i < k; i++) S.im[L[i].n] += L[i].a; }
bool spec(XArena& ar, int w, double f0, Spec& S) {
  if (w >= 9 && w <= 11) { vowel(w - 9, f0, S); return true; }
  const int H = kH; const double PI = kPi;
  for (int n = 0; n <= H; n++) { S.re[n] = 0; S.im[n] = 0; }
  if (w == 0) S.im[1] = 1;
  else if (w == 1) { for (int n = 1; n <= H; n += 2) S.im[n] = 8 / (PI * PI * n * n) * (((n - 1) >> 1) & 1 ? -1 : 1); }
  else if (w == 2) { for (int n = 1; n <= H; n++) S.im[n] = -2 / (PI * n); }
  else if (w == 3) { for (int n = 1; n <= H; n += 2) S.im[n] = 4 / (PI * n); }
  else if (w == 4 || w == 5) { const double d = w == 4 ? 0.25 : 0.1; for (int n = 1; n <= H; n++) { S.re[n] = 2 * js_sin(2 * PI * n * d) / (PI * n); S.im[n] = 2 * (1 - js_cos(2 * PI * n * d)) / (PI * n); } }
  else if (w == 6) { static const PN L[] = {{1, 1}, {2, 0.75}, {3, 0.55}, {4, 0.5}, {5, 0.12}, {6, 0.32}, {8, 0.3}, {10, 0.1}, {12, 0.12}, {16, 0.08}}; list(S, L, 10); }
  else if (w == 7) { for (int n = 1; n <= H; n++) S.im[n] = n % 2 ? js_pow(n, -1.25) : 0.03 / n; }
  else if (w == 8) { for (int n = 1; n <= H; n++) S.im[n] = js_pow(n, -0.55) / (1 + js_pow((double)n / 60, 2)); }
  else if (w == 12) return dft(ar, [&](double x) { return 1 - (1 - x) * (1 - js_cos(2 * PI * x * 6)); }, S);
  else if (w == 13) return dft(ar, [&](double x) { double y = js_sin(2 * PI * x) * 3.4; y = jabs(std::fmod(std::fmod(y + 1, 4) + 4, 4) - 2) - 1; return js_round(y * 2.5) / 2.5; }, S);
  else if (w == 14) { static const PN L[] = {{1, 0.55}, {3, 0.12}, {5, 0.7}, {7, 0.9}, {11, 0.6}, {13, 0.5}, {17, 0.42}, {19, 0.36}, {23, 0.26}, {29, 0.2}, {31, 0.16}, {37, 0.12}, {41, 0.1}, {43, 0.08}}; list(S, L, 14); }
  else if (w == 15) { static const PN L[] = {{1, 1}, {2, 0.25}, {3, 0.05}, {5, 0.42}, {8, 0.3}, {12, 0.24}, {17, 0.17}, {23, 0.11}, {30, 0.07}, {38, 0.04}}; list(S, L, 10); }
  else if (w == 16) return dft(ar, [&](double x) { return 2 * std::fmod(x * 2.4, 1) - 1; }, S);
  return true;
}
// VectorCore.cycle(w, f0, nmax, L): one cycle of wave w for this note: harmonics 1..nmax, RMS 0.35, built with an inverse FFT
float* cycle(XArena& ar, int w, double f0, int nmax, double* Zr, double* Zi) {
  Spec* sp = ar.get<Spec>(1); if (!sp) return nullptr;
  if (!spec(ar, w, f0, *sp)) return nullptr;
  for (int i = 0; i < kL; i++) { Zr[i] = 0; Zi[i] = 0; }
  double e = 0; for (int n = 1; n <= nmax; n++) e += sp->re[n] * sp->re[n] + sp->im[n] * sp->im[n];
  const double g = e > 0 ? 0.35 / std::sqrt(e / 2) : 0;
  const double p1 = js_hypot2(sp->re[1], sp->im[1]) > 1e-9 ? js_atan2(sp->re[1], sp->im[1]) : 0;
  for (int n = 1; n <= nmax; n++) {
    const double A = js_hypot2(sp->re[n], sp->im[n]) * g, ph = js_atan2(sp->re[n], sp->im[n]) - n * p1;
    Zr[n] = A * js_sin(ph) * 0.5; Zi[n] = -A * js_cos(ph) * 0.5; Zr[kL - n] = Zr[n]; Zi[kL - n] = -Zi[n];
  }
  spec_fft(Zr, Zi, kL, true);
  float* T = ar.get<float>(kL + 1); if (!T) return nullptr;
  for (int j = 0; j < kL; j++) T[j] = (float)Zr[j]; T[kL] = T[0]; return T;
}
}  // namespace

size_t xeng_vector(const VectorP& p, double sr, double hold, double bpm0, float* out, size_t cap) {
  const double f0 = 65.406 * js_pow(2, p.oct + orz(p.tune) / 12), h = mx(0.02, orv(hold, 1.5));
  const double atk = mx(0.0005, p.atk / 1000), dec = mx(0.001, p.dec / 1000), rel = mx(0.001, p.rel / 1000), sus = cl3(p.sus, 0, 1);
  const double len = mn(12, h + rel * 6 + 0.05), Nd = mx(1, std::ceil(len * sr));
  if (!okN(Nd)) return 0;
  const size_t N = (size_t)Nd;
  if (!out) return N;
  if (N > cap) return 0;
  XArena& ar = xarena(); ar.reset();
  KeysRnd rnd(0x7ec70);
  auto wix = [](double v) { return cidx(cl3(js_round(orz(v)), 0, kNW - 1), 0, kNW - 1); };
  const int U = cidx(cl3(js_round(p.unison), 1, 7), 1, 7); const double detC = p.det * p.det * 50; const bool seq = js_round(p.mode) == 1;
  const int nmax = cidx(mx(1, mn(256, std::floor(sr * 0.45 / (f0 * js_pow(2, detC / 1200) * 1.005)))), 1, 256);
  // slots: the tables the voices mix. Vector: the four corners. Sequence: each wave the steps use, once.
  const int S = cidx(cl3(js_round(p.steps), 1, 16), 1, 16); int stepW[16];
  { const double sv[16] = {p.s1, p.s2, p.s3, p.s4, p.s5, p.s6, p.s7, p.s8, p.s9, p.s10, p.s11, p.s12, p.s13, p.s14, p.s15, p.s16}; for (int k = 0; k < 16; k++) stepW[k] = wix(sv[k]); }
  int slotW[16], NS = 0;
  if (seq) { for (int k = 0; k < S; k++) { bool seen = false; for (int q = 0; q < NS; q++) if (slotW[q] == stepW[k]) seen = true; if (!seen) slotW[NS++] = stepW[k]; } }
  else { slotW[0] = wix(p.wa); slotW[1] = wix(p.wb); slotW[2] = wix(p.wc); slotW[3] = wix(p.wd); NS = 4; }
  double* Zr = ar.get<double>(kL); double* Zi = ar.get<double>(kL); if (!Zr || !Zi) return kRefuse;
  float* made[kNW]; for (int w = 0; w < kNW; w++) made[w] = nullptr;
  const float* tabs[16];
  for (int k = 0; k < NS; k++) { const int w = slotW[k]; if (w == kNZ) { tabs[k] = nullptr; continue; } if (!made[w]) { made[w] = cycle(ar, w, f0, nmax, Zr, Zi); if (!made[w]) return kRefuse; } tabs[k] = made[w]; }
  int stepSlot[16]; for (int k = 0; k < 16; k++) { stepSlot[k] = -1; for (int q = 0; q < NS; q++) if (slotW[q] == stepW[k]) { stepSlot[k] = q; break; } }
  bool hasNz = false; for (int k = 0; k < NS; k++) if (slotW[k] == kNZ) hasNz = true;
  // sequence timing
  static const double BEATS[12] = {0, 4, 2, 1.5, 1, 2.0 / 3, 0.75, 0.5, 1.0 / 3, 0.25, 1.0 / 6, 0.125};
  const double bpm = cl3(orv(bpm0, 120), 20, 400); const int di = cidx(cl3(js_round(p.sdiv), 0, 11), 0, 11);
  const double D = di == 0 ? mx(0.01, p.stime / 1000) : BEATS[di] * 60 / bpm;
  const int L0 = cidx(mn(S - 1, cl3(js_round(p.lstart), 1, 16) - 1), 0, S - 1);   // (NaN, not reachable from a song: the first step)
  const bool loop = js_round(p.sloop) >= 1; const double xf = mn(D, mx(0.002, cl3(p.xfade, 0, 1) * D));
  auto stepAt = [&](double t, double* r) {   // r = [current step, next step, fade 0..1, time into the step]; the steps stay doubles: a NaN time gives NaN steps, as in the JS
    if (t < 0) t = 0; const double k = std::floor(t / D), tau = t - k * D; double cur;
    if (k < S) cur = k; else if (loop) cur = L0 + std::fmod(k - S, (double)(S - L0)); else cur = S - 1;
    const double nxt = cur + 1 < S ? cur + 1 : loop ? (double)L0 : cur;
    double f = nxt != cur && tau > D - xf ? (tau - (D - xf)) / xf : 0; f = 0.5 - 0.5 * js_cos(kPi * f);
    r[0] = cur; r[1] = nxt; r[2] = f; r[3] = tau;
  };
  // stepSlot[x] of the JS: undefined (NaN, or no such step) for an index that is not a step; the weight written there goes nowhere (a typed array ignores a NaN index)
  auto slotOf = [&](double x) -> int { return (x == x && x >= 0 && x < 16 && x == std::floor(x)) ? stepSlot[(int)x] : -1; };
  // the vector envelope: from the joystick through points 1-4, then hold, loop or go back and forth
  const double PX[5] = {p.jx, p.x1, p.x2, p.x3, p.x4}, PY[5] = {p.jy, p.y1, p.y2, p.y3, p.y4}; double PT[5] = {0, p.t1, p.t2, p.t3, p.t4}; for (int k = 0; k < 5; k++) PT[k] = mx(0.001, PT[k] / 1000);
  const int vl = cidx(cl3(js_round(p.vloop), 0, 2), 0, 2); const double first = PT[1] + PT[2] + PT[3] + PT[4];
  int segA[6], segB[6]; double segD[6]; int nseg;
  if (vl == 1) { const int sa[4] = {4, 1, 2, 3}, sb[4] = {1, 2, 3, 4}; nseg = 4; for (int k = 0; k < 4; k++) { segA[k] = sa[k]; segB[k] = sb[k]; segD[k] = PT[k + 1]; } }
  else { const int sa[6] = {4, 3, 2, 1, 2, 3}, sb[6] = {3, 2, 1, 2, 3, 4}; nseg = 6; for (int k = 0; k < 6; k++) { segA[k] = sa[k]; segB[k] = sb[k]; } segD[0] = PT[4]; segD[1] = PT[3]; segD[2] = PT[2]; segD[3] = PT[2]; segD[4] = PT[3]; segD[5] = PT[4]; }
  double cyc = 0; for (int k = 0; k < nseg; k++) cyc += segD[k];
  double ep[2] = {0, 0};
  auto envPos = [&](double t) {
    if (t < first) { double a = 0; for (int k = 1; k <= 4; k++) { if (t < a + PT[k]) { const double f = (t - a) / PT[k]; ep[0] = PX[k - 1] + (PX[k] - PX[k - 1]) * f; ep[1] = PY[k - 1] + (PY[k] - PY[k - 1]) * f; return; } a += PT[k]; } }
    if (vl == 0) { ep[0] = PX[4]; ep[1] = PY[4]; return; }
    double u = std::fmod(t - first, cyc);
    for (int k = 0; k < nseg; k++) { const int a = segA[k], b = segB[k]; const double d = segD[k]; if (u < d) { const double f = u / d; ep[0] = PX[a] + (PX[b] - PX[a]) * f; ep[1] = PY[a] + (PY[b] - PY[a]) * f; return; } u -= d; }
    ep[0] = PX[4]; ep[1] = PY[4];
  };
  // unison: detune, phase, and the voice's spot (vector) or lag (sequence)
  double ph[7], inc[7], ox[7], oy[7], lag[7];
  for (int u = 0; u < U; u++) {
    const double d = U == 1 ? 0 : ((double)u / (U - 1) * 2 - 1) * detC; ph[u] = rnd(); inc[u] = f0 * js_pow(2, d / 1200) / sr;
    const double a = 2 * kPi * u / U + 0.4; ox[u] = U > 1 ? js_cos(a) * p.spread * 0.35 : 0; oy[u] = U > 1 ? js_sin(a) * p.spread * 0.35 : 0;
    lag[u] = U > 1 ? p.spread * D * u / (U - 1) : 0;
  }
  const double lph = rnd() * 6.283; const bool law = js_round(p.law) >= 1; double sr4[4] = {0, 0, 0, 0};
  auto weights = [&](double t, double* Wt) {   // fills Wt[u * NS + slot] for time t
    for (int k = 0; k < U * NS; k++) Wt[k] = 0;
    if (seq) { for (int u = 0; u < U; u++) { stepAt(t - lag[u], sr4); const int s0 = slotOf(sr4[0]), s1 = slotOf(sr4[1]); if (s0 >= 0) Wt[u * NS + s0] += 1 - sr4[2]; if (s1 >= 0) Wt[u * NS + s1] += sr4[2]; } return; }
    double bx = p.jx, by = p.jy; if (p.venv > 0) { envPos(t); bx += (ep[0] - bx) * p.venv; by += (ep[1] - by) * p.venv; }
    if (p.lx > 0 || p.ly > 0) { const double a = lph + 2 * kPi * p.lrate * t; bx += p.lx * 0.5 * js_sin(a); by += p.ly * 0.5 * js_cos(a); }
    for (int u = 0; u < U; u++) {
      const double X = cl3(bx + ox[u], 0, 1), Y = cl3(by + oy[u], 0, 1); const int b = u * NS;
      double a = (1 - X) * (1 - Y), q = X * (1 - Y), c = (1 - X) * Y, d = X * Y;
      if (law) { a = std::sqrt(a); q = std::sqrt(q); c = std::sqrt(c); d = std::sqrt(d); }
      Wt[b] = a; Wt[b + 1] = q; Wt[b + 2] = c; Wt[b + 3] = d;
    }
  };
  const size_t CR = 16; double W0[7 * 16], W1[7 * 16];
  const float* aT[28]; double aW[28], aD[28]; int an[7]; for (int k = 0; k < 28; k++) { aT[k] = nullptr; aW[k] = 0; aD[k] = 0; }
  weights(0, W0);
  const double ug = 1 / std::sqrt((double)U), kf = js_pow(f0 / 261.6, p.key), fdec = mx(0.005, p.fdec / 1000), kq = 2 - 1.9 * cl3(p.res, 0, 1);
  const double nzc = 1 - js_exp(-2 * kPi * mn(sr * 0.45, 7000) / sr), sg = seq ? cl3(p.sgate, 0, 1) : 0, sfe = seq ? p.sfenv : 0;
  double env = 0, st = 0, lp1 = 0, lp2 = 0, lpn = 0;
  for (size_t i0 = 0; i0 < N; i0 += CR) {
    const size_t i1 = i0 + CR < N ? i0 + CR : N; const double t0 = (double)i0 / sr; const size_t n = i1 - i0;
    weights((double)i1 / sr, W1);
    for (int u = 0; u < U; u++) {
      int m = 0;
      for (int k = 0; k < NS; k++) { const double a = W0[u * NS + k], b = W1[u * NS + k]; if (a < 1e-6 && b < 1e-6) continue; if (m >= 4) break; const int j = u * 4 + m++; aT[j] = tabs[k]; aW[j] = a * ug; aD[j] = (b - a) * ug / (double)n; }
      an[u] = m;
    }
    for (int k = 0; k < U * NS; k++) W0[k] = W1[k];
    // the filter, once a block: its envelope, key follow, and (sequence) a pulse on every step
    double fm = p.fenv * 5 * js_exp(-t0 / fdec);
    if (truthy(sfe)) { stepAt(t0, sr4); fm += sfe * 4 * js_exp(-sr4[3] * 6 / D); }
    const double fc = cl3(p.cut * kf * js_pow(2, fm), 20, sr * 0.45), g = js_tan(kPi * fc / sr), a1 = 1 / (1 + g * (g + kq)), a2 = g * a1;
    for (size_t i = i0; i < i1; i++) {
      const double t = (double)i / sr;
      if (t < h) { if (st == 0) { env += 1 / (atk * sr); if (env >= 1) { env = 1; st = 1; } } else env += (sus - env) * 2.2 / (dec * sr); }
      else env -= env * 3 / (rel * sr);
      double nz = 0; if (hasNz) { lpn += ((rnd() * 2 - 1) - lpn) * nzc; nz = lpn * ug; }   // one noise for all voices
      double y = 0;
      for (int u = 0; u < U; u++) {
        double x = ph[u] + inc[u]; if (x >= 1) x -= 1; ph[u] = x;
        const double r = x * kL; const int32_t ri = js_to_int32(r); const double rf = r - ri; const int b = u * 4, m = an[u];
        for (int k = 0; k < m; k++) { const int j = b + k; const float* T = aT[j]; const double w = aW[j]; aW[j] = w + aD[j]; double v; if (T) { const double ta = tabget(T, kL + 1, ri), tb2 = tabget(T, kL + 1, ri + 1); v = ta + (tb2 - ta) * rf; } else v = nz; y += w * v; }
      }
      if (sg > 0) { const double tau = std::fmod(t, D); y *= 1 - sg + sg * mn(1, tau / 0.002) * js_exp(-tau * 4 / D); }
      const double v1 = a1 * lp1 + a2 * (y - lp2), v2 = lp2 + g * v1; lp1 = 2 * v1 - lp1; lp2 = 2 * v2 - lp2;
      out[i] = (float)(v2 * env);
    }
  }
  return finish(out, N, p.level, p.drive, sr, 0.003);
}

}  // namespace thunder
