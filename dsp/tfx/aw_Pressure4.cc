// Airwindows Pressure4 (MIT, Chris Johnson) as a realtime track effect.
#include "Pressure4/Pressure4.h"
#include "tfx.h"
struct Pressure4Fx : Fx {
  Pressure4 plug; float dl[128], dr[128];
  Pressure4Fx(float sr) : plug(0) { plug.setSampleRate(sr); }
  void process(float* L, float* R, int n) override {
    for (int k = 0; k < 4; ++k) plug.setParameter(k, p[k]);
    for (int i = 0; i < n; ++i) { dl[i] = L[i]; dr[i] = R[i]; }
    float* io[2] = {L, R};
    plug.processReplacing(io, io, n);
    const float m = p[4]; if (m < 1.f) for (int i = 0; i < n; ++i) { L[i] = dl[i] + (L[i] - dl[i]) * m; R[i] = dr[i] + (R[i] - dr[i]) * m; }
  }
};
Fx* make_comp(float sr) { return new Pressure4Fx(sr); }
