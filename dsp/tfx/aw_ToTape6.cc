// Airwindows ToTape6 (MIT, Chris Johnson) as a realtime track effect.
#include "ToTape6/ToTape6.h"
#include "tfx.h"
struct ToTape6Fx : Fx {
  ToTape6 plug; float dl[128], dr[128];
  ToTape6Fx(float sr) : plug(0) { plug.setSampleRate(sr); }
  void process(float* L, float* R, int n) override {
    for (int k = 0; k < 6; ++k) plug.setParameter(k, p[k]);

    float* io[2] = {L, R};
    plug.processReplacing(io, io, n);

  }
};
Fx* make_totape(float sr) { return new ToTape6Fx(sr); }
