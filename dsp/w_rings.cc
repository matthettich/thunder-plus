// Rings (Mutable Instruments, MIT) as a resonator effect: pad audio excites the resonator.
#include <string.h>
#include "rings/dsp/part.h"
#include "rings/dsp/patch.h"
#include "rings/dsp/performance_state.h"

static uint16_t reverb_buffer[32768];
static rings::Part part;
static rings::Patch patch;
static rings::PerformanceState ps;
static float rin[4096], rout[4096], raux[4096];
static uint32_t rng_state = 1;

extern "C" {
__attribute__((export_name("rx_in"))) float* rx_in() { return rin; }
__attribute__((export_name("rx_out"))) float* rx_out() { return rout; }
__attribute__((export_name("rx_aux"))) float* rx_aux() { return raux; }
__attribute__((export_name("rx_init"))) void rx_init() {
  memset(reverb_buffer, 0, sizeof(reverb_buffer));
  part.Init(reverb_buffer);
  memset(&ps, 0, sizeof(ps));
}
__attribute__((export_name("rx_set"))) void rx_set(int model, int poly, float note,
    float structure, float brightness, float damping, float position) {
  part.set_model((rings::ResonatorModel)model);
  part.set_polyphony(poly);
  patch.structure = structure; patch.brightness = brightness;
  patch.damping = damping; patch.position = position;
  ps.internal_exciter = false; ps.internal_strum = false; ps.internal_note = false;
  ps.tonic = 0.0f; ps.note = note; ps.fm = 0.0f; ps.chord = 0;
}
// n: multiple of 24. strum: 1 on the first block.
__attribute__((export_name("rx_process"))) void rx_process(int n, int strum) {
  // Inaudible dither (about -180 dB) keeps the resonators out of slow denormal numbers.
  for (int i = 0; i < n; ++i) { rng_state = rng_state * 1664525u + 1013904223u; rin[i] += ((int32_t)rng_state) * 4.6e-19f; }
  for (int off = 0; off + 24 <= n; off += 24) {
    ps.strum = strum && off == 0;
    part.Process(ps, patch, rin + off, rout + off, raux + off, 24);
  }
}
}
