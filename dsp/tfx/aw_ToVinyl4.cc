// Airwindows ToVinyl4 (MIT, Chris Johnson) as a realtime track effect.
#include "ToVinyl4/ToVinyl4.h"
#include "tfx.h"
struct ToVinyl4Fx : Fx {
  ToVinyl4 plug;
  ToVinyl4Fx(float sr) : plug(0) { plug.setSampleRate(sr); }
  void process(float* L, float* R, int n) override {
    for (int k = 0; k < 4; ++k) plug.setParameter(k, p[k]);
    float* io[2] = {L, R};
    plug.processReplacing(io, io, n);
  }
};
Fx* make_tovinyl4(float sr) { return new ToVinyl4Fx(sr); }
