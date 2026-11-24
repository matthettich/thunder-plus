// THE BEAT CLOCK (N1, unit U10): the tempo-locked effects' clock, the core processor's `this.clk` (core/core-processor.js, also the end of
// core/seq-core.js) and the page's clkBus. Flex, Step gate, Retrigger and every tempo-synced effect read the song position in beats from it.
//   - the sequencer's 'beat' events {t, b} (one a row) ramp an Auto lane from row to row: the lane is `beat - clkBase + 1` while a song plays
//     (clkBase: the beat's last multiple of 16 at the start, +16 each time the ramp passes 16, so the value stays small and exact: 1..17),
//     and 0 when it is stopped (the core's output NT). Linear between events, so it follows TPO exactly.
//   - clkBus = the lane + the page's ConstantSource at -1 = the beat while playing, -1 stopped: what an effects node's `bp` param is fed.
//   - each effects node hears clkBus through its own DelayNode (n._cd): 0 s, or pdcPlan's c (how late its input is) so the effect stays on the beat of
//     the sound it hears. A new delay starts from silence (0), not -1: a DelayNode's history is zeros.
// FX1 (page mode, setPage): the page player's OWN clock, which the page's offline render (pass(), the reference of the whole-song comparison) uses: play('beat')
// sets / ramps the ConstantSource's `offset` (beat - clkBase directly; -1 while stopped), so clkBus = that value, with no +1 / -1 float32 round trip (the core's
// lane quantises beat + 1 to 1.2e-7, the page player's clock does not). The clkSpb tempo estimate of Flex / synced effects reads differences of this clock over
// a block, so a one-ulp difference moves it by 0.1 sample per block. NOT ported: HOLD_CLK_FLAT (rsHold, live only).
// The JS is the specification: BeatClock::event / stop / render are the core's lines. NOT ported: the page player's own clock (play('beat') on a
// ConstantSource, with HOLD_CLK_FLAT and clkSpb's stalled-clock rule): the core's is the one the native engine has.
#ifndef THUNDER_MIX_BEAT_H
#define THUNDER_MIX_BEAT_H
#include "rt/auto.h"
#include "seq/seq.h"

namespace thunder {

class BeatClock {
 public:
  static constexpr int kAuto = 96;   // beat events booked in the next LEAD (a row each, at most 3 steps a block); measure with peak()
  BeatClock() = default;
  // 'beat': at = max(e.t, now). true: taken (any other type: false). e.v is the beat.
  bool event(const SeqEvent& e, double now);
  // PLAY, STOP, an exact stop at a pattern edge, the watchdog: stopped from `at`
  void stop(double at);
  // the output NT block: beat + 1 while playing, 0 stopped, as float32 (the worklet output); scratch: N doubles
  void render(float* C, int N, double t0, double dt, double* scratch);
  // page mode (see above): set before the first event. render() then gives the clkBus value itself (the caller adds no -1)
  void setPage(bool on, double sr) { page_ = on; sr_ = sr; A_.reset(on ? -1 : 0); pn_ = 0; }
  bool page() const { return page_; }
  bool on() const { return on_; }
  double base() const { return base_; }
  uint32_t dropped() const { return A_.dropped + pdrop_; }
  int peak() const { return peak_; }
 private:
  AutoN<kAuto> A_{0};
  bool on_ = false, page_ = false;
  // page mode: the ConstantSource offset's own timeline (set / linear ramp events), evaluated as Chrome's AudioParamTimeline does for a linear ramp:
  // per chunk of frames inside one ramp (a render block, cut at an event's frame) four lanes then `value += inc4` per four frames, float32 (see beat.cpp)
  struct PEv { double t, v; int k; };
  static constexpr int kPEv = 128;
  PEv pev_[kPEv]; int pn_ = 0; uint32_t pdrop_ = 0; double sr_ = 48000;
  void pins(int k, double v, double t);
  void pcancel(double t);
  void prender(float* C, int N, double t0);
  double base_ = 0;
  int peak_ = 0;
};

}  // namespace thunder
#endif
