// U24: the layer oscillators of the FM family (fm2 fmfb fmmetal fmcross fm4 fm6 genesis sb arcade), the additive family (addsaw addodd membrane bell bar chord) and karplus / vosim / formant:
// index.html's makeOsc / newOsc / fmOsc / addOsc / KSString, line for line. One object per layer, living on the caller's stack (no heap). The JS is the specification, quirks included:
// every Number is a double in the JS's order of operations, the float32 values are the ones the JS writes into Float32Arrays (the Karplus-Strong delay line and excitation), Math.sin / cos /
// exp / pow / round are the js_* functions. `blk` is the per-16-sample block hook (osc.blk(t, e0, inc) of renderLayer); `s` is the sample.
// What the JS throws on (a chord shape or an FM ratio read past its table) is refused here with kEngUnsupported by u24_layer_ok (the JS has no value for it: never silence).
#ifndef THUNDER_ENGINE_LAYER_U24_H
#define THUNDER_ENGINE_LAYER_U24_H
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "engine/engine.h"

namespace thunder {

bool u24_is(LType t);                                  // a layer type of this unit
LType u24_layer_type(const char* name, size_t n);      // the JS name -> LType (Other if it is not one of this unit's)
bool u24_layer_ok(const EngLayer& L);                  // false: the JS throws or this port's fixed arrays cannot hold the layer (see layer_add.cpp)

// Math.max / Math.min on two doubles (NaN wins; +0 beats -0 for max, -0 beats +0 for min), and `x || d`
inline double u24_max(double a, double b) { if (a != a || b != b) return NAN; if (a == 0 && b == 0) return std::signbit(a) ? b : a; return a > b ? a : b; }
inline double u24_min(double a, double b) { if (a != a || b != b) return NAN; if (a == 0 && b == 0) return std::signbit(a) ? a : b; return a < b ? a : b; }

constexpr int kU24AddMax = 64;                         // partials of addOsc (addsaw: 1 + round(a * 31) = 32 at the most for a in [0, 1])

// the one-, two- and three-sine FM layers: fm2, fmfb, fmmetal, fmcross
struct U24Fm {
  int kind = 0;                                        // 0 fm2, 1 fmfb, 2 fmmetal, 3 fmcross
  double A = 0, B = 0, ratio = 1, I0 = 0, I = 0, r1 = 0, r2 = 0, r3 = 0, fb = 0;
  double p = 0, p1 = 0, p2 = 0, pa = 0, pb = 0;
  void init(int k, double a, double b);
  void blk(double env);
  double s(double inc);
};
// fmOsc: the operator models fm4, fm6, genesis, sb, arcade
struct U24FmOp {
  int n = 4; int alg = 0; int wf = 0; int q = 0;       // wf 0 sine, 1 opl (half-sine modulators), 2 fold
  const double* ratios = nullptr; double idx = 0, fbMax = 0, fb = 0, I = 0, base = 0;
  double ph[6] = {}, o[6] = {}, f1 = 0, f2 = 0;
  void init(LType t, double a, double b);
  void blk(double env);
  double s(double inc);
};
// addOsc: a bank of decaying sines at fixed ratios of the phase
struct U24Add {
  int n = 0, live = 0; double rs[kU24AddMax], am[kU24AddMax], dm[kU24AddMax], g[kU24AddMax]; double norm = 1, ph = 0;
  void init(LType t, double a, double b);
  void blk(double t, double env, double inc);
  double s(double inc);
};
struct U24Karplus {
  static constexpr int N = 4096;
  float b[N]; int w = 0; double lp = 0, dc = 0, dx = 0, g = 0, cP = 0, cT = 0; bool on = false, haveC = false;
  double A = 0, a = 0, t60 = 0; double sr = 0;
  void init(double sr, double a, double b);
  double s(double inc, EngRng& R);
};
struct U24Formant { double ratio = 1, wd = 0, ph = 0; void init(double a, double b); double s(double inc); };
struct U24Vosim { double ratio = 1, dec = 0, ph = 0, dc = 0; void init(double a, double b); double s(double inc); };

struct U24Osc {
  LType t = LType::Other;
  U24Fm fm; U24FmOp fo; U24Add add; U24Karplus ks; U24Formant fmt; U24Vosim vs;
  void init(const EngLayer& L, double sr, EngRng& R);
  void blk(double t, double env, double inc);
  double s(double inc, EngRng& R);
};

}  // namespace thunder
#endif
