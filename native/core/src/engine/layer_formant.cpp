// U24: the formant and VOSIM layers (index.html newOsc's 'formant' and 'vosim'). See layer_u24.h.
#include "engine/layer_u24.h"

#include "rt/jsmath.h"

namespace thunder {

void U24Formant::init(double a, double b) { ratio = 1 + a * a * 15; wd = 0.15 + b * 0.85; ph = 0; }
double U24Formant::s(double inc) {
  const double k = std::floor(ratio), f = ratio - k, win = ph < wd ? 0.5 - 0.5 * js_cos(2 * 3.141592653589793 * ph / wd) : 0;
  const double y = (js_sin(2 * 3.141592653589793 * ph * k) * (1 - f) + js_sin(2 * 3.141592653589793 * ph * (k + 1)) * f) * win;
  ph += inc; if (ph >= 1) ph -= 1; return y * 0.9;
}

void U24Vosim::init(double a, double b) { ratio = 1 + a * a * 15; dec = 0.35 + b * 0.6; ph = 0; dc = 0; }
double U24Vosim::s(double inc) {
  const double q = ph * ratio, k = std::floor(q); double y = 0;
  if (k < u24_max(1, std::floor(ratio))) { const double sn = js_sin(3.141592653589793 * (q - k)); y = sn * sn * js_pow(dec, k); }
  dc += (y - dc) * 0.0015; ph += inc; if (ph >= 1) ph -= 1; return (y - dc) * 1.6;
}

}  // namespace thunder
