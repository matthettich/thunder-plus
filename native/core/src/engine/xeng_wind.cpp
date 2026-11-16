// U28: WindCore (XENG wind): blown and bowed physical models (reed, jet, bow): delay loops with a nonlinear excitation, then a body of three resonances; the loop's real period is measured once on a quiet run and the
// delays are trimmed to it. index.html WindCore.render, line for line. ALLOCATES (offline render): the delay lines, the quiet run and the correlation table are Float32 / Float64 arrays sized from the sample rate and the pitch (std::vector here).
// A read past the quiet run (the correlation's lag window) gives NaN, as an undefined element does in the JS.
#include <vector>

#include "engine/xeng_util.h"

namespace thunder {
using namespace xe;
namespace {
struct Dl { std::vector<float> b; double w, L, d; };
struct Mode { double c1, c2, a, y1, y2; };
void mk(Dl& D, double n) { D.L = std::ceil(n * 2.6) + 8; D.b.assign((size_t)D.L, 0.0f); D.w = 0; D.d = n; }
inline size_t wrapIdx(double x, double L) { const double r = std::fmod(std::fmod(x, L) + L, L); return (r >= 0 && r < L) ? (size_t)r : 0; }
double rd(const Dl& D) { const double r = D.w - D.d, ri = std::floor(r), f = r - ri; const size_t i0 = wrapIdx(ri, D.L), i1 = (size_t)std::fmod((double)i0 + 1, D.L); return (double)D.b[i0] + ((double)D.b[i1] - (double)D.b[i0]) * f; }
void wr(Dl& D, double v) { D.b[(size_t)D.w] = (float)v; D.w = std::fmod(D.w + 1, D.L); }

struct Ctx { const WindP* p; double sr, f0, h, atk, rel, pres, slope, jetG, bowV, bowS, vibA, lpk, wet; int model; };

// run(N, scale, quiet): the loop for N samples into out (N floats)
void run(const Ctx& c, size_t N, double scale, bool quiet, float* out) {
  const WindP& p = *c.p; const double sr = c.sr, h = c.h; const int model = c.model;
  KeysRnd rnd(0x3b0e);
  Mode modes[3];
  { static const double m2[3][3] = {{190, 0.02, 1}, {380, 0.015, 0.6}, {620, 0.01, 0.4}}; const double m01[3][3] = {{c.f0 * 0.5, 0.004, 0.5}, {900, 0.006, 0.5}, {2400, 0.004, 0.3}};
    for (int i = 0; i < 3; i++) { const double f = model == 2 ? m2[i][0] : m01[i][0], tau = model == 2 ? m2[i][1] : m01[i][1], a = model == 2 ? m2[i][2] : m01[i][2];
      const double w = 2 * kPi * mn(sr * 0.4, f) / sr, r = js_exp(-1 / (tau * sr)); modes[i] = Mode{2 * r * js_cos(w), -r * r, a * (1 - r), 0, 0}; } }
  const double base = sr / c.f0; Dl D1, D2; mk(D1, base); mk(D2, base);
  double lp = 0, dcx = 0, dcy = 0, nlp = 0, odx = 0, ody = 0, prs = 0; const size_t CR = 16;
  for (size_t i0 = 0; i0 < N; i0 += CR) {
    const double t = (double)i0 / sr, env = quiet ? mn(1, t / 0.02) : t < h ? mn(1, t / c.atk) : js_exp(-(t - h) / c.rel);
    const double vib = quiet ? 1 : 1 + c.vibA * js_sin(6.2832 * p.vrate * t) * mn(1, t * 3), grw = !quiet && p.growl > 0 ? 1 + p.growl * 0.5 * js_sin(6.2832 * (28 + p.growl * 20) * t) : 1;
    const double per = base * scale / vib, nfix = model == 0 ? per * 0.5 - 1.5 : model == 1 ? per - 2 : per - 4;
    if (model == 2) { D1.d = mx(2, nfix * (0.1 + p.pos * 0.3)); D2.d = mx(2, nfix * (0.9 - p.pos * 0.3)); }
    else if (model == 1) { D1.d = mx(2, nfix * (0.38 + p.pos * 0.1)); D2.d = mx(2, nfix); }
    else D1.d = mx(2, nfix);
    const size_t i1 = (size_t)mn((double)N, (double)(i0 + CR));
    for (size_t i = i0; i < i1; i++) {
      prs += (c.pres * env * grw - prs) * 0.002;
      nlp += ((rnd() * 2 - 1) - nlp) * 0.3; const double nq = quiet ? 0 : p.noise, pr = prs * (1 + nq * (model == 1 ? 0.12 : 0.6) * nlp), nz = nq * 0.02 * nlp * env;
      double y;
      if (model == 0) {
        const double back = rd(D1); lp += (back - lp) * c.lpk; const double refl = -0.95 * lp;
        const double diff = refl - pr, rt = mx(-1, mn(1, 0.7 - c.slope * diff));
        wr(D1, pr + diff * rt + nz); y = back;
      } else if (model == 1) {
        const double bore = rd(D2); lp += (bore - lp) * c.lpk; const double dcb = lp - dcx + 0.995 * dcy; dcx = lp; dcy = dcb;
        wr(D1, pr - 0.5 * dcb);
        const double j = rd(D1) * c.jetG, jet = mx(-1, mn(1, j * (j * j - 1)));
        wr(D2, jet + 0.5 * dcb + nz); y = bore * 0.5;
      } else {
        const double bridge = rd(D1), nut = rd(D2); lp += (bridge - lp) * c.lpk; const double br = -lp, nr = -nut;
        const double sv = br + nr, bv = c.bowV * env * grw, vd = bv - sv;
        double fr = jabs(vd * c.bowS) + 0.75; fr = js_pow(fr, -4); fr = mx(0, mn(1, fr));
        const double nv = vd * fr; wr(D2, br + nv); wr(D1, nr + nv + nz); y = bridge;
      }
      const double od = y - odx + 0.995 * ody; odx = y; ody = od; y = od;
      if (quiet) { out[i] = (float)y; continue; }
      double b = 0; for (int m = 0; m < 3; m++) { Mode& q = modes[m]; const double v = q.a * y + q.c1 * q.y1 + q.c2 * q.y2; q.y2 = q.y1; q.y1 = v; b += v; }
      out[i] = (float)((y * (1 - c.wet * 0.5) + b * c.wet * 2.5) * (t < h ? 1 : env));
    }
  }
}
}  // namespace

size_t xeng_wind(const WindP& p, double sr, double hold, float* out, size_t cap) {
  const double model = mx(0, mn(2, js_round(p.model)));
  const double h = mx(0.03, orv(hold, 1)), atk = mx(0.0005, p.atk / 1000), rel = mx(0.001, p.rel / 1000);
  const double len = mn(12, h + rel * 6 + 0.1), Nd = mx(1, std::ceil(len * sr));
  if (!okN(Nd)) return 0;
  const size_t N = (size_t)Nd;
  if (!out) return N;
  if (N > cap) return 0;
  Ctx c; c.p = &p; c.sr = sr; c.model = (int)model; c.f0 = 65.406 * js_pow(2, p.oct + orz(p.tune) / 12); c.h = h; c.atk = atk; c.rel = rel;
  c.pres = 0.9 + p.breath * (model == 1 ? 0.45 : 0.6); c.slope = (0.22 + p.stiff * 0.07) / c.pres; c.jetG = (1.05 + p.stiff * 0.1) / c.pres; c.bowV = 0.08 + p.breath * 0.25; c.bowS = (0.48 + p.stiff * 0.1) / c.bowV;
  c.vibA = p.vib * p.vib * 0.02; c.lpk = 1 - js_exp(-2 * kPi * (model == 1 ? 500 + (1 - p.damp) * (1 - p.damp) * 3000 : 800 + (1 - p.damp) * (1 - p.damp) * 9000) / sr); c.wet = p.body;
  const double f0 = c.f0;
  // trim: run the loop for a moment, find its real period (the best-matching lag near the one asked for), and scale the delays to match; the lag is refined between samples with a parabola
  double scale = 1;
  {
    const double Md = js_round(sr * 0.5);
    if (okN(Md) && Md < 1e8) {
      const size_t M = (size_t)Md; std::vector<float> tst(M, 0.0f); run(c, M, 1, true, tst.data());
      auto T = [&](double i) -> double { return (i >= 0 && i < (double)M) ? (double)tst[(size_t)i] : (double)NAN; };   // tst[i]: undefined (NaN) outside the array
      const double s0 = js_round(Md * 0.6), W = Md - s0 - std::ceil(sr / f0 * 2.3) - 2, P = sr / f0;
      if (W > 64) {
        double e0 = 1e-9; for (double i = 0; i < W; i++) e0 += T(s0 + i) * T(s0 + i);
        double best = -1, bl = 0; const double lo = mx(2, std::floor(P * 0.84)), hi = std::ceil(P * 1.19);
        if (hi + 2 < 1e7) {
          std::vector<double> cs((size_t)hi + 2, 0.0);
          for (double L = lo; L <= hi; L++) { double cc = 0; for (double i = 0; i < W; i++) cc += T(s0 + i) * T(s0 + i + L); cs[(size_t)L] = cc / e0; }
          for (double L = lo + 1; L < hi; L++) { const double a = cs[(size_t)L]; if (a > best && a >= cs[(size_t)L - 1] && a >= cs[(size_t)L + 1]) { best = a; bl = L; } }
          if (best > 0.5 && bl != 0) {
            const size_t b = (size_t)bl; const double d = cs[b - 1] - cs[b + 1], dd = cs[b - 1] - 2 * cs[b] + cs[b + 1], fl = bl + (dd != 0 ? 0.5 * d / dd : 0), r = P / fl;
            if (r > 0.84 && r < 1.19 && jabs(r - 1) > 0.002) scale = r;
          }
        }
      }
    }
  }
  run(c, N, scale, false, out);
  return finish(out, N, p.level, p.drive, sr, 0.003);
}

}  // namespace thunder
