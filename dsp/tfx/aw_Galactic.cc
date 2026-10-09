// Airwindows Galactic (MIT, Chris Johnson) as a realtime track effect.
#include "Galactic/Galactic.h"
#include "tfx.h"
struct GalacticFx : Fx {
  Galactic plug; float dl[128], dr[128];
  GalacticFx(float sr) : plug(0) { plug.setSampleRate(sr); }
  void process(float* L, float* R, int n) override {
    for (int k = 0; k < 5; ++k) plug.setParameter(k, p[k]);

    float* io[2] = {L, R};
    plug.processReplacing(io, io, n);

  }
};
Fx* make_reverb(float sr) { return new GalacticFx(sr); }
