// U13: peak / RMS meters in the render path. See meter.h.
#include "meter/meter.h"

#include <cmath>

namespace thunder {

MeterBank::MeterBank(double sampleRate, double rateHz) : sr_(sampleRate), r_(new Ring[kMeterSlots]) {
  for (int s = 0; s < kMeterSlots; s++) { Ring& r = r_[s]; r.n = s < kMixTracks ? kMeterTrackWindow : kMeterMasterWindow; r.d.reset(new float[(size_t)r.n]()); }
  setRate(rateHz);
}
MeterBank::~MeterBank() = default;
void MeterBank::setRate(double rateHz) { period_ = sr_ / (rateHz > 1 ? (rateHz < 1000 ? rateHz : 1000) : 1); }

static inline void put(float* d, int n, int& w, float v) { d[w] = v; if (++w == n) w = 0; }

void MeterBank::feedTrack(int t, const float* L, const float* R, int n) {
  if (t < 0 || t >= kMixTracks) return;
  Ring& r = r_[t]; float* d = r.d.get();
  for (int i = 0; i < n; i++) put(d, r.n, r.w, (L[i] + R[i]) * 0.5f);   // the analyser's downmix: float add, then half
}
void MeterBank::feedMaster(const float* L, const float* R, int n) {
  Ring& a = r_[kMixTracks]; Ring& b = r_[kMixTracks + 1];
  for (int i = 0; i < n; i++) { put(a.d.get(), a.n, a.w, L[i]); put(b.d.get(), b.n, b.w, R[i]); }
}

void MeterBank::advance(int frames, double atSec) {
  acc_ += frames;
  if (acc_ >= period_) { acc_ -= period_; if (acc_ >= period_) acc_ = 0; publish(atSec); }
}

void MeterBank::publish(double at) {
  if (qn_ == kQueue) { qh_ = (qh_ + 1) % kQueue; qn_--; drop_++; }
  MeterEvent& e = q_[(qh_ + qn_) % kQueue]; qn_++; e.seq = ++seq_; e.at = at;
  for (int s = 0; s < kMeterSlots; s++) {
    const Ring& r = r_[s]; const float* d = r.d.get(); double p = 0, ss = 0;
    for (int i = 0; i < r.n; i++) { const double x = d[i], a = x < 0 ? -x : x; if (a > p) p = a; ss += x * x; }
    e.a[2 * s] = (float)p; e.a[2 * s + 1] = (float)std::sqrt(ss / r.n);
  }
}

bool MeterBank::pop(MeterEvent* out) { if (qn_ == 0) return false; *out = q_[qh_]; qh_ = (qh_ + 1) % kQueue; qn_--; return true; }

void meters_feed(MeterBank& bank, const Mixer& mix, const float* outL, const float* outR, int frames) {
  for (int t = 0; t < kMixTracks; t++) bank.feedTrack(t, mix.postL(t), mix.postR(t), frames);
  bank.feedMaster(outL, outR, frames);
}

}  // namespace thunder
