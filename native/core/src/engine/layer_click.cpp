// U23: the click layer (makeOsc's 'click'): one half or whole sine cycle of Width ms, then silence.
#include "engine/layers.h"
#include "rt/jsmath.h"

namespace thunder {
void ClickOsc::init(double A, double B, double sr) {
  W = l23::mx(1, (0.05 + A * A * 5) / 1000 * sr); dbl = B >= 0.5; n = 0;
}
double ClickOsc::s() {
  if (n >= W) return 0;
  const double y = dbl ? eng_sinT(n / W) : eng_sinT(0.5 * n / W); n++; return y;
}
}  // namespace thunder
