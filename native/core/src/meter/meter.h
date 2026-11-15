// METERS AND EVENTS OUT (N1, unit U13): peak and RMS per track and for the master, computed in the render path into fixed storage and published as ONE
// batched event at a fixed rate (the core host's `meters` event, docs/port/CORE-BRIDGE.md section 6 point 8: the proposed layout is the table below).
//
// WHAT THE PAGE DRAWS TODAY (index.html, ~33690): every strip has an AnalyserNode on its pan output `p` (after VOL / TVL / pan, before the delay: Mixer::postL / R)
// with fftSize 512; the page reads getFloatTimeDomainData and takes the PEAK of the buffer (max |x|); an analyser sees the MONO downmix (L + R) * 0.5 of a stereo
// input; the master has two analysers (one per channel, a ChannelSplitter, fftSize 1024) on the final output, read for the peak and, once per 250 ms, for the
// average: pbLvRead's mean of x * x over both channels' windows. The decay (0.86 per 8.3 ms), the 1.5 s peak hold, the floors (0.002 / 0.0002) and the 0.6 s
// TRIM-light hold are DRAWING and stay with the UI. A buffer starts as zeros (a window not yet filled counts the zeros). The FX bar's `lvl` (8 slot peaks, every
// 12 blocks, max(|L|, |R|) capped at 8) belongs to the effects host (U14); the master processor's own meter message is MasterProcessor::popMeter (U11).
//
// HERE: the same windows, exactly: a ring of the last 512 mono samples per track (float32 (L + R) * 0.5), of the last 1024 samples per master channel. At each
// publish the ring is read: peak = max |x|, rms = sqrt(mean(x * x)) over the whole ring. Numbers are linear (dB = 20 log10 on the UI side).
//
// THE BATCHED EVENT  {t:"meters", seq, at, a:[...]}  with `a` a Float32 array of kMeterFloats (= 2 * (kMixTracks + 2)):
//   a[2 * s]     peak of slot s,   a[2 * s + 1]   rms of slot s,   s = 0 .. kMixTracks - 1 (the tracks, then the preview bus kSongNT)
//   s = kMixTracks     the master LEFT (peak, rms),   s = kMixTracks + 1   the master RIGHT
//   seq: counts from 1; at: the context time (s) of the end of the window.
//
// REAL-TIME RULES: the constructor allocates; feed*(), advance() and pop() allocate nothing and take no lock. One thread (the audio thread) calls feed / advance;
// the host pops from the queue it fills there (the same thread, between blocks: as MasterProcessor).
#ifndef THUNDER_METER_METER_H
#define THUNDER_METER_METER_H
#include <cstdint>
#include <memory>

#include "mix/mix.h"

namespace thunder {

constexpr int kMeterSlots = kMixTracks + 2;      // tracks 0..NT-1, the preview bus, master L, master R
constexpr int kMeterFloats = 2 * kMeterSlots;
constexpr int kMeterTrackWindow = 512;           // the strips' analysers' fftSize
constexpr int kMeterMasterWindow = 1024;         // the master analysers' fftSize

struct MeterEvent { uint32_t seq = 0; double at = 0; float a[kMeterFloats] = {}; };

class MeterBank {
 public:
  static constexpr int kQueue = 4;
  explicit MeterBank(double sampleRate, double rateHz = 30.0);   // allocates (control thread)
  ~MeterBank();
  MeterBank(const MeterBank&) = delete;
  MeterBank& operator=(const MeterBank&) = delete;
  void setRate(double rateHz);                                   // events a second (the page reads at ~30 Hz: mScanT >= 33 ms)
  void feedTrack(int t, const float* L, const float* R, int n);  // slot t: the mono downmix into its ring
  void feedMaster(const float* L, const float* R, int n);        // the final output: each channel into its ring
  // after the block's feeds: `frames` frames have passed, the block ended at `atSec`. Publishes one event when a period has gone by.
  void advance(int frames, double atSec);
  bool pop(MeterEvent* out);                                     // oldest first; false: none
  uint32_t dropped() const { return drop_; }                     // events lost because the host did not pop (the oldest goes)
  uint32_t published() const { return seq_; }
 private:
  void publish(double at);
  struct Ring { std::unique_ptr<float[]> d; int n = 0, w = 0; };
  double sr_, period_, acc_ = 0;
  std::unique_ptr<Ring[]> r_;   // kMixTracks track rings, then master L, master R
  MeterEvent q_[kQueue]; int qh_ = 0, qn_ = 0; uint32_t seq_ = 0, drop_ = 0;
};

// The render path's feed: after Mixer::render(): every track's post-pan pair and the master's output block.
void meters_feed(MeterBank& bank, const Mixer& mix, const float* outL, const float* outR, int frames);

}  // namespace thunder
#endif
