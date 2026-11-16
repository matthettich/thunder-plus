// U19: the shared MODFX module block of ...chains and ...waves (chains/chains-worklet.js, between ===SHARED MODULES START=== and END===, lines 719 to 2078: the control / CV
// utilities, filters, stereo and modulation effects, the dynamics, the delays and BBD, the reverbs, the ensemble), module by module, line for line from the JS.
// THE JS IS THE SPECIFICATION, quirks included (see U19-NOTES.md). The same rules as clkcores.h / idmcores.h:
//   - every JS Number is a double, in the JS's own arithmetic order (the core is built with floating-point contraction off); the only float32 values are the ones the JS writes into a
//     Float32Array (the output ports, the rings, the all-pass state), so a store goes through (float);
//   - Math.exp / pow / sin / cos / log / tanh are js_exp / js_pow / js_sin / js_cos / js_log / js_tanh (fdlibm as V8 12.4 has them), Math.round is js_round, `x | 0` is js_to_int32, Math.min / max
//     keep NaN; Math.sqrt / floor / abs / ceil / sign are exact and used as they are;
//   - nothing allocates after construction (the JS makes its rings in create(); so do the constructors).
// THE MODULE INTERFACE (this is a decision of U19, written down in docs/port/U19-NOTES.md, and does not guess at U31): a module is a `Module` made by its `Def::make(sr, seeds)` (the JS's
// `create(sr)`), run by `process(io, ctx)` (the JS's `process(st, io, ctx)`). `IO` holds, in the order the JS module declares them, the input buffers (an unpatched input points to a
// zero-filled buffer, as in the engine), `linked` (the engine's `io.linked` set), the output buffers and the parameter values `p` (the JS's `io.p`, in the order of `params:`, already
// modulated by the host). `Ctx` is the part of the engine's ctx the modules read (sr, block, quality, the transport). `Seeds` is the engine's `nextSeed()`.
#ifndef THUNDER_MODFX_MODFX_H
#define THUNDER_MODFX_MODFX_H
#include <cstdint>
#include <memory>

#include "rt/jsmath.h"

namespace thunder {
namespace modfx {

constexpr int kBlock = 128;     // the engine's BLOCK: every buffer is this long
constexpr int kMaxPorts = 12;

struct Ctx {
  double sr = 48000;
  int block = kBlock;
  bool full = true;             // ctx.quality === 'full' (false: 'draft')
  double tempo = 120, lpb = 4, ticks = 6;
  bool playing = true;
  double startN = 0;            // ctx.transport.startN || 0
  // ctx.key(sck) (U21): the Source's mono buffer of 128 floats, nullptr for Self (sck rounds to 0 or less). nullptr here: the JS's `ctx.key` undefined (the same as Self).
  const float* (*key)(void* user, double sck) = nullptr; void* keyUser = nullptr;
};

struct IO {
  const float* in[kMaxPorts] = {};
  bool linked[kMaxPorts] = {};
  float* out[kMaxPorts] = {};
  const double* p = nullptr;
};

// nextSeed(): the engine's seed counter (an LCG over doubles), or a recorded list: the one who makes the module decides.
class Seeds {
 public:
  virtual ~Seeds() = default;
  virtual uint32_t next() = 0;
};
// The JS's own counter: seedN = (seedN * 1103515245 + 12345) >>> 0 with the product in doubles (it exceeds 2^53 and rounds, as it does there).
class SeedCounter : public Seeds {
 public:
  explicit SeedCounter(uint32_t start = 12345) : n_(start) {}
  // defined here, not in modfx.cpp: thunder_core is built with -fno-rtti, so a TU with RTTI (the sanitizer build of rt_harness) must emit the vtable and typeinfo itself
  uint32_t next() override { n_ = js_to_uint32((double)n_ * 1103515245 + 12345); return n_; }
 private:
  uint32_t n_;
};

class Module {
 public:
  virtual ~Module() = default;
  virtual void process(const IO& io, const Ctx& c) = 0;
};

struct ParamDef { const char* name; double def, mn, mx, st; };
struct PortDef { const char* name; const char* kind; };   // kind: audio, cv, gate
struct Def {
  const char* type;
  int nin; const PortDef* in;
  int nout; const PortDef* out;
  int np; const ParamDef* params;
  std::unique_ptr<Module> (*make)(double sr, Seeds& seeds);
};

const Def* find(const char* type);
int count();
const Def& at(int i);   // in the order the registry lists them

}  // namespace modfx
}  // namespace thunder
#endif
