// PFX: see padfx_mi.h. Line for line from dsp/w_rings.cc and dsp/w_clouds.cc with the statics made members (the page keeps them in one wasm instance).
#include <stdint.h>
#include <string.h>
#include "padfx_mi.h"
#include "rings/dsp/part.h"
#include "rings/dsp/patch.h"
#include "rings/dsp/performance_state.h"
#include "clouds/dsp/granular_processor.h"

#include <new>
namespace thunder_padfx {
namespace {
// the page's statics start as zeroed wasm memory: these objects do too (the tfx Fx objects have the same operator new)
struct Zeroed {
  static void* operator new(size_t n) { void* q = ::operator new(n); memset(q, 0, n); return q; }
  static void operator delete(void* q) { ::operator delete(q); }
};
struct RingsImpl : Rings, Zeroed {
  uint16_t reverb_buffer[32768];
  rings::Part part; rings::Patch patch; rings::PerformanceState ps;
  float raux[4096];
  uint32_t rng_state = 1;
  RingsImpl() { memset(reverb_buffer, 0, sizeof(reverb_buffer)); part.Init(reverb_buffer); memset(&ps, 0, sizeof(ps)); memset(raux, 0, sizeof(raux)); memset(&patch, 0, sizeof(patch)); }
  void set(int model, int poly, float note, float structure, float brightness, float damping, float position) override {
    part.set_model((rings::ResonatorModel)model);
    part.set_polyphony(poly);
    patch.structure = structure; patch.brightness = brightness;
    patch.damping = damping; patch.position = position;
    ps.internal_exciter = false; ps.internal_strum = false; ps.internal_note = false;
    ps.tonic = 0.0f; ps.note = note; ps.fm = 0.0f; ps.chord = 0;
  }
  void process(float* io, int n, int strum) override {
    for (int i = 0; i < n; ++i) { rng_state = rng_state * 1664525u + 1013904223u; io[i] += ((int32_t)rng_state) * 4.6e-19f; }
    static thread_local float out[4096];
    for (int off = 0; off + 24 <= n; off += 24) {
      ps.strum = strum && off == 0;
      part.Process(ps, patch, io + off, out + off, raux + off, 24);
    }
    memcpy(io, out, sizeof(float) * (size_t)n);
  }
};
struct CloudsImpl : Clouds, Zeroed {
  uint8_t block_mem[118784];
  uint8_t block_ccm[65536 - 128];
  clouds::GranularProcessor proc;
  clouds::ShortFrame cin[32], cout[32];
  bool first_block = true;
  CloudsImpl(int mode, int quality) {
    memset(block_mem, 0, sizeof(block_mem)); memset(block_ccm, 0, sizeof(block_ccm)); memset(cin, 0, sizeof(cin)); memset(cout, 0, sizeof(cout));
    proc.Init(block_mem, sizeof(block_mem), block_ccm, sizeof(block_ccm));
    proc.set_playback_mode((clouds::PlaybackMode)mode);
    proc.set_quality(quality);
    proc.Prepare();
  }
  void set(float position, float size, float pitch, float density, float texture, float dry_wet, float spread, float feedback, float reverb, int freeze) override {
    clouds::Parameters* p = proc.mutable_parameters();
    p->position = position; p->size = size; p->pitch = pitch; p->density = density;
    p->texture = texture; p->dry_wet = dry_wet; p->stereo_spread = spread;
    p->feedback = feedback; p->reverb = reverb; p->freeze = freeze != 0;
    p->trigger = false; p->gate = false;
  }
  void process(const float* in, float* outL, float* outR, int n) override {
    for (int off = 0; off + 32 <= n; off += 32) {
      for (int i = 0; i < 32; ++i) {
        float v = in[off + i] * 32767.0f;
        if (v > 32767.0f) v = 32767.0f; if (v < -32768.0f) v = -32768.0f;
        cin[i].l = cin[i].r = (short)v;
      }
      proc.mutable_parameters()->trigger = first_block; first_block = false;
      proc.Process(cin, cout, 32);
      for (int k = 0; k < 24; ++k) proc.Prepare();
      for (int i = 0; i < 32; ++i) { outL[off + i] = cout[i].l / 32768.0f; outR[off + i] = cout[i].r / 32768.0f; }
    }
  }
};
}  // namespace
Rings* Rings::make() { return new RingsImpl; }
Clouds* Clouds::make(int mode, int quality) { return new CloudsImpl(mode, quality); }
}  // namespace thunder_padfx
