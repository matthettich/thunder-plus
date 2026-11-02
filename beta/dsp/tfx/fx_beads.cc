// Mutable Instruments Beads (MIT, Emilie Gillet) as a realtime track effect.
//
// Beads is a texture / granular synthesizer; its DSP runs internally at the sample rate of the chosen
// "media" (Digital 48k, Vintage 32k, tape 24k). We run the Digital media (48 kHz) and resample the host
// audio around it, as the Clouds effect does for 32 kHz.
//
// p: 0 media (0 Dirty tape, 1 Clean tape, 2 Vintage digital, 3 Digital), 2 freeze, 3 time, 4 size,
//    5 pitch, 6 shape, 7 density, 8 feedback, 9 dry/wet, 10 reverb. (1 and 11 unused.)
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "beads/dsp/processor.h"
#include "tfx.h"

struct BeadsFx : Fx {
  static const int BLK = 32, RING = 2048, BANK = 262144;   // 3 banks = 768 KB, the delay line Beads wants

  uint8_t* db = nullptr;                 // the delay memory, owned here (Beads keeps only pointers)
  uint16_t rvb[32768];                   // the reverb's own buffer
  beads::Processor proc;
  beads::FloatFrame cin[BLK], cfo[BLK];
  bool freeze[BLK];
  int media = -1, nin = 0;
  double step, ph = 0.0, rstep, rp = -64.0;
  float xp = 0.f, yl = 0.f, yr = 0.f;
  float fl[RING], fr[RING];
  long w = 0;

  BeadsFx(float sr_) {
    step = sr_ / 48000.0; rstep = 48000.0 / sr_;
    db = (uint8_t*)malloc(3 * BANK);
    memset(db, 0, 3 * BANK);
    memset(rvb, 0, sizeof(rvb));
    memset(fl, 0, sizeof(fl)); memset(fr, 0, sizeof(fr));
    uint8_t* banks[3] = { db, db + BANK, db + 2 * BANK };
    proc.Init(banks, rvb);
  }
  ~BeadsFx() { free(db); }

  void setup() {
    int md = (int)(p[0] + 0.5f); if (md < 0) md = 0; if (md > 3) md = 3;
    if (md != media) { media = md; proc.Configure((beads::Processor::Media)md, false); }
  }
  void block() {
    setup();
    beads::Parameters bp;
    bp.time.base = p[3];  bp.time.attenuverter = 1.f; bp.time.cv = 0.f;  bp.time.has_cv = false;
    bp.size.base = p[4];  bp.size.attenuverter = 1.f; bp.size.cv = 0.f;  bp.size.has_cv = false;
    bp.shape.base = p[6]; bp.shape.attenuverter = 1.f; bp.shape.cv = 0.f; bp.shape.has_cv = false;
    bp.pitch.base = p[5]; bp.pitch.attenuverter = 1.f; bp.pitch.cv = 0.f; bp.pitch.has_cv = false;
    bp.monophonic_mode = false;
    beads::ModulatedParameter density; density.base = p[7]; density.cv = 0.f;
    beads::Processor::MixParameters mx;
    mx.feedback = p[8]; mx.dry_wet = p[9]; mx.reverb_amount = p[10]; mx.output_gate = false;
    for (int i = 0; i < BLK; ++i) freeze[i] = p[2] > 0.5f;
    proc.Process(beads::SEED_BUTTON_STATE_ON, NULL, freeze, density, bp, mx, cin, cfo, BLK);
    for (int i = 0; i < BLK; ++i, ++w) { fl[w & (RING - 1)] = cfo[i].l; fr[w & (RING - 1)] = cfo[i].r; }
  }
  void push(float l, float r) {
    cin[nin].l = l; cin[nin].r = r;
    if (++nin < BLK) return;
    nin = 0; block();
  }
  void process(float* L, float* R, int n) override {
    for (int i = 0; i < n; ++i) {
      const float l = L[i], r = R[i];
      while (ph <= 1.0) { push(xp + (l - xp) * (float)ph, yl + (r - yl) * (float)ph); ph += step; }
      ph -= 1.0; xp = l; yl = r;
      float ol = 0.f, orr = 0.f;
      if (rp >= 0.0) {
        long j = (long)rp; if (j + 1 >= w) { j = w - 2; rp = (double)j; }
        if (j >= 0) { const float f = (float)(rp - j);
          ol = fl[j & (RING - 1)] + (fl[(j + 1) & (RING - 1)] - fl[j & (RING - 1)]) * f;
          orr = fr[j & (RING - 1)] + (fr[(j + 1) & (RING - 1)] - fr[j & (RING - 1)]) * f; }
      }
      rp += rstep;
      L[i] = ol; R[i] = orr;
    }
  }
};
Fx* make_beads(float sr) { return new BeadsFx(sr); }
