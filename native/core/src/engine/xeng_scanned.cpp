// U29: ScannedCore (XENG scanned): scanned synthesis (Verplank, Mathews and Shaw): a ring or string of up to 128 masses joined by springs, struck at a position and left to move at a slow update rate; its shape, read along a scan path as a
// wavetable at the note's pitch (every update's shape turned into harmonics and rebuilt band-limited with an inverse FFT, the audio crossfading from one shape to the next), is the sound. index.html ScannedCore, line for line.
#include <utility>

#include "engine/xeng_util.h"

namespace thunder {
using namespace xe;

size_t xeng_scanned(const ScannedP& p, double sr, double hold, float* out, size_t cap) {
  const double f0 = 65.406 * js_pow(2, p.oct + orz(p.tune) / 12), h = mx(0.02, orv(hold, 2));
  const double atk = mx(0.0005, p.atk / 1000), dec = mx(0.001, p.dec / 1000), rel = mx(0.001, p.rel / 1000), sus = cl3(p.sus, 0, 1);
  const double len = mn(12, h + rel * 6 + 0.05), Nd = mx(1, std::ceil(len * sr));
  if (!okN(Nd)) return 0;
  const size_t N = (size_t)Nd;
  if (!out) return N;
  if (N > cap) return 0;
  XArena& ar = xarena(); ar.reset();
  KeysRnd rnd(0x5ca11);
  // the masses and springs, in units of one update (one time step) and unit masses
  const int M = cidx(cl3(js_round(p.masses), 16, 128), 16, 128); const bool ring = js_round(p.ends) != 1; const double ur = cl3(p.urate, 1, 4000);
  const double D = 1 - js_exp(-1 / (15 * js_pow(10, -cl3(p.damp, 0, 1) * 2.6) * ur));
  const double Dh = p.smooth > 0.001 ? mn(0.2, (1 - js_exp(-1 / (4 * js_pow(10, -cl3(p.smooth, 0, 1) * 2.6) * ur))) / 4) : 0, Dc = mn(Dh, (1 - D) / 4.5);
  const double K = 0.03 + 0.85 * p.stiff * p.stiff, C = cl3(p.center, 0, 1) * cl3(p.center, 0, 1) * 0.35, Kmax = (3.85 - C - 2 * (D + 4 * Dc)) / 4;
  const int NSp = ring ? M : M + 1;
  double* Kj = ar.get<double>((size_t)NSp);
  for (int j = 0; Kj && j < NSp; j++) Kj[j] = mn(Kmax, K * (1 + cl3(p.taper, 0, 1) * 0.9 * js_cos(2 * kPi * j / M)));
  const double NL = p.hard * p.hard * 60;
  double* x = ar.get<double>((size_t)M); double* v = ar.get<double>((size_t)M); double* fs = ar.get<double>((size_t)NSp); double* lv = ar.get<double>((size_t)M);
  if (!Kj || !x || !v || !fs || !lv) return kRefuse;
  const int ex = cidx(cl3(js_round(p.exc), 0, 4), 0, 4); const double cpos = cl3(p.pos, 0, 1) * M, hw = mx(1, cl3(p.width, 0, 1) * M * 0.5);
  auto excite = [&]() {
    for (int i = 0; i < M; i++) {
      double d = i - cpos; if (ring) { d = std::fmod(std::fmod(d, (double)M) + M, (double)M); if (d > M / 2.0) d -= M; }
      const double ad = jabs(d); if (ad >= hw) continue;
      const double win = 0.5 + 0.5 * js_cos(kPi * ad / hw);
      if (ex == 0) x[i] += 1 - ad / hw;
      else if (ex == 1) v[i] += 0.35 * win;
      else if (ex == 2) x[i] += (rnd() * 2 - 1) * win;
      else if (ex == 3) x[i] += win;
      else x[i] += (d / hw) * win * 1.6;
    }
    if (ring && ex == 1) { double m = 0; for (int i = 0; i < M; i++) m += v[i]; m /= M; for (int i = 0; i < M; i++) v[i] -= m; }
  };
  auto simStep = [&]() {
    // spring j joins mass j and j+1 (string: spring 0 joins the left wall to mass 0, spring M mass M-1 to the right wall)
    for (int j = 0; j < NSp; j++) {
      double a, b; if (ring) { a = x[j]; b = x[j + 1 < M ? j + 1 : 0]; } else { a = j > 0 ? x[j - 1] : 0; b = j < M ? x[j] : 0; }
      const double d = b - a; double k = Kj[j]; if (NL > 0) { k *= 1 + NL * d * d; if (k > Kmax) k = Kmax; } fs[j] = k * d;
    }
    if (Dc > 0) for (int i = 0; i < M; i++) { const double l = ring ? v[i ? i - 1 : M - 1] : i ? v[i - 1] : 0, r = ring ? v[i + 1 < M ? i + 1 : 0] : i + 1 < M ? v[i + 1] : 0; lv[i] = l - 2 * v[i] + r; }
    for (int i = 0; i < M; i++) {
      const double fr = ring ? fs[i] - fs[i ? i - 1 : M - 1] : fs[i + 1] - fs[i];
      v[i] += fr - C * x[i] - D * v[i] + (Dc > 0 ? Dc * lv[i] : 0); x[i] += v[i];
    }
  };
  // the scan path: neighbours swapped (Shuffle) or jumbled in small groups (Scatter)
  int* path = ar.get<int>((size_t)M); if (!path) return kRefuse;
  { const int kind = cidx(cl3(js_round(p.path), 0, 2), 0, 2); for (int j = 0; j < M; j++) path[j] = j;
    if (kind == 1) { KeysRnd r(0x51f7 + (double)M);
      for (int j = 0; j + 1 < M; j += 2) { const double q = r(); if (q < 0.55) { std::swap(path[j], path[j + 1]); } else if (q < 0.75 && j + 2 < M) { std::swap(path[j], path[j + 2]); } } }
    else if (kind == 2) { KeysRnd r(0xa77e + (double)M); const int B = M >> 3 > 4 ? M >> 3 : 4;
      for (int b = 0; b < M; b += B) { const int e = M < b + B ? M : b + B; for (int j = e - 1; j > b; j--) { const int k = b + (int)std::floor(r() * (j - b + 1)); std::swap(path[j], path[k]); } } } }
  const int Mh = M >> 1;
  const int nmax = cidx(mx(1, mn(mn(2.0 * M, 128), std::floor(sr * 0.45 / f0))), 1, 128);
  int L = 1024; while (L < nmax * 16 && L < 2048) L <<= 1;   // big enough that the linear read's images stay ~60 dB down
  double* CS = ar.get<double>((size_t)M); double* SN = ar.get<double>((size_t)M); double* sc2 = ar.get<double>((size_t)nmax + 1); double* sq = ar.get<double>((size_t)M);
  double* Xr = ar.get<double>((size_t)Mh + 1); double* Xi = ar.get<double>((size_t)Mh + 1); double* Zr = ar.get<double>((size_t)L); double* Zi = ar.get<double>((size_t)L); double* rshape = ar.get<double>((size_t)M);
  float* A = ar.get<float>((size_t)L + 1); float* B = ar.get<float>((size_t)L + 1);
  if (!CS || !SN || !sc2 || !sq || !Xr || !Xi || !Zr || !Zi || !rshape || !A || !B) return kRefuse;
  for (int j = 0; j < M; j++) { CS[j] = js_cos(2 * kPi * j / M); SN[j] = js_sin(2 * kPi * j / M); }
  for (int n = 1; n <= nmax; n++) { const double z = kPi * n / M, s = js_sin(z) / z; sc2[n] = s * s / M; }
  const double keep = cl3(p.keep, 0, 1); double refE = 0;
  // the rest shape: a sine round a ring, one hump along a string
  const double ra = cl3(p.rest, 0, 1) * 0.8; for (int j = 0; j < M; j++) rshape[j] = ra * (ring ? js_sin(2 * kPi * j / M) : js_sin(kPi * (j + 1) / (M + 1)));
  // Level hold follows the ring's energy (moving + stretched + centred)
  auto energy = [&]() {
    double e = 0; for (int i = 0; i < M; i++) e += v[i] * v[i] + C * x[i] * x[i];
    for (int j = 0; j < NSp; j++) { const double a = ring ? x[j] : j > 0 ? x[j - 1] : 0, b = ring ? x[j + 1 < M ? j + 1 : 0] : j < M ? x[j] : 0; e += Kj[j] * (b - a) * (b - a); }
    return e;
  };
  auto build = [&](float* T) {
    double g = 1; if (keep > 0) { const double en = energy(); if (en > refE) refE = en; g = en > 1e-30 ? mn(31.6, js_pow(refE / en, keep * 0.5)) : 1; }
    for (int j = 0; j < M; j++) { const int q = path[j]; sq[j] = x[q] * g + rshape[q]; }
    for (int m = 0; m <= Mh; m++) { double a = 0, b = 0; int idx = 0; for (int j = 0; j < M; j++) { a += sq[j] * CS[idx]; b -= sq[j] * SN[idx]; idx += m; if (idx >= M) idx -= M; } Xr[m] = a; Xi[m] = b; }
    for (int i = 0; i < L; i++) { Zr[i] = 0; Zi[i] = 0; }
    for (int n = 1; n <= nmax; n++) {
      int m = n % M, sgn = 1; if (m > Mh) { m = M - m; sgn = -1; }
      if (m == 0) continue;
      const double hv = m == Mh && !(M & 1) ? 0.5 : 1, cr = Xr[m] * sc2[n] * hv, ci = sgn * Xi[m] * sc2[n] * hv;   // the ring's own Nyquist term appears once
      Zr[n] = cr; Zi[n] = ci; Zr[L - n] = cr; Zi[L - n] = -ci;
    }
    spec_fft(Zr, Zi, L, true);
    for (int j = 0; j < L; j++) T[j] = (float)Zr[j]; T[L] = T[0];
  };
  const double sub = mx(1, std::ceil(ur / 200)), spf = sub / ur * sr, rsteps = p.rexc > 0.01 ? mx(1, js_round(ur / p.rexc)) : 0;
  double steps = 0;
  auto advance = [&](float* T) { for (double s = 0; s < sub; s++) { simStep(); steps++; if (rsteps != 0 && std::fmod(steps, rsteps) == 0) excite(); } build(T); };
  excite();
  double fA = 0; build(A); advance(B);
  const double inc = f0 / sr, kf = js_pow(f0 / 261.6, p.key), fdec = mx(0.005, p.fdec / 1000), kq = 2 - 1.9 * cl3(p.res, 0, 1); const size_t CR = 16;
  double ph = 0, env = 0, st = 0, lp1 = 0, lp2 = 0;
  for (size_t i0 = 0; i0 < N; i0 += CR) {
    const size_t i1 = i0 + CR < N ? i0 + CR : N; const double t0 = (double)i0 / sr;
    const double fc = cl3(p.cut * kf * js_pow(2, p.fenv * 5 * js_exp(-t0 / fdec)), 20, sr * 0.45), g = js_tan(kPi * fc / sr), a1 = 1 / (1 + g * (g + kq)), a2 = g * a1;
    for (size_t i = i0; i < i1; i++) {
      const double fp = (double)i / spf; while (fp >= fA + 1) { fA++; std::swap(A, B); advance(B); }
      const double al = fp - fA, t = (double)i / sr;
      if (t < h) { if (st == 0) { env += 1 / (atk * sr); if (env >= 1) { env = 1; st = 1; } } else env += (sus - env) * 2.2 / (dec * sr); }
      else env -= env * 3 / (rel * sr);
      ph += inc; if (ph >= 1) ph -= 1;
      const double r = ph * L; const int32_t ri = js_to_int32(r); const double rf = r - ri;
      const double pa = tabget(A, L + 1, ri), pb = tabget(A, L + 1, ri + 1), qa = tabget(B, L + 1, ri), qb = tabget(B, L + 1, ri + 1);
      const double ya = pa + (pb - pa) * rf, yb = qa + (qb - qa) * rf, y = ya + (yb - ya) * al;
      const double v1 = a1 * lp1 + a2 * (y - lp2), v2 = lp2 + g * v1; lp1 = 2 * v1 - lp1; lp2 = 2 * v2 - lp2;
      out[i] = (float)(v2 * env);
    }
  }
  return finish(out, N, p.level, p.drive, sr, 0.003);
}

}  // namespace thunder
