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
constexpr int kMaxPorts = 32;   // U31: the …chains Stereo mixer has 32 inputs (U30: 20, the …waves Mixer has 16; it was 12)

// One of the song's impulse responses (the JS's globalThis.__thunderIR[i] = { L, R or null, sr, id }): read-only views; the host keeps the buffers alive and unchanged while a module may read
// them (the JS's garbage collector does that there) and hands a NEW SongIR object whenever the song's IRs change (the module compares the pointer and `id`, as the JS compares the object and id).
struct SongIR {
  const float* L = nullptr; int nL = 0;
  const float* R = nullptr; int nR = 0;   // R null (or nR 0): the left one plays on both sides
  double sr = 48000;
  uint64_t id = 0;                        // the JS's id string ("ref" or "refL/refR"), as a number: equal strings give equal numbers
};

struct Ctx {
  double sr = 48000;
  int block = kBlock;
  bool full = true;             // ctx.quality === 'full' (false: 'draft')
  double tempo = 120, lpb = 4, ticks = 6;
  bool playing = true;
  double startN = 0;            // ctx.transport.startN || 0
  // ctx.key(sck) (U21): the Source's mono buffer of 128 floats, nullptr for Self (sck rounds to 0 or less). nullptr here: the JS's `ctx.key` undefined (the same as Self).
  const float* (*key)(void* user, double sck) = nullptr; void* keyUser = nullptr;
  const SongIR* const* irs = nullptr;   // globalThis.__thunderIR: four entries (a null one is an empty slot), or null for "no IRs at all" (U20: Convolve's Song IR 1 to 4)
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

// THE ONE ENTRY the host renders a module through: run(). Every buffer a module reads or writes is kBlock floats long (the Ctx contract: ctx.block <= kBlock; mod_mods' cp() / fill(kBlock) and every
// fixed-size state array rely on it), so a block of more than kBlock frames (or a negative one) is REFUSED here: nothing is called, nothing is written, run() returns false and refused() counts it. Why
// refuse and not split: a split block would need every port, ctx.key(sck) (a fixed 128-float buffer with no offset) and the track inputs offset per chunk, and the JS never does it (its engine's
// process(frames = BLOCK) is called by the worklet with at most one 128-frame quantum; a longer `frames` is a bug there, not a feature), whereas FxHost (the tfx host) CAN cut a longer call into 128s
// because its buffers are as long as the call and its lanes / sources are cut with it. The host that feeds modfx (not written yet) cuts its device buffers into <= kBlock chunks BEFORE this.
// process() is protected: no caller holding a Module* can reach it with a larger block. (Hardening, 17 Nov: docs/port/HARDENING-NOTES.md, B3-3.)
class Module {
 public:
  virtual ~Module() = default;
  bool run(const IO& io, const Ctx& c) {
    if (c.block < 0 || c.block > kBlock) { refused_++; return false; }
    process(io, c); return true;
  }
  uint32_t refused() const { return refused_; }
  // U30: the …waves engine makes a module's object ahead of its first block (the JS makes it at the first block, drawing its seeds then, in the order the modules first run). A module that draws
  // a seed in its constructor (nextSeed()) takes it again here, from the real counter, at the moment the JS's create() would have run; the constructor got a placeholder.
  virtual void arm(Seeds&) {}
 protected:
  virtual void process(const IO& io, const Ctx& c) = 0;
 private:
  uint32_t refused_ = 0;
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
