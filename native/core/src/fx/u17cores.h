// The four JS-only effects of U17: Pitch reverb (PitchFxCore kind 1, id 96), Pitch delay (PitchFxCore kind 0, id 97), Resonator (ResonCore, id 98) and Degrade
// (DegradeCore, id 99), line for line from index.html (the same classes are generated into chains/chains-worklet.js and waves/waves-worklet.js).
// THE JS IS THE SPECIFICATION, quirks included. The same rules as clkcores.h / idmcores.h: every JS Number is a double in the JS's own arithmetic order (the core is
// built with floating-point contraction off), the only float32 values are the ones the JS writes into a Float32Array (the block L / R, the delay lines, the combs, the
// resonator strings), Math.exp / pow / cos / sin / tanh are js_exp / js_pow / js_cos / js_sin / js_tanh (fdlibm as V8 12.4 has them), Math.round is js_round, Math.min /
// max keep NaN, a read past the end of a Float32Array is undefined (NaN in every sum) and a write there is dropped. Randomness: Degrade's xorshift32 noise (seed 0x2545f491)
// is integer arithmetic reproduced as written; nothing here uses a platform random number. No allocation after construction: the buffers are made in the
// constructors (the plan, on the control thread).
// None of the four reads the beat clock. In the track-effect host (the worklet's slotRun / slotNap) Degrade always runs (it makes its own noise) and the other three nap
// like a wasm effect; their pad use (index.html applyFx: the whole sound in, 128-frame blocks, a mono sum back) is pad_degrade / pad_reson / pad_pitchfx below.
#ifndef THUNDER_FX_U17CORES_H
#define THUNDER_FX_U17CORES_H
#include <cstdint>
#include <memory>
#include <vector>

namespace thunder {

// ---- Degrade ---------------------------------------------------------------------------------------------------------------------------
struct DgDp { double bits = 0, rate = 0, noise = 0, color = 0, follow = 0, dry = 0, wet = 0; };

class DegradeCore {
 public:
  explicit DegradeCore(double sr) : sr_(sr) {}
  DegradeCore(const DegradeCore&) = delete; DegradeCore& operator=(const DegradeCore&) = delete;
  void process(float* L, float* R, int n, const DgDp& p);
 private:
  double sr_;
  double ph_ = 1, hl_ = 0, hr_ = 0, nl_ = 0, nr_ = 0, env_ = 0;
  uint32_t s_ = 0x2545f491u;
};

// ---- Resonator -------------------------------------------------------------------------------------------------------------------------
struct RsDp { double note = 0, chord = 0, mode = 0, decay = 0, bright = 0, spread = 0, mix = 0; };

class ResonCore {
 public:
  explicit ResonCore(double sr);
  ResonCore(const ResonCore&) = delete; ResonCore& operator=(const ResonCore&) = delete;
  void process(float* L, float* R, int n, const RsDp& p);
 private:
  struct Voice { std::unique_ptr<float[]> b; int N = 0, w = 0; double lp = 0, dc = 0, dx = 0, d = 0, g = 0, gl = 0, gr = 0; };
  double sr_;
  Voice v_[5];
};

// ---- Pitch delay / Pitch reverb --------------------------------------------------------------------------------------------------------
struct PfDp { double kind = 0, mode = 0, time = 0, fb = 0, pitch = 0, pitch2 = 0, tone = 0, size = 0, wow = 0, mix = 0; };

class PitchFxCore {
 public:
  explicit PitchFxCore(double sr);
  PitchFxCore(const PitchFxCore&) = delete; PitchFxCore& operator=(const PitchFxCore&) = delete;
  void process(float* L, float* R, int n, const PfDp& p);
 private:
  struct Shifter { std::unique_ptr<float[]> b; int w = 0; double ph = 0; };
  struct Comb { std::unique_ptr<float[]> b; int N = 0, i = 0; double f = 0; };
  struct Allp { std::unique_ptr<float[]> b; int N = 0, i = 0; };
  double shift(Shifter& s, double x, double ratio, double win);
  double verb(int ch, double x, double fb, double damp, double size);
  double sr_;
  int D_;
  std::unique_ptr<float[]> dl_[2];
  int dw_ = 0;
  Shifter sh_[4];
  Comb cb_[2][4];
  Allp ap_[2][2];
  double lp_[2] = {0, 0}, fbS_[2] = {0, 0}, fenv_[2] = {0, 0};
  int step_ = 0;
  double cnt_ = 0, wph_ = 0, tsm_ = 0, rev_ = 0;
};

// ---- the pads' use (index.html applyFx): offline, the whole sound at once, 128-frame blocks ---------------------------------------------------
// A pad effect's values are the page's own (bits, rate in Hz, time in ms, ...), not the mapped slot parameters of the track effects. A value the JS leaves undefined (or null)
// is NaN here; the three that applyFx defaults when `!= null` are pitch2 (-12), size (0.6, Pitch delay only) and wow (0.2): pass NaN for "absent".
struct PadDegrade { double bits, rate, noise, color, follow, dry, wet; };
struct PadReson { double note, chord, decay, bright, mode, spread, mix; };
struct PadPitchFx { double mode, time, fb, pitch, pitch2, tone, size, wow, mix; };
// Each returns the processed sound; pad_pitchfx's is longer by the tail (the JS's `N`), and is the mono sum (L + R) * 0.5 as the JS makes it. `reverb`: Pitch reverb.
// A preverb with no `size` makes a JS exception (new Float32Array(NaN)'s set): pad_pitchfx returns an empty vector then.
std::vector<float> pad_degrade(const PadDegrade& f, const float* a, size_t n, double sr);
std::vector<float> pad_reson(const PadReson& f, const float* a, size_t n, double sr);
std::vector<float> pad_pitchfx(bool reverb, const PadPitchFx& f, const float* a, size_t n, double sr);

}  // namespace thunder

#endif
