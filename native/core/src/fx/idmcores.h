// The four JS-only track effects of U16: Beat grains (GrainsCore, id 100), Beat scramble (ScrambleCore, 101), Glitch sequencer (GlitchCore, 102) and the
// Vintage sampler (VSampCore, 103), line for line from index.html (the same code is generated into chains/chains-worklet.js by tools/make-fx-cores.mjs).
// THE JS IS THE SPECIFICATION, quirks included. The same rules as clkcores.h: every JS Number is a double in the JS's own arithmetic order (the core is built
// with floating-point contraction off), the only float32 values are the ones the JS writes into a Float32Array (the block L / R, the rings bL / bR), Math.exp /
// pow / cos / sin / log / tanh are js_exp / js_pow / js_cos / js_sin / js_log / js_tanh (fdlibm as V8 12.4 has them), Math.round is js_round, Math.min / max keep NaN.
// Randomness: Grains' xorshift32 `rnd()` (seed 1), the Vintage sampler's xorshift32 noise (seed 0x1234567) and `bkHash` are integer arithmetic, reproduced as written;
// nothing here uses a platform random number. No allocation after construction: the rings are made in the constructors (the plan, on the control thread; the JS
// makes Scramble's and Glitch's on their first block, the sound is the same), the grain list is a fixed array of 24.
// Grains, Scramble and Glitch are tempo-locked like Flex (the worklet's `ck` 3: they keep running while the clock is stopped); the Vintage sampler is not.
#ifndef THUNDER_FX_IDMCORES_H
#define THUNDER_FX_IDMCORES_H
#include <cstdint>
#include <memory>

#include "fx/clkcores.h"

namespace thunder {

// ---- Beat grains -----------------------------------------------------------------------------------------------------------------------
struct GrDp { double div = 0, density = 0, size = 0, pos = 0, spray = 0, pitch = 0, rpitch = 0, rev = 0, shape = 0, spread = 0, freeze = 0, mix = 0; };

class GrainsCore {
 public:
  explicit GrainsCore(double sr);
  GrainsCore(const GrainsCore&) = delete; GrainsCore& operator=(const GrainsCore&) = delete;
  void process(float* L, float* R, int n, const GrDp& dp, const ClkTr& tr);
 private:
  struct Voice { double x, rate, len, age, gl, gr, span; bool rev; };
  double rnd();
  void start(const GrDp& dp, double stepS);
  double sr_;
  int N_;
  std::unique_ptr<float[]> bL_, bR_;
  int w_ = 0;
  ClkSpb c_;
  double sp_, ph_ = -1, fph_ = 0, last_ = -1;
  Voice v_[24]; int nv_ = 0;
  uint32_t seed_ = 1;
  double fill_ = 0;
};

// ---- Beat scramble ---------------------------------------------------------------------------------------------------------------------
struct ScDp { double div = 0, chance = 0, rev = 0, keep = 0, vary = 0, seed = 0, xf = 0, mix = 0; };

class ScrambleCore {
 public:
  explicit ScrambleCore(double sr);
  ScrambleCore(const ScrambleCore&) = delete; ScrambleCore& operator=(const ScrambleCore&) = delete;
  void process(float* L, float* R, int n, const ScDp& dp, const ClkTr& tr);
 private:
  double rd(const float* b, double D) const;
  double sr_;
  int N_;
  std::unique_ptr<float[]> bL_, bR_;
  int w_ = 0;
  double D_ = 0, xo_ = 0, xc_ = 0, xn_ = 1;
  ClkSpb c_;
  double sp_, ph_ = -1, wr_ = 0, pq_ = -1, sl_ = -1, src_ = 0;
  bool rv_ = false;
};

// ---- Glitch sequencer ------------------------------------------------------------------------------------------------------------------
struct GlDp { double div = 0, chance = 0, stut = 0, rev = 0, pitch = 0, tape = 0, crush = 0, gate = 0, vary = 0, seed = 0, mix = 0; };

class GlitchCore {
 public:
  explicit GlitchCore(double sr);
  GlitchCore(const GlitchCore&) = delete; GlitchCore& operator=(const GlitchCore&) = delete;
  void process(float* L, float* R, int n, const GlDp& dp, const ClkTr& tr);
 private:
  double rd(const float* b, double D) const;
  double pick(double key, double s, const GlDp& dp) const;
  double sr_;
  int N_;
  std::unique_ptr<float[]> bL_, bR_;
  int w_ = 0;
  double D_ = 0, xo_ = 0, xc_ = 0, xn_ = 1, g_ = 1;
  ClkSpb c_;
  double sp_, ph_ = -1, wr_ = 0, pq_ = -1, sl_ = -1, act_ = 0, sub_ = 4, hl_ = 0, hr_ = 0, hc_ = 0;
};

// ---- Vintage sampler -------------------------------------------------------------------------------------------------------------------
struct VsDp { double model = 0, drive = 0, filter = 0, rate = 0, noise = 0, mix = 0; };

class VSampCore {
 public:
  explicit VSampCore(double sr);
  VSampCore(const VSampCore&) = delete; VSampCore& operator=(const VSampCore&) = delete;
  void process(float* L, float* R, int n, const VsDp& dp);
 private:
  struct Chan { double ph = 1, h = 0, a[4] = {0, 0, 0, 0}, b[8] = {0, 0, 0, 0, 0, 0, 0, 0}, dc = 0, dx = 0; };
  void setup(const VsDp& p);
  double quant(double x) const;
  double sr_;
  Chan c_[2];
  bool kset_ = false; double k0_ = 0, k1_ = 0, k2_ = 0;   // the JS's `key` string (model | 0, rate, filter) as its three numbers
  uint32_t s_ = 0x1234567;
  double hp_ = 0;
  int mi_ = 0; double rate_ = 0, inc_ = 0, q_ = 0;
  bool hasAa_ = false, hasOut_ = false; double aa_[5] = {0, 0, 0, 0, 0}, out_[5] = {0, 0, 0, 0, 0};
};

}  // namespace thunder
#endif
