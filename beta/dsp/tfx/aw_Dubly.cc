// Airwindows Dubly (MIT, Chris Johnson) as a realtime track effect.
#include "Dubly/Dubly.h"
#include "tfx.h"
struct DublyFx : Fx {
  Dubly plug;
  DublyFx(float sr) : plug(0) { plug.setSampleRate(sr); }
  void process(float* L, float* R, int n) override {
    for (int k = 0; k < 2; ++k) plug.setParameter(k, p[k]);
    float* io[2] = {L, R};
    plug.processReplacing(io, io, n);
  }
};
Fx* make_dubly(float sr) { return new DublyFx(sr); }
