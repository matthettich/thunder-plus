// U29: BytebeatCore (XENG bytebeat): a one-line integer formula (V.bbExpr, compiled by modfx/bbexpr: reused, not copied) clocked at a pitch-tracked rate, 8-bit / signed / linear, bit-crushed, optionally smoothed, DC-blocked, ADSR.
// index.html BytebeatCore.render, line for line. A formula that does not compile gives a one-sample silence (`new Float32Array(1)`), as the JS's catch does. The compiled formula allocates (std::vector): offline render.
#include <string>

#include "engine/xeng_util.h"
#include "modfx/bbexpr.h"

namespace thunder {
using namespace xe;

namespace {
const char* const kDefaultExpr = "t*(t>>5|t>>8)";
// String.prototype.trim() leaves something: not only ASCII whitespace, no-break space and the other Unicode spaces (UTF-8 here), the line / paragraph separators and the BOM
bool hasNonSpace(const std::string& s) {
  size_t i = 0; const size_t n = s.size();
  while (i < n) {
    const unsigned char c = (unsigned char)s[i];
    if (c == ' ' || (c >= 9 && c <= 13)) { i++; continue; }
    if (c == 0xC2 && i + 1 < n && (unsigned char)s[i + 1] == 0xA0) { i += 2; continue; }   // U+00A0
    if (c == 0xE1 && i + 2 < n && (unsigned char)s[i + 1] == 0x9A && (unsigned char)s[i + 2] == 0x80) { i += 3; continue; }   // U+1680
    if (c == 0xE2 && i + 2 < n) {
      const unsigned char d = (unsigned char)s[i + 1], e = (unsigned char)s[i + 2];
      if ((d == 0x80 && ((e >= 0x80 && e <= 0x8A) || e == 0xA8 || e == 0xA9 || e == 0xAF)) || (d == 0x81 && e == 0x9F)) { i += 3; continue; }   // U+2000..200A, 2028, 2029, 202F, 205F
    }
    if (c == 0xE3 && i + 2 < n && (unsigned char)s[i + 1] == 0x80 && (unsigned char)s[i + 2] == 0x80) { i += 3; continue; }   // U+3000
    if (c == 0xEF && i + 2 < n && (unsigned char)s[i + 1] == 0xBB && (unsigned char)s[i + 2] == 0xBF) { i += 3; continue; }   // U+FEFF
    return true;
  }
  return false;
}
inline double smoothHz(double s) { return 20000 * js_pow(2, -7 * mx(0, mn(1, s))); }
inline double loopLen(double v) { const double k = js_round(v); return k < 1 ? 0 : js_pow(2, mn(13, k) + 7); }
}  // namespace

size_t xeng_bytebeat(const BytebeatP& p, double sr, double hold, const std::string* extra, float* out, size_t cap) {
  modfx::BBProg P;
  if (out) {
    const std::string src = (extra && hasNonSpace(*extra)) ? *extra : std::string(kDefaultExpr);
    if (!modfx::BBProg::compile(src, &P)) { if (cap < 1) return 0; out[0] = 0.0f; return 1; }   // catch (e) { return new Float32Array(1) }
  }
  const double semis = 12 * (js_round(p.oct) - 2) + orz(p.tune), tr = c01(p.track);
  const double clock = mx(250, mn(96000, mx(1000, mn(48000, p.rate)) * js_pow(2, tr * semis / 12)));
  const double h = mx(0.02, orv(hold, 1)), atk = mx(0.0005, p.atk / 1000), dec = mx(0.001, p.dec / 1000), sus = c01(p.sus), rel = mx(0.001, p.rel / 1000);
  const double len = mn(12, h + rel * 5 + 0.01), Nd = mx(1, std::ceil(len * sr));
  if (!okN(Nd)) return 0;
  const size_t N = (size_t)Nd;
  if (!out) return N;
  if (N > cap) return 0;
  const int32_t A = (int32_t)js_round(mx(0, mn(255, p.a))), B = (int32_t)js_round(mx(0, mn(255, p.b))), Cc = (int32_t)js_round(mx(0, mn(255, p.c)));
  const int mode = (int)mx(0, mn(2, js_round(p.mode))), bits = (int)mx(1, mn(8, js_round(p.bits))); const int32_t keep = ~((1 << (8 - bits)) - 1);
  const int32_t t0 = js_to_int32(mx(0, mn((double)(1 << 30), js_round(p.start)))); const double L = loopLen(p.loop);
  const double step = clock / sr; if (!(step <= 1e4)) return kRefuse;   // (a clock of 1e4 ticks a sample or more: the JS would not finish either)
  const bool sm = p.smooth > 0.005; const double g = sm ? 1 - js_exp(-2 * kPi * mn(sr * 0.45, smoothHz(p.smooth)) / sr) : 1;
  const double dcR = 1 - 2 * kPi * 6 / sr, gLv = sus + (1 - sus) * js_exp(-6.9 * mx(0, h - atk) / dec);
  double ph = 0, n = 0, held = (double)(P.s8(t0, A, B, Cc, mode) & keep) / 128, s1 = 0, s2 = 0, dx = 0, dy = 0;
  for (size_t i = 0; i < N; i++) {
    ph += step;
    if (ph >= 1) {
      double acc = 0, k = 0;
      while (ph >= 1) { ph -= 1; n++; if (L != 0 && n >= L) n -= L; acc += (double)(P.s8(js_to_int32((double)t0 + n), A, B, Cc, mode) & keep); k++; }
      held = acc / (k * 128);
    }
    double y = held;
    if (sm) { s1 += (y - s1) * g; s2 += (s1 - s2) * g; y = s2; }
    const double o = y - dx + dcR * dy; dx = y; dy = o;   // DC blocker
    const double tt = (double)i / sr, env = tt < h ? (tt < atk ? tt / atk : sus + (1 - sus) * js_exp(-6.9 * (tt - atk) / dec)) : gLv * js_exp(-6.9 * (tt - h) / rel);
    out[i] = (float)(o * env);
  }
  return finish(out, N, p.level, p.drive, sr, 0.003);
}

}  // namespace thunder
