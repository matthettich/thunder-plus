// U24: the Karplus-Strong layer (index.html newOsc's 'karplus' and class KSString). See layer_u24.h.
// The delay line and the excitation are Float32Arrays in the JS: every store into them is rounded to float, every read is a double.
#include <cstring>

#include "engine/layer_u24.h"
#include "rt/jsmath.h"

namespace thunder {
namespace {
constexpr int N = U24Karplus::N;
}

void U24Karplus::init(double srIn, double A_, double B_) {
  sr = srIn; A = A_; a = 0.25 + 0.74 * A_; t60 = 0.08 * js_pow(100, B_);
  std::memset(b, 0, sizeof b); w = 0; lp = 0; dc = 0; dx = 0; g = 0; cP = 0; cT = 0; on = false; haveC = false;
}

double U24Karplus::s(double inc, EngRng& R) {
  double P = 1 / u24_max(inc, 1e-5);
  if (!on) {
    // pluck(P, A, 0.13, 0.9, R)
    const double pos = 0.13, amp = 0.9, bright = A;
    P = u24_max(2, u24_min(N - 4, P));
    if (P != P) on = true;   // (a NaN period: `new Float32Array(NaN)` is empty, nothing is written; tick then runs on NaN)
    else {
      const int n = (int)std::ceil(P) + 2;
      static_assert(N + 8 >= 4100, "excitation buffer");
      float ex[N + 8], src[N + 8];
      const double k = 0.04 + 0.96 * bright * bright; double l = 0, pk = 1e-9;
      for (int i = 0; i < n; i++) { l += (R.next() * 2 - 1 - l) * k; ex[i] = (float)l; }
      const int d = (int)u24_max(1, js_round(pos * P));
      if (pos > 0.01) {
        std::memcpy(src, ex, sizeof(float) * (size_t)n); const int Ln = (int)u24_max(2, js_round(P));
        for (int i = 0; i < n; i++) ex[i] = (float)((double)src[i] - (double)src[((i - d) % Ln + Ln) % Ln]);
      }
      double m = 0; for (int i = 0; i < n; i++) m += ex[i];
      m /= n;
      for (int i = 0; i < n; i++) { ex[i] = (float)((double)ex[i] - m); pk = u24_max(pk, std::fabs((double)ex[i])); }
      const double gg = amp / pk;
      for (int i = 0; i < n; i++) { const int j = (w - n + i + N) % N; b[j] = (float)((double)b[j] + (double)ex[i] * gg); }
      on = true;
    }
  }
  // tick(P, t60, a)
  P = u24_max(2, u24_min(N - 4, P));
  auto rd = [&](double i) -> double { return (i >= 0 && i < N) ? (double)b[(int)i] : (double)NAN; };   // an index outside the array reads undefined: NaN
  const double dd = u24_max(1.2, P - (1 - a) / a), r = w - dd, ri = std::floor(r), fr = r - ri;
  const double i0 = ri < 0 ? ri + N : ri, i1 = i0 + 1 >= N ? 0 : i0 + 1, x = rd(i0) + (rd(i1) - rd(i0)) * fr;
  lp += (x - lp) * a;
  if (!haveC || P != cP || t60 != cT) { cP = P; cT = t60; haveC = true; g = js_pow(10, -3 * (P / sr) / u24_max(0.01, t60)); }   // loss per pass
  const double y = lp * g;
  b[w] = (float)y; w = w + 1 == N ? 0 : w + 1;
  const double o = y - dx + 0.995 * dc; dx = y; dc = o; return o;
}

}  // namespace thunder
