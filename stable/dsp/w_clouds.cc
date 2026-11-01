// Clouds (Mutable Instruments, MIT): granular, stretch, looping delay and spectral texture.
#include <string.h>
#include "clouds/dsp/granular_processor.h"

static uint8_t block_mem[118784];
static uint8_t block_ccm[65536 - 128];
static clouds::GranularProcessor proc;
static clouds::ShortFrame cin[32], cout[32];
static float ci[4096], cl[4096], cr[4096];
static bool first_block = true;

extern "C" {
__attribute__((export_name("cl_in"))) float* cl_in() { return ci; }
__attribute__((export_name("cl_l"))) float* cl_l() { return cl; }
__attribute__((export_name("cl_r"))) float* cl_r() { return cr; }
__attribute__((export_name("cl_init"))) void cl_init(int mode, int quality) {
  memset(block_mem, 0, sizeof(block_mem)); memset(block_ccm, 0, sizeof(block_ccm));
  proc.Init(block_mem, sizeof(block_mem), block_ccm, sizeof(block_ccm));
  proc.set_playback_mode((clouds::PlaybackMode)mode);
  proc.set_quality(quality);
  proc.Prepare();
  first_block = true;
}
__attribute__((export_name("cl_set"))) void cl_set(float position, float size, float pitch,
    float density, float texture, float dry_wet, float spread, float feedback, float reverb, int freeze) {
  clouds::Parameters* p = proc.mutable_parameters();
  p->position = position; p->size = size; p->pitch = pitch; p->density = density;
  p->texture = texture; p->dry_wet = dry_wet; p->stereo_spread = spread;
  p->feedback = feedback; p->reverb = reverb; p->freeze = freeze != 0;
  p->trigger = false; p->gate = false;
}
// n: multiple of 32. Mono in, stereo out.
__attribute__((export_name("cl_process"))) void cl_process(int n) {
  for (int off = 0; off + 32 <= n; off += 32) {
    for (int i = 0; i < 32; ++i) {
      float v = ci[off + i] * 32767.0f;
      if (v > 32767.0f) v = 32767.0f; if (v < -32768.0f) v = -32768.0f;
      cin[i].l = cin[i].r = (short)v;
    }
    // Stretch mode follows the TRIG input to know where the sound starts.
    proc.mutable_parameters()->trigger = first_block; first_block = false;
    proc.Process(cin, cout, 32);
    // The module calls Prepare() many times between audio blocks; Stretch mode's
    // splice search (the correlator) needs those extra passes to find candidates.
    for (int k = 0; k < 24; ++k) proc.Prepare();
    for (int i = 0; i < 32; ++i) { cl[off + i] = cout[i].l / 32768.0f; cr[off + i] = cout[i].r / 32768.0f; }
  }
}
}
