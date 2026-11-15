// WEB AUDIO'S DelayNode, for the latency compensation (U10): a stereo history ring read at a delay that is a constant or a lane, and the
// delayTime param with makePlayer's pdcSync `set`. Fixed size, made once: push / read / set allocate nothing and take no lock.
//   - the delay is in seconds on the param (as the JS) and read in samples (seconds x sample rate, a double); a whole number of samples is a
//     plain copy, anything else is linearly interpolated between the two samples around it (Chrome's DelayNode does the same). A delay
//     that is whole to 1e-9 of a sample counts as whole (a plan in seconds does not come back as the exact integer: 2048 / 48000 * 48000).
//   - the history before the first sample is silence; a ring holds cap + maxBlock + 2 samples.
//   - NOT reproduced: Chrome holds delayTime in float32, so 2048 samples read back as 2048.00002 and leak about -90 dB into the next sample.
//     Latencies are whole samples, so the port delivers them whole.
#ifndef THUNDER_MIX_DELAY_H
#define THUNDER_MIX_DELAY_H
#include <cmath>
#include <cstdint>
#include <memory>

#include "rt/auto.h"

namespace thunder {

class DelayRing {
 public:
  DelayRing() = default;
  void init(int capSamples, int maxBlock, int channels = 2);     // allocates (control thread). One channel: the R pointers are not read or written (a mono signal: the beat clock)
  bool ready() const { return b_ != nullptr; }
  int cap() const { return cap_; }
  void push(const float* L, const float* R, int N);                          // the next N samples (N <= maxBlock)
  // out[i] = the sample i of the block just pushed, D samples earlier (D: 0..cap; one value, or one for every sample)
  void read(double D, float* oL, float* oR, int N) const;
  void readVar(const double* D, float* oL, float* oR, int N) const;
 private:
  float at(int ch, int64_t a) const { return b_[(size_t)ch * (size_t)R_ + (size_t)(a % R_)]; }   // a >= 0 (the callers clamp)
  std::unique_ptr<float[]> b_;
  int R_ = 0, cap_ = 0, nch_ = 2;
  int64_t n_ = 0;   // samples pushed so far (the next one's absolute index)
};

// A DelayNode's delayTime: seconds as an Auto lane. pdcSync's `set(n, v)`: v is 0 while idle, else clamped to 0..0.99 (here: to the ring's cap
// too); nothing is done when it is the target already set; with glide (live) a linear ramp from the value now, else the value at once.
struct DelayParam {
  AutoN<4> lane{0};
  double pv = std::nan("");   // the last target (`_pv`)
  uint32_t clamped = 0;       // requests above the cap
  // returns true when a new target was taken
  bool set(double v, double now, bool glide, double ramp, bool idle, double capSec);
  bool constant(double t0, double t1, double* v) { lane.prune(t0); return lane.constBlk(t0, t1, v); }
};

constexpr double kPdcMaxSec = 0.99;   // pdcSync: Math.min(0.99, v) (a DelayNode(1) holds a second)

}  // namespace thunder
#endif
