// U26: BassCore of index.html (the Lazerbass-style additive mono bass), the cores the live bass node and the pad render share. A plain object with explicit state: no allocation after construction, no locks, no I/O.
// THE JS IS THE SPECIFICATION: every expression keeps its order, the state arrays that are Float32Array in the JS (the sine table, base, panL / panR, aA / aT and the L / R buffers) are float here and every store into one
// rounds like the JS's, the phases are doubles, every Math.* is a js_* function. render(L, R, len, a, b, rate) is the JS's render(L, R, a, b, rate) in control blocks of BLK = 32 samples anchored at a; `len` is the length of
// L and R (a typed array written out of range or at a fractional index is a no-op in the JS: here too). L and R must both be given and must not alias. THE LIVE INTERFACE (live_shell.h) IS PROVISIONAL.
#ifndef THUNDER_ENGINE_BASS_CORE_H
#define THUNDER_ENGINE_BASS_CORE_H
#include <cstddef>
#include <cstdint>

#include "engine/xeng.h"

namespace thunder {

class BassCore {
 public:
  static constexpr int kN = 64;          // partials
  static constexpr double kBlk = 32;     // control block, in samples
  explicit BassCore(double sr);
  void set(const BassP& o);              // set(o) with the whole parameter object (a partial object is the caller's merge)
  void on(bool leg);
  void off();
  void reset();                          // back to a fresh core's state (the voice at rest, bpm 120, wob 0, the phases as constructed, `bad` cleared), the parameters kept (p()); no allocation (LIVE ROUTE C1)
  void render(float* L, float* R, size_t len, double a, double b, double rate);
  // the JS would have thrown (Vowel is NaN: this.vow[NaN][0]): the render is refused. Sticky; the core writes nothing more.
  bool bad() const { return bad_; }
  const BassP& p() const { return p_; }
  BassP& pm() { return p_; }   // the engine targets swap values in for one control block (eng_targets.h)
  double bpm = 120, wob = 0;             // the tempo and the WOB command (beats per wobble cycle, 0 = the Rate division), set by the host as the JS's c.bpm / c.wob
 private:
  void block(float* L, float* R, size_t len, double a, double b, double rate);
  double rand_();
  double shape_(double ph) const;
  double sr_; BassP p_;
  float tab_[4097], base_[kN + 1], panL_[kN + 1], panR_[kN + 1], aA_[kN + 1], aT_[kN + 1];
  double phA_[kN + 1], phB_[kN + 1];
  double stage_ = 0, env_ = 0, fe_ = 0, lph_ = 0, lu_ = 0, lsm_ = 0, sh_ = 0.5, rnd_ = 22222, f_ = 0, lf_ = 0, subPh_ = 0, norm_ = 0.3, wt_ = 0;
  bool held_ = false, go_ = false, bad_ = false;
};

}  // namespace thunder
#endif
