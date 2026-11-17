// U29: TableCore (XENG table): a wavetable synth. Four built-in tables of eight frames (their partials found once with a DFT of the time-domain shape) or the pad's own loaded table (V.wtData: Int16 cos / sin pairs, base64),
// each frame rebuilt band-limited for the note with an inverse FFT the first time the position reaches it; unison, warp, sub, noise, a two-pole low-pass with an envelope, an ADSR.
// index.html TableCore (partials, shape, wtPartials, render), line for line. The loaded table is bounded (frames x partials <= 2^21; the JS would allocate whatever the data says); non-integer frame / partial counts are refused.
#include "engine/xeng_util.h"

namespace thunder {
using namespace xe;

namespace {
constexpr int kL = 2048;

// TableCore.shape(t, k, x): one cycle of frame k (0..7) of built-in table t at phase x
double shape(int t, int k, double x) {
  const double m = (double)k / 7;
  if (t == 0) {
    const double s = m * 4, i0 = mn(3, std::floor(s)), f = s - i0; const int i = (int)i0;
    const double w[5] = {js_sin(6.2832 * x), 1 - 4 * jabs(x - 0.5), 2 * x - 1, x < 0.5 ? 1.0 : -1.0, x < 0.12 ? 1 : -0.27};
    return w[i] * (1 - f) + w[i + 1] * f;
  }
  if (t == 1) {
    double y = 0;
    for (int n = 1; n <= 24; n++) { const double g = 0.5 + 0.5 * js_cos(n * (1.2 - m) * 1.7), a = js_pow(n, -0.8 - (1 - m) * 1.2) * g; y += a * js_sin(6.2832 * n * x); }
    return y;
  }
  if (t == 2) {
    static const double F[8][2] = {{730, 1090}, {660, 1720}, {530, 1840}, {390, 1990}, {270, 2290}, {570, 840}, {440, 1020}, {300, 870}};
    const double s = m * 7, i0 = mn(6, std::floor(s)), f = s - i0; const int i = (int)i0;
    const double f1 = F[i][0] * (1 - f) + F[i + 1][0] * f, f2 = F[i][1] * (1 - f) + F[i + 1][1] * f; double y = 0;
    for (int n = 1; n <= 40; n++) { const double h = n * 110, a = js_exp(-js_pow((h - f1) / 160, 2)) + 0.6 * js_exp(-js_pow((h - f2) / 220, 2)) + 0.02 / n; y += a * js_sin(6.2832 * n * x); }
    return y;
  }
  double y = js_sin(6.2832 * x) * (1 + m * 5); y = jabs(std::fmod(std::fmod(y + 1, 4) + 4, 4) - 2) - 1; const double q = 2 + js_round((1 - m) * 30); return js_round(y * q) / q;
}

// The frames of a table: nf pairs of partial arrays (Float32Array h + 1 each: index 1..h)
struct Frames { float** re = nullptr; float** im = nullptr; int nf = 0, H = 0; };
bool alloc_frames(XArena& ar, Frames& F, int nf, int h) {
  F.nf = nf; F.H = h; F.re = ar.get<float*>((size_t)nf); F.im = ar.get<float*>((size_t)nf); if (!F.re || !F.im) return false;
  for (int k = 0; k < nf; k++) { F.re[k] = ar.get<float>((size_t)h + 1); F.im[k] = ar.get<float>((size_t)h + 1); if (!F.re[k] || !F.im[k]) return false; }
  return true;
}

// TableCore.partials(t): the 8 frames' partials 1..128 by a DFT of the shape (Float32Array time signal and partial arrays)
bool builtIn(XArena& ar, int t, Frames& F) {
  const int M = 1024, H = 128;
  float* w = ar.get<float>(M); if (!w || !alloc_frames(ar, F, 8, H)) return false;
  for (int k = 0; k < 8; k++) {
    double dc = 0; for (int i = 0; i < M; i++) { w[i] = (float)shape(t, k, (double)i / M); dc += (double)w[i]; } dc /= M;
    float* re = F.re[k]; float* im = F.im[k];
    for (int n = 1; n <= H; n++) {
      double a = 0, b = 0, c = 1, sn = 0; const double wc = js_cos(6.283185307 * n / M), ws = js_sin(6.283185307 * n / M);
      for (int i = 0; i < M; i++) { const double x = (double)w[i] - dc; a += x * c; b += x * sn; const double c2 = c * wc - sn * ws; sn = sn * wc + c * ws; c = c2; }
      re[n] = (float)(a * 2 / M); im[n] = (float)(b * 2 / M);
    }
  }
  return true;
}

// TableCore.wtPartials(wt): the loaded table's frames, q read as little-endian Int16 (past the data: undefined, `|| 0`)
bool loaded(XArena& ar, const XWtData& wt, Frames& F, int nf, int h) {
  if (!alloc_frames(ar, F, nf, h)) return false;
  size_t nb = 0; const uint8_t* u = spec_unb64(ar, wt.a, &nb); if (!u) return false;
  const size_t nq = nb / 2; const double s = orv(wt.pk, 1) / 32767;
  auto q = [&](size_t i) -> double { return i < nq ? (double)(int16_t)(uint16_t)(u[2 * i] | (u[2 * i + 1] << 8)) : 0.0; };
  for (int k = 0; k < nf; k++)
    for (int n = 1; n <= h; n++) { const size_t i0 = ((size_t)k * (size_t)h + (size_t)(n - 1)) * 2; F.re[k][n] = (float)(q(i0) * s); F.im[k][n] = (float)(q(i0 + 1) * s); }
  return true;
}
}  // namespace

size_t xeng_table(const TableP& p, double sr, double hold, const XWtData* wt, float* out, size_t cap) {
  const double f0 = 65.406 * js_pow(2, p.oct + orz(p.tune) / 12), h = mx(0.02, orv(hold, 1)), atk = mx(0.0005, p.atk / 1000), dec = mx(0.001, p.dec / 1000), rel = mx(0.001, p.rel / 1000);
  const double len = mn(12, h + rel * 6 + 0.05), Nd = mx(1, std::ceil(len * sr));
  int tb = cidx(mx(0, mn(4, js_round(p.table))), 0, 4); const bool own = tb == 4 && wt && wt->valid;
  if (tb == 4 && !own) tb = 0;
  if (own && !(std::floor(wt->nf) == wt->nf && std::floor(wt->h) == wt->h && wt->nf <= 4096 && wt->h <= 4096 && wt->nf * wt->h <= 2097152)) return kRefuse;   // (the JS would allocate nf frames of h + 1 floats whatever the data says)
  if (!okN(Nd)) return 0;
  const size_t N = (size_t)Nd;
  if (!out) return N;
  if (N > cap) return 0;
  XArena& ar = xarena(); ar.reset();
  KeysRnd rnd(0x7ab1e);
  Frames F; const bool ok = own ? loaded(ar, *wt, F, (int)wt->nf, (int)wt->h) : builtIn(ar, tb, F);
  if (!ok) return kRefuse;
  const int NF = F.nf, H = F.H;
  const double nmaxD = mx(1, mn((double)H, std::floor(sr * 0.45 / (f0 * 1.06)))); const int nmax = cidx(nmaxD, 1, H);
  float** cyc = ar.get<float*>((size_t)NF); if (!cyc) return kRefuse;
  double* Zr = ar.get<double>(kL); double* Zi = ar.get<double>(kL); if (!Zr || !Zi) return kRefuse;
  // each frame's cycle is built (an inverse FFT) the first time the position reaches it
  auto cycle = [&](int k) -> float* {
    if (cyc[k]) return cyc[k];
    const float* re = F.re[k]; const float* im = F.im[k]; float* w = ar.get<float>(kL + 1); if (!w) return nullptr;
    for (int i = 0; i < kL; i++) { Zr[i] = 0; Zi[i] = 0; }
    double pk = 0;
    for (int n = 1; n <= nmax; n++) if (n < kL) { Zr[n] = orz((double)re[n]); Zi[n] = -orz((double)im[n]); }   // (a write past the array is ignored)
    spec_fft(Zr, Zi, kL, true);
    for (int i = 0; i < kL; i++) { w[i] = (float)Zr[i]; pk = mx(pk, jabs((double)w[i])); }
    if (pk > 0) for (int i = 0; i < kL; i++) w[i] = (float)((double)w[i] / pk);
    w[kL] = w[0]; return cyc[k] = w;
  };
  const int U = cidx(mx(1, mn(7, js_round(p.unison))), 1, 7); double ph[7], inc[7];
  for (int u = 0; u < U; u++) { const double d = U == 1 ? 0 : (((double)u / (U - 1)) * 2 - 1) * p.det * p.det * 40; ph[u] = rnd(); inc[u] = f0 * js_pow(2, d / 1200) / sr; }
  const double ug = 1 / std::sqrt((double)U), warp = mx(0, mn(1, p.warp)), wp = 0.5 - warp * 0.46;
  double sph = 0, lp1 = 0, lp2 = 0, env = 0, st = 0, lfo = rnd() * 6.283;
  const size_t CR = 16; double posNow = p.pos;
  for (size_t i0 = 0; i0 < N; i0 += CR) {
    const double t = (double)i0 / sr;
    const double fe = js_exp(-t / mx(0.005, p.fdec / 1000)); const double pe = js_exp(-t / mx(0.005, p.pdec / 1000));
    posNow = mx(0, mn(1, p.pos + p.pmod * pe + p.pdepth * 0.5 * js_sin(lfo + 6.2832 * p.prate * t)));
    const double cutNow = mn(sr * 0.45, p.cut * js_pow(2, p.fenv * 5 * fe) * js_pow(f0 / 261.6, p.key)), resNow = p.res;
    const double g = js_tan(kPi * mx(20, cutNow) / sr), k = 2 - 1.9 * resNow, a1 = 1 / (1 + g * (g + k)), a2 = g * a1;
    const double fpos = posNow * (NF - 1), fiD = mn(mx(0, NF - 2), std::floor(fpos));
    if (!(fiD == fiD)) return kRefuse;   // the JS: cyc[NaN] is undefined and destructuring it throws
    const int fi = (int)fiD; const double ff = NF > 1 ? fpos - fi : 0;
    const float* A = cycle(fi); const float* B = NF > 1 ? cycle(fi + 1) : A; if (!A || !B) return kRefuse;
    const size_t i1 = i0 + CR < N ? i0 + CR : N;
    for (size_t i = i0; i < i1; i++) {
      const double tt = (double)i / sr;
      if (tt < h) { if (st == 0) { env += (1 / (atk * sr)); if (env >= 1) { env = 1; st = 1; } } else env += (p.sus - env) * (1 / (dec * sr)) * 2.2; }
      else env -= env * (1 / (rel * sr)) * 3;
      double y = 0;
      for (int u = 0; u < U; u++) {
        double x = ph[u]; ph[u] += inc[u]; if (ph[u] >= 1) ph[u] -= 1;
        if (warp > 0) x = x < wp ? x * 0.5 / wp : 0.5 + (x - wp) * 0.5 / (1 - wp);
        const double r = x * kL; const int32_t ri = js_to_int32(r); const double rf = r - ri;
        const double pa = tabget(A, kL + 1, ri), pb = tabget(A, kL + 1, ri + 1), qa = tabget(B, kL + 1, ri), qb = tabget(B, kL + 1, ri + 1);
        const double s = pa + (pb - pa) * rf, s2 = qa + (qb - qa) * rf;
        y += s + (s2 - s) * ff;
      }
      y *= ug;
      sph += inc[0] * 0.5; if (sph >= 1) sph -= 1; y += p.sub * 0.8 * js_sin(6.2832 * sph) + p.noise * 0.5 * (rnd() * 2 - 1);
      const double v1 = a1 * lp1 + a2 * (y - lp2), v2 = lp2 + g * v1; lp1 = 2 * v1 - lp1; lp2 = 2 * v2 - lp2; y = v2;
      out[i] = (float)(y * env);
    }
  }
  return finish(out, N, p.level, p.drive, sr, 0.003);
}

}  // namespace thunder
