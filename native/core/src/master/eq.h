// THE MASTER EQ (N1, unit U12): the page's eqBuild (index.html): the EQ as native Web Audio BiquadFilterNodes, in series, in band order.
//   bell -> 'peaking' (f, Q linear, gain dB)      lowshelf / highshelf (f, gain dB; Web Audio ignores Q for a shelf)
//   lowcut / highcut -> 'highpass' / 'lowpass', one node per Butterworth stage of the slope (12: Q .7071, 24: .5412 1.3066, 48: .5098 .6013 .9 2.5629;
//   each node's Q is 20*log10(q) dB, as the page sets it, because Web Audio takes a pass filter's Q in dB)
// The node's maths is CHROMIUM'S (third_party/blink/renderer/platform/audio/biquad.cc, BiquadDSPKernel): the parameter values are float32
// AudioParam values; frequency is normalised by Nyquist (clamped 0..1); the coefficients are doubles divided through a0 as a multiplication by
// 1/a0; the filter is direct form I in doubles whose y state is the float32 output (measured: feeding back the double y is -90 dB off Chrome), A = 10^(gain/40) and a pass filter's 10^(Q/20) are held as floats (measured: bit-exact with them, 1e-6 off without), and each node writes a float32 buffer (the next node reads it). Constant parameters, so no
// per-frame coefficients: an offline render sets them once (index.html pass(): `n.frequency.value = ...`).
//   NOT PORTED: the live page glides a changed band with setTargetAtTime (time constant 10 ms; mstSync), which Chrome renders with a new set of
//   coefficients every frame; a live host would need that (U13 / the live path). Chrome's flush of denormal filter states at the end of a block.
// std::sin / cos / sqrt are used where Chromium calls its libm; pow(10, x) is js_pow. See tools/eq-dump.mjs (the JS model, bit for bit) and
// tools/eq-webaudio-check.mjs (the model against a real OfflineAudioContext).
#ifndef THUNDER_MASTER_EQ_H
#define THUNDER_MASTER_EQ_H
#include <cstdint>

namespace thunder {

enum class EqType : uint8_t { LowCut, LowShelf, Bell, HighShelf, HighCut };
struct EqBand { EqType type = EqType::Bell; double f = 1000, g = 0, q = 1; int slope = 24; bool on = true; };   // freshBand
constexpr int kEqMaxBands = 8;                // normMst: bands.slice(0, 8)
constexpr int kEqMaxNodes = kEqMaxBands * 4;  // a 48 dB/oct cut is four nodes

enum class BqType : uint8_t { Lowpass, Highpass, Peaking, Lowshelf, Highshelf };
struct BqCoef { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };   // normalised (divided through a0)
// Chromium's Biquad::Set*Params for one node: f (Hz), q (Q: dB for lowpass / highpass, linear for peaking, unused for a shelf) and gain (dB) are the
// float32 parameter values; sr the context's rate.
BqCoef bq_coef(BqType type, double sr, float f, float q, float gain);

class MasterEq {
 public:
  MasterEq() = default;
  // eqBuild(): the nodes for `bands` (n <= kEqMaxBands), or none when !eqOn. Control thread (no allocation either way).
  void configure(double sr, bool eqOn, const EqBand* bands, int n);
  int nodes() const { return n_; }
  void process(float* L, float* R, int frames);   // in place, both channels (each node keeps one state per channel)
  void reset();                                   // states to zero
  // the node list as built (for tests): type, the float32 parameter values
  BqType nodeType(int i) const { return node_[i].type; }
  float nodeF(int i) const { return node_[i].f; }
  float nodeQ(int i) const { return node_[i].q; }
  float nodeG(int i) const { return node_[i].g; }
  BqCoef nodeCoef(int i) const { return node_[i].c; }
 private:
  struct Node { BqType type = BqType::Peaking; float f = 0, q = 0, g = 0; BqCoef c; double x1[2] = {0, 0}, x2[2] = {0, 0}, y1[2] = {0, 0}, y2[2] = {0, 0}; };
  Node node_[kEqMaxNodes]; int n_ = 0;
};

}  // namespace thunder
#endif
