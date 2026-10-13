// Airwindows Cabs (MIT, Chris Johnson) as a realtime track effect.
#include "Cabs/Cabs.h"
#include "tfx.h"
struct CabsFx : Fx {
  Cabs plug;
  CabsFx(float sr) : plug(0) { plug.setSampleRate(sr); }
  void process(float* L, float* R, int n) override {
    for (int k = 0; k < 6; ++k) plug.setParameter(k, p[k]);
    float* io[2] = {L, R};
    plug.processReplacing(io, io, n);
  }
};
Fx* make_cabs(float sr) { return new CabsFx(sr); }
