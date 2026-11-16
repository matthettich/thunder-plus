// U23: the supersaw layer (newOsc's 'supersaw'): a centre saw and six detuned side saws.
#include "engine/layers.h"
#include "rt/jsmath.h"

namespace thunder {
namespace {
const double kSsaw[7] = {-0.11002313, -0.06288439, -0.01952356, 0, 0.01991221, 0.06216538, 0.10745242};   // SSAW
}
void SupersawOsc::init(double A, double B, EngRng& R) {
  det = js_pow(A, 1.6) * 1.0; side = 0.15 + B * 0.85;
  for (int k = 0; k < 7; k++) ph[k] = R.next();   // SSAW.map(() => R())
  g = 0.8 / (1 + side * 2.2);
}
double SupersawOsc::s(double inc) {
  double y = 0;
  for (int k = 0; k < 7; k++) {
    const double dt = l23::mn(inc * (1 + kSsaw[k] * det), 0.5); double p = ph[k];
    const double v = 2 * p - 1 - eng_blep(p, dt); y += k == 3 ? v : v * side; p += dt; if (p >= 1) p -= 1; ph[k] = p;
  }
  return y * g;
}
}  // namespace thunder
