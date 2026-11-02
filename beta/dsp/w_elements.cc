// Elements (Mutable Instruments, MIT): modal synthesis voice with bow, blow and strike exciters.
#include <string.h>
#include "elements/dsp/part.h"

static uint16_t reverb_buffer[32768];
static elements::Part part;
static elements::PerformanceState ps;
static float zeros[16];
static uint32_t rng_state = 1;
// Inaudible dither (about -180 dB) keeps the resonators out of denormal numbers, which are very slow on desktop and phone CPUs.
static void dither() { for (int i = 0; i < 16; ++i) { rng_state = rng_state * 1664525u + 1013904223u; zeros[i] = ((int32_t)rng_state) * 4.6e-19f; } }
static float emain[4096], eaux[4096];

extern "C" {
__attribute__((export_name("el_main"))) float* el_main() { return emain; }
__attribute__((export_name("el_aux"))) float* el_aux() { return eaux; }
__attribute__((export_name("el_init"))) void el_init() {
  memset(reverb_buffer, 0, sizeof(reverb_buffer));
  part.Init(reverb_buffer);
  uint32_t seed[3] = {0x1234567u, 0x89abcdefu, 0x2468aceu};
  part.Seed(seed, 3);
  memset(&ps, 0, sizeof(ps));
  memset(zeros, 0, sizeof(zeros));
}
__attribute__((export_name("el_set"))) void el_set(int model, float note, float strength,
    float contour, float bow, float blow, float strike,
    float bow_timbre, float flow, float blow_timbre, float mallet, float strike_timbre,
    float geometry, float brightness, float damping, float position, float space) {
  part.set_resonator_model((elements::ResonatorModel)model);
  elements::Patch* p = part.mutable_patch();
  p->exciter_envelope_shape = contour;
  p->exciter_bow_level = bow; p->exciter_blow_level = blow; p->exciter_strike_level = strike;
  p->exciter_bow_timbre = bow_timbre; p->exciter_blow_meta = flow; p->exciter_blow_timbre = blow_timbre;
  p->exciter_strike_meta = mallet; p->exciter_strike_timbre = strike_timbre;
  p->resonator_geometry = geometry; p->resonator_brightness = brightness;
  p->resonator_damping = damping; p->resonator_position = position; p->space = space;
  ps.note = note; ps.strength = strength; ps.modulation = 0.0f;
}
__attribute__((export_name("el_gate"))) void el_gate(int g) { ps.gate = g != 0; }
// n: multiple of 16.
__attribute__((export_name("el_process"))) void el_process(int n) {
  for (int off = 0; off + 16 <= n; off += 16) {
    dither();
    part.Process(ps, zeros, zeros, emain + off, eaux + off, 16);
  }
}
}
