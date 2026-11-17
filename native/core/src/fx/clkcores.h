// The three tempo-locked track effects (U15): FlexCore, StepGateCore, RetrigCore and the clock slope `clkSpb` they share, line for line from
// index.html (the same code is generated into chains/chains-worklet.js by tools/make-fx-cores.mjs). THE JS IS THE SPECIFICATION, quirks included.
//
// How the numbers are kept as the JS has them: every JS Number is a double here, in the JS's own arithmetic order (the core is built with
// floating-point contraction off); the only float32 values are the ones the JS writes into a Float32Array (the block L / R, the rings bL / bR,
// the clock). Math.exp / pow are js_exp / js_pow, Math.cos / sin / tan / log are js_cos / js_sin / js_tan / js_log (fdlibm as V8 has it),
// Math.round is js_round, Math.min / max keep NaN. No allocation after construction: the rings and the pattern table are made in the
// constructors (the plan, on the control thread).
//
// The worklet hands a core its `tr`: the beat clock block `bp` (a Float32Array of the block's length, or of length 1 when the AudioParam is
// constant over the block), the tracker commands flx / gat / rtg (-1: the slot's own setting) and `sw`, the slot's FXO switch.
#ifndef THUNDER_FX_CLKCORES_H
#define THUNDER_FX_CLKCORES_H
#include <cstdint>
#include <memory>

namespace thunder {

struct ClkTr {
  const float* bp = nullptr;   // tr.bp (nullptr: undefined)
  int bpn = 0;                 // bp.length
  double flx = -1, gat = -1, rtg = -1, sw = -1;
};

// clkSpb's state: the fields it keeps on the object that calls it (o.spb, o.cn, o.cu, o.cl, o.sm, o.ps, o.ca).
struct ClkSpb {
  double spb, cn = 0, cu = 0, cl = 0, ps = 0, ca = 0;
  bool sm = false;
  explicit ClkSpb(double sr) : spb(sr / 2) {}
};
double clk_spb(ClkSpb& o, const float* bp, int bpn, int n);
// FlexCore's 5-tuplet slot table read: `o.q === 5 ? FlexCore.K.tup[j] : j`'s `K.tup[j]` for q == 5. K.tup is [0, 0, 1, 2, 3]; an index outside it (j = -1 when the position is a hair below a
// beat boundary, or NaN) reads `undefined` in the JS and the sum that follows is NaN for that sample: the same here, never a read outside the table (hardening, 17 Nov; HARDENING-NOTES.md).
double flex_tup_slot(double j);

// ---- Flex ------------------------------------------------------------------------------------------------------------------------------
struct FlexDp { double pat = 0, mix = 0, xf = 0, wait = 0; };   // the slot's mapped parameters p[0..3]

// One entry of FlexCore.SPEC() (generated: fx/flexspec.inc by tools/flex-spec.mjs). See that tool for the fields.
struct FlexPat {
  int f = 0;                         // FlexKind (PK_none: the JS array holds null)
  int mw = 0, dw = 0, op = 0, only = 0, set = 0;   // words: m (rev, up, rroll), d (warp), op, only, set
  double mn = 0, lps = 0;            // m as a number (mask, mut); lp as a number (crush)
  double s = 0, L = 0, C = 0, a = 0, b = 0, T = 0, d = 0, T2 = 0, d2 = 0, k = 0, gt = 0, g = 0, bw = 0, lpb = 0, h = 0, alt = 0, G = 0, c = 0, rv = 0, seed = 0,
         q = 0, nb = 0, N = 0, rot = 0, x = 0, rho = 0, pr = 0, sw = 0, w = 0, dk = 0, L0 = 0, cyc = 0, fd = 0, M = 0, pp = 0, W = 0, gap = 0, slow = 0;
  bool has_lp = false, has_hp = false, has_lpw = false, has_hpw = false, has_hs = false, has_beats = false;
  double lp[2] = {0, 0}, hp[2] = {0, 0}, lpw[3] = {0, 0, 0}, hpw[3] = {0, 0, 0}, hs[2] = {0, 0}, beats[2] = {0, 0};
  int nsg = 0; double sg[5][2] = {{0, 0}};
  int nS = 0; double S[10] = {0};
};

class FlexCore {
 public:
  explicit FlexCore(double sr);
  FlexCore(const FlexCore&) = delete; FlexCore& operator=(const FlexCore&) = delete;
  // feed(): the slot is off: the ring and the clock lock keep going, the sound is not touched
  void feed(float* L, float* R, int n, const ClkTr& tr) { run(L, R, n, nullptr, tr); }
  void process(float* L, float* R, int n, const FlexDp& dp, const ClkTr& tr) { run(L, R, n, &dp, tr); }
 private:
  void run(float* L, float* R, int n, const FlexDp* dp, const ClkTr& tr);
  double ext(int k, double p, double bar);
  double pat(int k, double p, double bar);
  double rd(const float* b, int w, double D) const;
  void svc(double* o, double f, double nyq) const;
  static double svf(double* fs, double x, int j, const double* c, bool hi);

  double sr_;
  int N_;
  std::unique_ptr<float[]> bL_, bR_;
  int w_ = 0;
  double D_ = 0, xo_ = 0, xc_ = 0, xn_ = 1, g_ = 1, pg_ = 1;
  ClkSpb c_;
  double sp_, ph_ = -1;
  double slp_ = 1, shp_ = 0, fs_[8] = {0, 0, 0, 0, 0, 0, 0, 0}, fa_[3] = {0, 0, 0}, fb_[3] = {0, 0, 0};   // fa / fb: `1` until svc makes their arrays (never read before)
  int64_t fk_ = 0;
  double hc_ = 0, hl_ = 0, hr_ = 0;
  double plp_ = 1, php_ = 0, pcr_ = 1, pbt_ = 0;   // ext() / pat()'s side results
  const FlexPat* T_;                                // the pattern table (shared, built once)
};

// ---- Step gate -------------------------------------------------------------------------------------------------------------------------
struct SgDp { double pat = 0, m1 = 0, m2 = 0, div = 0, len = 0, depth = 0, smooth = 0, wait = 0; };

class StepGateCore {
 public:
  explicit StepGateCore(double sr) : sr_(sr) {}
  void process(float* L, float* R, int n, const SgDp& dp, const ClkTr& tr);
 private:
  double sr_, g_ = 1;
};

// ---- Retrigger -------------------------------------------------------------------------------------------------------------------------
struct RtDp { double mode = 0, div = 0, decay = 0, pitch = 0, gate = 0, mix = 0; };

class RetrigCore {
 public:
  explicit RetrigCore(double sr);
  RetrigCore(const RetrigCore&) = delete; RetrigCore& operator=(const RetrigCore&) = delete;
  void process(float* L, float* R, int n, const RtDp& dp, const ClkTr& tr);
  void setSpb(double v) { c_.spb = v; }   // the module (chains' Retrigger) sets the core's `spb` from the tempo each block, as the JS does (`st.c.spb = ...`)
 private:
  double sr_;
  int N_;
  std::unique_ptr<float[]> bL_, bR_;
  int w_ = 0;
  bool on_ = false;
  double div_ = -1, st_ = 0, el_ = 0, Ls_ = 1, g_ = 0;
  ClkSpb c_;
};

}  // namespace thunder
#endif
