// MODFX tempo-locked wrappers (chains-worklet.js 2919 to 2963): Flex, Step gate and Retrigger as modules. They wrap the track effects' cores (FlexCore, StepGateCore, RetrigCore: U15's
// fx/clkcores.*) and make their own beat clock (`fxBeats`): the beat count at the song tempo from the start of the song, wrapping every 16, "stopped" (-1) when the transport is not playing.
// The clock array is made on the first block with that block's length, as the JS makes it (`new Float32Array(ctx.block)`): a first block shorter than later ones leaves the core a shorter clock.
#include <vector>

#include "fx/clkcores.h"
#include "modfx/mods.h"
#include "modfx/util.h"

namespace thunder {
namespace modfx {
namespace {

#define PD(a) (int)(sizeof(a) / sizeof(a[0])), a
inline double u01(double v) { return clamp(v, 0, 1); }

// the module's own beat clock (fxBeats) and the dry pass (fxPass)
class BeatClock {
 public:
  BeatClock() : bp_(new float[kBlock]()) {}
  // returns the clock array and its length (what `bp` and `bp.length` are)
  const float* run(const Ctx& c, int* len) {
    if (!made_) { made_ = true; n_ = c.block; beat_ = 0; }
    if (rs_(c)) beat_ = 0;
    if (!c.playing) { *len = 1; return &stop_; }
    const double inc = (truthy(c.tempo) ? c.tempo : 120) / 60 / c.sr;
    for (int i = 0; i < c.block; i++) { if (i < n_) bp_[i] = (float)beat_; beat_ += inc; if (beat_ >= 16) beat_ -= 16; }
    *len = n_; return bp_.get();
  }
 private:
  std::unique_ptr<float[]> bp_; int n_ = 0; bool made_ = false; double beat_ = 0; Restart rs_; const float stop_ = -1;
};
inline void fxPass(const IO& io) { const In a = inp(io, 0), b = inR(io, 0, 1); const Out L = outp(io, 0), R = outp(io, 1); for (int i = 0; i < kBlock; i++) { L[i] = a[i]; R[i] = b[i]; } }

const PortDef kClkIn[] = {{"in", "audio"}, {"inR", "audio"}, {"pat", "cv"}}, kRtIn[] = {{"in", "audio"}, {"inR", "audio"}, {"trig", "gate"}}, kStOut[] = {{"out", "audio"}, {"outR", "audio"}};

// ---- Flex ----------------------------------------------------------------------------------------------------------------------------
const ParamDef kFlexP[] = {{"pat", 1, 0, 16, 1}, {"mix", 1, 0, 1, .01}, {"xf", 5, .5, 20, .1}};
class FlexM : public Module {
 public:
  explicit FlexM(double sr, Seeds&) : c_(sr) {}
  void process(const IO& io, const Ctx& ctx) override {
    fxPass(io); ClkTr tr; int len; tr.bp = clk_.run(ctx, &len); tr.bpn = len;
    tr.flx = io.linked[2] ? clamp(js_round(inp(io, 2)[0] * 16), 0, 16) : -1;
    FlexDp dp; dp.pat = clamp((double)i32(io.p[0]), 0, 16); dp.mix = u01(io.p[1]); dp.xf = clamp(io.p[2], .5, 20);
    c_.process(io.out[0], io.out[1], ctx.block, dp, tr);
  }
 private:
  FlexCore c_; BeatClock clk_;
};

// ---- Step gate -----------------------------------------------------------------------------------------------------------------------
const ParamDef kSgP[] = {{"pat", 2, 0, 15, 1}, {"m1", 170, 0, 255, 1}, {"m2", 170, 0, 255, 1}, {"div", 1, 0, 4, 1}, {"len", .75, .03, 1, .01}, {"depth", 1, 0, 1, .01}, {"smooth", 2, .3, 20, .1}};
class StepGateM : public Module {
 public:
  explicit StepGateM(double sr, Seeds&) : c_(sr) {}
  void process(const IO& io, const Ctx& ctx) override {
    fxPass(io); ClkTr tr; int len; tr.bp = clk_.run(ctx, &len); tr.bpn = len;
    if (io.linked[2]) { const double g = clamp(js_round(inp(io, 2)[0] * 15), 0, 15); tr.gat = truthy(g) ? g : 16; } else tr.gat = -1;
    const double* p = io.p; SgDp dp; dp.pat = clamp((double)i32(p[0]), 0, 15); dp.m1 = clamp((double)i32(p[1]), 0, 255); dp.m2 = clamp((double)i32(p[2]), 0, 255);
    dp.div = clamp((double)i32(p[3]), 0, 4); dp.len = clamp(p[4], .03, 1); dp.depth = u01(p[5]); dp.smooth = clamp(p[6], .3, 20);
    c_.process(io.out[0], io.out[1], ctx.block, dp, tr);
  }
 private:
  StepGateCore c_; BeatClock clk_;
};

// ---- Retrigger -----------------------------------------------------------------------------------------------------------------------
const ParamDef kRtP[] = {{"mode", 1, 0, 1, 1}, {"div", 2, 0, 7, 1}, {"decay", .15, 0, 1, .01}, {"pitch", 0, -24, 24, 1}, {"gate", 1, .05, 1, .01}, {"mix", 1, 0, 1, .01}};
class RetrigM : public Module {
 public:
  explicit RetrigM(double sr, Seeds&) : c_(sr) {}
  void process(const IO& io, const Ctx& ctx) override {
    fxPass(io); ClkTr tr; int len; tr.bp = clk_.run(ctx, &len); tr.bpn = len;
    const double* p = io.p; RtDp dp; dp.mode = clamp((double)i32(p[0]), 0, 1); dp.div = clamp((double)i32(p[1]), 0, 7); dp.decay = u01(p[2]); dp.pitch = clamp(p[3], -24, 24); dp.gate = clamp(p[4], .05, 1); dp.mix = u01(p[5]);
    tr.rtg = io.linked[2] ? (inp(io, 2)[0] > .5 ? dp.div + 1 : 0) : -1;
    c_.setSpb(ctx.sr * 60 / (truthy(ctx.tempo) ? ctx.tempo : 120));
    c_.process(io.out[0], io.out[1], ctx.block, dp, tr);
  }
 private:
  RetrigCore c_; BeatClock clk_;
};

template <class M> std::unique_ptr<Module> mk(double sr, Seeds& s) { return std::unique_ptr<Module>(new M(sr, s)); }
const Def kDefs[] = {
  {"flex", PD(kClkIn), PD(kStOut), PD(kFlexP), mk<FlexM>},
  {"sgate", PD(kClkIn), PD(kStOut), PD(kSgP), mk<StepGateM>},
  {"retrig", PD(kRtIn), PD(kStOut), PD(kRtP), mk<RetrigM>},
};
}  // namespace

void addClkMods(std::vector<const Def*>& v) { for (const Def& d : kDefs) v.push_back(&d); }

}  // namespace modfx
}  // namespace thunder
