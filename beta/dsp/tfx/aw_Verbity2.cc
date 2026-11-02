// Airwindows Verbity2 (MIT, Chris Johnson) as a realtime track effect.
#include "Verbity2/Verbity2.h"
#include "tfx.h"
struct Verbity2Fx : Fx {
  Verbity2 plug;
  Verbity2Fx(float sr) : plug(0) { plug.setSampleRate(sr); }
  void process(float* L, float* R, int n) override {
    for (int k = 0; k < 4; ++k) plug.setParameter(k, p[k]);
    float* io[2] = {L, R};
    plug.processReplacing(io, io, n);
  }
};
Fx* make_verbity2(float sr) { return new Verbity2Fx(sr); }
