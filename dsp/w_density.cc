// Airwindows Density (MIT, Chris Johnson), wrapped for ...Thunder's per-pad effects chain.
#include <string.h>
#define private public
#include "Density/Density.h"
#undef private
extern float fin[4096], fl[4096], fr[4096];
static Density* fxo = 0;
extern "C" __attribute__((export_name("aw_density"))) void aw_density(int reset, float sr, float a, float b, float c, float d, int n) {
  if (!fxo || reset) { delete fxo; fxo = new Density(0); }
  fxo->setSampleRate(sr);
  fxo->A = a; fxo->B = b; fxo->C = c; fxo->D = d;
  float* ins[2] = {fin, fin}; float* outs[2] = {fl, fr};
  fxo->processReplacing(ins, outs, n);
}
