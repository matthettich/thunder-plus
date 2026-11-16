// U23: the chip layer (newOsc's 'chip'): 4-bit levels updated at a chip clock.
#include "engine/layers.h"
#include "rt/jsmath.h"

namespace thunder {
void ChipOsc::init(double sr, double A, double B) {
  const double wv = l23::mn(5, std::floor(A * 6));   // chipWave
  w = (wv != wv) ? 99 : (wv < -1e9 ? -99 : (int)wv);   // NaN: no branch of the JS's `w <= 2 / === 3 / === 4` matches, so it is the noise one (99)
  const double clk = 4000 * js_pow(12, B);            // chipClock
  hold = l23::mx(1, sr / clk); ph = 0; c = 0; out = 0; lf = 0x4000;
}
double ChipOsc::s(double inc) {
  ph += inc;
  if (ph >= 1) { ph -= 1; if (w == 5) { const int32_t fb = (lf ^ (lf >> 1)) & 1; lf = (lf >> 1) | (fb << 14); } }
  if (--c <= 0) {
    c += hold; double v;
    if (w <= 2) { static const double duty[3] = {0.125, 0.25, 0.5}; v = (w >= 0 && ph < duty[w]) ? 15 : 0; }   // [..][w] is undefined for a negative w: ph < undefined is false: 0
    else if (w == 3) { const double st = std::floor(ph * 32); v = st < 16 ? 15 - st : st - 16; }
    else if (w == 4) v = std::floor(ph * 16);
    else {
      if (std::floor(ph * 16) != std::floor((ph - inc) * 16)) { const int32_t fb = (lf ^ (lf >> 1)) & 1; lf = (lf >> 1) | (fb << 14); }
      v = (lf & 1) ? 15 : 0;
    }
    out = v / 7.5 - 1;
  }
  return out * 0.7;
}
}  // namespace thunder
