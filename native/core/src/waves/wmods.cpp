// U30: the waves-only modules of waves/waves-worklet.js (outside the shared block), line for line: Host In, VCO, FM, Additive, Noise, Plaits, Elements, VCF, VCA, Mixer, LFO, ADSR, Random,
// Attenuverter, Sequencer, Out, String, the 33 tfx effects (effect()), and the shared-block modules U19 to U21 did not take: Expression (MathExpr), Pitch delay / reverb, Scope (no picture:
// show() is not ported, as for the other meters). Same rules as modfx: doubles in the JS's order, float32 only where the JS stores into a Float32Array, Math.* through js_*, no allocation
// in process() (see waves.h for the exceptions, named in the notes).
#include <cmath>
#include <cstring>

#include "fx/u17cores.h"
#include "modfx/bfutil.h"
#include "modfx/mods.h"
#include "modfx/util.h"
#include "waves/graph.h"
#include "waves/waves.h"

#ifdef THUNDER_HAVE_TFX
#include "tfx.h"
extern "C" void thunder_wasi_rand_use(uint64_t* state);
extern "C" void thunder_fx_state_use(uint32_t* stmlib_rng, uint32_t* daisy_myrand);
#endif
#ifdef THUNDER_ENGINE_PE
#include "pe_host.h"
#endif

namespace thunder {
namespace waves {
#include "waves/wtable.inc"

using namespace modfx;

const WInfo* winfo(std::string_view t) { for (int i = 0; i < kWInfoCount; i++) if (t == kWInfo[i].type) return &kWInfo[i]; return nullptr; }
int winfo_count() { return kWInfoCount; }
const WInfo& winfo_at(int i) { return kWInfo[i]; }
int wport_in(const WInfo& w, std::string_view n) { for (int i = 0; i < w.nin; i++) if (n == w.in[i].name) return i; return -1; }
int wport_out(const WInfo& w, std::string_view n) { for (int i = 0; i < w.nout; i++) if (n == w.out[i].name) return i; return -1; }
int wparam(const WInfo& w, std::string_view n) { for (int i = 0; i < w.np; i++) if (n == w.params[i].name) return i; return -1; }

bool Host::haveEffects() {
#ifdef THUNDER_HAVE_TFX
  return true;
#else
  return false;
#endif
}
bool Host::havePE() {
#ifdef THUNDER_ENGINE_PE
  return true;
#else
  return false;
#endif
}
uint32_t Host::nextSeed() { seedN = js_to_uint32((double)seedN * 1103515245 + 12345); return seedN; }
void Host::setRandom(Rnd k, uint32_t s) { rndKind = k; rndS = s; }
double Host::random() {
  if (rndKind == Rnd::Fixed) return fixed;
  if (rndKind == Rnd::Xorshift) { uint32_t s = rndS; s ^= s << 13; s ^= s >> 17; s ^= s << 5; rndS = s; return (double)s / 4294967296.0; }
  rndS += 0x6d2b79f5u; uint32_t t = rndS; t = (t ^ (t >> 15)) * (t | 1u); t ^= t + (t ^ (t >> 7)) * (t | 61u); return (double)(t ^ (t >> 14)) / 4294967296.0;
}

namespace {
inline const WCtx& W(const Ctx& c) { return static_cast<const WCtx&>(c); }
inline double hzOf(double p) { return 261.6256 * js_pow(2, p / 12); }
inline double frac(double x) { return x - std::floor(x); }
inline double blep(double t, double dt) {
  if (t < dt) { t /= dt; return t + t - t * t - 1; }
  if (t > 1 - dt) { t = (t - 1) / dt; return t * t + t + t + 1; }
  return 0;
}
inline double u01(double v) { return clamp(v, 0, 1); }
inline double tab(const double* t, int n, double i) { return i >= 0 && i < n && i == std::floor(i) ? t[(int)i] : kNaN; }   // t[i] (undefined: NaN)
inline double jround(double x) { return js_round(x); }
inline bool lk(const IO& io, int k) { return io.linked[k]; }
inline void fill(const IO& io, int k, double v) { for (int i = 0; i < kBlock; i++) io.out[k][i] = (float)v; }

// ---- Host In ----------------------------------------------------------------------------------------------------------------------
class HostIn : public WModule {
 public:
  HostIn(double, Seeds&) {}
  void process(const IO& io, const Ctx& c) override {
    const WCtx& w = W(c); const bool has = w.voice >= 0 && w.voice < w.nNotes; const double tr = io.p[0];
    const VoiceRow* n = has ? &w.notes[w.voice] : nullptr; const bool on = n && n->gate > 0;
    fill(io, 0, n ? n->pitch + tr : 0); fill(io, 1, on ? 1 : 0); fill(io, 2, on ? n->vel : 0);
  }
};
// ---- VCO --------------------------------------------------------------------------------------------------------------------------
class VCO : public WModule {
 public:
  VCO(double, Seeds&) {}
  void arm(Seeds& s) override { Host* h = static_cast<WSeeds&>(s).host; phase_ = h ? h->random() : 0; }
  void process(const IO& io, const Ctx& c) override {
    const double v = js_pow(2, (io.p[1] + io.p[2] / 100) / 12); const int w = i32(io.p[0]);
    for (int i = 0; i < c.block; i++) {
      const double dt = jmin(hzOf((double)io.in[0][i] + (double)io.in[1][i]) * v / c.sr, .5);
      phase_ += dt; phase_ -= std::floor(phase_);
      const double t = phase_;
      io.out[0][i] = (float)(w == 0 ? (2 * t - 1 - blep(t, dt)) * .8
        : w == 1 ? ((t < .5 ? 1 : -1) + blep(t, dt) - blep(frac(t + .5), dt)) * .8
        : w == 2 ? 4 * std::fabs(t - .5) - 1 : sinT(t));
    }
  }
 private:
  double phase_ = 0;
};
// ---- FM ---------------------------------------------------------------------------------------------------------------------------
const double kRatios[24] = {0.25, 0.5, 0.75, 1, 1.25, 1.5, 1.75, 2, 2.5, 3, 3.5, 4, 4.5, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 16};
class FM : public WModule {
 public:
  FM(double, Seeds&) {}
  void process(const IO& io, const Ctx& c) override {
    const int md = i32(io.p[0]); const double v = js_pow(2, (io.p[1] + io.p[2] / 100) / 12), A = io.p[3];
    const double ratio = tab(kRatios, 24, jround(A * 23));
    const double r1 = 1.4142 + A * 2.4, r2 = 1.6818 + A * 1.3, r3 = 2.613 + A * 3.1;
    for (int i = 0; i < c.block; i++) {
      const double inc = jmin(hzOf((double)io.in[0][i] + (double)io.in[1][i]) * v / c.sr, .5), B = clamp(io.p[4] + (double)io.in[2][i], 0, 1);
      double y;
      if (md == 0) { const double I = B * B * 12 * .159155; y = sinT(p_ + I * sinT(p_ * ratio)); }
      else if (md == 1) { const double fb = B * 1.8 * .159155; y = sinT(p_ + fb * (p1_ + p2_) * .5); p2_ = p1_; p1_ = y; }
      else if (md == 2) { const double I = B * B * 9 * .159155; y = .5 * (sinT(p_ + I * sinT(p_ * r1)) + sinT(p_ * r2 + I * sinT(p_ * r3))); }
      else { const double I = B * 1.8 * .159155, a = sinT(p_ + I * pb_), b = sinT(p_ * ratio + I * pa_); pa_ = a; pb_ = b; y = (a + b) * .5; }
      p_ += inc; if (p_ > 1e6) p_ -= std::floor(p_);
      io.out[0][i] = (float)y;
    }
  }
 private:
  double p_ = 0, p1_ = 0, p2_ = 0, pa_ = 0, pb_ = 0;
};
// ---- Additive ---------------------------------------------------------------------------------------------------------------------
const double kMem[12] = {1, 1.594, 2.136, 2.296, 2.653, 2.918, 3.156, 3.501, 3.6, 3.652, 4.06, 4.154};
const double kBell[12] = {0.5, 1, 1.183, 1.506, 2, 2.514, 2.662, 3.011, 4.166, 5.433, 6.796, 8.215};
const double kBellA[12] = {0.9, 1, 0.7, 0.6, 0.55, 0.45, 0.4, 0.35, 0.28, 0.22, 0.16, 0.12};
const double kBar[6] = {1, 2.756, 5.404, 8.933, 13.344, 18.638};
const int kChords[14][5] = {{0, 12, -1, -1, -1}, {0, 7, -1, -1, -1}, {0, 7, 12, -1, -1}, {0, 4, 7, -1, -1}, {0, 3, 7, -1, -1}, {0, 2, 7, -1, -1}, {0, 5, 7, -1, -1}, {0, 4, 7, 10, -1}, {0, 3, 7, 10, -1}, {0, 4, 7, 11, -1}, {0, 3, 7, 10, 14}, {0, 3, 6, -1, -1}, {0, 4, 8, -1, -1}, {0, 5, 10, 15, -1}};
constexpr int kMaxPart = 128;
class Additive : public WModule {
 public:
  Additive(double, Seeds&) {}
  void process(const IO& io, const Ctx& c) override {
    const int md = i32(io.p[0]); const double shape = io.p[2], color = io.p[3];
    const Key k = keyOf(md, shape, color);
    if (!haveKey_ || !(k == key_)) { key_ = k; haveKey_ = true; if (!partials(md, shape, color)) { W(c).fail = true; return; } }
    const int n = n_; const bool gate = lk(io, 1); const double v = js_pow(2, io.p[1] / 12);
    const double inc0 = hzOf((double)io.in[0][0]) * v / c.sr;
    if (gate) { const bool g = io.in[1][0] > .5; if (g && !g_) t_ = 0; g_ = g; }
    int live = 0;
    for (int kk = 0; kk < n; kk++) {
      if (rs_[kk] * inc0 > .45) { if (kk < 40) gains_[kk] = 0; continue; }
      if (kk < 40) gains_[kk] = am_[kk] * (gate ? js_exp(-t_ * dm_[kk]) : 1);
      live = kk + 1;
    }
    for (int i = 0; i < c.block; i++) {
      const double inc = hzOf((double)io.in[0][i]) * v / c.sr;
      double y = 0;
      for (int kk = 0; kk < live; kk++) { const double g = kk < 40 ? gains_[kk] : kNaN; if (g > 1e-5) y += g * sinT(ph_ * rs_[kk]); }
      ph_ += inc; if (ph_ > 1e6) ph_ -= std::floor(ph_);
      io.out[0][i] = (float)y;
    }
    t_ += c.block / c.sr;
  }
 private:
  struct Key { int md; int kind1, kind2; long long n1, n2; bool neg1, neg2; double raw1, raw2;
    bool operator==(const Key& o) const { return md == o.md && kind1 == o.kind1 && kind2 == o.kind2 && n1 == o.n1 && n2 == o.n2 && neg1 == o.neg1 && neg2 == o.neg2 && raw1 == o.raw1 && raw2 == o.raw2; } };
  // x.toFixed(2) as a comparable value: NaN, +-Infinity, |x| >= 1e21 (the number's own text), else the integer n of the nearest n / 100 (ties away from zero) and the sign JS writes
  static void fixed2(double x, int& kind, long long& n, bool& neg, double& raw) {
    kind = 0; n = 0; neg = false; raw = 0;
    if (x != x) { kind = 1; return; }
    if (x == INFINITY) { kind = 2; return; }
    if (x == -INFINITY) { kind = 3; return; }
    if (std::fabs(x) >= 1e21) { kind = 4; raw = x; return; }
    neg = x < 0; const double a = std::fabs(x); const double z = a * 100; double r = std::floor(z); const double err = std::fma(a, 100.0, -z);   // z + err = a * 100 exactly
    const double fr = (z - r) + err;
    if (fr > .5 || (fr == .5)) r += 1;   // nearest, a tie goes up
    n = (long long)r;
  }
  static Key keyOf(int md, double shape, double color) { Key k{}; k.md = md; fixed2(shape, k.kind1, k.n1, k.neg1, k.raw1); fixed2(color, k.kind2, k.n2, k.neg2, k.raw2); return k; }
  bool partials(int md, double A, double B) {
    int n = 0; double rs[kMaxPart], am[kMaxPart], dm[kMaxPart];
    auto push = [&](double r, double a, double d) { rs[n] = r; am[n] = a; dm[n] = d; n++; };
    if (md <= 1) {
      const double nn = 1 + jround(A * 31); if (nn > kMaxPart) return false;
      for (int k = 0; k < nn; k++) { const double r = md == 1 ? 2 * k + 1 : k + 1; push(r, js_pow(r, -(0.4 + B * 1.6)), k * 3); }
    } else if (md == 2) {
      const double nn = 2 + jround(A * 10); if (nn > kMaxPart) return false;
      for (int k = 0; k < nn; k++) push(tab(kMem, 12, k), 1 / (1 + k * .7), B * B * 50 * k);
    } else if (md == 3) {
      const double nn = 2 + jround(A * 10); if (nn > kMaxPart) return false;
      for (int k = 0; k < nn; k++) push(tab(kBell, 12, k), tab(kBellA, 12, k), (0.3 + B * B * 14) * tab(kBell, 12, k));
    } else if (md == 4) {
      const double s = 0.12 + A * 0.85;
      for (int k = 0; k < 6; k++) push(kBar[k], js_pow(s, k), (0.5 + B * B * 12) * kBar[k]);
    } else {
      const double ci = jround(A * 13); if (!(ci >= 0 && ci < 14)) return false;   // CHORDS[ci] undefined: the JS's for-of throws
      for (int j = 0; j < 5; j++) { const int sem = kChords[(int)ci][j]; if (j > 0 && sem < 0) break; const double r = js_pow(2, sem / 12.0); if (n + 3 > kMaxPart) return false; push(r, 1, 0); push(r * 2, B * .5, 2); push(r * 3, B * .33, 4); }
      // (the JS pushes rs, rs * 2, rs * 3 then am 1, B * .5, B * .33 then dm 0, 2, 4: the same triples)
    }
    double sum = 0, mx = 0; for (int k = 0; k < n; k++) { sum += am[k]; if (am[k] > mx) mx = am[k]; }
    const double norm = 1 / jmax(.5 * sum + .5 * mx, 1e-6);
    n_ = n; for (int k = 0; k < n; k++) { rs_[k] = rs[k]; am_[k] = am[k] * norm; dm_[k] = dm[k]; }
    return true;
  }
  Key key_{}; bool haveKey_ = false; int n_ = 0; double rs_[kMaxPart] = {}, am_[kMaxPart] = {}, dm_[kMaxPart] = {}, gains_[40] = {};
  double ph_ = 0, t_ = 0; bool g_ = false;
};
// ---- Noise ------------------------------------------------------------------------------------------------------------------------
const double kHat[6] = {205.3, 304.4, 369.6, 522.7, 540, 800};
class Noise : public WModule {
 public:
  Noise(double, Seeds& s) : R_(s.next()) { for (int i = 0; i < 6; i++) ps_[i] = i * .137; }
  void arm(Seeds& s) override { R_ = Rng(s.next()); }
  void process(const IO& io, const Ctx& c) override {
    const int md = i32(io.p[0]); const double A = io.p[1], B = io.p[2], sr = c.sr;
    for (int i = 0; i < c.block; i++) {
      const double inc = hzOf((double)io.in[0][i] - 24) / sr; double y = 0;
      if (md <= 3) {
        if (c_ <= 0) {
          c_ = 1 + jround(A * A * 63 * sr / 48000); const double q = B > .01 ? js_pow(2, 16 - B * 14) / 2 : 0, w = R_() * 2 - 1;
          if (md == 0) y = w;
          else if (md == 1) { b_[0] = .99886 * b_[0] + w * .0555179; b_[1] = .99332 * b_[1] + w * .0750759; b_[2] = .969 * b_[2] + w * .153852; b_[3] = .8665 * b_[3] + w * .3104856;
            b_[4] = .55 * b_[4] + w * .5329522; b_[5] = -.7616 * b_[5] - w * .016898; y = (b_[0] + b_[1] + b_[2] + b_[3] + b_[4] + b_[5] + b_[6] + w * .5362) * .11; b_[6] = w * .115926; }
          else if (md == 2) { br_ = (br_ + .02 * w) / 1.02; y = br_ * 3.5; }
          else { y = (w - pw_) * .5; pw_ = w; }
          if (q) y = jround(y * q) / q; held_ = y;
        }
        c_--; y = held_;
      } else if (md == 4) {
        const double ex = .6 + A * .8, w = .5 - B * .4;
        for (int k = 0; k < 6; k++) { double p = ps_[k] + inc * 205.3 * js_pow(kHat[k] / 205.3, ex) / 65.40639; p -= std::floor(p); ps_[k] = p; y += p < w ? 1 : -1; }
        y = (y / 6 - (2 * w - 1)) * .9;
      } else if (md == 5) {
        acc_ = jmin(64, acc_ + inc * js_pow(2, A * 8 - 1) * 16);
        while (acc_ >= 1) { acc_ -= 1; const int32_t fb = (reg_ ^ (reg_ >> 1)) & 1; reg_ = (reg_ >> 1) | (fb << 14); if (B >= .5) reg_ = (reg_ & ~64) | (fb << 6); out_ = (reg_ & 1) ? 1 : -1; }
        y = out_ * .7;
      } else if (md == 6) {
        const double p = A * A * .25 * 48000 / sr, cf = .03 + B * .97, imp = R_() < p ? R_() * 2 - 1 : 0;
        y_ += (imp - y_) * cf; y = clamp(y_ * 1.5 / (.2 + cf), -1, 1);
      } else {
        acc_ += inc * js_pow(2, B * 6) * 4; if (acc_ >= 1) { acc_ -= std::floor(acc_); held_ = R_() * 2 - 1; }
        y_ += (held_ - y_) * (1 - A * .995); y = y_;
      }
      io.out[0][i] = (float)y;
    }
  }
 private:
  Rng R_; double c_ = 0, held_ = 0, b_[7] = {}, br_ = 0, pw_ = 0, ps_[6] = {}, acc_ = 1, out_ = 1, y_ = 0; int32_t reg_ = 0x7fff;
};
// ---- Plaits / Elements ------------------------------------------------------------------------------------------------------------
#ifdef THUNDER_ENGINE_PE
// Feeds fixed-rate wasm blocks out at the context's rate (the JS's Resampler)
struct Resampler {
  double step, p; int chunk; float a[17], b[17];
  Resampler() : step(0), p(0), chunk(12) { std::memset(a, 0, sizeof a); std::memset(b, 0, sizeof b); }
  void init(double rateIn, double sr, int ch) { step = rateIn / sr; chunk = ch; p = ch; }
  template <class F> void next(double* out, F render) {
    while (p >= chunk) { a[0] = a[chunk]; b[0] = b[chunk]; render(a, b); p -= chunk; }
    const int i = i32(p); const double f = p - i;
    out[0] = (double)a[i] + ((double)a[i + 1] - (double)a[i]) * f;
    out[1] = (double)b[i] + ((double)b[i + 1] - (double)b[i]) * f;
    p += step;
  }
};
// Hostile values (the wasm indexes its tables with them unchecked): a NaN becomes the parameter's default, any value is held inside the parameter's range, and a note inside +-200 semitones
inline double pin(const ParamDef& d, double v) { return v != v ? d.def : clamp(v, d.mn, d.mx); }
inline double noteIn(double v) { return v != v ? 0 : clamp(v, -200, 200); }
class PlaitsM : public WModule {
 public:
  PlaitsM(double sr, Seeds&) : ok_(sr >= 1000 && sr <= 1e6) { if (ok_) { s_ = thunder_pe::session_new(); thunder_pe::pl_init(s_); rs_.init(48000, sr, 12); } }
  ~PlaitsM() override { if (s_) thunder_pe::session_free(s_); }
  void process(const IO& io, const Ctx& c) override {
    if (!ok_ || !s_ || W(c).draft) { fill(io, 0, 0); fill(io, 1, 0); return; }
    double p[6]; for (int k = 0; k < 6; k++) p[k] = pin(kWP_plaits[k], io.p[k]);
    const bool g = lk(io, 1); const int eng = i32(p[0]);
    if (eng < 0 || eng > 23) { W(c).fail = true; return; }
    for (int i = 0; i < c.block; i++) {
      double fr[2];
      rs_.next(fr, [&](float* a, float* b) {
        thunder_pe::pl_set(s_, eng, (float)(60 + noteIn((double)io.in[0][i])), (float)clamp(p[1] + (double)io.in[4][i], 0, 1), (float)clamp(p[2] + (double)io.in[2][i], 0, 1),
          (float)clamp(p[3] + (double)io.in[3][i], 0, 1), (float)p[4], (float)p[5], 0.f, 0.f, 0.f, g ? 1 : 0);
        thunder_pe::pl_trigger(s_, g && io.in[1][i] > .5 ? 1.f : 0.f);
        thunder_pe::pl_render(s_, 12);
        const float* vo = thunder_pe::pl_out(s_); const float* va = thunder_pe::pl_aux(s_);
        for (int k = 0; k < 12; k++) { a[1 + k] = vo[k]; b[1 + k] = va[k]; }
      });
      io.out[0][i] = (float)fr[0]; io.out[1][i] = (float)fr[1];
    }
  }
 private:
  bool ok_; thunder_pe::Session* s_ = nullptr; Resampler rs_;
};
class ElementsM : public WModule {
 public:
  ElementsM(double sr, Seeds&) : ok_(sr >= 1000 && sr <= 1e6) { if (ok_) { s_ = thunder_pe::session_new(); thunder_pe::el_init(s_); rs_.init(32000, sr, 16); } }
  ~ElementsM() override { if (s_) thunder_pe::session_free(s_); }
  void process(const IO& io, const Ctx& c) override {
    if (!ok_ || !s_ || W(c).draft) { fill(io, 0, 0); fill(io, 1, 0); return; }
    double p[13]; for (int k = 0; k < 13; k++) p[k] = pin(kWP_elements[k], io.p[k]);
    const int model = i32(p[0]);
    if (model < 0 || model > 2) { W(c).fail = true; return; }
    for (int i = 0; i < c.block; i++) {
      double fr[2];
      rs_.next(fr, [&](float* a, float* b) {
        const bool g = lk(io, 1) ? io.in[1][i] > .5 : true;
        thunder_pe::el_set(s_, model, (float)(60 + noteIn((double)io.in[0][i])), (float)clamp(.8 + (double)io.in[2][i], 0, 1), (float)p[1], (float)p[2], (float)p[3], (float)p[4],
          (float)p[6], (float)p[7], (float)p[6], (float)p[5], (float)p[6], (float)p[8], (float)p[9], (float)p[10], (float)p[11], (float)p[12]);
        thunder_pe::el_gate(s_, g ? 1 : 0);
        thunder_pe::el_process(s_, 16);
        const float* vo = thunder_pe::el_main(s_); const float* va = thunder_pe::el_aux(s_);
        for (int k = 0; k < 16; k++) { a[1 + k] = vo[k]; b[1 + k] = va[k]; }
      });
      io.out[0][i] = (float)fr[0]; io.out[1][i] = (float)fr[1];
    }
  }
 private:
  bool ok_; thunder_pe::Session* s_ = nullptr; Resampler rs_;
};
#else
class PlaitsM : public WModule { public: PlaitsM(double, Seeds&) {} void process(const IO& io, const Ctx&) override { fill(io, 0, 0); fill(io, 1, 0); } };
class ElementsM : public WModule { public: ElementsM(double, Seeds&) {} void process(const IO& io, const Ctx&) override { fill(io, 0, 0); fill(io, 1, 0); } };
#endif
// ---- VCF / VCA / Mixer ------------------------------------------------------------------------------------------------------------
class VCF : public WModule {
 public:
  VCF(double, Seeds&) {}
  void process(const IO& io, const Ctx& c) override {
    const int mode = i32(io.p[2]); const double q = 1 - io.p[1] * .95; const double pc = js_pow(10, io.p[0] * 3);
    for (int i = 0; i < c.block; i++) {
      const double fc = jmin(.45, (.01 + pc * .004) * js_pow(2, (double)io.in[1][i] * 3) * 44100 / c.sr);
      const double g = js_tan(kPi * fc), a1 = 1 / (1 + g * (g + q)), a2 = g * a1, a3 = g * a2;
      const double x = io.in[0][i], v3 = x - ic2_, bp = a1 * ic1_ + a2 * v3, lp = ic2_ + a2 * ic1_ + a3 * v3;
      ic1_ = 2 * bp - ic1_; ic2_ = 2 * lp - ic2_;
      if (!(std::fabs(ic1_) < 1e4 && std::fabs(ic2_) < 1e4)) ic1_ = ic2_ = 0;
      io.out[0][i] = (float)(mode == 0 ? lp : mode == 1 ? bp : x - q * bp - lp);
    }
  }
 private:
  double ic1_ = 0, ic2_ = 0;
};
class VCA : public WModule {
 public:
  VCA(double, Seeds&) {}
  void process(const IO& io, const Ctx& c) override {
    const double g = io.p[0]; const bool cv = lk(io, 1);
    for (int i = 0; i < c.block; i++) io.out[0][i] = (float)((double)io.in[0][i] * (cv ? (double)io.in[1][i] : 1) * g);
  }
};
class Mixer : public WModule {
 public:
  Mixer(double, Seeds&) {}
  void process(const IO& io, const Ctx& c) override {
    const double* p = io.p; const int n = c.block; const double r = jround(p[0]); const double nn = (r != 0 && r == r) ? r : 4; const int N = (int)clamp(nn, 1, 16);
    for (int i = 0; i < kBlock; i++) io.out[0][i] = 0;
    for (int k = 0; k < N; k++) {
      if (!lk(io, k)) continue;
      const float* x = io.in[k]; const double g = p[1 + k] * p[17];
      for (int i = 0; i < n; i++) io.out[0][i] = (float)((double)io.out[0][i] + (double)x[i] * g);
    }
  }
};
// ---- control ----------------------------------------------------------------------------------------------------------------------
class LFO : public WModule {
 public:
  LFO(double, Seeds& s) : R_(s.next()) {}
  void arm(Seeds& s) override { R_ = Rng(s.next()); }
  void process(const IO& io, const Ctx& c) override {
    const double* p = io.p;
    const double hz = p[1] >= .5 ? (c.tempo / 60) * p[0] * .25 : p[0]; const int sh = i32(p[2]); const double d = p[3];
    for (int i = 0; i < c.block; i++) {
      const bool g = io.in[0][i] > .5; if (g && !r_) phase_ = 0; r_ = g;
      const double before = phase_;
      phase_ += hz / c.sr; phase_ -= std::floor(phase_);
      if (phase_ < before) held_ = R_() * 2 - 1;
      const double t = phase_;
      io.out[0][i] = (float)(d * (sh == 1 ? (t < .5 ? 1 : -1) : sh == 2 ? 2 * t - 1 : sh == 3 ? 1 - 4 * std::fabs(t - .5) : sh == 4 ? held_ : sinT(t)));
    }
  }
 private:
  Rng R_; double phase_ = 0, held_ = 0; bool r_ = false;
};
class ADSR : public WModule {
 public:
  ADSR(double, Seeds&) {}
  void process(const IO& io, const Ctx& c) override {
    const double a = io.p[0], d = io.p[1], s = io.p[2], r = io.p[3], dt = 1 / c.sr, dep = io.p[4];
    for (int i = 0; i < c.block; i++) {
      const bool g = io.in[0][i] > .5;
      if (g && (stage_ == 0 || stage_ == 4)) stage_ = 1;
      if (!g && stage_ != 0 && stage_ != 4) stage_ = 4;
      if (stage_ == 1) { level_ += dt / jmax(a, 1e-4); if (level_ >= 1) { level_ = 1; stage_ = 2; } }
      else if (stage_ == 2) level_ += (s - level_) * dt / jmax(d, 1e-4) * 6;
      else if (stage_ == 4) { level_ -= level_ * dt / jmax(r, 1e-4) * 6 + dt * 1e-3; if (level_ <= 0) { level_ = 0; stage_ = 0; } }
      io.out[0][i] = (float)(level_ * dep);
    }
  }
 private:
  int stage_ = 0; double level_ = 0;
};
class RandomM : public WModule {
 public:
  RandomM(double, Seeds& s) : R_(s.next()) {}
  void arm(Seeds& s) override { R_ = Rng(s.next()); }
  void process(const IO& io, const Ctx& c) override {
    const bool clk = lk(io, 0); const double k = 1 - js_pow(io.p[1], .15) * .9995;
    for (int i = 0; i < c.block; i++) {
      bool fire = false;
      if (clk) { const bool g = io.in[0][i] > .5; fire = g && !c_; c_ = g; }
      else { ph_ += io.p[0] / c.sr; if (ph_ >= 1) { ph_ -= 1; fire = true; } }
      if (fire) held_ = R_() * 2 - 1;
      y_ += (held_ - y_) * k;
      io.out[0][i] = (float)(y_ * io.p[2]);
    }
  }
 private:
  Rng R_; double ph_ = 0, held_ = 0, y_ = 0; bool c_ = false;
};
class Atten : public WModule {
 public:
  Atten(double, Seeds&) {}
  void process(const IO& io, const Ctx& c) override { const double a = io.p[0], o = io.p[1]; for (int i = 0; i < c.block; i++) io.out[0][i] = (float)((double)io.in[0][i] * a + o); }
};
const double kRates[6] = {1, 2, 3, 4, 6, 8};
class Sequencer : public WModule {
 public:
  Sequencer(double, Seeds&) {}
  void destroy(WavesEngine* e) override;
  void process(const IO& io, const Ctx& c) override;
 private:
  double pos_ = -1, acc_ = 1, off_ = 0, gateT_ = 0, clkT_ = 0, pitch_ = 0, note_ = 0; bool c_ = false, rs_ = false, hasNote_ = false;
};
class Out : public WModule {
 public:
  Out(double, Seeds&) {}
  void process(const IO& io, const Ctx& c) override {
    const WCtx& w = W(c); const int k = (int)clamp(((double)i32(io.p[0]) - 1), 0, kBuses - 1); const double g = io.p[1]; float* L = w.busL[k]; float* R = w.busR[k];
    const float* inR = lk(io, 1) ? io.in[1] : io.in[0];
    for (int i = 0; i < c.block; i++) { L[i] = (float)((double)L[i] + (double)io.in[0][i] * g); R[i] = (float)((double)R[i] + (double)inR[i] * g); }
  }
};
class Scope : public WModule { public: Scope(double, Seeds&) {} void process(const IO&, const Ctx&) override {} };   // a probe: it only draws a picture (show() is not ported)

// ---- String (Karplus-Strong) --------------------------------------------------------------------------------------------------------
class KSString {
 public:
  explicit KSString(double sr) : sr_(sr) {}
  void pluck(double P, double bright, double pos, double amp, Rng& rnd) {
    P = jmax(2, jmin(N - 4, P));
    const double nd = std::ceil(P) + 2; const int n = nd == nd && nd >= 0 && nd <= kScratch ? (int)nd : 0;
    const double k = 0.04 + 0.96 * bright * bright; double lp = 0, pk = 1e-9;
    for (int i = 0; i < n; i++) { lp += (rnd() * 2 - 1 - lp) * k; ex_[i] = (float)lp; }
    const double d = jmax(1, jround(pos * P));
    if (pos > 0.01) {
      for (int i = 0; i < n; i++) src_[i] = ex_[i];
      const double L = jmax(2, jround(P));
      for (int i = 0; i < n; i++) { const double idx = jmod(jmod(i - d, L) + L, L); ex_[i] = (float)((double)src_[i] - rdf(src_, n, idx)); }
    }
    double m = 0; for (int i = 0; i < n; i++) m += ex_[i]; m /= n;
    for (int i = 0; i < n; i++) { ex_[i] = (float)((double)ex_[i] - m); pk = jmax(pk, std::fabs((double)ex_[i])); }
    const double g = amp / pk;
    for (int i = 0; i < n; i++) { const int j = (int)jmod(w_ - n + i + N, N); b_[j] = (float)((double)b_[j] + (double)ex_[i] * g); }
  }
  double tick(double P, double t60, double a) {
    P = jmax(2, jmin(N - 4, P));
    const double dd = jmax(1.2, P - (1 - a) / a), r = w_ - dd, ri = std::floor(r), fr = r - ri;
    const double i0 = ri < 0 ? ri + N : ri, i1 = i0 + 1 >= N ? 0 : i0 + 1;
    const double b0 = rdf(b_, N, i0), x = b0 + (rdf(b_, N, i1) - b0) * fr;
    lp_ += (x - lp_) * a;
    if (!haveC_ || P != cP_ || t60 != cT_) { haveC_ = true; cP_ = P; cT_ = t60; g_ = js_pow(10, -3 * (P / sr_) / jmax(0.01, t60)); }
    const double y = lp_ * g_;
    b_[w_] = (float)y; w_ = w_ + 1 == N ? 0 : w_ + 1;
    const double o = y - dx_ + 0.995 * dc_; dx_ = y; dc_ = o; return o;
  }
  static constexpr int N = 4096, kScratch = 4100;
 private:
  double sr_; float b_[N] = {}; float ex_[kScratch] = {}, src_[kScratch] = {}; int w_ = 0; double lp_ = 0, dc_ = 0, dx_ = 0, cP_ = 0, cT_ = 0, g_ = 0; bool haveC_ = false;
};
class KString : public WModule {
 public:
  KString(double sr, Seeds& s) : ks_(new KSString(sr)), r_(s.next()) {}
  void arm(Seeds& s) override { r_ = Rng(s.next()); }
  void process(const IO& io, const Ctx& c) override {
    const double* p = io.p; const double sr = c.sr;
    const double a = .25 + .74 * p[1], t60 = .08 * js_pow(100, p[2]), tm = t60 * (1 - p[4] * .97);
    for (int i = 0; i < c.block; i++) {
      const double P = sr / hzOf((double)io.in[0][i] + p[0]); const bool on = io.in[1][i] >= .5;
      if (on && !lg_) ks_->pluck(P, p[1], p[3], .9, r_);
      lg_ = on;
      io.out[0][i] = (float)(ks_->tick(P, on ? t60 : tm, a) * p[5]);
    }
  }
 private:
  std::unique_ptr<KSString> ks_; Rng r_; bool lg_ = false;
};

// ---- Pitch delay / Pitch reverb (modules of the pads' PitchFxCore) ---------------------------------------------------------------------
inline void fxPass(const IO& io, int nout = 2) { (void)nout; std::memcpy(io.out[0], io.in[0], sizeof(float) * kBlock); std::memcpy(io.out[1], io.linked[1] ? io.in[1] : io.in[0], sizeof(float) * kBlock); }
class PDelayM : public WModule {
 public:
  PDelayM(double sr, Seeds&) : c_(sr) {}
  void process(const IO& io, const Ctx& c) override {
    fxPass(io); const double* p = io.p; PfDp q; q.kind = 0; q.mode = clamp((double)i32(p[0]), 0, 4); q.time = clamp(p[1], 20, 2000); q.fb = clamp(p[2], 0, .95); q.pitch = clamp(jround(p[3]), -24, 24);
    q.tone = u01(p[4]); q.wow = u01(p[5]); q.mix = u01(p[6]); q.pitch2 = -12; q.size = .6;
    c_.process(io.out[0], io.out[1], c.block, q);
  }
 private:
  PitchFxCore c_;
};
class PReverbM : public WModule {
 public:
  PReverbM(double sr, Seeds&) : c_(sr) {}
  void process(const IO& io, const Ctx& c) override {
    fxPass(io); const double* p = io.p; PfDp q; q.kind = 1; q.mode = clamp((double)i32(p[0]), 0, 2); q.size = u01(p[1]); q.fb = clamp(p[2], 0, .9); q.pitch = clamp(jround(p[3]), -24, 24);
    q.pitch2 = clamp(jround(p[4]), -24, 24); q.tone = u01(p[5]); q.mix = u01(p[6]); q.time = 380; q.wow = .2;
    c_.process(io.out[0], io.out[1], c.block, q);
  }
 private:
  PitchFxCore c_;
};

// ---- Expression (MathExpr) ----------------------------------------------------------------------------------------------------------
struct XProg { int err = 0, pos = 0, n = 0, depth = 0; bool rnd = false; uint8_t ops[128]; double args[128]; };
struct MxFn { const char* name; int op, n; };
const MxFn kMxFuncs[] = {{"sin", 20, 1}, {"cos", 21, 1}, {"tan", 22, 1}, {"tanh", 23, 1}, {"abs", 24, 1}, {"sqrt", 25, 1}, {"exp", 26, 1}, {"log", 27, 1}, {"floor", 28, 1}, {"ceil", 29, 1}, {"round", 30, 1}, {"sign", 31, 1}, {"fract", 32, 1},
  {"min", 33, 2}, {"max", 34, 2}, {"pow", 7, 2}, {"mod", 6, 2}, {"clip", 35, 3}, {"mix", 36, 3}, {"tri", 37, 1}, {"saw", 38, 1}, {"sq", 39, 1}};
const char* const kMxVars[] = {"a", "b", "c", "d", "k1", "k2", "t", "y", "pi", "sr", "rnd"};
struct XComp {
  const uint16_t* s; int len; int i = 0, depth = 0, maxDepth = 0, err = 0, epos = 0, level = 0; bool rnd = false; XProg* P;
  bool fail(int c, int p) { if (!err) { err = c; epos = p; } return false; }
  bool emit(int op, double arg, int d) { if (P->n >= 128) return fail(3, i); P->ops[P->n] = (uint8_t)op; P->args[P->n] = arg; P->n++; depth += d; if (depth > maxDepth) maxDepth = depth; if (maxDepth > 32) return fail(3, i); return true; }
  int ch(int k) const { return k >= 0 && k < len ? s[k] : -1; }
  void ws() { while (i < len && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++; }
  bool eat(const char* tok) { ws(); const int n = (int)std::strlen(tok); for (int k = 0; k < n; k++) if (ch(i + k) != tok[k]) return false; i += n; return true; }
  static bool isD(int c) { return c >= '0' && c <= '9'; }
  static bool isA(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
  static bool opch(int c) { return c >= 0 && c < 128 && std::strchr("+-*/%^<>=!&|?:(),.", c) && c != 0; }
  bool primary() {
    ws(); if (i >= len) return fail(8, i);
    const int c = s[i], p0 = i;
    if (isD(c) || (c == '.' && isD(ch(i + 1)))) {
      int j = i; while (j < len && isD(s[j])) j++; if (ch(j) == '.') { j++; while (j < len && isD(s[j])) j++; }
      if ((ch(j) == 'e' || ch(j) == 'E') && (isD(ch(j + 1)) || ((ch(j + 1) == '-' || ch(j + 1) == '+') && isD(ch(j + 2))))) { j += 2; while (j < len && isD(s[j])) j++; }
      char buf[256]; int n = 0; for (int k = i; k < j && n < 255; k++) buf[n++] = (char)s[k]; buf[n] = 0;
      double v = std::strtod(buf, nullptr); i = j; return emit(0, v == v ? v : 0, 1);
    }
    if (isA(c)) {
      int j = i; while (j < len && (isA(s[j]) || isD(s[j]))) j++;
      char nm[64]; int n = 0; for (int k = i; k < j && n < 63; k++) nm[n++] = (char)(s[k] >= 'A' && s[k] <= 'Z' ? s[k] + 32 : s[k]); nm[n] = 0; const bool longName = (j - i) > 63; i = j; ws();
      if (ch(i) == '(') {
        const MxFn* fn = nullptr; if (!longName) for (const MxFn& f : kMxFuncs) if (!std::strcmp(f.name, nm)) fn = &f;
        if (!fn) return fail(5, p0);
        i++; if (++level > 24) return fail(3, i); int cnt = 0; ws();
        if (ch(i) != ')') { for (;;) { if (!ternary()) return false; cnt++; ws(); if (ch(i) == ',') { i++; continue; } break; } }
        if (ch(i) != ')') return fail(6, i); i++; level--;
        if (cnt != fn->n) return fail(7, p0);
        return emit(fn->op, 0, 1 - cnt);
      }
      int vi = -1; if (!longName) for (int k = 0; k < 11; k++) if (!std::strcmp(kMxVars[k], nm)) vi = k;
      if (vi < 0) return fail(5, p0);
      if (vi == 10) rnd = true; return emit(1, vi, 1);
    }
    if (c == '(') { i++; if (++level > 24) return fail(3, i); if (!ternary()) return false; ws(); if (ch(i) != ')') return fail(6, i); i++; level--; return true; }
    return fail(isA(c) || isD(c) || opch(c) ? 8 : 4, i);
  }
  bool power() { if (!primary()) return false; ws(); if (ch(i) == '^') { i++; if (!unary()) return false; return emit(7, 0, -1); } return true; }
  bool unary() {
    ws();
    if (ch(i) == '-') { i++; if (++level > 24) return fail(3, i); if (!unary()) return false; level--; return emit(8, 0, 0); }
    if (ch(i) == '+') { i++; return unary(); }
    if (ch(i) == '!' && ch(i + 1) != '=') { i++; if (++level > 24) return fail(3, i); if (!unary()) return false; level--; return emit(9, 0, 0); }
    return power();
  }
  bool mul() { if (!unary()) return false; for (;;) { ws(); const int c = ch(i); if (c == '*' || c == '/' || c == '%') { i++; if (!unary()) return false; if (!emit(c == '*' ? 4 : c == '/' ? 5 : 6, 0, -1)) return false; } else return true; } }
  bool add() { if (!mul()) return false; for (;;) { ws(); const int c = ch(i); if (c == '+' || c == '-') { i++; if (!mul()) return false; if (!emit(c == '+' ? 2 : 3, 0, -1)) return false; } else return true; } }
  bool cmp() {
    if (!add()) return false;
    for (;;) {
      ws(); int op = 0;
      if (eat("<=")) op = 11; else if (eat(">=")) op = 13; else if (eat("==")) op = 14; else if (eat("!=")) op = 15; else if (ch(i) == '<') { i++; op = 10; } else if (ch(i) == '>') { i++; op = 12; } else return true;
      if (!add()) return false; if (!emit(op, 0, -1)) return false;
    }
  }
  bool andd() { if (!cmp()) return false; for (;;) { if (eat("&&")) { if (!cmp()) return false; if (!emit(16, 0, -1)) return false; } else return true; } }
  bool orr() { if (!andd()) return false; for (;;) { if (eat("||")) { if (!andd()) return false; if (!emit(17, 0, -1)) return false; } else return true; } }
  bool ternary() {
    if (!orr()) return false; ws();
    if (ch(i) == '?') { i++; if (++level > 24) return fail(3, i); if (!ternary()) return false; ws(); if (ch(i) != ':') return fail(8, i); i++; if (!ternary()) return false; level--; return emit(18, 0, -2); }
    return true;
  }
};
void mxCompile(const uint16_t* s, int len, XProg& P) {
  P = XProg();
  if (len > 200) { P.err = 2; P.pos = 200; return; }
  XComp c; c.s = s; c.len = len; c.P = &P;
  c.ws(); if (c.i >= len) { P.err = 1; P.pos = 0; return; }
  if (!c.ternary()) { P.n = 0; P.err = c.err ? c.err : 8; P.pos = c.epos; return; }
  c.ws();
  if (c.i < len) { const int ch = s[c.i]; P.n = 0; P.err = (XComp::opch(ch) || XComp::isA(ch) || XComp::isD(ch)) ? 9 : 4; P.pos = c.i; return; }
  P.err = 0; P.pos = 0; P.depth = c.maxDepth; P.rnd = c.rnd;
}
double mxRun(const XProg& P, double* S, const double* V) {
  int sp = 0;
  for (int k = 0; k < P.n; k++) {
    const int op = P.ops[k];
    if (op == 0) S[sp++] = P.args[k];
    else if (op == 1) S[sp++] = V[(int)P.args[k]];
    else if (op < 8 || (op >= 10 && op <= 17) || op == 33 || op == 34) {
      const double b = S[--sp], a = S[sp - 1]; double r;
      switch (op) {
        case 2: r = a + b; break; case 3: r = a - b; break; case 4: r = a * b; break; case 5: r = b == 0 ? 0 : a / b; break; case 6: r = b == 0 ? 0 : a - b * std::floor(a / b); break; case 7: r = js_pow(a, b); break;
        case 10: r = a < b ? 1 : 0; break; case 11: r = a <= b ? 1 : 0; break; case 12: r = a > b ? 1 : 0; break; case 13: r = a >= b ? 1 : 0; break; case 14: r = a == b ? 1 : 0; break; case 15: r = a != b ? 1 : 0; break;
        case 16: r = a != 0 && b != 0 ? 1 : 0; break; case 17: r = a != 0 || b != 0 ? 1 : 0; break; case 33: r = a < b ? a : b; break; default: r = a > b ? a : b; }
      S[sp - 1] = r;
    } else if (op == 18) { const double c = S[--sp], b = S[--sp]; S[sp - 1] = S[sp - 1] != 0 ? b : c; }
    else if (op == 35) { const double hi = S[--sp], lo = S[--sp], x = S[sp - 1]; S[sp - 1] = x < lo ? lo : x > hi ? hi : x; }
    else if (op == 36) { const double m = S[--sp], b = S[--sp], a = S[sp - 1]; S[sp - 1] = a + (b - a) * m; }
    else {
      const double x = S[sp - 1]; double r;
      switch (op) {
        case 8: r = -x; break; case 9: r = x == 0 ? 1 : 0; break; case 20: r = js_sin(x); break; case 21: r = js_cos(x); break; case 22: r = js_tan(x); break; case 23: r = js_tanh(x); break;
        case 24: r = x < 0 ? -x : x; break; case 25: r = x > 0 ? std::sqrt(x) : 0; break; case 26: r = js_exp(x); break; case 27: r = x > 1e-300 ? js_log(x) : 0; break; case 28: r = std::floor(x); break; case 29: r = std::ceil(x); break;
        case 30: r = js_round(x); break; case 31: r = x > 0 ? 1 : x < 0 ? -1 : 0; break; case 32: r = x - std::floor(x); break;
        case 37: { const double f = x - std::floor(x); r = f < .5 ? 4 * f - 1 : 3 - 4 * f; break; } case 38: r = 2 * (x - std::floor(x)) - 1; break; default: r = x - std::floor(x) < .5 ? 1 : -1; }
      S[sp - 1] = r;
    }
  }
  return S[0];
}
const uint16_t kMxDefault[] = {'a', ' ', '*', ' ', 'k', '1'};
class Expression : public WModule {
 public:
  Expression(double, Seeds&) {}
  void prepareText(const uint16_t* text, int len) override { ensureSrc(len >= 0 ? text : kMxDefault, len >= 0 ? len : 6); }   // U31: compiled when the patch is prepared; the first block finds the same text and compiles nothing
  void process(const IO& io, const Ctx& c) override {
    const WCtx& w = W(c); const uint16_t* txt = w.textLen >= 0 ? w.text : kMxDefault; const int tl = w.textLen >= 0 ? w.textLen : 6;
    ensureSrc(txt, tl);
    if (rs_(c)) n_ = 0;
    float* O = io.out[0];
    if (prog_.err) { for (int i = 0; i < c.block; i++) O[i] = 0; return; }
    const bool useR = prog_.rnd; double y = y_, n = n_; uint32_t rs = rs_s_ ? rs_s_ : 0;
    if (!rs_s_ && useR) { const uint32_t sd = w.host ? w.host->nextSeed() : 1; rs_s_ = sd ? sd : 1; rs = rs_s_; }
    V_[4] = io.p[0]; V_[5] = io.p[1]; V_[8] = kPi; V_[9] = c.sr;
    for (int i = 0; i < c.block; i++) {
      V_[0] = io.in[0][i]; V_[1] = io.in[1][i]; V_[2] = io.in[2][i]; V_[3] = io.in[3][i]; V_[6] = n / c.sr; V_[7] = y;
      if (useR) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; V_[10] = (double)rs / 2147483648.0 - 1; }
      y = mxRun(prog_, S_, V_);
      if (!(y > -4)) y = y < 0 ? -4 : 0; else if (y > 4) y = 4;
      O[i] = (float)y; n++;
    }
    y_ = y; n_ = n; rs_s_ = useR ? rs : rs_s_;
  }
 private:
  void ensureSrc(const uint16_t* txt, int tl) {
    if (!haveSrc_ || tl != srcLen_ || (tl && std::memcmp(txt, src_, sizeof(uint16_t) * (size_t)tl))) {
      haveSrc_ = true; srcLen_ = tl > 200 ? 200 : tl; std::memcpy(src_, txt, sizeof(uint16_t) * (size_t)srcLen_); mxCompile(src_, tl, prog_); y_ = 0;
    }
  }
  Restart rs_; XProg prog_; uint16_t src_[200]; int srcLen_ = 0; bool haveSrc_ = false; double y_ = 0, n_ = 0, S_[40] = {}, V_[11] = {}; uint32_t rs_s_ = 0;
};

// ---- the tfx effects (effect()) --------------------------------------------------------------------------------------------------------
using MapFn = void (*)(const double* p, const WCtx& c, double* out);
#ifdef THUNDER_HAVE_TFX
Fx* make_fx(int type, float sr) {
  Fx* f = nullptr;
  switch (type) {
    case 0: f = make_rings(sr); break;   case 1: f = make_clouds(sr); break;
    case 2: f = make_density(sr); break; case 3: f = make_totape(sr); break;
    case 4: f = make_chorus(sr); break;  case 5: f = make_crush(sr); break;
    case 6: f = make_eq(sr); break;      case 7: f = make_comp(sr); break;
    case 8: f = make_reverb(sr); break;  case 9: f = make_delay(sr); break;
    case 10: f = make_beads(sr); break;
    case 11: f = make_flanger(sr); break;   case 12: f = make_phaser(sr); break;
    case 13: f = make_tremolo(sr); break;   case 14: f = make_overdrive(sr); break;
    case 15: f = make_folder(sr); break;    case 16: f = make_autowah(sr); break;
    case 17: f = make_pitch(sr); break;     case 18: f = make_ladder(sr); break;
    case 19: f = make_warps(sr); break;     case 20: f = make_cloudseed(sr); break;
    case 21: f = make_derez2(sr); break;    case 22: f = make_purestdrive(sr); break;
    case 23: f = make_air3(sr); break;      case 24: f = make_verbity2(sr); break;
    case 25: f = make_comb(sr); break;      case 26: f = make_transient(sr); break;
    case 27: f = make_gate(sr); break;      case 28: f = make_ott(sr); break;
    case 29: f = make_shift(sr); break;     case 30: f = make_tovinyl4(sr); break;
    case 31: f = make_dubly(sr); break;     case 32: f = make_cabs(sr); break;
    case 33: f = make_tube2(sr); break;
  }
  if (f) f->sr = sr;
  return f;
}
struct FxState { FxState(Host* h) { if (h) { thunder_wasi_rand_use(&h->wasiRand); thunder_fx_state_use(&h->stmRng, &h->daisySeed); } } ~FxState() { thunder_wasi_rand_use(nullptr); thunder_fx_state_use(nullptr, nullptr); } };
#endif
class Eff : public WModule {
 public:
  Eff(double sr, Seeds& s, const char* type, int id, MapFn map, int wetAt, bool rings) : id_(id), map_(map), wetAt_(wetAt), rings_(rings), host_(static_cast<WSeeds&>(s).host), wi_(winfo(type)) {
#ifdef THUNDER_HAVE_TFX
    // space (Cloud Seed): the host build sizes its buffers for 96 kHz (third_party/prepare.sh), so above that it is bypassed, in every build
    if (!((id == 3 || id == 20) && sr > 96000)) {   // tape (id 3): its flutter delay (500 samples) is scaled by the rate and overruns it above about 157 kHz, so it is bypassed above 96 kHz in every build
 FxState st(host_); fx_ = make_fx(id, (float)sr); }
#else
    (void)sr;
#endif
  }
  ~Eff() override {
#ifdef THUNDER_HAVE_TFX
    FxState st(host_); delete fx_;
#endif
  }
  void process(const IO& io, const Ctx& c) override {
    const WCtx& w = W(c); const int n = c.block;
    const float* L0 = io.in[0]; const float* R0 = io.linked[1] ? io.in[1] : io.in[0];
    if (!fx_) { std::memcpy(io.out[0], L0, sizeof(float) * kBlock); std::memcpy(io.out[1], R0, sizeof(float) * kBlock); return; }
#ifdef THUNDER_HAVE_TFX
    // Hostile values (the wasm effects trap or read out of bounds on them, the JS then bypasses the effect; here they would crash): a parameter is held inside its range (a NaN takes its default), and what the map
    // makes of it must be finite and sane; in range, which is every value a slider or a modulation cable gives, nothing changes
    double q[16]; const int np = wi_ ? wi_->np : 0; for (int k = 0; k < np && k < 16; k++) q[k] = pin(wi_->params[k], io.p[k]);
    double P[12]; for (int k = 0; k < 12; k++) P[k] = 0;
    if (rings_) { q[2] = noteIn(q[2] + (double)io.in[2][0]); }   // Rings: the note + the pitch input
    map_(q, w, P);
    for (int k = 0; k < 12; k++) P[k] = P[k] != P[k] ? 0 : clamp(P[k], -1e7, 1e7);
    for (int k = 0; k < 12; k++) fx_->p[k] = (float)P[k];
    const double wm = wetAt_ < 0 ? -1 : (double)fx_->p[wetAt_]; if (wm >= 0) fx_->p[wetAt_] = 1;
    for (int i = 0; i < n; i++) { const double a = L0[i], b = R0[i], dn = (i & 1) ? 1e-18 : -1e-18; L_[i] = (float)((a > -8 && a < 8 ? a : 0) + dn); R_[i] = (float)((b > -8 && b < 8 ? b : 0) + dn); }
    { FxState st(host_); fx_->process(L_, R_, n); }
    for (int i = 0; i < n; i++) { const float l = L_[i], r = R_[i]; if (!(l > -8.f && l < 8.f)) L_[i] = 0.f; if (!(r > -8.f && r < 8.f)) R_[i] = 0.f; }   // tfx_process's cleaning
    if (wm >= 0) { double rv[2]; rvMix(wm, rv); for (int i = 0; i < n; i++) { io.out[0][i] = (float)((double)L0[i] * rv[0] + (double)L_[i] * rv[1]); io.out[1][i] = (float)((double)R0[i] * rv[0] + (double)R_[i] * rv[1]); } return; }
    for (int i = 0; i < n; i++) { io.out[0][i] = L_[i]; io.out[1][i] = R_[i]; }
#endif
  }
 private:
  int id_; MapFn map_; int wetAt_; bool rings_; Host* host_; const WInfo* wi_;
#ifdef THUNDER_HAVE_TFX
  Fx* fx_ = nullptr; float L_[128] = {}, R_[128] = {};
#else
  void* fx_ = nullptr;
#endif
};
// the maps (index = the parameter's place in the JS's params object)
#define MAPF(name) void name(const double* p, const WCtx& c, double* o)
MAPF(m_eq) { (void)c; o[0] = u01(p[0]); o[1] = u01(p[1]); o[2] = u01(p[2]); o[3] = u01(p[5]); o[4] = u01(p[3]); o[5] = u01(p[4]); o[6] = u01(p[6]); o[7] = u01(p[7]); }
MAPF(m_comp) { (void)c; for (int k = 0; k < 5; k++) o[k] = u01(p[k]); }
MAPF(m_reverb) { (void)c; for (int k = 0; k < 5; k++) o[k] = u01(p[k]); }
MAPF(m_delay) { const double sp = clamp(88200 / (clamp(p[0], .07, 1.8) * c.sr), 1, 26); o[0] = js_pow((sp - 1) / 25, .25); o[1] = u01(p[1]); o[2] = u01(p[2]); o[3] = u01(p[3]); o[4] = u01(p[4]); o[5] = u01(p[5]); }
MAPF(m_sat) { (void)c; o[0] = .2 + u01(p[0]) * .8; o[1] = u01(p[1]); o[2] = u01(p[2]); o[3] = u01(p[3]); }
MAPF(m_tape) { (void)c; for (int k = 0; k < 6; k++) o[k] = u01(p[k]); }
MAPF(m_chorus) { (void)c; o[0] = u01(p[0]); o[1] = .05 * js_pow(160, u01(p[1])); o[2] = 1 + u01(p[2]) * 29; o[3] = u01(p[3]) * .95; o[4] = u01(p[4]); }
MAPF(m_crush) { (void)c; o[0] = u01(p[0]); o[1] = jround(clamp(p[1], 1, 16)); o[2] = p[2] >= .5 ? 1 : 0; o[3] = u01(p[3]); }
MAPF(m_rings) { (void)c; const double po[3] = {1, 2, 4}; const double t = tab(po, 3, (double)i32(p[1])); o[0] = i32(p[0]); o[1] = (t != 0 && t == t) ? t : 1; o[2] = 60 + p[2]; o[3] = u01(p[3]); o[4] = u01(p[4]); o[5] = u01(p[5]); o[6] = u01(p[6]); o[7] = u01(p[7]) * 2; o[8] = u01(p[8]); }
MAPF(m_clouds) { (void)c; o[0] = i32(p[0]); o[1] = p[1] >= .5 ? 1 : 0; o[2] = p[2] >= .5 ? 1 : 0; o[3] = u01(p[3]); o[4] = u01(p[4]); o[5] = p[5]; o[6] = u01(p[6]); o[7] = u01(p[7]); o[8] = u01(p[8]); o[9] = u01(p[9]); o[10] = u01(p[10]); o[11] = u01(p[11]); }
MAPF(m_beads) { (void)c; o[0] = i32(p[0]); o[1] = 0; o[2] = p[1] >= .5 ? 1 : 0; o[3] = u01(p[2]); o[4] = jmin(u01(p[3]), .995); o[5] = u01(p[4]); o[6] = u01(p[5]); o[7] = u01(p[6]); o[8] = u01(p[7]); o[9] = u01(p[8]); o[10] = u01(p[9]); o[11] = 0; }
MAPF(m_flanger) { (void)c; for (int k = 0; k < 5; k++) o[k] = p[k]; }
MAPF(m_tremolo) { (void)c; o[0] = p[0]; o[1] = p[1]; o[2] = i32(p[2]); }
MAPF(m_4raw) { (void)c; for (int k = 0; k < 4; k++) o[k] = p[k]; }
MAPF(m_3raw) { (void)c; for (int k = 0; k < 3; k++) o[k] = p[k]; }
MAPF(m_2raw) { (void)c; o[0] = p[0]; o[1] = p[1]; }
MAPF(m_1raw) { (void)c; o[0] = p[0]; }
MAPF(m_pshift) { (void)c; o[0] = i32(p[0]); o[1] = p[1]; o[2] = p[2]; o[3] = p[3]; }
MAPF(m_ladder) { (void)c; o[0] = p[0]; o[1] = p[1]; o[2] = p[2]; o[3] = i32(p[3]); o[4] = p[4]; }
MAPF(m_xmod) { (void)c; o[0] = p[0]; o[1] = p[1]; o[2] = p[4]; o[3] = p[5]; o[4] = i32(p[2]); o[5] = i32(p[3]); o[6] = p[6]; }
MAPF(m_space) { (void)c; o[0] = jmin(p[0], .99); for (int k = 1; k < 8; k++) o[k] = p[k]; }
MAPF(m_comb) { (void)c; o[0] = 440 * js_pow(2, (i32(p[0]) - 69) / 12.0); o[1] = p[1]; o[2] = p[2]; o[3] = p[3]; o[4] = p[4]; }
MAPF(m_tgate) {
  static const double dv[6] = {0.125, 0.16666667, 0.25, 0.33333333, 0.5, 1}, pt[9] = {65535, 21845, 43690, 18761, 22235, 54749, 4879, 4353, 5193};
  o[0] = c.tempo / 60 / tab(dv, 6, (double)i32(p[0])); o[1] = p[2]; o[2] = p[3]; o[3] = p[4]; o[4] = tab(pt, 9, (double)i32(p[1])); o[5] = 16; o[6] = truthy(c.startN) ? c.startN : 0; o[7] = 0;
}
MAPF(m_ott) { (void)c; for (int k = 0; k < 6; k++) o[k] = p[k]; }
MAPF(m_fshift) { (void)c; o[0] = jsign(p[0]) * p[0] * p[0] * 2000; o[1] = p[1]; o[2] = p[2]; }
MAPF(m_cab) { (void)c; o[0] = ((i32(p[0]) + 0.5) / 6); for (int k = 1; k < 6; k++) o[k] = p[k]; }
#undef MAPF


// a parameter count is needed by Eff: the factory table below sets it from the generated table
template <class M> std::unique_ptr<Module> mk(double sr, Seeds& s) { return std::unique_ptr<Module>(new M(sr, s)); }

void Sequencer::destroy(WavesEngine* e) { if (hasNote_ && e) e->noteOff(note_); hasNote_ = false; }
void Sequencer::process(const IO& io, const Ctx& c) {
  const WCtx& w = W(c); const double* p = io.p; WavesEngine* eng = w.eng;
  const double steps = clamp((double)i32(p[2]), 1, 8); const bool ext = lk(io, 0);
  const double stepLen = 60 / jmax(20, c.tempo) / tab(kRates, 6, clamp((double)i32(p[1]), 0, 5)) * c.sr;
  for (int i = 0; i < c.block; i++) {
    const bool r = io.in[1][i] > .5; if (r && !rs_) pos_ = -1; rs_ = r;
    bool fire = false;
    if (ext) { const bool g = io.in[0][i] > .5; fire = g && !c_; c_ = g; }
    else if (p[0] >= .5) { acc_ += 1 / stepLen; if (acc_ >= 1) { acc_ -= 1; fire = true; } }
    if (hasNote_ && (--off_ <= 0)) { if (eng) eng->noteOff(note_); hasNote_ = false; }
    if (fire) {
      pos_ = jmod(pos_ + 1, steps);
      const int si = (int)pos_ + 1; const double v = (si >= 1 && si <= 8) ? p[4 + si - 1] : kNaN;
      if (hasNote_) { if (eng) eng->noteOff(note_); hasNote_ = false; }
      clkT_ = jround(c.sr * .005);
      if (v > -25) {
        pitch_ = v; note_ = v; hasNote_ = true; if (eng) eng->noteOn(v, 1);
        off_ = gateT_ = jmax(16, jround((ext ? c.sr * .25 : stepLen) * p[3]));
      }
    }
    io.out[0][i] = (float)pitch_;
    io.out[1][i] = gateT_ > 0 ? 1 : 0; if (gateT_ > 0) gateT_--;
    io.out[2][i] = clkT_ > 0 ? 1 : 0; if (clkT_ > 0) clkT_--;
  }
  if (p[0] < .5 && !ext && hasNote_) { if (eng) eng->noteOff(note_); hasNote_ = false; }
}

struct Own { const char* type; std::unique_ptr<Module> (*make)(double, Seeds&); bool fx = false; };
// effect factories: id, map, wetAt (the effect()'s arguments)
#define EF(T, ID, MAP, WET, R) {T, [](double sr, Seeds& s) -> std::unique_ptr<Module> { return std::unique_ptr<Module>(new Eff(sr, s, T, ID, MAP, WET, R)); }, true}
const Own kOwn[] = {
  {"hostIn", mk<HostIn>}, {"vco", mk<VCO>}, {"fm", mk<FM>}, {"additive", mk<Additive>}, {"noise", mk<Noise>}, {"plaits", mk<PlaitsM>}, {"elements", mk<ElementsM>}, {"vcf", mk<VCF>}, {"vca", mk<VCA>}, {"mixer", mk<Mixer>},
  {"lfo", mk<LFO>}, {"adsr", mk<ADSR>}, {"random", mk<RandomM>}, {"atten", mk<Atten>}, {"seq", mk<Sequencer>}, {"out", mk<Out>}, {"string", mk<KString>}, {"scope", mk<Scope>}, {"expr", mk<Expression>},
  {"pdelay", mk<PDelayM>}, {"preverb", mk<PReverbM>},
  EF("eq", 6, m_eq, -1, false), EF("comp", 7, m_comp, -1, false), EF("reverb", 8, m_reverb, 4, false), EF("delay", 9, m_delay, -1, false), EF("saturation", 2, m_sat, -1, false), EF("tape", 3, m_tape, -1, false),
  EF("chorus", 4, m_chorus, -1, false), EF("crush", 5, m_crush, -1, false), EF("rings", 0, m_rings, -1, true), EF("clouds", 1, m_clouds, -1, false), EF("beads", 10, m_beads, -1, false),
  EF("flanger", 11, m_flanger, -1, false), EF("tremolo", 13, m_tremolo, -1, false), EF("odrive", 14, m_4raw, -1, false), EF("folder", 15, m_4raw, -1, false), EF("autowah", 16, m_3raw, -1, false),
  EF("pshift", 17, m_pshift, -1, false), EF("ladder", 18, m_ladder, -1, false), EF("xmod", 19, m_xmod, -1, false), EF("space", 20, m_space, 7, false), EF("lofi", 21, m_4raw, -1, false),
  EF("drive", 22, m_1raw, -1, false), EF("air", 23, m_2raw, -1, false), EF("room", 24, m_4raw, 3, false), EF("comb", 25, m_comb, -1, false), EF("transient", 26, m_4raw, -1, false),
  EF("tgate", 27, m_tgate, -1, false), EF("ott", 28, m_ott, -1, false), EF("fshift", 29, m_fshift, -1, false), EF("vinyl", 30, m_4raw, -1, false), EF("dub", 31, m_2raw, -1, false),
  EF("cab", 32, m_cab, -1, false), EF("tube", 33, m_2raw, -1, false),
};
#undef EF
std::vector<Def>& ownDefs() {
  static std::vector<Def> v = [] {
    std::vector<Def> r;
    for (const Own& o : kOwn) { const WInfo* w = winfo(o.type); if (!w) continue; r.push_back(Def{o.type, w->nin, w->in, w->nout, w->out, w->np, w->params, o.make}); }
    return r;
  }();
  return v;
}
}  // namespace

const Def* wdef(std::string_view type) {
  std::string t(type);
  if (const Def* d = modfx::find(t.c_str())) return d;
  for (const Def& d : ownDefs()) if (t == d.type) return &d;
  return nullptr;
}

const ModTable& wavesTable() { static const ModTable t = {&winfo, &wdef}; return t; }

bool waves_type_is_fx(std::string_view type) { const std::string t(type); for (const Own& o : kOwn) if (o.fx && t == o.type) return true; return false; }

}  // namespace waves
}  // namespace thunder
