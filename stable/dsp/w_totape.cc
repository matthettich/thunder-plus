// Airwindows ToTape6 (MIT, Chris Johnson), wrapped for ...Thunder's per-pad effects chain.
#include <string.h>
#define private public
#include "ToTape6/ToTape6.h"
#undef private
extern float fin[4096], fl[4096], fr[4096];
static ToTape6* fxo = 0;
extern "C" __attribute__((export_name("aw_totape"))) void aw_totape(int reset, float sr, float a, float b, float c, float d, float e, float f, int n) {
  if (!fxo || reset) { delete fxo; fxo = new ToTape6(0); }
  fxo->setSampleRate(sr);
  fxo->A = a; fxo->B = b; fxo->C = c; fxo->D = d; fxo->E = e; fxo->F = f;
  float* ins[2] = {fin, fin}; float* outs[2] = {fl, fr};
  fxo->processReplacing(ins, outs, n);
}
