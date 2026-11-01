// Mutable Instruments Rings and Clouds (MIT, Emilie Gillet) as realtime track effects.
#include <string.h>
#include <math.h>
#include "rings/dsp/part.h"
#include "rings/dsp/patch.h"
#include "rings/dsp/performance_state.h"
#include "clouds/dsp/granular_processor.h"
#include "tfx.h"

// p: 0 model, 1 poly, 2 note, 3 structure, 4 brightness, 5 damping, 6 position, 7 input, 8 mix
struct RingsFx : Fx {
  uint16_t reverb[32768];
  rings::Part part; rings::Patch patch; rings::PerformanceState ps;
  float in[24], out[24], aux[24], qo[24], qa[24];
  int idx = 0, model = -1, poly = -1; bool first = true; uint32_t rng = 1;
  RingsFx() {
    memset(reverb, 0, sizeof(reverb)); part.Init(reverb); memset(&ps, 0, sizeof(ps));
    memset(qo, 0, sizeof(qo)); memset(qa, 0, sizeof(qa));
  }
  void block() {
    // Both setters reset the resonators, so only call them on a real change.
    const int md = (int)p[0], po = (int)p[1];
    if (md != model) { model = md; part.set_model((rings::ResonatorModel)md); }
    if (po != poly) { poly = po; part.set_polyphony(po); }
    patch.structure = p[3]; patch.brightness = p[4]; patch.damping = p[5]; patch.position = p[6];
    ps.internal_exciter = false; ps.internal_strum = false; ps.internal_note = false;
    // Rings runs at 48 kHz; shift the note so it stays in tune at other rates.
    ps.tonic = 0.f; ps.note = p[2] + 12.f * log2f(48000.f / sr); ps.fm = 0.f; ps.chord = 0;
    ps.strum = first; first = false;
    for (int i = 0; i < 24; ++i) { rng = rng * 1664525u + 1013904223u; in[i] += ((int32_t)rng) * 4.6e-19f; }
    part.Process(ps, patch, in, out, aux, 24);
    memcpy(qo, out, sizeof(qo)); memcpy(qa, aux, sizeof(qa));
  }
  void process(float* L, float* R, int n) override {
    const float g = p[7], m = p[8];
    for (int i = 0; i < n; ++i) {
      in[idx] = (L[i] + R[i]) * 0.5f * g;
      const float wl = qo[idx], wr = qa[idx];
      if (++idx == 24) { idx = 0; block(); }
      L[i] = L[i] * (1.f - m) + wl * m; R[i] = R[i] * (1.f - m) + wr * m;
    }
  }
};
Fx* make_rings(float) { return new RingsFx(); }

// p: 0 mode, 1 lofi, 2 freeze, 3 position, 4 size, 5 pitch, 6 density, 7 texture, 8 blend, 9 feedback, 10 reverb, 11 spread
// Clouds runs at 32 kHz, so audio is resampled in and out around it.
struct CloudsFx : Fx {
  uint8_t mem[118784]; uint8_t ccm[65536 - 128];
  clouds::GranularProcessor proc;
  clouds::ShortFrame cin[32], cout[32];
  int mode = -1, quality = -1, nin = 0; bool first = true;
  double step, ph = 0.0, rstep, rp = -64.0; float xp = 0.f;
  float fl[1024], fr[1024]; long w = 0;
  CloudsFx(float sr_) {
    step = sr_ / 32000.0; rstep = 32000.0 / sr_;
    memset(mem, 0, sizeof(mem)); memset(ccm, 0, sizeof(ccm)); memset(fl, 0, sizeof(fl)); memset(fr, 0, sizeof(fr));
    proc.Init(mem, sizeof(mem), ccm, sizeof(ccm));
  }
  void setup() {
    int md = (int)p[0], q = p[1] > 0.5f ? 3 : 1;
    if (md != mode || q != quality) { mode = md; quality = q; proc.set_playback_mode((clouds::PlaybackMode)md); proc.set_quality(q); proc.Prepare(); }
    clouds::Parameters* P = proc.mutable_parameters();
    P->position = p[3]; P->size = p[4]; P->pitch = p[5]; P->density = p[6]; P->texture = p[7];
    P->dry_wet = p[8]; P->feedback = p[9]; P->reverb = p[10]; P->stereo_spread = p[11];
    P->freeze = p[2] > 0.5f; P->trigger = first; P->gate = false; first = false;
  }
  void push(float x) {
    float v = x * 32767.f; if (v > 32767.f) v = 32767.f; if (v < -32768.f) v = -32768.f;
    cin[nin].l = cin[nin].r = (short)v;
    if (++nin < 32) return;
    nin = 0; setup();
    proc.Process(cin, cout, 32);
    const int prep = mode == 1 ? 24 : 2;
    for (int k = 0; k < prep; ++k) proc.Prepare();
    for (int i = 0; i < 32; ++i, ++w) { fl[w & 1023] = cout[i].l / 32768.f; fr[w & 1023] = cout[i].r / 32768.f; }
  }
  void process(float* L, float* R, int n) override {
    for (int i = 0; i < n; ++i) {
      const float x = (L[i] + R[i]) * 0.5f;
      while (ph <= 1.0) { push(xp + (x - xp) * (float)ph); ph += step; }
      ph -= 1.0; xp = x;
      float ol = 0.f, orr = 0.f;
      if (rp >= 0.0) {
        long j = (long)rp; if (j + 1 >= w) { j = w - 2; rp = (double)j; }
        if (j >= 0) { const float f = (float)(rp - j);
          ol = fl[j & 1023] + (fl[(j + 1) & 1023] - fl[j & 1023]) * f;
          orr = fr[j & 1023] + (fr[(j + 1) & 1023] - fr[j & 1023]) * f; }
      }
      rp += rstep;
      L[i] = ol; R[i] = orr;
    }
  }
};
Fx* make_clouds(float sr) { return new CloudsFx(sr); }
