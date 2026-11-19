// THE BLOCK RUNNER (N1, SEQ-LIVE-DESIGN A2): core/core-processor.js's process1 part 1 (:773-796) and the play / hold / stop state,
// statement for statement. Once per host block the host calls pump(now, lead, ...): the runner steps the sequencer up to `lead`
// seconds ahead of `now` (the audio clock at the block's start), and hands every step's events to the host's onStep.
// It owns no events: the host gives the buffer (evs / cap) and a SeqStep for the positions. Audio thread: no allocation, no lock
// (header-only; the callable is a template parameter, never a std::function).
//
// The same constants as the JS: a sequencer that is more than 0.03 s behind `now` is moved on to now + 0.02 (a held-up audio thread
// does not race through the rows it missed: `skips` counts these), at most 3 steps a block (a longer lead, screen off, fills over a few
// blocks, not in one burst), and EXACT STOP: with hold() armed the first step at a pattern edge (a new song row, or the same row again:
// the highest step went down) is NOT played; the runner reports it as held and drops the sequencer.
// `skips` is never reset by the JS (this.skips is set only in process1 and sent with every rows message), so start() keeps it:
// it counts for the runner's whole life, as the processor's.
#ifndef THUNDER_SEQ_RUNNER_H
#define THUNDER_SEQ_RUNNER_H
#include <cstdint>

#include "seq/seq.h"

namespace thunder {

constexpr double kRunSkipBehind = 0.03;   // core-processor :778
constexpr double kRunSkipTo = 0.02;
constexpr int kRunMaxSteps = 3;           // :780

struct SeqPump {
  int steps = 0;          // steps played (handed to onStep) in this block
  bool skipped = false;   // the catch-up skip fired
  bool held = false;      // EXACT STOP: a step at a pattern edge was not played; the sequencer is dropped (heldT, heldPos())
  bool ended = false;     // the sequencer ran out (reported once, never together with `held`)
  double heldT = 0;       // the held step's time (JS: held.t)
};

class SeqRunner {
 public:
  // play(): a new sequencer. hold = false, lr / ls unset, ended = false; skips kept.
  void start(Seq* s) { seq_ = s; hold_ = false; hasLr_ = false; lr_ = 0; ls_ = 0; ended_ = false; }
  void hold() { hold_ = true; }              // 'hold': EXACT STOP armed (also with no sequencer: the JS sets the flag anyway)
  // rebind: the host adopted a successor Seq (SeqSession / takeOver): re-point at it. The pointer only: hold, lr / ls, ended and skips are kept.
  // A runner that has no sequencer (stopped, held, never started) stays without one: an adoption must not bring a stopped sequencer back.
  void rebind(Seq* s) { if (seq_) seq_ = s; }
  void stop() { seq_ = nullptr; }            // 'stop': this.seq = null
  Seq* seq() const { return seq_; }
  bool holdArmed() const { return hold_; }
  bool ended() const { return ended_; }
  uint64_t skips() const { return skips_; }
  const SeqPos* heldPos() const { return heldPos_; }   // the held step's positions (valid after a pump that reported held)

  // onStep(const SeqEvent* evs, int n, const SeqStep& step): one call per played step, in order.
  template <class F>
  SeqPump pump(double now, double lead, SeqEvent* evs, int cap, SeqStep* stepOut, F&& onStep) {
    SeqPump r; Seq* s = seq_; if (!s) return r;
    SeqStep local; SeqStep* st = stepOut ? stepOut : &local;
    if (s->alive() && s->hasBase() && s->time() < now - kRunSkipBehind) { skips_++; r.skipped = true; s->shift(now + kRunSkipTo - s->time()); }
    int n = 0;
    while (s->alive() && s->time() < now + lead && n++ < kRunMaxSteps) {
      const int k = s->step(evs, cap, st);
      int row = -1, sp = -1;
      for (int t = 0; t < kSeqTracks; t++) { const SeqPos& p = st->pos[t]; if (!p.valid) continue; if (p.row > row) row = p.row; if (p.step > sp) sp = p.step; }
      if (row >= 0) {
        const bool edge = hasLr_ && (row != lr_ || sp < ls_); hasLr_ = true; lr_ = row; ls_ = sp;
        if (edge && hold_) {
          r.held = true; r.heldT = st->t; hold_ = false; seq_ = nullptr;
          for (int t = 0; t < kSeqTracks; t++) heldPos_[t] = st->pos[t];
          break;
        }
      }
      onStep(static_cast<const SeqEvent*>(evs), k, static_cast<const SeqStep&>(*st));
      r.steps++;
    }
    if (!r.held && !s->alive() && !ended_) { ended_ = true; r.ended = true; }
    return r;
  }

 private:
  Seq* seq_ = nullptr;
  bool hold_ = false, hasLr_ = false, ended_ = false;
  int lr_ = 0, ls_ = 0;
  uint64_t skips_ = 0;
  SeqPos heldPos_[kSeqTracks];
};

}  // namespace thunder
#endif
