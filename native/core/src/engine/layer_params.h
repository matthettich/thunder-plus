// U23: the freshLayer fields of the layer types click, wavetable, pd, chip, supersaw, pwm, sample (with its loop and the vintage modes) and grains. Included by engine.h (EngLayer::u23).
// click, pd, chip and supersaw read only a / b; the rest of the fields are the wavetable's (wt*), the sample's (vint*, lp*) and the sample itself. THE SAMPLE IS THE CALLER'S: a layer
// points at float data it does not own (the JS layer's `sample.data`, a Float32Array, and `sample.sr`); nothing here copies or frees it. A null / empty sample is a layer that is silent
// (sample, grains) or plays the built-in cymbal (pwm), as in the JS.
#ifndef THUNDER_ENGINE_LAYER_PARAMS_H
#define THUNDER_ENGINE_LAYER_PARAMS_H
#include <cstddef>

namespace thunder {
struct EngU23 {
  double wtBend = 0, wtUni = 1, wtSpread = 0.3, wtSub = 0, wtNoise = 0;       // wavetable
  double vint = 0, vintF = 0.5;                                                  // sample: the vintage sampler mode (0 = clean) and its colour
  double lpMode = 0, lpS = 0, lpE = 1, lpX = 0.05;                               // sample: loop mode (0 none, 1 forward with crossfade, else ping-pong), start, end, crossfade
  const float* smp = nullptr; size_t smpLen = 0; double smpSr = 48000;          // L.sample.data / .sr (not owned)
};
}  // namespace thunder
#endif
