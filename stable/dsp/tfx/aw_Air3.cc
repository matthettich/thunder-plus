// Airwindows Air3 (MIT, Chris Johnson) as a realtime track effect.
#include "Air3/Air3.h"
#include "tfx.h"
struct Air3Fx : Fx {
  Air3 plug;
  Air3Fx(float sr) : plug(0) { plug.setSampleRate(sr); }
  void process(float* L, float* R, int n) override {
    for (int k = 0; k < 2; ++k) plug.setParameter(k, p[k]);
    float* io[2] = {L, R};
    plug.processReplacing(io, io, n);
  }
};
Fx* make_air3(float sr) { return new Air3Fx(sr); }
