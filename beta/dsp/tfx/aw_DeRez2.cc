// Airwindows DeRez2 (MIT, Chris Johnson) as a realtime track effect.
#include "DeRez2/DeRez2.h"
#include "tfx.h"
struct DeRez2Fx : Fx {
  DeRez2 plug;
  DeRez2Fx(float sr) : plug(0) { plug.setSampleRate(sr); }
  void process(float* L, float* R, int n) override {
    for (int k = 0; k < 4; ++k) plug.setParameter(k, p[k]);
    float* io[2] = {L, R};
    plug.processReplacing(io, io, n);
  }
};
Fx* make_derez2(float sr) { return new DeRez2Fx(sr); }
