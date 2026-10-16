// Mutable Instruments Tides (2) (MIT, Emilie Gillet) as a …chains/…waves module: the poly-slope generator.
//
// tides.cc's Process(), run at the host's sample rate in 8-sample blocks (the hardware runs 8-sample blocks at
// 62.5 kHz; every frequency in the DSP is per sample, so only the range roots and the ramp extractor need the rate).
// The hardware runs the slope-phase and frequency modes at half rate to save CPU; here every mode runs at full rate.
//
// p: 0 ramp mode (0 AD, 1 looping, 2 AR), 1 output mode (0 gates, 1 amplitudes, 2 slope phases, 3 frequencies),
//    2 range (0 control, 1 audio), 3 frequency (semitones from A4 = 440 Hz), 4 shape, 5 slope, 6 smoothness, 7 shift/level (all 0–1, as the
//    knobs), 8 pitch patched, 9 trig patched, 10 clock patched, 11 clock ratio (0–18: ÷16 … ×1 … ×16),
//    12 restart (once, when the song starts: a clocked ramp starts its divided cycle again on the next pulse).
// in:  0 trig (gate), 1 clock (gate), 2 pitch (added to Frequency: 0.5 adds nothing, each 0.125 is an octave; with
//      Frequency at 440 Hz the input plays in tune, 0.5 = A4 as everywhere in the app).
// The hardware's three range positions (roots 0.125 Hz, 2 Hz, 130.81 Hz, ±4 octaves) become one Hz control plus
// Control/Audio, which is what the DSP itself cares about (band-limiting, the frequency-ratio tables).
// out: 0–3 the four outputs, in the app's units: looping slopes ±1 (Tides' ±5 V), AD/AR slopes 0–1 (0–8 V), gates 0/1.
#include "tides2/poly_slope_generator.h"
#include "tides2/ramp/ramp_extractor.h"
#include "stmlib/dsp/units.h"
#include "stmlib/utils/gate_flags.h"
#include "mods.h"

using namespace tides;
using namespace stmlib;

namespace {

const int kSub = 8;

// tides.cc's clock ratios (its doubled ×1, there to widen the knob's centre, is listed once)
const Ratio kRatios[19] = {
  { 0.0625f, 16 }, { 0.125f, 8 }, { 0.1666666f, 6 }, { 0.25f, 4 }, { 0.3333333f, 3 }, { 0.5f, 2 }, { 0.6666666f, 3 },
  { 0.75f, 4 }, { 0.8f, 5 }, { 1.0f, 1 }, { 1.25f, 4 }, { 1.3333333f, 3 }, { 1.5f, 2 }, { 2.0f, 1 },
  { 3.0f, 1 }, { 4.0f, 1 }, { 6.0f, 1 }, { 8.0f, 1 }, { 16.0f, 1 },
};

// cv_reader.cc: the Shape knob's response
const float kShapeBreakpoints[] = { 0.0f, 0.26f, 0.34f, 0.42f, 0.5f, 0.58f, 0.66f, 0.74f, 1.0f, 1.0f };

inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
inline int clampi(float v, int a, int b) { int i = (int)(v + (v < 0 ? -0.5f : 0.5f)); return i < a ? a : i > b ? b : i; }

}  // namespace

struct TidesMod : Mod {
  PolySlopeGenerator gen;
  RampExtractor extractor;
  PolySlopeGenerator::OutputSample out_samples[kSub];
  GateFlags trig[kSub], clock[kSub], no_gate[kSub];
  float ramp[kSub];
  GateFlags trig_prev = GATE_FLAG_LOW, clock_prev = GATE_FLAG_LOW;
  int previous_output_mode = -1;
  bool must_reset_extractor = true;
  float eight_hertz;
  // smoothed knobs (the hardware filters its pots too)
  float s_note = 0.f, s_shape = 0.5f, s_slope = 0.5f, s_smooth = 0.5f, s_shift = 0.5f;
  bool primed = false;

  TidesMod(float sr_) {
    gen.Init();
    extractor.Init(sr_, 40.0f / sr_);
    eight_hertz = 8.0f / sr_;
    for (int i = 0; i < kSub; ++i) no_gate[i] = GATE_FLAG_LOW;
  }

  void process(float (*in)[kModBlock], float (*out)[kModBlock], int n) override {
    const RampMode ramp_mode = RampMode(clampi(p[0], 0, 2));
    const OutputMode output_mode = OutputMode(clampi(p[1], 0, 3));
    const Range range = clampi(p[2], 0, 1) ? RANGE_AUDIO : RANGE_CONTROL;
    const bool pitch_patched = p[8] > 0.5f, trig_patched = p[9] > 0.5f, clock_patched = p[10] > 0.5f;
    const float shape_knob = Interpolate(kShapeBreakpoints, clampf(p[4], 0.0f, 1.0f), 8.0f);
    // the frequency as semitones over 8 Hz (stmlib's SemitonesToRatio covers ±128 of them: 0.005 Hz … 12 kHz);
    // A4 is 69.3765 semitones over 8 Hz
    const float knob_note = clampf(p[3], -200.0f, 60.0f) + 69.37631656f;
    if (!primed) { primed = true; s_note = knob_note; s_shape = shape_knob; s_slope = p[5]; s_smooth = p[6]; s_shift = p[7]; }

    if (output_mode != previous_output_mode) { gen.Reset(); previous_output_mode = output_mode; }

    // Out, in the app's units: a looping slope is Tides' ±5 V (→ ±1), an AD/AR slope and the gates 0–8 V (→ 0–1).
    const float k_slope = ramp_mode == RAMP_MODE_LOOPING ? 0.2f : 0.125f;
    const bool gates_mode = output_mode == OUTPUT_MODE_GATES;

    bool restart = p[12] > 0.5f;
    for (int s = 0; s < n; s += kSub) {
      const int m = n - s < kSub ? n - s : kSub;
      for (int i = 0; i < m; ++i) {
        trig[i] = trig_prev = ExtractGateFlags(trig_prev, in[0][s + i] > 0.5f);
        clock[i] = clock_prev = ExtractGateFlags(clock_prev, in[1][s + i] > 0.5f);
      }
      // The pitch input works like the hardware's V/Oct: 0.5 leaves Frequency as it is, each 0.125 is an octave.
      float note = knob_note;
      if (pitch_patched) note += clampf(in[2][s] - 0.5f, -1.0f, 1.0f) * 96.0f;
      ONE_POLE(s_note, note, 0.2f);
      ONE_POLE(s_shape, shape_knob, 0.05f);
      ONE_POLE(s_slope, clampf(p[5], 0.0f, 1.0f), 0.05f);
      ONE_POLE(s_smooth, clampf(p[6], 0.0f, 1.0f), 0.05f);
      ONE_POLE(s_shift, clampf(p[7], 0.0f, 1.0f), 0.05f);
      float transposition = clampf(s_note, -128.0f, 127.0f);

      const GateFlags* gates = trig_patched ? trig : no_gate;
      float frequency;
      if (clock_patched) {
        if (must_reset_extractor) extractor.Reset();
        if (restart) extractor.Restart();
        const Ratio r = kRatios[clampi(p[11], 0, 18)];
        frequency = extractor.Process(range == RANGE_AUDIO, range == RANGE_AUDIO && ramp_mode == RAMP_MODE_AR,
                                      r, clock, ramp, m);
        must_reset_extractor = false;
      } else {
        frequency = eight_hertz * SemitonesToRatio(transposition);
        must_reset_extractor = true;
      }

      gen.Render(ramp_mode, output_mode, range, frequency, s_slope, s_shape, s_smooth, s_shift,
                 gates, !trig_patched && clock_patched ? ramp : NULL, out_samples, m);
      restart = false;

      for (int i = 0; i < m; ++i) {
        const float* c = out_samples[i].channel;
        if (gates_mode) {
          out[0][s + i] = c[0] * k_slope;
          out[1][s + i] = c[1] * k_slope;
          out[2][s + i] = c[2] * 0.125f;
          out[3][s + i] = c[3] * 0.125f;
        } else {
          for (int j = 0; j < 4; ++j) out[j][s + i] = c[j] * k_slope;
        }
      }
    }
  }
};

Mod* make_tides(float sr) { return new TidesMod(sr); }
