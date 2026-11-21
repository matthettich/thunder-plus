// U27: AcidCore and MachCore of index.html (the live engines' cores: the tracker plays them in an AudioWorklet node per track, a pad renders one held note through renderLiveCore).
// Written so the live path can reuse them (U26 / later units decide): each core is a plain object with explicit state (no allocation, no locks, no I/O), `render(L, R, len, a, b, rate)` is the JS's
// render(L, R, a, b, rate) in blocks of BLK = 16 samples, on()/off() are the note on / off. R may be nullptr (the rendered path does not read it). THE INTERFACE IS PROVISIONAL.
// `len` is the length of L (and R) in samples: the JS writes a typed array out of range into the void, here the write is skipped.
#ifndef THUNDER_ENGINE_XENG_LIVE_H
#define THUNDER_ENGINE_XENG_LIVE_H
#include <cstddef>
#include <cstdint>

#include "engine/xeng.h"

namespace thunder {

class AcidCore {
 public:
  explicit AcidCore(double sr);
  void set(const AcidP& o);              // set(o): a field that is not finite is the default
  void on(bool leg, bool acc);
  void off();
  void reset();                          // back to a fresh core's state, the parameters kept; no allocation (LIVE ROUTE C1)
  void render(float* L, float* R, size_t len, double a, double b, double rate);
  bool idle() const { return stage_ == 0 && env_ == 0; }   // block() returns at once: the live shell may skip it (LIVE ROUTE 1b)
  const AcidP& p() const { return p_; }
  AcidP& pm() { return p_; }   // the engine targets swap values in for one control block (eng_targets.h)
 private:
  void block(float* L, float* R, size_t len, double a, double b, double rate);
  static double pblep(double t, double dt);
  static double ft(double x);
  double sr_, ph_ = 0, subPh_ = 0, lf_ = 0; bool go_ = true, gate_ = false, leg_ = false;
  double env_ = 0, fe_ = 0, accE_ = 0, accC_ = 0, accOn_ = 0, s0_ = 0, s1_ = 0, s2_ = 0, s3_ = 0, dc_ = 0, dcx_ = 0; int stage_ = 0; uint32_t rnd_ = 9137; AcidP p_;
};

class MachCore {
 public:
  explicit MachCore(double sr);
  void set(const MachP& o);
  void on(bool leg);
  void off();
  void reset();                          // back to a fresh core's state, the parameters kept; no allocation (LIVE ROUTE C1)
  void render(float* L, float* R, size_t len, double a, double b, double rate);
  bool idle() const { return stage_ == 0 && env_ == 0; }   // block() returns at once: the live shell may skip it (LIVE ROUTE 1b)
  bool bad() const { return bad_; }      // the JS would have thrown (a TypeError: the Ensemble chord index is NaN) or never finished (a Swarm voice count above 1e4): the render is refused
  const MachP& p() const { return p_; }
  MachP& pm() { return p_; }
 private:
  void block(float* L, float* R, size_t len, double a, double b, double rate);
  static double pblep(double t, double dt);
  double saw(int j, double inc);
  double gof(double fc) const;
  double svf(double x, double g, double r, double* st);
  double sr_, ph_[10], fbm_ = 0, lf_ = 0; bool go_ = true, gate_ = false, leg_ = false;
  double env_ = 0, fe_ = 0, lfo_ = 0, hold_ = 0, n1_ = 0, oh_ = 0, ob_ = 0, fh_[2] = {0, 0}, fl_[2] = {0, 0}, fn_[2] = {0, 0}; int stage_ = 0; uint32_t rnd_ = 4242; bool bad_ = false; MachP p_;
};

}  // namespace thunder
#endif
