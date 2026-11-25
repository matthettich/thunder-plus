// PFX: the page's pad Rings and pad Clouds (dsp/w_rings.cc, dsp/w_clouds.cc: rx_* and cl_*) as objects, made of the same Mutable Instruments sources the tfx effects use (built strict).
// Host builds only. Control-thread / offline: the objects allocate (Rings' reverb buffer, Clouds' 180 KB of working memory).
#pragma once
#include <cstddef>
namespace thunder_padfx {
struct Rings {                       // rx_init + rx_set + rx_process
  virtual ~Rings() {}
  virtual void set(int model, int poly, float note, float structure, float brightness, float damping, float position) = 0;
  virtual void process(float* io, int n, int strum) = 0;   // n a multiple of 24: io holds the input, the output replaces it (rx_in / rx_out)
  static Rings* make();
};
struct Clouds {                      // cl_init + cl_set + cl_process
  virtual ~Clouds() {}
  virtual void set(float position, float size, float pitch, float density, float texture, float dryWet, float spread, float feedback, float reverb, int freeze) = 0;
  virtual void process(const float* in, float* outL, float* outR, int n) = 0;   // n a multiple of 32
  static Clouds* make(int mode, int quality);
};
}  // namespace thunder_padfx
