// Mutable Instruments Warps (MIT, Emilie Gillet) as a realtime track effect: cross-modulation (crossfade,
// fold, analog and digital ring mod, XOR, comparator) blending into a vocoder.
//
// Warps runs at 96 kHz (its filter banks are designed for that rate), so audio is resampled in and out
// around it, as the Clouds and Beads effects do at their own rates.
//
// p: 0 algorithm (0–1), 1 timbre (0–1), 2 carrier level (0–1), 3 modulator level (0–1),
//    4 carrier (0 the input's right side, 1 sine, 2 triangle, 3 saw; the vocoder uses saw, pulse, noise),
//    5 note of the internal carrier (MIDI, 60 = C4), 6 mix.
// Inputs: left = modulator; right = carrier when p4 is 0. With a mono source both are the same signal.
#include <string.h>
#include "warps/dsp/modulator.h"
#include "tfx.h"

struct WarpsFx : Fx {
  static const int BLK = 32, RING = 1024;
  warps::Modulator mod;
  warps::ShortFrame cin[BLK], cout[BLK];
  int nin = 0;
  double step, ph = 0.0, rstep, rp = -96.0;
  float xl = 0.f, xr = 0.f, fl[RING];
  long w = 0;
  WarpsFx(float sr_) {
    step = sr_ / 96000.0; rstep = 96000.0 / sr_;
    memset(fl, 0, sizeof(fl));
    mod.Init(96000.0f);
  }
  static short s16(float v) { v *= 32767.f; if (v > 32767.f) v = 32767.f; if (v < -32768.f) v = -32768.f; return (short)v; }
  void push(float m, float c) {
    // Warps' input 1 is the carrier, input 2 the modulator.
    cin[nin].l = s16(c); cin[nin].r = s16(m);
    if (++nin < BLK) return;
    nin = 0;
    warps::Parameters* P = mod.mutable_parameters();
    float a = p[0] < 0 ? 0 : p[0] > 1 ? 1 : p[0];
    P->modulation_algorithm = a; P->modulation_parameter = p[1] < 0 ? 0 : p[1] > 1 ? 1 : p[1];
    P->channel_drive[0] = p[2]; P->channel_drive[1] = p[3];
    int sh = (int)p[4]; if (sh < 0) sh = 0; if (sh > 3) sh = 3; P->carrier_shape = sh;
    P->note = p[5];          // MIDI note: 60 plays C4 (261.6 Hz)
    P->frequency_shift_pot = 0.5f; P->frequency_shift_cv = 0.f; P->phase_shift = 0.f;
    mod.Process(cin, cout, BLK);
    for (int i = 0; i < BLK; ++i, ++w) fl[w & (RING - 1)] = cout[i].l / 32768.f;
  }
  void process(float* L, float* R, int n) override {
    const float m = p[6];
    for (int i = 0; i < n; ++i) {
      const float l = L[i], r = R[i];
      while (ph <= 1.0) { const float f = (float)ph; push(xl + (l - xl) * f, xr + (r - xr) * f); ph += step; }
      ph -= 1.0; xl = l; xr = r;
      float o = 0.f;
      if (rp >= 0.0) {
        long j = (long)rp; if (j + 1 >= w) { j = w - 2; rp = (double)j; }
        if (j >= 0) { const float f = (float)(rp - j); o = fl[j & (RING - 1)] + (fl[(j + 1) & (RING - 1)] - fl[j & (RING - 1)]) * f; }
      }
      rp += rstep;
      L[i] = l + (o - l) * m; R[i] = r + (o - r) * m;
    }
  }
};
Fx* make_warps(float sr) { return new WarpsFx(sr); }
