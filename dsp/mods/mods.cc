// Module table and the exports the worklet calls (in the style of tfx.cc).
#include <string.h>
#include <stdlib.h>
#include "mods.h"

static const int kSlots = 64;
static Mod* slots[kSlots];
static float io_in[kModInputs][kModBlock], io_out[kModOutputs][kModBlock];

extern "C" {
__attribute__((export_name("mods_in"))) float* mods_in() { return &io_in[0][0]; }
__attribute__((export_name("mods_out"))) float* mods_out() { return &io_out[0][0]; }
// type: 0 random generator (Marbles), 1 function generator (Tides 2). seed: the random generator's seed.
__attribute__((export_name("mods_new"))) int mods_new(int type, float sr, unsigned seed) {
  int h = 0; while (h < kSlots && slots[h]) h++;
  if (h == kSlots || !(sr > 1000.f && sr < 400000.f)) return -1;
  Mod* m = 0;
  switch (type) {
    case 0: m = make_marbles(sr, seed); break;
    case 1: m = make_tides(sr); break;
  }
  if (!m) return -1;
  m->sr = sr;
  slots[h] = m; return h;
}
__attribute__((export_name("mods_free"))) void mods_free(int h) { if (h >= 0 && h < kSlots && slots[h]) { delete slots[h]; slots[h] = 0; } }
__attribute__((export_name("mods_params"))) float* mods_params(int h) { return h >= 0 && h < kSlots && slots[h] ? slots[h]->p : 0; }
// Returns 1 when the outputs are good, 0 when a bad value had to be cleared (JS then starts the module afresh).
__attribute__((export_name("mods_process"))) int mods_process(int h, int n) {
  if (h < 0 || h >= kSlots || !slots[h] || n < 1 || n > kModBlock) return 0;
  for (int k = 0; k < kModInputs; ++k)
    for (int i = 0; i < n; ++i) { float v = io_in[k][i]; if (!(v > -64.f && v < 64.f)) io_in[k][i] = 0.f; }
  slots[h]->process(io_in, io_out, n);
  int ok = 1;
  for (int k = 0; k < kModOutputs; ++k)
    for (int i = 0; i < n; ++i) { float v = io_out[k][i]; if (!(v > -16.f && v < 16.f)) { io_out[k][i] = 0.f; ok = 0; } }
  return ok;
}
}
