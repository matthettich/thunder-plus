// Mutable Instruments Marbles (MIT, Emilie Gillet) as a …chains/…waves module: the random sampler.
//
// The firmware's own T and X/Y generators run here at the host's sample rate, in 8-sample blocks, as marbles.cc runs
// them in 5-sample blocks at 32 kHz. The T section always takes its clock from in[0]: the JS side either copies the
// patched Clock input there or makes a steady clock from the song tempo, so Marbles' ramp extractor locks to the song
// and the Rate control becomes Marbles' clock ratio (÷4 … ×4), as on the hardware with a clock patched in.
// The six preset scales are copied from marbles/settings.cc.
//
// p: 0 rate (clock ratio index 0–8: ÷4 ÷3 ÷2 ×2/3 ×1 ×3/2 ×2 ×3 ×4), 1 range (0 ×¼, 1 ×1, 2 ×4), 2 t model (0–6),
//    3 t bias, 4 jitter, 5 gate length, 6 gate length randomness, 7 déjà vu (knob, 0–1), 8 loop length (1–16),
//    9 t déjà vu (0 off, 1 on, 2 locked), 10 x déjà vu, 11 x spread, 12 x bias, 13 x steps, 14 scale (0–5),
//    15 x range (0 0…+2 V, 1 0…+5 V, 2 ±5 V), 16 x control mode (0 identical, 1 bump, 2 tilt),
//    17 x clock (0 T1/T2/T3, 1 all T1, 2 all T2, 3 all T3), 18 y range, 19 y divider (0–11: 1/64 … 1/1),
//    20 y spread, 21 y bias, 22 y steps, 23 x octave offset, 24 x output (0 pitch, 1 CV), 28 x clock patched,
//    29 restart (once).
// in:  0 t clock (gate), 1 reset (gate), 2 x clock (gate), 3 déjà vu CV (added to the knob).
// out: 0–2 t1 t2 t3 (0/1); 3–5 x1 x2 x3: as pitch, 1 V = one octave = 0.125 with 0 V at C4 (0.40625, so A4 stays 0.5
//      and the scales' root is C), plus the octave offset; as CV, volts / 5 (±5 V → ±1, 0…+5 V → 0…1); 6 y (volts / 5).
#include "marbles/random/random_generator.h"
#include "marbles/random/random_stream.h"
#include "marbles/random/t_generator.h"
#include "marbles/random/x_y_generator.h"
#include "stmlib/utils/gate_flags.h"
#include "mods.h"

float marbles_sample_rate_hz = 32000.0f;   // the ramp extractors' time constants (build.sh patches their 32000s)

using namespace marbles;
using namespace stmlib;

namespace {

const Scale kPresetScales[6] = {
  { 1.0f, 12, { { 0.0000f, 255 }, { 0.0833f, 16 }, { 0.1667f, 96 }, { 0.2500f, 24 }, { 0.3333f, 128 }, { 0.4167f, 64 },
                { 0.5000f, 8 }, { 0.5833f, 192 }, { 0.6667f, 16 }, { 0.7500f, 96 }, { 0.8333f, 24 }, { 0.9167f, 128 } } },  // major
  { 1.0f, 12, { { 0.0000f, 255 }, { 0.0833f, 16 }, { 0.1667f, 96 }, { 0.2500f, 128 }, { 0.3333f, 8 }, { 0.4167f, 64 },
                { 0.5000f, 4 }, { 0.5833f, 192 }, { 0.6667f, 96 }, { 0.7500f, 16 }, { 0.8333f, 128 }, { 0.9167f, 16 } } },  // minor
  { 1.0f, 12, { { 0.0000f, 255 }, { 0.0833f, 4 }, { 0.1667f, 96 }, { 0.2500f, 4 }, { 0.3333f, 4 }, { 0.4167f, 140 },
                { 0.5000f, 4 }, { 0.5833f, 192 }, { 0.6667f, 4 }, { 0.7500f, 96 }, { 0.8333f, 4 }, { 0.9167f, 4 } } },  // pentatonic
  { 1.0f, 7,  { { 0.0000f, 255 }, { 0.1275f, 128 }, { 0.2625f, 32 }, { 0.4600f, 8 }, { 0.5883f, 192 }, { 0.7067f, 64 },
                { 0.8817f, 16 } } },  // pelog
  { 1.0f, 12, { { 0.0000f, 255 }, { 0.0752f, 128 }, { 0.1699f, 4 }, { 0.2630f, 4 }, { 0.3219f, 128 }, { 0.4150f, 64 },
                { 0.4918f, 4 }, { 0.5850f, 192 }, { 0.6601f, 64 }, { 0.7549f, 4 }, { 0.8479f, 4 }, { 0.9069f, 64 } } },  // raag bhairav that
  { 1.0f, 12, { { 0.0000f, 255 }, { 0.0752f, 4 }, { 0.1699f, 128 }, { 0.2630f, 64 }, { 0.3219f, 4 }, { 0.4150f, 128 },
                { 0.4918f, 4 }, { 0.5850f, 192 }, { 0.6601f, 4 }, { 0.7549f, 64 }, { 0.8479f, 128 }, { 0.9069f, 4 } } },  // raag shri
};

const Ratio kYDividerRatios[12] = {
  { 1, 64 }, { 1, 48 }, { 1, 32 }, { 1, 24 }, { 1, 16 }, { 1, 12 }, { 1, 8 }, { 1, 6 }, { 1, 4 }, { 1, 3 }, { 1, 2 }, { 1, 1 },
};

const int kSub = 8;          // samples per Marbles block
const int kGateDelay = 2;    // as marbles.cc: the gates come out 2 samples after the voltages, so CV is settled first

inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
inline int clampi(float v, int a, int b) { int i = (int)(v + (v < 0 ? -0.5f : 0.5f)); return i < a ? a : i > b ? b : i; }

}  // namespace

struct MarblesMod : Mod {
  RandomGenerator generator;
  RandomStream stream;
  TGenerator t;
  XYGenerator xy;
  GroupSettings x, y;
  float ramp_buffer[kSub * 4];
  bool gates[kSub * 2];
  float voltages[kSub * 4];
  GateFlags t_clock[kSub], x_clock[kSub];
  GateFlags t_prev = GATE_FLAG_LOW, x_prev = GATE_FLAG_LOW;
  bool reset_prev = false;
  bool tail[3][kGateDelay];       // the gates' last samples, carried into the next block

  MarblesMod(float sr_, unsigned seed) {
    marbles_sample_rate_hz = sr_;
    generator.Init(seed ? seed : 1);
    stream.Init(&generator);
    t.Init(&stream, sr_);
    xy.Init(&stream, sr_);
    // All four channels get the scales (the firmware loads only X; Y then quantizes against an empty scale).
    for (int i = 0; i < 6; ++i) for (int c = 0; c < 4; ++c) xy.LoadScale(c, i, kPresetScales[i]);
    memset(tail, 0, sizeof(tail));
  }
  bool primed = false;

  static float DejaVu(float d) {   // the knob's response, with the dead band at 12 o'clock (marbles.cc)
    d = clampf(d, 0.0f, 1.0f);
    if (d < 0.47f) return d * 1.06382978723f;
    if (d > 0.53f) return 0.5f + (d - 0.53f) * 1.06382978723f;
    return 0.5f;
  }

  void process(float (*in)[kModBlock], float (*out)[kModBlock], int n) override {
    marbles_sample_rate_hz = sr;
    bool restart = p[29] > 0.5f;
    for (int s = 0; s < n; s += kSub) {
      const int m = n - s < kSub ? n - s : kSub;
      bool reset = restart; restart = false;
      for (int i = 0; i < m; ++i) {
        t_clock[i] = t_prev = ExtractGateFlags(t_prev, in[0][s + i] > 0.5f);
        x_clock[i] = x_prev = ExtractGateFlags(x_prev, in[2][s + i] > 0.5f);
        const bool r = in[1][s + i] > 0.5f; if (r && !reset_prev) reset = true; reset_prev = r;
      }
      const float deja_vu = DejaVu(p[7] + in[3][s]);
      const int length = clampi(p[8], 1, 16);
      const int t_dv = clampi(p[9], 0, 2), x_dv = clampi(p[10], 0, 2);

      // T section: the clock ratio is picked as on the hardware (1.05 × rate / 96 + 0.5 through a 9-step quantizer);
      // set_rate gets the knob value that lands in the middle of the chosen step.
      const int ratio = clampi(p[0], 0, 8);
      t.set_model(TGeneratorModel(clampi(p[2], 0, 6)));
      t.set_range(TGeneratorRange(clampi(p[1], 0, 2)));
      t.set_rate(((ratio + 0.5f) / 9.0f - 0.5f) * 96.0f / 1.05f);
      t.set_bias(clampf(p[3], 0.0f, 1.0f));
      t.set_jitter(clampf(p[4], 0.0f, 1.0f));
      t.set_deja_vu(t_dv == 2 ? 0.5f : (t_dv == 1 ? deja_vu : 0.0f));
      t.set_length(length);
      t.set_pulse_width_mean(clampf(p[5], 0.0f, 1.0f));
      t.set_pulse_width_std(clampf(p[6], 0.0f, 1.0f));

      Ramps ramps;
      ramps.external = &ramp_buffer[0];
      ramps.master = &ramp_buffer[kSub];
      ramps.slave[0] = &ramp_buffer[kSub * 2];
      ramps.slave[1] = &ramp_buffer[kSub * 3];
      bool t_reset = reset;
      t.Process(true, &t_reset, t_clock, ramps, gates, m);

      // X and Y sections
      x.control_mode = ControlMode(clampi(p[16], 0, 2));
      x.voltage_range = VoltageRange(clampi(p[15], 0, 2));
      x.register_mode = false;
      x.register_value = 0.0f;
      x.spread = clampf(p[11], 0.0f, 1.0f);
      x.bias = clampf(p[12], 0.0f, 1.0f);
      x.steps = clampf(p[13], 0.0f, 1.0f);
      x.deja_vu = x_dv == 2 ? 0.5f : (x_dv == 1 ? deja_vu : 0.0f);
      x.length = length;
      x.ratio.p = 1; x.ratio.q = 1;
      x.scale_index = clampi(p[14], 0, 5);

      y.control_mode = CONTROL_MODE_IDENTICAL;
      y.voltage_range = VoltageRange(clampi(p[18], 0, 2));
      y.register_mode = false;
      y.register_value = 0.0f;
      y.spread = clampf(p[20], 0.0f, 1.0f);
      y.bias = clampf(p[21], 0.0f, 1.0f);
      y.steps = clampf(p[22], 0.0f, 1.0f);
      y.deja_vu = 0.0f;
      y.length = 1;
      y.ratio = kYDividerRatios[clampi(p[19], 0, 11)];
      y.scale_index = x.scale_index;

      const bool x_external = p[28] > 0.5f;
      const ClockSource source = x_external ? CLOCK_SOURCE_EXTERNAL : ClockSource(clampi(p[17], 0, 3));
      if (!primed) {
        // One sample with every ramp at its top, so the first tick after the module is made already takes a new value
        // (the loop's first), as every later restart does. (The hardware plays 0 V until its first tick.)
        primed = true;
        float top[kSub * 4]; for (int i = 0; i < kSub * 4; ++i) top[i] = 0.999f;
        Ramps r; r.external = &top[0]; r.master = &top[kSub]; r.slave[0] = &top[kSub * 2]; r.slave[1] = &top[kSub * 3];
        GateFlags low[1] = { GATE_FLAG_LOW };
        bool no_reset = false;
        xy.Process(CLOCK_SOURCE_INTERNAL_T1_T2_T3, x, y, &no_reset, low, r, voltages, 1);
      }
      bool x_reset = reset || (!x_external && t_reset);
      xy.Process(source, x, y, &x_reset, x_clock, ramps, voltages, m);

      // Out, in the app's units. Gates go through the 2-sample delay.
      const bool as_cv = p[24] > 0.5f;
      const float xk = as_cv ? 0.2f : 0.125f, x0 = as_cv ? 0.0f : 0.40625f + p[23] * 0.125f;
      for (int i = 0; i < m; ++i) {
        const float* v = &voltages[i * 4];
        out[3][s + i] = x0 + v[0] * xk;
        out[4][s + i] = x0 + v[1] * xk;
        out[5][s + i] = x0 + v[2] * xk;
        out[6][s + i] = v[3] * 0.2f;
      }
      bool g[3][kSub + kGateDelay];
      for (int k = 0; k < 3; ++k) for (int j = 0; j < kGateDelay; ++j) g[k][j] = tail[k][j];
      for (int i = 0; i < m; ++i) {
        g[0][i + kGateDelay] = gates[i * 2];
        g[1][i + kGateDelay] = ramps.master[i] < 0.5f;
        g[2][i + kGateDelay] = gates[i * 2 + 1];
      }
      for (int k = 0; k < 3; ++k) {
        for (int i = 0; i < m; ++i) out[k][s + i] = g[k][i] ? 1.0f : 0.0f;
        for (int j = 0; j < kGateDelay; ++j) tail[k][j] = g[k][m + j];
      }
    }
  }
};

Mod* make_marbles(float sr, unsigned seed) { return new MarblesMod(sr, seed); }
