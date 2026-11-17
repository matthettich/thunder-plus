// FxBank (U14): the track-effect hosts of a song (one per track and one for the master), the commands that reach them (the sequencer's FXO / FLX /
// GAT / RTG `fxc`, FXV `fxp`, `fxreset`) and the hooks the mixer calls (Mixer::setFx / setMasterFx). Commands as the page player books them:
// at max(t, now) + the delay the node's beat clock has (Mixer::clockDelaySec, n._cd._pv); a command for a node that has no slots yet waits in
// `fxcPend` and is booked when the node is made, at max(t, now) WITHOUT that shift; FXV (the core's tcAt) is not shifted either. Those are
// the three "latency AUTO leftovers" of U10 as far as tfx can reach them (a clocked effect after a latency effect needs U15 / U18).
#ifndef THUNDER_FX_FXBANK_H
#define THUNDER_FX_FXBANK_H
#include <memory>

#include "fx/fxhost.h"
#include "mix/mix.h"
#include "mix/pdc.h"
#include "seq/seq.h"
#include "song/song.h"

namespace thunder {

class FxBank {
 public:
  explicit FxBank(double sr);
  FxHost& host(int t) { return *h_[t]; }              // 0..kSongNT-1 the tracks, kSongNT the master
  const FxHost& host(int t) const { return *h_[t]; }
  // Control thread: the song's S.tfx / S.mfx (normTfx + tfxChain, wasm effects only), committed now (offline: before the first block). bpm: the song's;
  // restartN: tgate's restart counter (the page's fxRestartN).
  void load(const Song& S, double bpm, double restartN = 0);
  // The mixer's hooks for every track that has slots (and the master's through `masterHook`, which the caller may wrap: the master chain follows).
  void attach(Mixer& m, bool master = true);
  static void hook(void* user, int track, float* L, float* R, int frames, double t0, const MixFeed* feeds, int nFeeds);
  // The sequencer's events. now: the block's start time. true: taken.
  bool event(const SeqEvent& e, double now, const Mixer& m);
  void reset(int track, double t, double now);      // fxreset
  // LATENCY COMP: one record per track slot (the pump slots' measured latency: the page counts only pump effects) for pdc_plan; returns the count (<= max)
  int pdcSlots(PdcSlot* out, int max) const;
  void setBpm(double bpm) { bpm_ = bpm; }
  uint32_t pendDropped() const { return pendDropped_; }
  int pending() const { return nPend_; }

 private:
  struct Pend { int track, p; double v, t; };
  struct Ov { double v[12]; unsigned mask; };
  void flush(int track, double now);
  double sr_, bpm_ = 120, restartN_ = 0;
  const Mixer* mix_ = nullptr;   // attach(): the hooks read the beat clock each effects node hears (Mixer::beat) from it
  std::unique_ptr<FxHost> h_[kSongNT + 1];
  static constexpr int kPend = 256;
  Pend pend_[kPend]; int nPend_ = 0; uint32_t pendDropped_ = 0;
  Ov ov_[kSongNT + 1][kFxSlots];
};

}  // namespace thunder
#endif
