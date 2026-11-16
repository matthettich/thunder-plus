// U23: the sample layer (makeOsc's 'sample'): the sample at the note's pitch, forward or reversed (Shape >= 0.5), from Position; SAMPLE LOOP (5 Nov): loop between Loop start and Loop end (fractions of
// the sample as it plays: reversed, the ends swap), forward with a crossfade into the loop end, or ping-pong. The vintage modes (vint > 0) are layer_vint.cpp.
#include "engine/layers.h"
#include "rt/jsmath.h"

namespace thunder {
using l23::mn; using l23::mx; using l23::orv;

void SampleOsc::init(const EngLayer& L, double A_, double B) {
  const EngU23& U = L.u23; d = U.smp; len = U.smpLen; A = A_; rev = B >= 0.5; k = U.smpSr / kBase; pos = A * (double)len;
  lm = js_to_int32(U.lpMode); looped = false; dir = 1;
  const double dl = (double)len;
  const double s0 = std::floor(mx(0, mn(1, orv(U.lpS, 0))) * dl), e0 = std::floor(mx(0, mn(1, U.lpE)) * (dl - 1));   // (L.lpE == null ? 1 : L.lpE: the loader's default is 1)
  if (lm != 0 && e0 - s0 >= 32) {
    looped = true;
    ls = rev ? dl - 1 - e0 : s0; le = rev ? dl - 1 - s0 : e0; LL = le - ls;
    X = lm == 1 ? mn(std::floor(mx(0, mn(0.5, orv(U.lpX, 0))) * LL), ls) : 0;
  }
}

double SampleOsc::atL(double p) const {
  const double i = mx(0, mn((double)len - 2, (double)js_to_int32(p))), f = mx(0, mn(1, p - i));
  const double a = rev ? l23::at(d, len, (double)len - 1 - i) : l23::at(d, len, i), b = rev ? l23::at(d, len, (double)len - 2 - i) : l23::at(d, len, i + 1);
  return a + (b - a) * f;
}

double SampleOsc::s(double inc) {
  if (looped) {
    double y;
    if (lm == 1) {
      if (pos >= le) pos -= LL * mx(1, std::floor((pos - ls) / LL));
      if (X > 0 && pos >= le - X) { const double w = (pos - (le - X)) / X; y = atL(pos) * (1 - w) + atL(pos - LL) * w; } else y = atL(pos);
      pos += inc * k; return y;
    }
    if (dir > 0 && pos >= le) { pos = mx(ls, 2 * le - pos); dir = -1; } else if (dir < 0 && pos <= ls) { pos = mn(le, 2 * ls - pos); dir = 1; }
    y = atL(pos); pos += dir * inc * k; return y;
  }
  if (pos >= (double)len - 1) return 0;
  const int32_t i = js_to_int32(pos); const double f = pos - i, dl = (double)len;
  const double a = rev ? l23::at(d, len, dl - 1 - i) : l23::at(d, len, i), b = rev ? l23::at(d, len, mx(dl - 2 - i, 0)) : l23::at(d, len, (double)i + 1);
  pos += inc * k; return a + (b - a) * f;
}
}  // namespace thunder
