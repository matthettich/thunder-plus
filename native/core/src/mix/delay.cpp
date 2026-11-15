// U10: see delay.h.
#include "mix/delay.h"

#include <algorithm>

namespace thunder {

void DelayRing::init(int capSamples, int maxBlock, int channels) {
  cap_ = capSamples; R_ = capSamples + maxBlock + 4; n_ = 0; nch_ = channels == 1 ? 1 : 2;
  b_.reset(new float[(size_t)R_ * (size_t)nch_]()); }

void DelayRing::push(const float* L, const float* R, int N) {
  float* bl = b_.get(); float* br = bl + R_; int64_t i0 = n_ % R_;
  if (nch_ == 1) for (int i = 0; i < N; i++) { bl[i0] = L[i]; if (++i0 == R_) i0 = 0; }
  else for (int i = 0; i < N; i++) { bl[i0] = L[i]; br[i0] = R[i]; if (++i0 == R_) i0 = 0; }
  n_ += N;
}

void DelayRing::read(double D, float* oL, float* oR, int N) const {
  if (!(D > 0)) D = 0;
  if (D > cap_) D = cap_;
  double fl = std::floor(D); double f = D - fl;
  if (f < 1e-9) f = 0; else if (f > 1 - 1e-9) { fl += 1; f = 0; }
  const int64_t k = (int64_t)fl, a0 = n_ - N;
  if (f == 0) { for (int i = 0; i < N; i++) { const int64_t a = a0 + i - k; oL[i] = a < 0 ? 0.f : at(0, a); if (nch_ == 2) oR[i] = a < 0 ? 0.f : at(1, a); } return; }
  for (int i = 0; i < N; i++) {   // between x[a - k - 1] (weight f) and x[a - k]
    const int64_t a = a0 + i - k, b = a - 1;
    const double xl = a < 0 ? 0.0 : at(0, a), yl = b < 0 ? 0.0 : at(0, b);
    oL[i] = (float)(xl * (1 - f) + yl * f);
    if (nch_ == 2) { const double xr = a < 0 ? 0.0 : at(1, a), yr = b < 0 ? 0.0 : at(1, b); oR[i] = (float)(xr * (1 - f) + yr * f); } }
}

void DelayRing::readVar(const double* D, float* oL, float* oR, int N) const {
  const int64_t a0 = n_ - N;
  for (int i = 0; i < N; i++) {
    double d = D[i]; if (!(d > 0)) d = 0;
    if (d > cap_) d = cap_;
    double fl = std::floor(d), f = d - fl; if (f < 1e-9) f = 0; else if (f > 1 - 1e-9) { fl += 1; f = 0; }
    const int64_t a = a0 + i - (int64_t)fl, b = a - 1;
    if (f == 0) { oL[i] = a < 0 ? 0.f : at(0, a); if (nch_ == 2) oR[i] = a < 0 ? 0.f : at(1, a); continue; }
    const double xl = a < 0 ? 0.0 : at(0, a), yl = b < 0 ? 0.0 : at(0, b);
    oL[i] = (float)(xl * (1 - f) + yl * f);
    if (nch_ == 2) { const double xr = a < 0 ? 0.0 : at(1, a), yr = b < 0 ? 0.0 : at(1, b); oR[i] = (float)(xr * (1 - f) + yr * f); }
  }
}

bool DelayParam::set(double v, double now, bool glide, double ramp, bool idle, double capSec) {
  if (idle) v = 0; else if (!(v > 0)) v = 0;
  const double lim = capSec < kPdcMaxSec ? capSec : kPdcMaxSec;   // the JS's 0.99 s, or the ring's size when that is shorter
  if (v > lim) { v = lim; clamped++; }
  if (pv == v) return false;
  pv = v;
  if (!glide) { lane.reset(v); return true; }   // cancelScheduledValues(0); value = v
  lane.prune(now); lane.hold(now); lane.lin(v, now + ramp);      // cancel(at); setValueAtTime(value, at); linearRampToValueAtTime(v, at + ramp)
  return true;
}

}  // namespace thunder
