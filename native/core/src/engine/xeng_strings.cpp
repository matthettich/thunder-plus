// U28: StringsCore (XENG strings): a plucked string ensemble with a body: each string a delay loop with a damping low-pass and a fractional tap, set ringing by a noise burst, strummed, then four body resonances.
// index.html StringsCore.render + KeysUtil. ALLOCATES (offline render): each string's loop and burst are Float32Arrays sized from the sample rate and the pitch (std::vector here).
#include <vector>

#include "engine/xeng_util.h"

namespace thunder {
using namespace xe;
namespace {
const double kIvl[9][6] = {{0, 0, 0, 0, 0, 0}, {0, 12, 24, -12, 36, 7}, {0, 7, 12, 19, 24, 31}, {0, 4, 7, 12, 16, 19}, {0, 3, 7, 12, 15, 19}, {0, 5, 7, 12, 17, 19}, {0, 4, 7, 11, 14, 19}, {0, 3, 7, 10, 14, 17}, {0, 7, 12, 16, 19, 24}};
struct Str { double f, D; std::vector<float> buf, exc; double L; double w; double start, G, dc, lp, dcb; };
struct Mode { double c1, c2, a, y1, y2; };
}

size_t xeng_strings(const StringsP& p, double sr, double hold, float* out, size_t cap) {
  const double h = mx(0.03, orv(hold, 2)), nvD = mx(1, mn(6, js_round(p.voices)));
  const double T = mx(0.01, p.decay), rel = mx(0.001, p.rel / 1000), strum = p.strum / 1000;
  const double len = mn(12, strum * (nvD - 1) + mn(h, T * 3) + rel * 8 + 0.05), Nd = mx(1, std::ceil(len * sr));
  if (!okN(Nd)) return 0;
  const size_t N = (size_t)Nd;
  if (!out) return N;
  if (N > cap) return 0;
  KeysRnd rnd(0x57a1);
  const double f0 = 65.406 * js_pow(2, p.oct + orz(p.tune) / 12); const int nv = (int)nvD;
  const double* ivl = kIvl[(int)mx(0, mn(8, js_round(p.chord)))];
  std::vector<Str> strs; strs.reserve((size_t)nv);
  for (int v = 0; v < nv; v++) {
    const double order = p.down >= 0.5 ? nv - 1 - v : v, f = mn(sr / 4, f0 * js_pow(2, (ivl[v] + (rnd() * 2 - 1) * p.det * p.det * 18 / 100) / 12)), D = sr / f;
    const double L = std::ceil(D) + 2, ex = std::ceil(D);
    if (!(L >= 1 && L < 1e8)) return 0;
    Str s; s.f = f; s.L = L; s.w = 0; s.buf.assign((size_t)L, 0.0f); s.exc.assign((size_t)ex, 0.0f);
    const double pb = 0.08 + p.pick * p.pick * 0.9; double e1 = 0; for (size_t i = 0; i < (size_t)ex; i++) { e1 += ((rnd() * 2 - 1) - e1) * pb; s.exc[i] = (float)e1; }
    const double pd = mx(1, js_round(p.pos * ex)); for (double i = ex - 1; i >= pd; i--) s.exc[(size_t)i] = (float)((double)s.exc[(size_t)i] - (double)s.exc[(size_t)(i - pd)] * 0.9);
    const double G = js_exp(-D / (T * sr)), dc = 0.08 + (1 - p.damp) * (1 - p.damp) * 0.9;
    s.D = mx(2, D - (1 - dc) / dc); s.start = js_round(order * strum * sr); s.G = G; s.dc = dc; s.lp = 0; s.dcb = 0;
    strs.push_back(std::move(s));
  }
  const double sz = js_pow(2, (0.5 - p.size) * 3); Mode modes[4];
  { static const double mf[4][3] = {{105, 0.03, 1}, {215, 0.022, 0.7}, {420, 0.014, 0.5}, {790, 0.009, 0.35}};
    for (int i = 0; i < 4; i++) { const double w = 2 * kPi * mn(sr * 0.4, mf[i][0] * sz) / sr, r = js_exp(-1 / (mf[i][1] * sr)); modes[i] = Mode{2 * r * js_cos(w), -r * r, mf[i][2] * (1 - r), 0, 0}; } }
  const double wet = mx(0, mn(1, p.body)), vibA = p.vib * p.vib * 0.012;
  for (size_t i = 0; i < N; i++) {
    const double t = (double)i / sr, rl = t < h ? 1 : js_exp(-(t - h) / rel), vib = vibA != 0 ? 1 + vibA * js_sin(6.2832 * p.vrate * t) * mn(1, t * 2) : 1;
    double y = 0;
    for (Str& s : strs) {
      const double k = (double)i - s.start; if (k < 0) continue;
      const double Dn = s.D / vib, r = s.w - Dn, ri = std::floor(r), rf = r - ri, i0 = std::fmod(std::fmod(ri, s.L) + s.L, s.L), i1 = std::fmod(i0 + 1, s.L);
      const size_t a = (i0 >= 0 && i0 < s.L) ? (size_t)i0 : 0, b = (i1 >= 0 && i1 < s.L) ? (size_t)i1 : 0;
      const double back = (double)s.buf[a] + ((double)s.buf[b] - (double)s.buf[a]) * rf;
      s.lp += (back - s.lp) * s.dc;
      double x = s.lp * s.G * (t < h ? 1 : js_pow(rl, 0.08)) + (k < (double)s.exc.size() ? (double)s.exc[(size_t)k] : 0);
      s.dcb = s.dcb * 0.999 + x * 0.001; x -= s.dcb;
      s.buf[(size_t)s.w] = (float)x; s.w = std::fmod(s.w + 1, s.L); y += x;
    }
    y /= std::sqrt((double)nv);
    double b = 0; for (int m = 0; m < 4; m++) { Mode& q = modes[m]; const double v = q.a * y + q.c1 * q.y1 + q.c2 * q.y2; q.y2 = q.y1; q.y1 = v; b += v; }
    out[i] = (float)((y * (1 - wet * 0.6) + b * wet * 3) * (t < h ? 1 : rl));
  }
  return finish(out, N, p.level, p.drive, sr, 0.003);
}

}  // namespace thunder
