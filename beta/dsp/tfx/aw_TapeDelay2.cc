// Airwindows TapeDelay2 (MIT, Chris Johnson) as a realtime track effect.
#include "TapeDelay2/TapeDelay2.h"
#include "tfx.h"
struct TapeDelay2Fx : Fx {
  TapeDelay2 plug; float dl[128], dr[128];
  TapeDelay2Fx(float sr) : plug(0) { plug.setSampleRate(sr); }
  void process(float* L, float* R, int n) override {
    for (int k = 0; k < 6; ++k) plug.setParameter(k, p[k]);

    float* io[2] = {L, R};
    plug.processReplacing(io, io, n);

  }
};
Fx* make_delay(float sr) { return new TapeDelay2Fx(sr); }
