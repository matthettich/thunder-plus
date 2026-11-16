// U23: the layer oscillators click, wavetable, pd, chip, supersaw, pwm, sample (+ loop + vint) and grains of index.html's ENGINE block (newOsc / makeOsc / wavetableOsc / pwmOsc / vintOsc),
// one source file each (layer_<type>.cpp). Used by Osc in engine.cpp: `LayerOsc::init` builds the state of one layer, `blk` is makeOsc's per-block hook (only the wavetable has one), `s(inc)` is the sample.
// Every Math.* is a js_* function, every expression keeps the JS's order of operations. `R` is the layer's rng (the JS closure's `R`): the oscillator that draws from it takes it by reference.
#ifndef THUNDER_ENGINE_LAYERS_H
#define THUNDER_ENGINE_LAYERS_H
#include <cmath>
#include <cstdint>
#include <vector>

#include "engine/engine.h"
#include "rt/json.h"

namespace thunder {
namespace l23 {
// Math.max / Math.min on two doubles (NaN wins; +0 beats -0 for max and -0 beats +0 for min), x || d
inline double mx(double a, double b) { if (a != a || b != b) return NAN; if (a == 0 && b == 0) return std::signbit(a) ? b : a; return a > b ? a : b; }
inline double mn(double a, double b) { if (a != a || b != b) return NAN; if (a == 0 && b == 0) return std::signbit(a) ? a : b; return a < b ? a : b; }
inline double orv(double x, double d) { return (x != 0 && x == x) ? x : d; }
inline float f32(double x) { return (float)x; }
constexpr double kPi = 3.141592653589793;
// d[i] of a typed array: undefined (NaN in arithmetic) outside it
inline double at(const float* d, size_t len, double i) { return (i >= 0 && i < (double)len) ? (double)d[(size_t)i] : (double)NAN; }
}  // namespace l23

struct ClickOsc { double W = 1, n = 0; bool dbl = false; void init(double A, double B, double sr); double s(); };

struct WavetableOsc {
  static constexpr int TN = 1024;
  float tab[TN + 1];
  double phases[8], dets[8]; int uni = 1; double sub = 0, noise = 0, bend = 0, A = 0, B = 0, sr = 48000, builtH = -1, subPh = 0;
  void init(const EngLayer& L, double sr, double A, double B, EngRng& R);
  void build(double hmax);
  void blk(double inc);
  double s(double inc, EngRng& R);
};

struct PdOsc { int w = 0, wi = 99; double wd = 0, d = 0, res = 1, ph = 0, prev = 0; void init(double A, double B); double s(double inc); };
struct ChipOsc { int w = 0; double hold = 1, ph = 0, c = 0, out = 0; int32_t lf = 0x4000; void init(double sr, double A, double B); double s(double inc); };
struct SupersawOsc { double det = 0, side = 0, g = 0, ph[7]; void init(double A, double B, EngRng& R); double s(double inc); };
struct PwmOsc {
  const float* d = nullptr; size_t len = 0; double k = 0, clk = 0, md = 0, q = 0, pos = 0, acc = 1, held = 0, cnt = 64, duty = 0.5;
  void init(const EngLayer& L, double A, double B); double s(double inc);
};
struct SampleOsc {   // the plain sample, with its loop modes
  const float* d = nullptr; size_t len = 0; double A = 0, k = 0, pos = 0; bool rev = false;
  int32_t lm = 0; bool looped = false; double ls = 0, le = 0, LL = 0, X = 0; int dir = 1;
  void init(const EngLayer& L, double A, double B); double s(double inc);
  double atL(double p) const;   // the loop modes' `at`
};
struct VintOsc {     // sample + vint: the sample re-sampled at the machine's rate and bit depth (the copy is made here: this one allocates, as the JS's new Float32Array does)
  std::vector<float> dv; double k = 0, g = 1, pos = 0, y1 = 0, y2 = 0; bool rev = false, hasFc = false;
  void init(const EngLayer& L, double sr, double A, double B); double s(double inc);
};
struct GrainsOsc {
  const float* d = nullptr; size_t len = 0; double k = 0, gl = 32, span = 0, A = 0; double pos[4], age[4];
  void init(const EngLayer& L, double sr, double A, double B); double s(double inc, EngRng& R);
};

// The dispatcher Osc uses for the types of this unit (layer_u23.cpp).
struct LayerOsc {
  enum Which : uint8_t { None, Click, Wavetable, Pd, Chip, Supersaw, Pwm, Sample, Vint, Grains, Silent } which = None;
  ClickOsc click; WavetableOsc wt; PdOsc pd; ChipOsc chip; SupersawOsc ss; PwmOsc pwm; SampleOsc smp; VintOsc vint; GrainsOsc gr;
  static bool handles(LType t);
  void init(const EngLayer& L, double sr, EngRng& R);   // L.type is one of this unit's
  void blk(double inc) { if (which == Wavetable) wt.blk(inc); }
  double s(double inc, EngRng& R);
};

// the built-in cymbal of the PWM sampler (pwmDefault): 48000 floats at 32 kHz, made once (eng_init() makes it now; the lazy path stays)
const float* eng_pwm_default(size_t* len, double* sr);
// the layer's u23 fields from a song's / the test's JSON (wtBend, wtUni, wtSpread, wtSub, wtNoise, vint, vintF, lpMode, lpS, lpE, lpX); the sample stays the caller's (see EngSampleBank)
void eng_layer_u23_from_json(EngLayer& L, Json j);
struct EngSampleBank {   // owns the sample data of the layers a loader fills (control thread): attach(L, j) reads j = {"sr": n, "data": [...]} (float32 values) and points L at it
  std::vector<std::vector<float>> store;
  void attach(EngLayer& L, Json sample);
};
}  // namespace thunder
#endif
