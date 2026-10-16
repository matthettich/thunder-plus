// ...Thunder modular modules: Mutable Instruments Marbles and Tides (2) as …chains / …waves modules, compiled to
// WebAssembly. One copy of the module per JS realm (audio thread, worker); every module made in it is an object here,
// found by a handle, so several Random generators and Function generators share one small memory.
#pragma once
#include <stdlib.h>
#include <string.h>

const int kModBlock = 128;          // the engines' block
const int kModInputs = 4;           // gate/CV inputs, written by JS before mods_process
const int kModOutputs = 8;          // gate/CV outputs, read by JS after it

struct Mod {
  // Zeroed memory, as on the hardware (where everything is a static).
  static void* operator new(size_t n) { void* q = malloc(n); if (q) memset(q, 0, n); return q; }
  static void operator delete(void* q) { free(q); }
  float p[32];                      // parameters, written by JS straight into wasm memory
  float sr = 48000.f;
  Mod() { for (int i = 0; i < 32; ++i) p[i] = 0.f; }
  virtual ~Mod() {}
  // in[k][i] / out[k][i]: k = port, i < n <= 128
  virtual void process(float (*in)[kModBlock], float (*out)[kModBlock], int n) = 0;
};

Mod* make_marbles(float sr, unsigned seed);
Mod* make_tides(float sr);
