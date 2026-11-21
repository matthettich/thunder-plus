// ONE SECONDS-TO-SAMPLE RULE for the note players of a song render (LIVE-ROUTE P6d). The sampler (voice.cpp run()) starts a voice at the first sample i of a render quantum whose time
// `tb0 + i * dt` (tb0 = quantumStart / sr, dt = 1 / sr, both doubles) is at or after the note's time. sample_at_or_after() is that rule as a function of the absolute sample, so a live note
// and a sampler note with the same ev.t start on the same sample. The quantum is kRenderQuantum (128) frames on one global grid.
#ifndef THUNDER_RT_SAMPLETIME_H
#define THUNDER_RT_SAMPLETIME_H
#include <cmath>
#include <cstdint>

namespace thunder {

// The time the sampler compares with a note's time at absolute sample s.
inline double sample_time(int64_t s, double sr) {
  const int64_t q = s & ~(int64_t)127;
  return (double)q / sr + (double)(s - q) * (1 / sr);
}
// The first sample whose sample_time() is >= t. t <= 0 (or NaN): 0. A time beyond 2^50 samples is clamped there.
inline int64_t sample_at_or_after(double t, double sr) {
  if (!(t > 0)) return 0;
  const double e = std::ceil(t * sr);
  if (!(e < 1125899906842624.0)) return (int64_t)1125899906842624LL;
  int64_t s = (int64_t)e;
  while (s > 0 && sample_time(s - 1, sr) >= t) s--;
  while (sample_time(s, sr) < t) s++;
  return s;
}

}  // namespace thunder
#endif
