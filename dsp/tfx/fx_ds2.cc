// More DaisySP effects (MIT, Electrosmith) as realtime track effects: flanger, phaser, tremolo, overdrive,
// wavefolder, autowah, pitch shifter and ladder filter. Each runs one instance per side, in place, with its
// own dry/wet where the effect has none. Parameter lists (p[]) are given above each one.
#include <math.h>
#include "Effects/flanger.h"
#include "Effects/phaser.h"
#include "Effects/tremolo.h"
#include "Effects/overdrive.h"
#include "Effects/wavefolder.h"
#include "Effects/autowah.h"
#include "Effects/pitchshifter.h"
#include "Filters/ladder.h"
#include "tfx.h"

static inline float mixf(float dry, float wet, float m) { return dry + (wet - dry) * m; }

// p: 0 depth, 1 rate Hz, 2 delay (0–1 = 0.1–7 ms), 3 feedback, 4 mix
struct FlangerFx : Fx {
  daisysp::Flanger a, b;
  FlangerFx(float sr) { a.Init(sr); b.Init(sr); }
  void process(float* L, float* R, int n) override {
    daisysp::Flanger* fs[2] = {&a, &b};
    for (daisysp::Flanger* f : fs) { f->SetLfoDepth(p[0]); f->SetLfoFreq(p[1]); f->SetDelay(p[2]); f->SetFeedback(p[3]); }
    for (int i = 0; i < n; ++i) { L[i] = mixf(L[i], a.Process(L[i]), p[4]); R[i] = mixf(R[i], b.Process(R[i]), p[4]); }
  }
};
Fx* make_flanger(float sr) { return new FlangerFx(sr); }

// p: 0 depth, 1 rate Hz, 2 centre Hz, 3 feedback, 4 poles (1–8), 5 mix
struct PhaserFx : Fx {
  daisysp::Phaser a, b;
  PhaserFx(float sr) { a.Init(sr); b.Init(sr); }
  void process(float* L, float* R, int n) override {
    daisysp::Phaser* fs[2] = {&a, &b}; int poles = (int)p[4]; if (poles < 1) poles = 1; if (poles > 8) poles = 8;
    for (daisysp::Phaser* f : fs) { f->SetLfoDepth(p[0]); f->SetLfoFreq(p[1]); f->SetFreq(p[2]); f->SetFeedback(p[3]); f->SetPoles(poles); }
    // DaisySP's phaser sums its stages hot (+3 dB with feedback): trim the wet side back to unity.
    for (int i = 0; i < n; ++i) { L[i] = mixf(L[i], a.Process(L[i]) * 0.7f, p[5]); R[i] = mixf(R[i], b.Process(R[i]) * 0.7f, p[5]); }
  }
};
Fx* make_phaser(float sr) { return new PhaserFx(sr); }

// p: 0 rate Hz, 1 depth, 2 shape (0 sine, 1 triangle, 2 saw, 3 ramp, 4 square)
struct TremoloFx : Fx {
  daisysp::Tremolo a, b;
  TremoloFx(float sr) { a.Init(sr); b.Init(sr); }
  void process(float* L, float* R, int n) override {
    int sh = (int)p[2]; if (sh < 0) sh = 0; if (sh > 4) sh = 4;
    daisysp::Tremolo* fs[2] = {&a, &b};
    for (daisysp::Tremolo* f : fs) { f->SetFreq(p[0]); f->SetDepth(p[1]); f->SetWaveform(sh); }
    for (int i = 0; i < n; ++i) { L[i] = a.Process(L[i]); R[i] = b.Process(R[i]); }
  }
};
Fx* make_tremolo(float sr) { return new TremoloFx(sr); }

// p: 0 drive, 1 tone (0 dark – 1 open), 2 output, 3 mix
struct OverdriveFx : Fx {
  daisysp::Overdrive a, b; float la = 0.f, lb = 0.f;
  OverdriveFx() { a.Init(); b.Init(); }
  void process(float* L, float* R, int n) override {
    a.SetDrive(p[0]); b.SetDrive(p[0]);
    const float hz = 400.f * powf(50.f, p[1] < 0 ? 0 : p[1] > 1 ? 1 : p[1]), k = 1.f - expf(-6.2831853f * hz / sr), g = p[2] * 2.f;
    for (int i = 0; i < n; ++i) {
      la += (a.Process(L[i]) - la) * k; lb += (b.Process(R[i]) - lb) * k;
      L[i] = mixf(L[i], la * g, p[3]); R[i] = mixf(R[i], lb * g, p[3]);
    }
  }
};
Fx* make_overdrive(float) { return new OverdriveFx(); }

// p: 0 gain (1–20), 1 offset (−1–1), 2 output, 3 mix
struct FolderFx : Fx {
  daisysp::Wavefolder a, b; float dc[2] = {0, 0}, xp[2] = {0, 0};
  FolderFx() { a.Init(); b.Init(); }
  void process(float* L, float* R, int n) override {
    a.SetGain(p[0]); b.SetGain(p[0]); a.SetOffset(p[1]); b.SetOffset(p[1]);
    float* io[2] = {L, R}; daisysp::Wavefolder* fs[2] = {&a, &b};
    for (int c = 0; c < 2; ++c) for (int i = 0; i < n; ++i) {
      // the offset makes the fold asymmetric, which adds DC: a gentle high-pass takes it out
      const float y = fs[c]->Process(io[c][i]), hp = y - xp[c] + 0.995f * dc[c]; xp[c] = y; dc[c] = hp;
      io[c][i] = mixf(io[c][i], hp * p[2], p[3]);
    }
  }
};
Fx* make_folder(float) { return new FolderFx(); }

// p: 0 wah, 1 level, 2 mix
struct AutowahFx : Fx {
  daisysp::Autowah a, b;
  AutowahFx(float sr) { a.Init(sr); b.Init(sr); }
  void process(float* L, float* R, int n) override {
    daisysp::Autowah* fs[2] = {&a, &b};
    for (daisysp::Autowah* f : fs) { f->SetWah(p[0]); f->SetLevel(p[1]); f->SetDryWet(100.f); }
    for (int i = 0; i < n; ++i) { L[i] = mixf(L[i], a.Process(L[i]), p[2]); R[i] = mixf(R[i], b.Process(R[i]), p[2]); }
  }
};
Fx* make_autowah(float sr) { return new AutowahFx(sr); }

// p: 0 semitones (−24–24), 1 window ms (10–300), 2 spray (0–1), 3 mix
struct PitchFx : Fx {
  daisysp::PitchShifter a, b; float lastW = -1.f;
  PitchFx(float sr) { a.Init(sr); b.Init(sr); }
  void process(float* L, float* R, int n) override {
    a.SetTransposition(p[0]); b.SetTransposition(p[0]);
    if (p[1] != lastW) { lastW = p[1]; float w = p[1] < 10 ? 10 : p[1] > 300 ? 300 : p[1]; uint32_t sz = (uint32_t)(w / 1000.f * sr); a.SetDelSize(sz); b.SetDelSize(sz); }
    a.SetFun(p[2]); b.SetFun(p[2]);
    for (int i = 0; i < n; ++i) { L[i] = mixf(L[i], a.Process(L[i]), p[3]); R[i] = mixf(R[i], b.Process(R[i]), p[3]); }
  }
};
Fx* make_pitch(float sr) { return new PitchFx(sr); }

// p: 0 cutoff Hz, 1 resonance (0–1.8), 2 drive (0–4), 3 mode (0 LP24, 1 LP12, 2 BP24, 3 BP12, 4 HP24, 5 HP12), 4 mix
struct LadderFx : Fx {
  daisysp::LadderFilter a, b;
  LadderFx(float sr) { a.Init(sr); b.Init(sr); }
  void process(float* L, float* R, int n) override {
    int m = (int)p[3]; if (m < 0) m = 0; if (m > 5) m = 5;
    daisysp::LadderFilter* fs[2] = {&a, &b};
    for (daisysp::LadderFilter* f : fs) {
      f->SetFreq(p[0]); f->SetRes(p[1]); f->SetInputDrive(p[2] < 0.05f ? 0.05f : p[2]); f->SetPassbandGain(0.25f);
      f->SetFilterMode((daisysp::LadderFilter::FilterMode)m);
    }
    for (int i = 0; i < n; ++i) { L[i] = mixf(L[i], a.Process(L[i]), p[4]); R[i] = mixf(R[i], b.Process(R[i]), p[4]); }
  }
};
Fx* make_ladder(float sr) { return new LadderFx(sr); }
