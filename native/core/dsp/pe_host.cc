// U25: see pe_host.h. The bodies are dsp/w_plaits.cc and dsp/w_elements.cc with their file statics moved into a Session.
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <new>

#include "plaits/dsp/voice.h"
#include "elements/dsp/part.h"
#ifndef __wasm__
#include "host_fx_state.h"
#endif
#include "pe_host.h"

namespace thunder_pe {
struct Session {   // no user-provided constructor: value-initialisation zeroes the whole object (session_new)
  // Plaits
  char shared_buffer[16384];
  plaits::Voice voice;
  plaits::Patch patch;
  plaits::Modulations mods;
  plaits::Voice::Frame frames[plaits::kBlockSize];
  float out_buf[4096];
  float aux_buf[4096];
  // Elements
  uint16_t reverb_buffer[32768];
  elements::Part part;
  elements::PerformanceState ps;
  float zeros[16];
  uint32_t dither_state;
  float emain[4096], eaux[4096];
  // stmlib::Random
  uint32_t rng;
};

namespace {
struct RngScope {   // the Session's Random state is the one the upstream code draws from while a call runs
#ifndef __wasm__
  uint32_t* prev;
  explicit RngScope(Session* s) : prev(thunder_host::stmlib_rng_cur()) { thunder_host::stmlib_rng_cur() = &s->rng; }
  ~RngScope() { thunder_host::stmlib_rng_cur() = prev; }
#else
  explicit RngScope(Session*) {}
#endif
};
}  // namespace

static Session* start(void* mem) {
  Session* s = new (mem) Session();   // value-initialisation: zero the whole object, then the members' empty constructors
  s->rng = 0x21;                      // stmlib::Random::rng_state_ at the start
  s->dither_state = 1;                // w_elements.cc: static uint32_t rng_state = 1
  return s;
}
Session* session_new() {
  void* mem = calloc(1, sizeof(Session));
  return mem ? start(mem) : nullptr;
}
void session_reset(Session* s) { s->~Session(); memset((void*)s, 0, sizeof(Session)); start(s); }
void session_free(Session* s) { if (s) { s->~Session(); free(s); } }

void pl_init(Session* s) {
  RngScope rs(s);
  stmlib::BufferAllocator allocator(s->shared_buffer, sizeof(s->shared_buffer));
  s->voice.Init(&allocator);
  memset(&s->patch, 0, sizeof(s->patch));
  memset(&s->mods, 0, sizeof(s->mods));
  s->patch.engine = 8;
  s->patch.note = 48.0f;
  s->patch.harmonics = 0.5f;
  s->patch.timbre = 0.5f;
  s->patch.morph = 0.5f;
  s->patch.decay = 0.5f;
  s->patch.lpg_colour = 0.5f;
}
void pl_set(Session* s, int engine, float note, float harmonics, float timbre, float morph, float decay, float lpg_colour, float fm_amt, float timbre_amt, float morph_amt, int strike) {
  plaits::Patch& p = s->patch;
  p.engine = engine; p.note = note; p.harmonics = harmonics; p.timbre = timbre; p.morph = morph; p.decay = decay; p.lpg_colour = lpg_colour;
  p.frequency_modulation_amount = fm_amt; p.timbre_modulation_amount = timbre_amt; p.morph_modulation_amount = morph_amt;
  s->mods.trigger_patched = strike != 0;
}
void pl_trigger(Session* s, float v) { s->mods.trigger = v; }
void pl_render(Session* s, int n) {
  RngScope rs(s);
  for (int off = 0; off + (int)plaits::kBlockSize <= n; off += plaits::kBlockSize) {
    s->voice.Render(s->patch, s->mods, s->frames, plaits::kBlockSize);
    for (size_t i = 0; i < plaits::kBlockSize; ++i) {
      s->out_buf[off + i] = s->frames[i].out / 32768.0f;
      s->aux_buf[off + i] = s->frames[i].aux / 32768.0f;
    }
  }
}
const float* pl_out(const Session* s) { return s->out_buf; }
const float* pl_aux(const Session* s) { return s->aux_buf; }

void el_init(Session* s) {
  RngScope rs(s);
  memset(s->reverb_buffer, 0, sizeof(s->reverb_buffer));
  s->part.Init(s->reverb_buffer);
  uint32_t seed[3] = {0x1234567u, 0x89abcdefu, 0x2468aceu};
  s->part.Seed(seed, 3);
  memset(&s->ps, 0, sizeof(s->ps));
  memset(s->zeros, 0, sizeof(s->zeros));
}
void el_set(Session* s, int model, float note, float strength, float contour, float bow, float blow, float strike, float bow_timbre, float flow, float blow_timbre, float mallet,
            float strike_timbre, float geometry, float brightness, float damping, float position, float space) {
  s->part.set_resonator_model((elements::ResonatorModel)model);
  elements::Patch* p = s->part.mutable_patch();
  p->exciter_envelope_shape = contour;
  p->exciter_bow_level = bow; p->exciter_blow_level = blow; p->exciter_strike_level = strike;
  p->exciter_bow_timbre = bow_timbre; p->exciter_blow_meta = flow; p->exciter_blow_timbre = blow_timbre;
  p->exciter_strike_meta = mallet; p->exciter_strike_timbre = strike_timbre;
  p->resonator_geometry = geometry; p->resonator_brightness = brightness;
  p->resonator_damping = damping; p->resonator_position = position; p->space = space;
  s->ps.note = note; s->ps.strength = strength; s->ps.modulation = 0.0f;
}
void el_gate(Session* s, int g) { s->ps.gate = g != 0; }
void el_process(Session* s, int n) {
  RngScope rs(s);
  for (int off = 0; off + 16 <= n; off += 16) {
    // Inaudible dither (about -180 dB) keeps the resonators out of denormal numbers (w_elements.cc dither())
    for (int i = 0; i < 16; ++i) { s->dither_state = s->dither_state * 1664525u + 1013904223u; s->zeros[i] = ((int32_t)s->dither_state) * 4.6e-19f; }
    s->part.Process(s->ps, s->zeros, s->zeros, s->emain + off, s->eaux + off, 16);
  }
}
const float* el_main(const Session* s) { return s->emain; }
const float* el_aux(const Session* s) { return s->eaux; }
}  // namespace thunder_pe
