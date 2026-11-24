// U10: see beat.h. The lines of core/core-processor.js: ev() 'beat', the stop / play resets, process1's clock fill.
#include "mix/beat.h"

#include <algorithm>
#include <cmath>

namespace thunder {

bool BeatClock::event(const SeqEvent& e, double now) {
  if (e.type != SEV_BEAT) return false;
  const double at = std::max(e.t, now), b = e.v;
  const double o = page_ ? 0 : 1;   // the core's lane is beat + 1; the page player's offset is the beat itself
  if (!on_) { on_ = true; base_ = std::floor(b / 16) * 16; A_.set(b - base_ + o, at); }
  else { const double v = b - base_; A_.lin(v + o, at); if (v >= 16) { base_ += 16; A_.set(v - 16 + o, at); } }
  if (A_.size() > peak_) peak_ = A_.size();
  return true;
}

void BeatClock::stop(double at) { on_ = false; A_.cancel(at); A_.set(page_ ? -1 : 0, at); }

void BeatClock::render(float* C, int N, double t0, double dt, double* scratch) {
  A_.prune(t0);
  if (A_.size() == 0 && A_.v0 == (page_ ? -1 : 0)) { for (int i = 0; i < N; i++) C[i] = page_ ? -1.0f : 0.0f; return; }
  A_.fill(scratch, t0, dt, N);
  for (int i = 0; i < N; i++) C[i] = (float)scratch[i];
}

}  // namespace thunder
