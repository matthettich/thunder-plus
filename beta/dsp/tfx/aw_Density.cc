// Airwindows Density (MIT, Chris Johnson) as a realtime track effect.
#include "Density/Density.h"
#include "tfx.h"
struct DensityFx : Fx {
  Density plug; float dl[128], dr[128];
  DensityFx(float sr) : plug(0) { plug.setSampleRate(sr); }
  void process(float* L, float* R, int n) override {
    for (int k = 0; k < 4; ++k) plug.setParameter(k, p[k]);

    float* io[2] = {L, R};
    plug.processReplacing(io, io, n);

  }
};
Fx* make_density(float sr) { return new DensityFx(sr); }
