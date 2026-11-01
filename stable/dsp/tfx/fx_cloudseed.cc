// Cloud Seed (MIT, Ghost Note Audio) as a realtime track effect: a big, modulated algorithmic reverb.
//
// Built from CloudSeedCore with its delay buffers shrunk for phones (see build.sh): the original sizes cover
// 192 kHz and come to ~67 MB per instance; these cover 48 kHz at the ranges below (~7 MB).
// It starts from the Dark Plate program and exposes a few controls on top of it.
//
// p: 0 size, 1 decay, 2 diffusion, 3 modulation, 4 tone (high cut), 5 low cut, 6 pre-delay, 7 mix (all 0–1)
#include <string.h>
#include "DSP/ReverbController.h"
#include "tfx.h"

using namespace Cloudseed;

static const struct { int id; double v; } kPlate[] = {
  {Parameter::DryOut, 0.0}, {Parameter::EarlyDiffuseCount, 0.296}, {Parameter::EarlyDiffuseDelay, 0.3067},
  {Parameter::EarlyDiffuseEnabled, 0.0}, {Parameter::EarlyDiffuseFeedback, 0.7707}, {Parameter::EarlyDiffuseModAmount, 0.1439},
  {Parameter::EarlyDiffuseModRate, 0.2467}, {Parameter::EarlyOut, 0.0}, {Parameter::EqCrossSeed, 0.0}, {Parameter::EqCutoff, 0.976},
  {Parameter::EqHighFreq, 0.5134}, {Parameter::EqHighGain, 0.768}, {Parameter::EqHighShelfEnabled, 1.0}, {Parameter::EqLowFreq, 0.388},
  {Parameter::EqLowGain, 0.556}, {Parameter::EqLowShelfEnabled, 0.0}, {Parameter::EqLowpassEnabled, 0.0}, {Parameter::HighCut, 0.2933},
  {Parameter::HighCutEnabled, 1.0}, {Parameter::InputMix, 0.2347}, {Parameter::Interpolation, 1.0}, {Parameter::LateDiffuseCount, 0.488},
  {Parameter::LateDiffuseDelay, 0.24}, {Parameter::LateDiffuseEnabled, 1.0}, {Parameter::LateDiffuseFeedback, 0.8507},
  {Parameter::LateDiffuseModAmount, 0.1468}, {Parameter::LateDiffuseModRate, 0.1667}, {Parameter::LateLineCount, 0.5},
  {Parameter::LateLineDecay, 0.6346}, {Parameter::LateLineModAmount, 0.272}, {Parameter::LateLineModRate, 0.2293},
  {Parameter::LateLineSize, 0.4694}, {Parameter::LateMode, 1.0}, {Parameter::LateOut, 0.66}, {Parameter::LowCut, 0.64},
  {Parameter::LowCutEnabled, 1.0}, {Parameter::SeedDelay, 0.2181}, {Parameter::SeedDiffusion, 0.185},
  {Parameter::SeedPostDiffusion, 0.3653}, {Parameter::SeedTap, 0.334}, {Parameter::TapDecay, 1.0}, {Parameter::TapLength, 0.25},
  {Parameter::TapPredelay, 0.0}, {Parameter::TapCount, 0.196}, {Parameter::TapEnabled, 0.0},
};

struct CloudSeedFx : Fx {
  ReverbController rv;
  float last[8];
  float wl[128], wr[128];
  CloudSeedFx(float sr) : rv((int)sr) {
    for (const auto& k : kPlate) rv.SetParameter(k.id, k.v);
    for (int i = 0; i < 8; ++i) last[i] = -1.f;
    rv.ClearBuffers();
  }
  static float c01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
  void process(float* L, float* R, int n) override {
    // The ranges stop where the shrunk buffers end: size ≤ ~520 ms lines, pre-delay ≤ ~260 ms.
    if (p[0] != last[0]) rv.SetParameter(Parameter::LateLineSize, c01(p[0]) * 0.86);
    if (p[1] != last[1]) rv.SetParameter(Parameter::LateLineDecay, c01(p[1]));
    if (p[2] != last[2]) { rv.SetParameter(Parameter::LateDiffuseFeedback, c01(p[2]) * 0.95); rv.SetParameter(Parameter::EarlyDiffuseFeedback, c01(p[2]) * 0.9); }
    if (p[3] != last[3]) { rv.SetParameter(Parameter::LateLineModAmount, c01(p[3]) * 0.6); rv.SetParameter(Parameter::LateDiffuseModAmount, c01(p[3]) * 0.5); }
    if (p[4] != last[4]) rv.SetParameter(Parameter::HighCut, c01(p[4]));
    if (p[5] != last[5]) rv.SetParameter(Parameter::LowCut, c01(p[5]) * 0.8);
    if (p[6] != last[6]) rv.SetParameter(Parameter::TapPredelay, c01(p[6]) * 0.75);
    for (int i = 0; i < 7; ++i) last[i] = p[i];
    rv.Process(L, R, wl, wr, n);
    // Fully wet inside; the blend happens here like the Galactic reverb's: dry stays full to half way.
    const float m = c01(p[7]), wg = (m < 0.5f ? m * 2.f : 1.f) * 1.6f, dg = m > 0.5f ? 2.f - m * 2.f : 1.f;
    for (int i = 0; i < n; ++i) { L[i] = L[i] * dg + wl[i] * wg; R[i] = R[i] * dg + wr[i] * wg; }
  }
};
Fx* make_cloudseed(float sr) { return new CloudSeedFx(sr); }
