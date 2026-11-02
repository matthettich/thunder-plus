// Airwindows PurestDrive (MIT, Chris Johnson) as a realtime track effect.
#include "PurestDrive/PurestDrive.h"
#include "tfx.h"
struct PurestDriveFx : Fx {
  PurestDrive plug;
  PurestDriveFx(float sr) : plug(0) { plug.setSampleRate(sr); }
  void process(float* L, float* R, int n) override {
    for (int k = 0; k < 1; ++k) plug.setParameter(k, p[k]);
    float* io[2] = {L, R};
    plug.processReplacing(io, io, n);
  }
};
Fx* make_purestdrive(float sr) { return new PurestDriveFx(sr); }
