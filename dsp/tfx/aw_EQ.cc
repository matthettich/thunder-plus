// Airwindows EQ (MIT, Chris Johnson) as a realtime track effect.
#include "EQ/EQ.h"
#include "tfx.h"
struct EQFx : Fx {
  EQ plug; float dl[128], dr[128];
  EQFx(float sr) : plug(0) { plug.setSampleRate(sr); }
  void process(float* L, float* R, int n) override {
    for (int k = 0; k < 8; ++k) plug.setParameter(k, p[k]);

    float* io[2] = {L, R};
    plug.processReplacing(io, io, n);

  }
};
Fx* make_eq(float sr) { return new EQFx(sr); }
