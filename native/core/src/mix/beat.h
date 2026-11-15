// THE BEAT CLOCK (N1, unit U10): the tempo-locked effects' clock, the core processor's `this.clk` (core/core-processor.js, also the end of
// core/seq-core.js) and the page's clkBus. Flex, Step gate, Retrigger and every tempo-synced effect read the song position in beats from it.
//   - the sequencer's 'beat' events {t, b} (one a row) ramp an Auto lane from row to row: the lane is `beat - clkBase + 1` while a song plays
//     (clkBase: the beat's last multiple of 16 at the start, +16 each time the ramp passes 16, so the value stays small and exact: 1..17),
//     and 0 when it is stopped (the core's output NT). Linear between events, so it follows TPO exactly.
//   - clkBus = the lane + the page's ConstantSource at -1 = the beat while playing, -1 stopped: what an effects node's `bp` param is fed.
//   - each effects node hears clkBus through its own DelayNode (n._cd): 0 s, or pdcPlan's c (how late its input is) so the effect stays on the beat of
//     the sound it hears. A new delay starts from silence (0), not -1: a DelayNode's history is zeros.
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
  bool on() const { return on_; }
  double base() const { return base_; }
  uint32_t dropped() const { return A_.dropped; }
  int peak() const { return peak_; }
 private:
  AutoN<kAuto> A_{0};
  bool on_ = false;
  double base_ = 0;
  int peak_ = 0;
};

}  // namespace thunder
#endif
