// Auto: one automation timeline (U05), the C++ of `class Auto` in core/core-processor.js (also in core/seq-core.js).
// It gives Web Audio's AudioParam scheduling on the core's own timeline: set at a time, linear ramp to a point, exponential
// approach to a target (setTargetAtTime), a curve over a span, cancel from a time, and is read per sample or per block.
// THE JS IS THE SPECIFICATION, quirks included; every method here is that method line for line, in the same arithmetic order,
// with js_exp for Math.exp, so a timeline gives the same bits in the web copy and in every native app
// (native/tests/test_runtime.cpp checks it against hashes made by the JS: native/tools/runtime-golden.mjs).
// What differs from the JS, and why:
//   - No allocation: the events live in storage the owner supplies (AutoN<N> below); ins() on a full list drops the event,
//     counts it in `dropped`, and returns false. The JS list is unbounded; with prune() every block it holds the events
//     booked in the next LEAD (0.25 s). `dropped` must stay 0: the units that own timelines (U07, U08, U09) assert it on the
//     census songs and size N from what they measure.
//   - A curve is a function pointer with six numbers (no closure): Curve.
//   - constFrom / constBlk return false where the JS returns null.
// Times are seconds on the core's sample timeline as doubles (frame / sample rate), as the JS has them.
#ifndef THUNDER_RT_AUTO_H
#define THUNDER_RT_AUTO_H
#include <cstdint>

namespace thunder {

struct Curve {
  double (*fn)(const double* p, double x) = nullptr;
  double p[6] = {0, 0, 0, 0, 0, 0};
  double operator()(double x) const { return fn(p, x); }
};

struct AutoEv {
  double t = 0, v = 0, tc = 0, d = 0;   // time; value (k 0, 1, 2); time constant (k 2); duration (k 3)
  Curve c;                              // k 3
  int k = 0;                            // 0 set, 1 linear ramp to (t, v), 2 target, 3 curve
};

class Auto {
 public:
  double v0 = 0;          // the value before the first event
  uint32_t dropped = 0;   // events refused because the list was full (must stay 0)

  Auto(AutoEv* storage, int capacity, double v) : v0(v), ev_(storage), cap_(capacity - kSlack) {}
  void reset(double v) { v0 = v; n_ = 0; }

  bool set(double v, double t) { AutoEv e; e.k = 0; e.t = t; e.v = v; return ins(e); }
  bool lin(double v, double t) { AutoEv e; e.k = 1; e.t = t; e.v = v; return ins(e); }
  bool target(double v, double t, double tc) { AutoEv e; e.k = 2; e.t = t; e.v = v; e.tc = tc > 1e-5 ? tc : 1e-5; return ins(e); }
  bool curve(const Curve& c, double t, double d) { AutoEv e; e.k = 3; e.t = t; e.d = d; e.c = c; return ins(e); }
  void cancel(double t);                           // drops every event at or after t
  void hold(double t) { const double v = at(t); cancel(t); set(v, t); }
  bool constFrom(double t, double* v) const;       // constant from t on: true and *v
  bool constBlk(double t, double t1, double* v) const;   // constant over [t, t1]: true and *v
  void prune(double t);                            // per block: folds what is over by t into v0 and at most three events
  double at(double t) const;                       // per sample
  void fill(double* C, double t0, double dt, int N, int i0 = 0) const;   // C[i] = at(t0 + i * dt)

  int size() const { return n_; }
  int capacity() const { return cap_; }   // events the list holds (without the prune slack)
  const AutoEv& event(int i) const { return ev_[i]; }

  static constexpr int kSlack = 2;   // prune() may grow the list by two events (a head of three replacing one)

 protected:
  bool ins(const AutoEv& e);
  AutoEv* ev_;
  int cap_, n_ = 0;
};

template <int N>
class AutoN : public Auto {
 public:
  explicit AutoN(double v = 0) : Auto(store_, N + kSlack, v) {}
  AutoN(const AutoN&) = delete;
  AutoN& operator=(const AutoN&) = delete;
 private:
  AutoEv store_[N + kSlack];
};

}  // namespace thunder
#endif
