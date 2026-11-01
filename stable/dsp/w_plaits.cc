// Thin C interface around Mutable Instruments Plaits (MIT license, Emilie Gillet)
// so ...Thunder can render Plaits engines in the browser.
#include <string.h>
#include "plaits/dsp/voice.h"

using namespace plaits;

static char shared_buffer[16384];
static Voice voice;
static Patch patch;
static Modulations mods;
static Voice::Frame frames[kBlockSize];
static float out_buf[4096];
static float aux_buf[4096];

extern "C" {

__attribute__((export_name("pl_init"))) void pl_init() {
  stmlib::BufferAllocator allocator(shared_buffer, sizeof(shared_buffer));
  voice.Init(&allocator);
  memset(&patch, 0, sizeof(patch));
  memset(&mods, 0, sizeof(mods));
  patch.engine = 8;
  patch.note = 48.0f;
  patch.harmonics = 0.5f;
  patch.timbre = 0.5f;
  patch.morph = 0.5f;
  patch.decay = 0.5f;
  patch.lpg_colour = 0.5f;
}

__attribute__((export_name("pl_out"))) float* pl_out() { return out_buf; }
__attribute__((export_name("pl_aux"))) float* pl_aux() { return aux_buf; }

// engine 0-23; note in MIDI semitones; strike != 0 patches the trigger input
// (engine envelopes + low-pass gate), otherwise the voice drones.
__attribute__((export_name("pl_set"))) void pl_set(int engine, float note,
    float harmonics, float timbre, float morph, float decay, float lpg_colour,
    float fm_amt, float timbre_amt, float morph_amt, int strike) {
  patch.engine = engine;
  patch.note = note;
  patch.harmonics = harmonics;
  patch.timbre = timbre;
  patch.morph = morph;
  patch.decay = decay;
  patch.lpg_colour = lpg_colour;
  patch.frequency_modulation_amount = fm_amt;
  patch.timbre_modulation_amount = timbre_amt;
  patch.morph_modulation_amount = morph_amt;
  mods.trigger_patched = strike != 0;
}

__attribute__((export_name("pl_trigger"))) void pl_trigger(float v) { mods.trigger = v; }

// Renders n samples (n a multiple of 12, at most 4096) at 48 kHz.
__attribute__((export_name("pl_render"))) void pl_render(int n) {
  for (int off = 0; off + (int)kBlockSize <= n; off += kBlockSize) {
    voice.Render(patch, mods, frames, kBlockSize);
    for (size_t i = 0; i < kBlockSize; ++i) {
      out_buf[off + i] = frames[i].out / 32768.0f;
      aux_buf[off + i] = frames[i].aux / 32768.0f;
    }
  }
}

}
