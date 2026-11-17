// U23: the grains layer (newOsc's 'grains'): four overlapping grains read from the layer's sample around Position, at the note's pitch.
#include "engine/layers.h"
#include "rt/jsmath.h"

namespace thunder {
using l23::mn; using l23::mx;

void GrainsOsc::init(const EngLayer& L, double sr, double A_, double B) {
  const EngU23& U = L.u23; d = U.smp; len = U.smpLen; A = A_;
  k = U.smpSr / kBase; gl = mx(32, (15 + B * B * 285) / 1000 * sr);   // grainMs(B) / 1000 * sr
  for (int i = 0; i < 4; i++) { pos[i] = 0; age[i] = -(double)i * gl / 4; }
  span = mx(0, (double)len - gl * 2);
}
double GrainsOsc::s(double inc, EngRng& R) {
  double y = 0;
  for (int g = 0; g < 4; g++) {
    if (age[g] < 0) { age[g]++; continue; }
    if (age[g] >= gl) { pos[g] = mx(0, mn((double)len - 2, A * span + (R.next() - 0.5) * 0.08 * (double)len)); age[g] = 0; }   // start(g)
    const double w = 0.5 - 0.5 * js_cos(2 * l23::kPi * age[g] / gl); const int32_t i = js_to_int32(pos[g]);
    if ((double)i < (double)len - 1) y += (l23::at(d, len, i) + (l23::at(d, len, (double)i + 1) - l23::at(d, len, i)) * (pos[g] - i)) * w;
    pos[g] += inc * k; age[g]++;
  }
  return y * 0.6;
}
}  // namespace thunder
