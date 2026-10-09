// ...Thunder track effects: a small realtime effect host compiled to WebAssembly and run in an
// AudioWorklet, one instance per tracker track (and one on the master). Every effect is an
// object, so a track can hold several of the same kind.
#pragma once
struct Fx {
  float p[12];                 // parameters, written by the worklet straight into wasm memory
  float sr = 48000.f;
  Fx() { for (int i = 0; i < 12; ++i) p[i] = 0.f; }
  virtual ~Fx() {}
  virtual void process(float* L, float* R, int n) = 0;   // stereo, in place, n <= 128
};
Fx* make_rings(float sr);
Fx* make_clouds(float sr);
Fx* make_density(float sr);
Fx* make_totape(float sr);
Fx* make_chorus(float sr);
Fx* make_crush(float sr);
Fx* make_eq(float sr);
Fx* make_comp(float sr);
Fx* make_reverb(float sr);
Fx* make_delay(float sr);
