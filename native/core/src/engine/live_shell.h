// U26: THE LIVE SHELL (provisional interface: the lead confirms the choices listed in docs/port/U26-NOTES.md "Choices for the lead to confirm").
// One voice of a live engine (bass, vox, acid, mach) as a plain object: note / rate / bpm / wob EVENTS at a sample offset inside the host block, processed in host blocks of any size (up to 128 per chunk) with no allocation,
// no locks, no I/O after construction. It is NOT wired into the mixer or the sequencer.
//
// THE RULE (one rule for live play and renders; docs/port/U26-NOTES.md part (a) describes the page's own live nodes and where they differ):
//   * a NOTE event (on / off) takes effect at its exact sample: the control block that is running ends there and the core's next control block starts there;
//   * the core's control blocks (32 samples for bass, 16 for the others: its envelopes, filters and partial amplitudes move linearly across one) run from the last note event, not from the host block;
//   * a RATE / BPM / WOB event takes effect at the first control block that starts at or after its sample (a rate ramp is a Web Audio linear ramp: the value at that block's first sample);
//   * the output for a note sequence is therefore the same whatever the host block sizes are: it equals seqRender in tools/engine-u26-dump.mjs, the JS cores driven by this rule, bit for bit.
// A control block's samples depend on the block's END, so a block can only be rendered once no later event can cut it. LiveMode::Exact therefore delays the output by latency() = BLK - 1 samples (31 for bass, 15 for the
// others): the first latency() samples are silence. LiveMode::Immediate has no delay and also ends a control block at every host block's end: its output depends on the host block sizes (it equals seqRender with those cuts).
#ifndef THUNDER_ENGINE_LIVE_SHELL_H
#define THUNDER_ENGINE_LIVE_SHELL_H
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "engine/bass_core.h"
#include "engine/vox_core.h"
#include "engine/xeng_live.h"
#include "rt/jsmath.h"

namespace thunder {

enum class LiveEvType : uint8_t { NoteOn, NoteOff, RateSet, RateRamp, Bpm, Wob };
struct LiveEvent {
  uint32_t at = 0;                 // sample offset inside the process() call (0 .. n-1)
  LiveEvType type = LiveEvType::NoteOn;
  bool leg = false, acc = false, same = false;   // NoteOn: the leg lane (>= 0.5), the acc lane (acid / mach), the same lane (vox)
  double v = 0;                    // NoteOn: the syl lane's value (vox; the shell rounds it like the page's Math.round); RateSet / RateRamp: the rate; Bpm; Wob
  double dur = 0;                  // RateRamp: samples to reach v, starting from the rate at `at`
};

// ---- the four cores as the shell uses them -----------------------------------------------------------------------------------------------------------------------------------------------------------
struct BassLive { using Core = BassCore; static constexpr uint32_t kBlk = 32;
  static bool on(Core& c, const LiveEvent& e) { c.on(e.leg); return true; }
  static void off(Core& c) { c.off(); } static void bpm(Core& c, double v) { c.bpm = v; } static void wob(Core& c, double v) { c.wob = v; } static bool bad(const Core& c) { return c.bad(); } };
struct VoxLive { using Core = VoxCore; static constexpr uint32_t kBlk = 16;
  static bool on(Core& c, const LiveEvent& e) { return c.on(e.leg, js_round(e.v), e.same); }
  static void off(Core& c) { c.off(); } static void bpm(Core&, double) {} static void wob(Core&, double) {} static bool bad(const Core&) { return false; } };
struct AcidLive { using Core = AcidCore; static constexpr uint32_t kBlk = 16;
  static bool on(Core& c, const LiveEvent& e) { c.on(e.leg, e.acc); return true; }
  static void off(Core& c) { c.off(); } static void bpm(Core&, double) {} static void wob(Core&, double) {} static bool bad(const Core&) { return false; } };
struct MachLive { using Core = MachCore; static constexpr uint32_t kBlk = 16;
  static bool on(Core& c, const LiveEvent& e) { c.on(e.leg); return true; }
  static void off(Core& c) { c.off(); } static void bpm(Core&, double) {} static void wob(Core&, double) {} static bool bad(const Core& c) { return c.bad(); } };

enum class LiveMode : uint8_t { Exact, Immediate };

// The rate lane: a Web Audio AudioParam as far as the page uses it: a step (setValueAtTime) or a linear ramp (linearRampToValueAtTime) from the value the lane has when the ramp's event is reached.
struct LiveRateLane {
  double v0 = 1, t0 = 0, v1 = 1, t1 = 0;   // the current piece: from (t0, v0) to (t1, v1), constant after t1
  double at(double t) const { return t >= t1 ? v1 : v0 + (v1 - v0) * ((t - t0) / (t1 - t0)); }
  void set(double t, double v) { v0 = v1 = v; t0 = t1 = t; }
  void ramp(double t, double v, double dur) { if (!(dur > 0)) { set(t, v); return; } const double cur = at(t); v0 = cur; t0 = t; v1 = v; t1 = t + dur; }
};

// The page's gate / trig / leg lanes as note events (BASS_PROC.process, one sample at a time; the same in VOX_PROC and XPROC): feed it the three lanes' values at each sample.
struct PageLaneDecoder {
  double lt = 0; bool lg = false;
  enum Out { kNone, kOn, kOff };
  Out step(double trig, double gate, double leg, bool* legato) {
    const bool ga = gate >= 0.5; Out r = kNone; *legato = false;
    if (trig != lt || ga != lg) {
      if (trig != lt && ga) { r = kOn; *legato = leg >= 0.5; }
      else if (ga && !lg) r = kOn;
      else if (!ga && lg) r = kOff;
      lt = trig; lg = ga;
    }
    return r;
  }
};

template <class A>
class LiveShell {
 public:
  static constexpr uint32_t kBlk = A::kBlk, kMaxBlock = 128, kPend = 256, kRing = 512;
  explicit LiveShell(double sr, LiveMode mode = LiveMode::Exact) : core_(sr), mode_(mode) {
    std::memset(ringL_, 0, sizeof ringL_); std::memset(ringR_, 0, sizeof ringR_); std::memset(stgL_, 0, sizeof stgL_); std::memset(stgR_, 0, sizeof stgR_);
  }
  typename A::Core& core() { return core_; }             // set(): between process() calls (it takes effect at the core's position, up to latency() samples behind the host clock)
  uint32_t latency() const { return mode_ == LiveMode::Exact ? kBlk - 1 : 0; }
  bool failed() const { return failed_; }                // the JS core would have thrown (a NaN vowel, a syllable number that is not a whole number, ...): the voice is silent from then on
  uint32_t dropped() const { return dropped_; }          // events that did not fit the queue (kPend) or lay outside the call
  uint32_t rejected() const { return rejected_; }        // events with a non-finite value (an AudioParam cannot hold one)

  // Renders n samples into L and R (separate arrays) and applies the n-sample call's events (any order; the same offset keeps its order). n of any size: it runs in chunks of up to kMaxBlock.
  void process(float* L, float* R, uint32_t n, const LiveEvent* ev, uint32_t nev) {
    for (uint32_t i = 0; i < nev; i++) if (ev[i].at >= n) dropped_++;
    for (uint32_t base = 0; base < n; base += kMaxBlock) {
      const uint32_t m = n - base < kMaxBlock ? n - base : kMaxBlock;
      for (uint32_t i = 0; i < nev; i++) if (ev[i].at >= base && ev[i].at < base + m) enqueue(ev[i], clock_ + (ev[i].at - base));
      advance(clock_ + m);
      for (uint32_t j = 0; j < m; j++) {
        const int64_t t = (int64_t)(clock_ + j) - (int64_t)latency();
        if (t < 0 || failed_) { L[base + j] = 0; R[base + j] = 0; } else { L[base + j] = ringL_[(uint64_t)t & (kRing - 1)]; R[base + j] = ringR_[(uint64_t)t & (kRing - 1)]; }
      }
      clock_ += m;
    }
  }

 private:
  struct Pend { uint64_t t; LiveEvent e; };
  static bool isNote(LiveEvType t) { return t == LiveEvType::NoteOn || t == LiveEvType::NoteOff; }
  void enqueue(LiveEvent e, uint64_t t) {
    // what the page's AudioParams do to a value: only finite numbers, clamped to the parameter's range (rate 0..256, bpm 10..999, wob 0..64)
    switch (e.type) {
      case LiveEvType::RateSet: case LiveEvType::RateRamp: if (!std::isfinite(e.v)) { rejected_++; return; } e.v = e.v < 0 ? 0 : e.v > 256 ? 256 : e.v; break;
      case LiveEvType::Bpm: if (!std::isfinite(e.v)) { rejected_++; return; } e.v = e.v < 10 ? 10 : e.v > 999 ? 999 : e.v; break;
      case LiveEvType::Wob: if (!std::isfinite(e.v)) { rejected_++; return; } e.v = e.v < 0 ? 0 : e.v > 64 ? 64 : e.v; break;
      case LiveEvType::NoteOn: if (!std::isfinite(e.v)) e.v = 0; break;
      default: break;
    }
    if (cnt_ >= kPend) { dropped_++; return; }
    uint32_t pos = cnt_;   // a stable insertion in time order
    while (pos > 0 && pend_[(head_ + pos - 1) % kPend].t > t) { pend_[(head_ + pos) % kPend] = pend_[(head_ + pos - 1) % kPend]; pos--; }
    pend_[(head_ + pos) % kPend] = Pend{t, e}; cnt_++;
  }
  void apply(const Pend& p) {
    const LiveEvent& e = p.e; const double t = (double)p.t;
    switch (e.type) {
      case LiveEvType::NoteOn: if (!A::on(core_, e)) failed_ = true; break;
      case LiveEvType::NoteOff: A::off(core_); break;
      case LiveEvType::RateSet: lane_.set(t, e.v); break;
      case LiveEvType::RateRamp: lane_.ramp(t, e.v, e.dur); break;
      case LiveEvType::Bpm: A::bpm(core_, e.v); break;
      case LiveEvType::Wob: A::wob(core_, e.v); break;
    }
  }
  // renders control blocks up to the horizon H (every event before H is known)
  void advance(uint64_t H) {
    for (;;) {
      while (cnt_ && pend_[head_].t <= rendered_) { apply(pend_[head_]); head_ = (head_ + 1) % kPend; cnt_--; }
      uint64_t end = rendered_ + kBlk;
      for (uint32_t k = 0; k < cnt_; k++) { const Pend& p = pend_[(head_ + k) % kPend]; if (isNote(p.e.type)) { if (p.t < end) end = p.t; break; } }   // the next note event cuts the block
      if (mode_ == LiveMode::Immediate) { if (H < end) end = H; }
      else if (end > H) break;   // a later event may still cut this block: wait
      if (end <= rendered_) break;
      block(rendered_, end);
      rendered_ = end;
    }
  }
  void block(uint64_t s, uint64_t e) {
    const uint32_t n = (uint32_t)(e - s);
    if (!failed_) {
      core_.render(stgL_, stgR_, kBlk, 0, (double)n, lane_.at((double)s));   // one control block (n <= kBlk), written at 0..n of the staging arrays
      if (A::bad(core_)) failed_ = true;
    }
    for (uint32_t i = 0; i < n; i++) { ringL_[(s + i) & (kRing - 1)] = failed_ ? 0.0f : stgL_[i]; ringR_[(s + i) & (kRing - 1)] = failed_ ? 0.0f : stgR_[i]; }
  }
  typename A::Core core_; LiveMode mode_; LiveRateLane lane_;
  uint64_t clock_ = 0, rendered_ = 0; bool failed_ = false; uint32_t dropped_ = 0, rejected_ = 0;
  Pend pend_[kPend]; uint32_t head_ = 0, cnt_ = 0;
  float ringL_[kRing], ringR_[kRing], stgL_[kBlk], stgR_[kBlk];
};

using LiveBass = LiveShell<BassLive>;
using LiveVox = LiveShell<VoxLive>;
using LiveAcid = LiveShell<AcidLive>;
using LiveMach = LiveShell<MachLive>;

}  // namespace thunder
#endif
