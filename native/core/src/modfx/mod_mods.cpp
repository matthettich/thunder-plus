// U21: the Random generator (Marbles) and Function generator (Tides 2) of chains-worklet.js (lines 4990 to 5160): the JS wrappers around Mutable Instruments' DSP in mods.wasm, here around the same C++ built
// natively (thunder_dsp_mods: dsp/mods/mod_marbles.cc, mod_tides.cc; the factories make_marbles / make_tides, not the wasm exports). The wrapper is the JS's, line for line: the parameter block
// (Float32Array stores), the steady clock at the song tempo (modsClock), the input / output copies of `n` samples, the exports' input and output sanitising (mods_process), and "a bad value came out:
// start it afresh" (the object is made again with the same seed on the next block).
// OFF by default (THUNDER_MODFX_MODS): the native build of the mods C++ is not bit-exact with the wasm the golden comes from (float-ULP differences; U21-NOTES.md). Not carried over from the JS: the wasm instance's 64-object limit (and its 375-block wait) and the instance crash / restart; the object is made in the constructor (the JS makes it at the first block).
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <vector>

#include <mods.h>   // dsp/mods/mods.h (the Mod base class and the factories)

#include "modfx/bfutil.h"
#include "modfx/mods.h"

// (global scope: core/dsp/mods_marbles_inplace.cc, mods_tides_inplace.cc)
size_t mods_marbles_size(); Mod* mods_marbles_make_at(void* mem, float sr, unsigned seed);
size_t mods_tides_size(); Mod* mods_tides_make_at(void* mem, float sr);

namespace thunder {
namespace modfx {
namespace {

#define PD(a) (int)(sizeof(a) / sizeof(a[0])), a
const double kDivBeats[10] = {.0625, .125, 1.0 / 6, .25, 1.0 / 3, .5, 1, 2, 4, 8};
inline double divBeats(double v) { return kDivBeats[(int)clamp((double)i32(v), 0, 9)]; }

// U31: the objects are made IN storage this wrapper owns (made with the module, when the patch is prepared): the first block of Marbles (its seed is drawn then, arm()) and the "start afresh" after a bad value
// allocate nothing. mods_*_make_at are in core/dsp/mods_marbles_inplace.cc / mods_tides_inplace.cc (the dsp/mods sources plus the maker; dsp/ itself is not edited).
class ModsWrap : public Module {
 protected:
  explicit ModsWrap(double sr, int type, uint32_t seed) : sr_(sr), type_(type), seed_(seed) {
    store_.resize((type == 0 ? mods_marbles_size() : mods_tides_size()) + 64);   // (a failed allocation is the throwing operator new's: the preparation boundary of the engine catches it)
    mem_ = reinterpret_cast<unsigned char*>((reinterpret_cast<uintptr_t>(store_.data()) + 63) & ~static_cast<uintptr_t>(63));
    make();
  }
  ~ModsWrap() override { kill(); }
  void kill() { if (m_) { m_->~Mod(); m_ = nullptr; } }
  void make() { kill(); m_ = type_ == 0 ? mods_marbles_make_at(mem_, (float)sr_, seed_) : mods_tides_make_at(mem_, (float)sr_); fresh_ = true; }
  // modsOpen: this module's object, made again after a bad value (the JS asks the instance; null: 64 in use, never here)
  bool open() { if (!m_) make(); return m_ != nullptr; }
  // modsRun: mods_process (the export): inputs outside (-64, 64) cleared, outputs outside (-16, 16) cleared; a bad output means "start afresh"
  void run(int n) {
    if (n < 1 || n > kModBlock) { kill(); return; }
    for (int k = 0; k < kModInputs; ++k) for (int i = 0; i < n; ++i) { const float v = IN[k][i]; if (!(v > -64.f && v < 64.f)) IN[k][i] = 0.f; }
    m_->process(IN, OUT, n);
    bool ok = true;
    for (int k = 0; k < kModOutputs; ++k) for (int i = 0; i < n; ++i) { const float v = OUT[k][i]; if (!(v > -16.f && v < 16.f)) { OUT[k][i] = 0.f; ok = false; } }
    if (!ok) kill();
  }
  void P(int k, double v) { m_->p[k] = (float)v; }   // st.P is a Float32Array on the object's parameter block
  // modsClock: a steady clock at hz (high for the first half of each note value) written to IN[off + i]; rs: Reset (a rising edge starts a pulse)
  void clock(int n, double hz, double sr, const float* rs, int row) {
    const double inc = hz / sr; if (ck_ < 0) ck_ = 0;
    for (int i = 0; i < n; i++) {
      if (rs) { const bool r = rs[i] > .5; if (r && !rk_) ck_ = 0; rk_ = r; }
      IN[row][i] = ck_ < .5 ? 1 : 0; ck_ += inc; if (ck_ >= 1) ck_ -= std::floor(ck_);
    }
  }
  void cp(int port, const float* src, int n) { for (int i = 0; i < n; i++) IN[port][i] = src ? src[i] : 0.f; }
  void silence(const IO& io, int nout) { for (int k = 0; k < nout; k++) std::memset(io.out[k], 0, sizeof(float) * kBlock); }
  std::vector<unsigned char> store_; unsigned char* mem_ = nullptr; Mod* m_ = nullptr; double sr_; int type_; uint32_t seed_; bool fresh_ = true; double ck_ = -1; bool rk_ = false; Restart rs_;
  float IN[kModInputs][kModBlock] = {}, OUT[kModOutputs][kModBlock] = {};
};

// ---- Random generator (Marbles) --------------------------------------------------------------------------------------------------------------------------------------------------------------------------
const PortDef kMaIn[] = {{"clock", "gate"}, {"reset", "gate"}, {"xclock", "gate"}, {"dejavu", "cv"}}, kMaOut[] = {{"t1", "gate"}, {"t2", "gate"}, {"t3", "gate"}, {"x1", "cv"}, {"x2", "cv"}, {"x3", "cv"}, {"y", "cv"}};
const ParamDef kMaP[] = {{"div", 3, 0, 9, 1}, {"rate", 4, 0, 8, 1}, {"model", 0, 0, 6, 1}, {"bias", .5, 0, 1, .01}, {"jitter", 0, 0, 1, .01}, {"dejavu", 0, 0, 1, .01}, {"length", 8, 1, 16, 1},
  {"spread", .5, 0, 1, .01}, {"xbias", .5, 0, 1, .01}, {"steps", .75, 0, 1, .01}, {"scale", 0, 0, 5, 1}, {"xrange", 0, 0, 2, 1}, {"oct", 0, -4, 3, 1},
  {"glen", .5, 0, 1, .01}, {"grand", 0, 0, 1, .01}, {"tdv", 1, 0, 2, 1}, {"xdv", 1, 0, 2, 1}, {"xmode", 0, 0, 2, 1}, {"xclk", 0, 0, 3, 1}, {"xout", 0, 0, 1, 1},
  {"ydiv", 6, 0, 11, 1}, {"yspread", .5, 0, 1, .01}, {"ybias", .5, 0, 1, .01}, {"ysteps", 0, 0, 1, .01}, {"yrange", 2, 0, 2, 1}};
class MarblesM : public ModsWrap {
 public:
  MarblesM(double sr, Seeds& s) : ModsWrap(sr, 0, s.next()) { rs_.seen = false; }
  // U30: the …waves engine makes the object ahead and arms it at its first block: the seed is drawn then and the object made again with it (the one allocation on the audio thread, named in U30-NOTES.md)
  void arm(Seeds& s) override { seed_ = s.next(); make(); }
  void process(const IO& io, const Ctx& c) override {
    const int n = c.block; const double* p = io.p; const bool rs = rs_(c);
    if (rs) ck_ = -1;
    if (!open()) { silence(io, 7); return; }
    P(0, p[1]); P(1, 1); P(2, p[2]); P(3, p[3]); P(4, p[4]); P(5, p[13]); P(6, p[14]); P(7, p[5]); P(8, p[6]);
    P(9, p[15]); P(10, p[16]); P(11, p[7]); P(12, p[8]); P(13, p[9]); P(14, p[10]); P(15, p[11]); P(16, p[17]);
    P(17, p[18]); P(18, p[24]); P(19, p[20]); P(20, p[21]); P(21, p[22]); P(22, p[23]); P(23, p[12]); P(24, p[19]);
    P(28, io.linked[2] ? 1 : 0); P(29, rs || fresh_ ? 1 : 0); fresh_ = false;
    if (io.linked[0]) cp(0, io.in[0], n);
    else clock(n, (truthy(c.tempo) ? c.tempo : 120) / 60 / divBeats(p[0]), c.sr, io.in[1], 0);
    cp(1, io.in[1], n); cp(2, io.in[2], n); cp(3, io.in[3], n);
    run(n);
    if (!m_) { /* a bad value came out: the outputs (cleared) are still copied, as in the JS */ }
    for (int k = 0; k < 7; k++) { float* o = io.out[k]; for (int i = 0; i < n; i++) o[i] = OUT[k][i]; }
  }
};

// ---- Function generator (Tides 2) ------------------------------------------------------------------------------------------------------------------------------------------------------------------
const PortDef kTiIn[] = {{"trig", "gate"}, {"clock", "gate"}, {"pitch", "cv"}}, kTiOut[] = {{"out1", "cv"}, {"out2", "cv"}, {"out3", "cv"}, {"out4", "cv"}};
const ParamDef kTiP[] = {{"mode", 1, 0, 2, 1}, {"output", 0, 0, 3, 1}, {"range", 0, 0, 1, 1}, {"freq", -105.38, -192, 36, .01}, {"shape", .5, 0, 1, .01}, {"slope", .5, 0, 1, .01},
  {"smooth", .5, 0, 1, .01}, {"shift", .75, 0, 1, .01}, {"sync", 0, 0, 1, 1}, {"div", 6, 0, 9, 1}, {"ratio", 9, 0, 18, 1}};
class TidesM : public ModsWrap {
 public:
  TidesM(double sr, Seeds&) : ModsWrap(sr, 1, 0) { rs_.seen = false; }
  void process(const IO& io, const Ctx& c) override {
    const int n = c.block; const double* p = io.p; const bool rs = rs_(c);
    if (rs) ck_ = -1;
    if (!open()) { silence(io, 4); return; }
    const bool audio = p[2] >= .5, clk = !audio && io.linked[1], sync = !audio && !clk && p[8] >= .5;
    P(0, p[0]); P(1, p[1]); P(2, p[2]); P(3, p[3]); P(4, p[4]); P(5, p[5]); P(6, p[6]); P(7, p[7]);
    P(8, io.linked[2] ? 1 : 0); P(9, io.linked[0] ? 1 : 0); P(10, clk || sync ? 1 : 0); P(11, p[10]); P(12, rs ? 1 : 0);
    cp(0, io.in[0], n);
    if (sync) clock(n, (truthy(c.tempo) ? c.tempo : 120) / 60 / divBeats(p[9]), c.sr, nullptr, 1);
    else cp(1, io.in[1], n);
    cp(2, io.in[2], n);
    run(n);
    for (int k = 0; k < 4; k++) { float* o = io.out[k]; for (int i = 0; i < n; i++) o[i] = OUT[k][i]; }
  }
};

template <class M> std::unique_ptr<Module> mk(double sr, Seeds& s) { return std::unique_ptr<Module>(new M(sr, s)); }
const Def kDefs[] = {
  {"marbles", PD(kMaIn), PD(kMaOut), PD(kMaP), mk<MarblesM>},
  {"tides", PD(kTiIn), PD(kTiOut), PD(kTiP), mk<TidesM>},
};
}  // namespace

void addModsMods(std::vector<const Def*>& v) { for (const Def& d : kDefs) v.push_back(&d); }

}  // namespace modfx
}  // namespace thunder
