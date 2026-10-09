// Effect table and the exports the worklet calls.
#include <string.h>
#include <stdlib.h>
#include "tfx.h"

static Fx* slots[32];
static float io_l[128], io_r[128];

extern "C" {
__attribute__((export_name("tfx_l"))) float* tfx_l() { return io_l; }
__attribute__((export_name("tfx_r"))) float* tfx_r() { return io_r; }
// type: 0 rings, 1 clouds, 2 density, 3 tape, 4 chorus, 5 crush, 6 eq, 7 comp, 8 reverb, 9 delay
__attribute__((export_name("tfx_new"))) int tfx_new(int type, float sr) {
  int h = 0; while (h < 32 && slots[h]) h++;
  if (h == 32) return -1;
  Fx* f = 0;
  switch (type) {
    case 0: f = make_rings(sr); break;   case 1: f = make_clouds(sr); break;
    case 2: f = make_density(sr); break; case 3: f = make_totape(sr); break;
    case 4: f = make_chorus(sr); break;  case 5: f = make_crush(sr); break;
    case 6: f = make_eq(sr); break;      case 7: f = make_comp(sr); break;
    case 8: f = make_reverb(sr); break;  case 9: f = make_delay(sr); break;
  }
  if (!f) return -1;
  f->sr = sr;
  slots[h] = f; return h;
}
__attribute__((export_name("tfx_free"))) void tfx_free(int h) { if (h >= 0 && h < 32 && slots[h]) { delete slots[h]; slots[h] = 0; } }
__attribute__((export_name("tfx_params"))) float* tfx_params(int h) { return h >= 0 && h < 32 && slots[h] ? slots[h]->p : 0; }
__attribute__((export_name("tfx_process"))) void tfx_process(int h, int n) {
  if (h < 0 || h >= 32 || !slots[h] || n > 128) return;
  slots[h]->process(io_l, io_r, n);
  // keep a broken effect from poisoning the whole track
  for (int i = 0; i < n; ++i) {
    float l = io_l[i], r = io_r[i];
    if (!(l > -8.f && l < 8.f)) io_l[i] = 0.f;
    if (!(r > -8.f && r < 8.f)) io_r[i] = 0.f;
  }
}
}
