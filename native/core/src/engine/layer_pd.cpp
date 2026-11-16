// U23: the pd layer (newOsc's 'pd'): CZ-style phase distortion, a cosine read through a bent phase.
#include "engine/layers.h"
#include "rt/jsmath.h"

namespace thunder {
namespace {
inline double bend(double p, double k) { const double m = 0.5 - k * 0.49; return p < m ? p * 0.5 / m : 0.5 + (p - m) * 0.5 / (1 - m); }
}
void PdOsc::init(double A, double B) {
  wd = l23::mn(7, std::floor(B * 8));   // pdWave
  wi = (wd == wd && wd >= 0 && wd <= 6) ? (int)wd : 99;   // the switch: cases 0..6, anything else (7, NaN, negative) is the default branch
  d = A * 0.98; res = 1 + A * 14; ph = 0; prev = 0;
}
double PdOsc::s(double inc) {
  const double p = ph; double y; const double TP = 2 * l23::kPi;
  switch (wi) {
    case 0: y = js_cos(TP * bend(p, d)); break;
    case 1: { const double k = 1 - d + 0.02, q = p < 0.5 ? l23::mn(p * 2 / k, 1) * 0.5 : 0.5 + l23::mn((p - 0.5) * 2 / k, 1) * 0.5; y = js_cos(TP * q); break; }
    case 2: { const double k = 1 - d, q = p < k ? p / k : 1; y = js_cos(TP * q); break; }
    case 3: { const double q = eng_frac(p * 2); y = js_cos(TP * bend(q, d)) * (p < 0.5 ? 1 : 0.8); break; }
    case 4: y = p < 0.5 ? js_cos(TP * bend(p * 2, d) * 0.5) : js_cos(TP * (0.5 + bend((p - 0.5) * 2, d) * 0.5)); break;
    case 5: y = (1 - p) * js_cos(TP * eng_frac(p * res)) * 2 - (1 - p); break;
    case 6: { const double win = 1 - std::fabs(p * 2 - 1); y = win * js_cos(TP * eng_frac(p * res)) * 2 - win; break; }
    default: { const double win = p < 0.5 ? 1 : 2 - 2 * p; y = win * js_cos(TP * eng_frac(p * res)) * 2 - win; break; }
  }
  // a one-pole smooth on the jumps the resonant waves make at the cycle start
  const bool hi = wd >= 5;
  const double o = hi ? prev + (y - prev) * l23::mn(1, inc * 40 + 0.35) : y; prev = o;
  ph += inc; if (ph >= 1) ph -= 1; return o * (hi ? 0.45 : 0.8);
}
}  // namespace thunder
