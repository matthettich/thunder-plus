// MODFX modules, stereo and modulation effects and dynamics (chains-worklet.js 933 to 1141): Meta-modulator, Phaser, Stereo width, Ping-pong delay, Meter, Limiter, Utility, Ring mod.
// Ensemble (1348) too. Each process() keeps the JS's statements and arithmetic order; `show()` (the editor's meters: Meter, Limiter) does not touch the audio and is not ported.
#include "modfx/mods.h"
#include "modfx/util.h"

namespace thunder {
namespace modfx {
namespace {

// ---- Meta-modulator ----------------------------------------------------------------------------------------------------------------
const PortDef kMetaIn[] = {{"in", "audio"}, {"inR", "audio"}, {"mod", "audio"}}, kStOut[] = {{"out", "audio"}, {"outR", "audio"}}, kStIn[] = {{"in", "audio"}, {"inR", "audio"}};
const ParamDef kMetaP[] = {{"mode", 0, 0, 3, 1}, {"amount", .5, 0, 1, .01}, {"mix", 1, 0, 1, .01}};
inline double metaF(int32_t mode, double a, double x, double m) {
  return mode == 0 ? x * (1 - a) + m * a
       : mode == 1 ? x * ((1 - a) + a * m * 1.4142)
       : mode == 2 ? js_sin(x * (1 + a * 7) * kPi)
       : x * (m >= 0 ? 1 : -1);
}
class Meta : public Module {
 public:
  Meta(double, Seeds&) {}
  void process(const IO& io, const Ctx& c) override {
    [[maybe_unused]] const In L0 = inp(io, 0);
    const int32_t mode = i32(io.p[0]); const double a = io.p[1], mx = io.p[2]; const In XR = inR(io, 0, 1); const Out oR = outp(io, 1);
    for (int i = 0; i < c.block; i++) {
      const double m = inp(io, 2)[i], x = L0[i], xr = XR[i];
      outp(io, 0)[i] = (float)(x + (metaF(mode, a, x, m) - x) * mx); oR[i] = (float)(xr + (metaF(mode, a, xr, m) - xr) * mx);
    }
  }
};

// ---- Phaser --------------------------------------------------------------------------------------------------------------------------
const PortDef kPhIn[] = {{"in", "audio"}, {"inR", "audio"}, {"cv", "cv"}};
const ParamDef kPhP[] = {{"rate", .3, 0, 1, .01}, {"depth", .5, 0, 1, .01}, {"feedback", .4, 0, 1, .01}, {"stages", 4, 2, 8, 2}, {"mix", .5, 0, 1, .01}, {"sync", 0, 0, 1, 1}};
class Phaser : public Module {
 public:
  Phaser(double, Seeds&) : z_(8), zR_(8) {}
  void process(const IO& io, const Ctx& c) override {
    [[maybe_unused]] const In L0 = inp(io, 0);
    const double* p = io.p;
    const double hz = p[5] >= .5 ? (c.tempo / 60) * p[0] : .05 * js_pow(160, p[0]);
    const int32_t n = (int32_t)clamp((double)i32(p[3]), 2, 8); const double dep = p[1], fbk = p[2] * .9, mx = p[4]; const bool cv = io.linked[2];
    const In XR = inR(io, 0, 1); const Out oR = outp(io, 1);
    for (int i = 0; i < c.block; i++) {
      ph_ += hz / c.sr; ph_ -= std::floor(ph_);
      const double f = clamp(.5 + (sinT(ph_) * dep + (cv ? inp(io, 2)[i] : 0)) * .5, 0, .999);
      const double a = (1 - f) / (1 + f);
      double x = L0[i] + fb_ * fbk;
      for (int s = 0; s < n; s++) { const double y = a * x + z_[s]; z_[s] = (float)(x - a * y); x = y; }
      fb_ = x; outp(io, 0)[i] = (float)(L0[i] * (1 - mx) + x * mx);
      const double xr0 = XR[i]; double xr = xr0 + fbR_ * fbk;
      for (int s = 0; s < n; s++) { const double y = a * xr + zR_[s]; zR_[s] = (float)(xr - a * y); xr = y; }
      fbR_ = xr; oR[i] = (float)(xr0 * (1 - mx) + xr * mx);
    }
    if (!(std::fabs(fb_) < 1e4 && std::fabs(fbR_) < 1e4)) { fb_ = fbR_ = 0; z_.fill(0); zR_.fill(0); }
  }
 private:
  double ph_ = 0, fb_ = 0, fbR_ = 0; F32 z_, zR_;
};

// ---- Stereo width ------------------------------------------------------------------------------------------------------------------
const ParamDef kWidthP[] = {{"width", 1, 0, 2, .01}, {"bass", 0, 0, 1, .01}, {"level", 1, 0, 2, .01}};
class Width : public Module {
 public:
  Width(double, Seeds&) {}
  void process(const IO& io, const Ctx& c) override {
    [[maybe_unused]] const In L0 = inp(io, 0);
    const In R = inR(io, 0, 1); const double w = io.p[0], b = io.p[1], g = io.p[2];
    const double k = 1 - js_exp(-2 * kPi * 200 / c.sr);
    for (int i = 0; i < c.block; i++) {
      const double l = L0[i], r = R[i], mid = (l + r) * .5, side = (l - r) * .5;
      lp_ += (side - lp_) * k;
      const double sOut = (side - lp_ * b) * w;
      outp(io, 0)[i] = (float)((mid + sOut) * g); outp(io, 1)[i] = (float)((mid - sOut) * g);
    }
  }
 private:
  double lp_ = 0;
};

// ---- Ping-pong delay -----------------------------------------------------------------------------------------------------------------
const double kPPDivs[7] = {.25, 1.0 / 3, .5, 2.0 / 3, 1, 2, 4};
const ParamDef kPPP[] = {{"div", 4, 0, 6, 1}, {"sync", 1, 0, 1, 1}, {"time", .3, .02, 1.5, .001}, {"fb", .4, 0, 1, .01}, {"tone", .5, 0, 1, .01}, {"mix", .3, 0, 1, .01}};
class PingPong : public Module {
 public:
  PingPong(double, Seeds&) : bL_(1 << 17), bR_(1 << 17) {}
  void process(const IO& io, const Ctx& c) override {
    [[maybe_unused]] const In L0 = inp(io, 0);
    const double* p = io.p; const In R = inR(io, 0, 1);
    const double beat = 60 / jmax(c.tempo, 20);
    const double tSec = p[1] >= .5 ? beat * kPPDivs[(int)clamp((double)i32(p[0]), 0, 6)] : p[2];
    const double d = clamp(js_round(tSec * c.sr), 1, 65536), fb = p[3] * .95, mx = p[5], k = .05 + p[4] * .9;
    const int32_t mask = (1 << 17) - 1;
    for (int i = 0; i < c.block; i++) {
      const int32_t rd = (w_ - i32(d)) & mask;
      const double dl = bL_[rd], dr = bR_[rd];
      lp_ += (dl - lp_) * k; lpR_ += (dr - lpR_) * k;
      const double wl = lp_, wr = lpR_;
      bL_[w_] = (float)(L0[i] + wr * fb);
      bR_[w_] = (float)(wl * fb);
      w_ = (w_ + 1) & mask;
      outp(io, 0)[i] = (float)(L0[i] * (1 - mx) + dl * mx);
      outp(io, 1)[i] = (float)(R[i] * (1 - mx) + dr * mx);
    }
  }
 private:
  F32 bL_, bR_; int32_t w_ = 0; double lp_ = 0, lpR_ = 0;
};

// ---- Meter ---------------------------------------------------------------------------------------------------------------------------
const ParamDef kMeterP[] = {{"trim", 0, -24, 24, .1}};
class Meter : public Module {
 public:
  Meter(double, Seeds&) {}
  void process(const IO& io, const Ctx& c) override {
    [[maybe_unused]] const In L0 = inp(io, 0);
    const In L = inp(io, 0), R = inR(io, 0, 1); const Out oL = outp(io, 0), oR = outp(io, 1); const double gt = dbLin(io.p[0]);
    double g = g_;
    for (int i = 0; i < c.block; i++) {
      g += (gt - g) * .002;
      const double l = L[i] * g, r = R[i] * g; oL[i] = (float)l; oR[i] = (float)r;
    }
    g_ = g;   // (the VU and peak readings feed show() only)
  }
 private:
  double g_ = 1;
};

// ---- Limiter -------------------------------------------------------------------------------------------------------------------------
const ParamDef kLimP[] = {{"input", 0, 0, 24, .1}, {"ceil", -.3, -24, 0, .1}, {"rel", .15, .01, 1, .001}};
class Limiter : public Module {
 public:
  Limiter(double sr, Seeds&) : n_((int)jmax(8, js_round(sr * .002))), bl_(n_), br_(n_), pk_(n_) {}
  void process(const IO& io, const Ctx& c) override {
    [[maybe_unused]] const In L0 = inp(io, 0);
    const In L = inp(io, 0), R = inR(io, 0, 1); const Out oL = outp(io, 0), oR = outp(io, 1); const int n = n_;
    const double gin = dbLin(io.p[0]), ceil_ = dbLin(io.p[1]), kr = 1 - js_exp(-1 / (c.sr * io.p[2])), ka = 1 - js_exp(-5.0 / n);
    double env = env_, peak = peak_; int w = w_, pi = pi_;
    for (int i = 0; i < c.block; i++) {
      const double yl0 = bl_[w], yr0 = br_[w];
      const double l = L[i] * gin, r = R[i] * gin, a = jmax(l < 0 ? -l : l, r < 0 ? -r : r);
      bl_[w] = (float)l; br_[w] = (float)r; pk_[w] = (float)a;
      if (a >= peak) { peak = a; pi = w; } else if (pi == w) { peak = 0; for (int j = 0; j < n; j++) if (pk_[j] > peak) { peak = pk_[j]; pi = j; } }
      w = w + 1 == n ? 0 : w + 1;
      const double target = peak > ceil_ ? ceil_ / peak : 1;
      env += (target - env) * (target < env ? ka : kr);
      double yl = yl0 * env, yr = yr0 * env;
      if (yl > ceil_) yl = ceil_; else if (yl < -ceil_) yl = -ceil_;
      if (yr > ceil_) yr = ceil_; else if (yr < -ceil_) yr = -ceil_;
      oL[i] = (float)yl; oR[i] = (float)yr;
    }
    env_ = env; w_ = w; peak_ = peak; pi_ = pi;
  }
 private:
  int n_; F32 bl_, br_, pk_; int w_ = 0, pi_ = 0; double peak_ = 0, env_ = 1;
};

// ---- Utility -------------------------------------------------------------------------------------------------------------------------
const ParamDef kUtilP[] = {{"gain", 0, -60, 24, .1}, {"pan", 0, -1, 1, .01}, {"mono", 0, 0, 1, 1}, {"flip", 0, 0, 3, 1}, {"mute", 0, 0, 1, 1}};
class Utility : public Module {
 public:
  Utility(double, Seeds&) {}
  void process(const IO& io, const Ctx& c) override {
    [[maybe_unused]] const In L0 = inp(io, 0);
    const In L = inp(io, 0), R = inR(io, 0, 1); const Out oL = outp(io, 0), oR = outp(io, 1); const double* p = io.p;
    const double g = p[4] >= .5 ? 0 : (p[0] <= -59.9 ? 0 : dbLin(p[0])), a = (clamp(p[1], -1, 1) + 1) * kPi / 4;
    const double tl = g * js_cos(a) * kSqrt2, tr = g * js_sin(a) * kSqrt2; const int32_t f = i32(p[3]); const double sl = f & 1 ? -1 : 1, sr = f & 2 ? -1 : 1; const bool mono = p[2] >= .5;
    double gl = gl_, gr = gr_;
    for (int i = 0; i < c.block; i++) {
      gl += (tl - gl) * .003; gr += (tr - gr) * .003;
      double l = L[i] * sl, r = R[i] * sr; if (mono) { const double m = (l + r) * .5; l = m; r = m; }
      oL[i] = (float)(l * gl); oR[i] = (float)(r * gr);
    }
    gl_ = gl; gr_ = gr;
  }
 private:
  double gl_ = 1, gr_ = 1;
};

// ---- Ring mod ------------------------------------------------------------------------------------------------------------------------
const PortDef kRmIn[] = {{"in", "audio"}, {"inR", "audio"}, {"car", "audio"}, {"freq", "cv"}};
const ParamDef kRmP[] = {{"freq", .45, 0, 1, .001}, {"fine", 0, -1, 1, .01}, {"wave", 0, 0, 3, 1}, {"spread", 0, 0, 1, .01}, {"shape", 0, 0, 1, .01}, {"mix", 1, 0, 1, .01}};
inline double rmOsc(int32_t w, double ph) { return w == 0 ? sinT(ph) : w == 1 ? 1 - 4 * std::fabs(ph - .5) : w == 2 ? (ph < .5 ? 1 : -1) : 0; }
class RingMod : public Module {
 public:
  RingMod(double, Seeds& s) : nr_(s.next()) {}
  void process(const IO& io, const Ctx& c) override {
    [[maybe_unused]] const In L0 = inp(io, 0);
    const In L = inp(io, 0), R = inR(io, 0, 1); const Out oL = outp(io, 0), oR = outp(io, 1); const bool ext = io.linked[2], fcv = io.linked[3]; const double* p = io.p;
    const double base = 5 * js_pow(1000, p[0]) * js_pow(2, p[1] / 12); const int32_t w = i32(p[2]); const double sp = p[3] * .25, mx = p[5];
    const double dr = 1 + p[4] * 12, nd = js_tanh(dr);
    for (int i = 0; i < c.block; i++) {
      double c1, c2;
      if (ext) { c1 = c2 = inp(io, 2)[i]; }
      else {
        const double f = base * (fcv ? js_pow(2, clamp(inp(io, 3)[i], -1, 1) * 4) : 1);
        const double ph0 = ph_; ph_ += f / c.sr; if (ph_ >= 1) { ph_ -= std::floor(ph_); if (w == 3) { n1_ = nr_() * 2 - 1; } }
        if (w == 3) { if (jmod(ph0 + sp, 1) > jmod(ph_ + sp, 1)) n2_ = nr_() * 2 - 1; c1 = n1_; c2 = truthy(sp) ? n2_ : n1_; }
        else { c1 = rmOsc(w, ph_); const double q = ph_ + sp; c2 = rmOsc(w, q - std::floor(q)); }
        if (dr > 1) { c1 = js_tanh(c1 * dr) / nd; c2 = js_tanh(c2 * dr) / nd; }
      }
      oL[i] = (float)(L[i] * (1 - mx + c1 * mx)); oR[i] = (float)(R[i] * (1 - mx + c2 * mx));
    }
  }
 private:
  double ph_ = 0, n1_ = 0, n2_ = 0; Rng nr_;
};

// ---- Ensemble ------------------------------------------------------------------------------------------------------------------------
const double kEns[4][3] = {{.6, .5, 3}, {.513, .35, 2}, {.863, .35, 2}, {9.75, .12, 2}};
const ParamDef kEnsP[] = {{"mode", 0, 0, 3, 1}, {"rate", .5, 0, 1, .01}, {"depth", .5, 0, 1, .01}, {"tone", .7, 0, 1, .01}, {"mix", .5, 0, 1, .01}};
class Ensemble : public Module {
 public:
  explicit Ensemble(double sr, Seeds&) : b_((int)std::ceil(sr * .05) + 4) {}
  void process(const IO& io, const Ctx& c) override {
    [[maybe_unused]] const In L0 = inp(io, 0);
    const double* p = io.p; const double sr = c.sr; const In L = inp(io, 0), R = inR(io, 0, 1); const Out oL = outp(io, 0), oR = outp(io, 1); const int N = b_.n;
    const double* e = kEns[(int)clamp((double)i32(p[0]), 0, 3)]; const double hz0 = e[0], dep0 = e[1], voices = e[2];
    const double hz = hz0 * js_pow(2, (p[1] - .5) * 4), dep = dep0 * p[2] * 2;
    const double ctr = (voices == 3 ? .012 : .0035) * sr, sw = (voices == 3 ? .004 : .0019) * sr * dep / .5;
    const double kt = 1 - js_exp(-2 * kPi * (2000 * js_pow(8, p[3])) / sr), mx = p[4];
    for (int i = 0; i < c.block; i++) {
      const double m = (L[i] + R[i]) * .5; b_[w_] = (float)m;
      ph_ += hz / sr; if (ph_ >= 1) ph_ -= 1;
      double wl, wr;
      if (voices == 3) {
        vib_ += 6.3 / sr; if (vib_ >= 1) vib_ -= 1;
        const double v = sinT(vib_) * .08;
        const double a = ringRd(N, ctr + sw * (sinT(ph_) + v)), b = ringRd(N, ctr + sw * (sinT(ph_ + 1.0 / 3) + v)), cc = ringRd(N, ctr + sw * (sinT(ph_ + 2.0 / 3) + v));
        wl = a * .7 + b * .45 + cc * .15; wr = cc * .7 + b * .45 + a * .15;
      } else {
        const double tri = 1 - 4 * std::fabs(ph_ - .5);
        wl = ringRd(N, ctr + sw * tri); wr = ringRd(N, ctr - sw * tri);
      }
      lp0_ += (wl - lp0_) * kt; lp1_ += (wr - lp1_) * kt;
      oL[i] = (float)(L[i] * (1 - mx * .5) + lp0_ * mx); oR[i] = (float)(R[i] * (1 - mx * .5) + lp1_ * mx);
      w_ = w_ + 1 == N ? 0 : w_ + 1;
    }
  }
 private:
  // ringRd(st, B, N, d): a linearly interpolated read d samples behind the write head
  double ringRd(int N, double d) const {
    const double r = w_ - d, ri = std::floor(r), fr = r - ri;
    const double a = rdf(b_.d.get(), N, jmod(jmod(ri, N) + N, N)), b = rdf(b_.d.get(), N, jmod(jmod(ri + 1, N) + N, N));
    return a + (b - a) * fr;
  }
  F32 b_; int w_ = 0; double ph_ = 0, vib_ = 0, lp0_ = 0, lp1_ = 0;
};

template <class M> std::unique_ptr<Module> mk(double sr, Seeds& s) { return std::unique_ptr<Module>(new M(sr, s)); }
#define PD(a) (int)(sizeof(a) / sizeof(a[0])), a
const Def kDefs[] = {
  {"meta", PD(kMetaIn), PD(kStOut), PD(kMetaP), mk<Meta>},
  {"phaser", PD(kPhIn), PD(kStOut), PD(kPhP), mk<Phaser>},
  {"width", PD(kStIn), PD(kStOut), PD(kWidthP), mk<Width>},
  {"pingpong", PD(kStIn), PD(kStOut), PD(kPPP), mk<PingPong>},
  {"meter", PD(kStIn), PD(kStOut), PD(kMeterP), mk<Meter>},
  {"limiter", PD(kStIn), PD(kStOut), PD(kLimP), mk<Limiter>},
  {"utility", PD(kStIn), PD(kStOut), PD(kUtilP), mk<Utility>},
  {"ringmod", PD(kRmIn), PD(kStOut), PD(kRmP), mk<RingMod>},
  {"ensemble", PD(kStIn), PD(kStOut), PD(kEnsP), mk<Ensemble>},
};
}  // namespace

void addStereoMods(std::vector<const Def*>& v) { for (const Def& d : kDefs) v.push_back(&d); }

}  // namespace modfx
}  // namespace thunder
