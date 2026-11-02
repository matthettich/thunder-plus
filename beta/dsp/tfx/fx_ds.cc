// DaisySP Chorus and Decimator (MIT, Electrosmith) as realtime track effects.
#include "Effects/chorus.h"
#include "Effects/decimator.h"
#include "tfx.h"

// p: 0 depth, 1 rate, 2 delay ms, 3 feedback, 4 mix
struct ChorusFx : Fx {
  daisysp::Chorus c;
  ChorusFx(float sr) { c.Init(sr); }
  void process(float* L, float* R, int n) override {
    c.SetLfoDepth(p[0]); c.SetLfoFreq(p[1]); c.SetDelayMs(p[2]); c.SetFeedback(p[3]);
    const float m = p[4];
    for (int i = 0; i < n; ++i) {
      c.Process((L[i] + R[i]) * 0.5f);
      L[i] = L[i] * (1.f - m) + c.GetLeft() * m * 1.4f; R[i] = R[i] * (1.f - m) + c.GetRight() * m * 1.4f;
    }
  }
};
Fx* make_chorus(float sr) { return new ChorusFx(sr); }

// p: 0 downsample, 1 bits, 2 smooth, 3 mix
struct CrushFx : Fx {
  daisysp::Decimator a, b;
  CrushFx() { a.Init(); b.Init(); }
  void process(float* L, float* R, int n) override {
    daisysp::Decimator* ds[2] = {&a, &b}; for (daisysp::Decimator* d : ds) { d->SetDownsampleFactor(p[0]); d->SetBitsToCrush((uint8_t)p[1]); d->SetSmoothCrushing(p[2] > 0.5f); }
    const float m = p[3];
    for (int i = 0; i < n; ++i) { L[i] = L[i] * (1.f - m) + a.Process(L[i]) * m; R[i] = R[i] * (1.f - m) + b.Process(R[i]) * m; }
  }
};
Fx* make_crush(float) { return new CrushFx(); }
