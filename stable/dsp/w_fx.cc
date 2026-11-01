// DaisySP chorus and decimator
// (MIT, Electrosmith), wrapped for ...Thunder's per-pad effects chain.
#include <string.h>
#include "Effects/chorus.h"
#include "Effects/decimator.h"

float fin[4096], fl[4096], fr[4096];
static daisysp::Chorus chorus;
static daisysp::Decimator decim;

extern "C" {
__attribute__((export_name("fx_in"))) float* fx_in() { return fin; }
__attribute__((export_name("fx_l"))) float* fx_l() { return fl; }
__attribute__((export_name("fx_r"))) float* fx_r() { return fr; }

__attribute__((export_name("ds_chorus"))) void ds_chorus(int reset, float sr, float depth, float rate, float delay_ms, float feedback, int n) {
  if (reset) chorus.Init(sr);
  chorus.SetLfoDepth(depth); chorus.SetLfoFreq(rate); chorus.SetDelayMs(delay_ms); chorus.SetFeedback(feedback);
  for (int i = 0; i < n; ++i) { chorus.Process(fin[i]); fl[i] = chorus.GetLeft(); fr[i] = chorus.GetRight(); }
}
__attribute__((export_name("ds_decimate"))) void ds_decimate(int reset, float downsample, float bits, int smooth, int n) {
  if (reset) decim.Init();
  decim.SetDownsampleFactor(downsample);
  decim.SetBitsToCrush((uint8_t)bits);
  decim.SetSmoothCrushing(smooth != 0);
  for (int i = 0; i < n; ++i) { fl[i] = fr[i] = decim.Process(fin[i]); }
}
}
