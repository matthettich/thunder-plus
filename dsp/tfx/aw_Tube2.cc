// Airwindows Tube2 (MIT, Chris Johnson) as a realtime track effect.
#include "Tube2/Tube2.h"
#include "tfx.h"
struct Tube2Fx : Fx {
  Tube2 plug;
  Tube2Fx(float sr) : plug(0) { plug.setSampleRate(sr); }
  void process(float* L, float* R, int n) override {
    for (int k = 0; k < 2; ++k) plug.setParameter(k, p[k]);
    float* io[2] = {L, R};
    plug.processReplacing(io, io, n);
  }
};
Fx* make_tube2(float sr) { return new Tube2Fx(sr); }
