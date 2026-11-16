// MODFX delays and chorus (chains-worklet.js 1716 to 1843 and 2002 to 2078): Delay line, Dimension, BBD delay. (Ping-pong is in mod_stereo.cpp, Ensemble too.)
#include <vector>

#include "modfx/mods.h"
#include "modfx/util.h"

namespace thunder {
namespace modfx {
namespace {

#define PD(a) (int)(sizeof(a) / sizeof(a[0])), a
const PortDef kStIn[] = {{"in", "audio"}, {"inR", "audio"}}, kStOut[] = {{"out", "audio"}, {"outR", "audio"}};

// ---- Delay line ----------------------------------------------------------------------------------------------------------------------
const PortDef kDlIn[] = {{"in", "audio"}, {"tmod", "cv"}}, kDlOut[] = {{"out", "audio"}};
const ParamDef kDlP[] = {{"time", 10, 0, 2000, .1}, {"fine", 0, -1, 1, .001}, {"fb", 0, -.99, .99, .01}, {"damp", 0, 0, 1, .01}, {"depth", 0, 0, 50, .1}};
class DelayLine : public Module {
 public:
  explicit DelayLine(double sr, Seeds&) {
    const int N = 1 << (int)std::ceil(std::log2(sr * 2.1 + 8)); M_ = N - 1; b_.init(N);
  }
  void process(const IO& io, const Ctx& c) override {
    const double* p = io.p; const double sr = c.sr; const In X = inp(io, 0), T = inp(io, 1); const Out O = outp(io, 0); const int32_t M = M_;
    const double want = clamp((p[0] + p[1]) * .001 * sr, 1, sr * 2), dep = p[4] * .001 * sr; const bool mod = dep > 0 && io.linked[1];
    const double fb = clamp(p[2], -.99, .99), kd = 1 - p[3] * .95, ks = 1 - js_exp(-1 / (sr * .02));
    double d = d_ < 0 ? want : d_, lp = lp_; int32_t w = w_;
    for (int i = 0; i < c.block; i++) {
      d += (want - d) * ks;
      double dd = mod ? d + clamp(T[i], -1, 1) * dep : d; if (dd < 1) dd = 1; else if (dd > M - 4) dd = M - 4;
      const double pos = w - dd + M + 1, i0 = std::floor(pos), f = pos - i0;
      const int32_t j0 = i32(i0);
      const double xm = b_[(j0 - 1) & M], x0 = b_[j0 & M], x1 = b_[(j0 + 1) & M], x2 = b_[(j0 + 2) & M];
      const double c1 = .5 * (x1 - xm), c2 = xm - 2.5 * x0 + 2 * x1 - .5 * x2, c3 = .5 * (x2 - xm) + 1.5 * (x0 - x1);
      const double y = dd < 2 ? x0 + (x1 - x0) * f : ((c3 * f + c2) * f + c1) * f + x0;
      lp += (y - lp) * kd;
      double v = X[i] + lp * fb;
      if (!(v > -1 && v < 1)) { if (v != v || v == INFINITY || v == -INFINITY) v = 0; else if (v < 0) v = -1 - js_tanh(-v - 1); else v = 1 + js_tanh(v - 1); }
      else if (v < 1e-20 && v > -1e-20) v = 0;
      b_[w] = v; w = (w + 1) & M;
      O[i] = y;
    }
    d_ = d; w_ = w; lp_ = lp == lp && (lp > 1e-20 || lp < -1e-20) ? lp : 0;
  }
 private:
  F32 b_; int32_t M_ = 0, w_ = 0; double d_ = -1, lp_ = 0;
};

// ---- Dimension -----------------------------------------------------------------------------------------------------------------------
const double kDimModes[4][2] = {{.25, .45}, {.25, .8}, {.25, 1.25}, {.45, 1.6}};
const ParamDef kDimP[] = {{"mode", 1, 0, 3, 1}, {"width", .7, 0, 1, .01}, {"tone", .6, 0, 1, .01}};
class Dimension : public Module {
 public:
  explicit Dimension(double sr, Seeds&) : b_((int)std::ceil(sr * .02) + 4) {}
  void process(const IO& io, const Ctx& c) override {
    const double* p = io.p; const double sr = c.sr; const In L = inp(io, 0), R = inR(io, 0, 1); const Out oL = outp(io, 0), oR = outp(io, 1); const int N = b_.n;
    const double* dm = kDimModes[(int)clamp((double)i32(p[0]), 0, 3)]; const double hz = dm[0], ms = dm[1], ctr = .0042 * sr, sw = ms * .001 * sr * .5, wd = p[1];
    const double kt = 1 - js_exp(-2 * kPi * (2500 * js_pow(4, p[2])) / sr);
    for (int i = 0; i < c.block; i++) {
      const double m = (L[i] + R[i]) * .5; b_[w_] = m;
      ph_ += hz / sr; if (ph_ >= 1) ph_ -= 1;
      const double tri = 1 - 4 * std::fabs(ph_ - .5);
      l1_ += (ringRd(N, ctr + sw * tri) - l1_) * kt; m1_ += (l1_ - m1_) * kt;
      l2_ += (ringRd(N, ctr - sw * tri) - l2_) * kt; m2_ += (l2_ - m2_) * kt;
      oL[i] = L[i] + (m1_ - m2_ * .4) * wd * .7; oR[i] = R[i] + (m2_ - m1_ * .4) * wd * .7;
      w_ = w_ + 1 == N ? 0 : w_ + 1;
    }
  }
 private:
  double ringRd(int N, double d) const {
    const double r = w_ - d, ri = std::floor(r), fr = r - ri;
    const double a = rdf(b_.d.get(), N, jmod(jmod(ri, N) + N, N)), b = rdf(b_.d.get(), N, jmod(jmod(ri + 1, N) + N, N));
    return a + (b - a) * fr;
  }
  F32 b_; int w_ = 0; double ph_ = 0, l1_ = 0, l2_ = 0, m1_ = 0, m2_ = 0;
};

// ---- BBD delay -----------------------------------------------------------------------------------------------------------------------
const double kPPDivs[7] = {.25, 1.0 / 3, .5, 2.0 / 3, 1, 2, 4};
const PortDef kBbdIn[] = {{"in", "audio"}, {"inR", "audio"}, {"time", "cv"}};
const ParamDef kBbdP[] = {{"time", .3, .02, .9, .001}, {"sync", 0, 0, 1, 1}, {"div", 2, 0, 6, 1}, {"fb", .45, 0, 1.1, .01}, {"tone", .5, 0, 1, .01}, {"wow", .25, 0, 1, .01}, {"age", .2, 0, 1, .01}, {"mix", .35, 0, 1, .01}};
class BBD : public Module {
 public:
  BBD(double sr, Seeds& s) : b_((int)std::ceil(sr * 1.3) + 4), r_(s.next()) {}
  void process(const IO& io, const Ctx& c) override {
    const double* p = io.p; const double sr = c.sr; const In L = inp(io, 0), R = inR(io, 0, 1); const Out oL = outp(io, 0), oR = outp(io, 1); const int N = b_.n;
    double tt = p[1] >= .5 ? 60 / (truthy(c.tempo) ? c.tempo : 120) * kPPDivs[(int)clamp((double)i32(p[2]), 0, 6)] : p[0];
    if (io.linked[2]) tt *= js_pow(2, clamp(inp(io, 2)[0], -1, 1));
    tt = clamp(tt, .02, 1.2);
    const double target = tt * sr; if (d_ < 0) d_ = target;
    const double kd = 1 - js_exp(-1 / (sr * .12));
    const double fc = clamp(4096 / (4 * tt) * .85 * js_pow(2, (p[4] - .5) * 2) * (1 - p[6] * .5), 300, 14000);
    const double k = 1 - js_exp(-2 * kPi * fc / sr), kh = 1 - js_exp(-2 * kPi * 60 / sr);
    const double fbk = p[3], wow = p[5], hiss = p[6] * p[6] * .004, mx = p[7];
    for (int i = 0; i < c.block; i++) {
      d_ += (target - d_) * kd;
      ph_ += .55 / sr; if (ph_ >= 1) ph_ -= 1; ph2_ += 3.1 / sr; if (ph2_ >= 1) ph2_ -= 1;
      const double mod = 1 + wow * (sinT(ph_) * .006 + sinT(ph2_) * .0012);
      const double r = w_ - d_ * mod, ri = std::floor(r), fr = r - ri;
      const double a = rdf(b_.d.get(), N, jmod(jmod(ri, N) + N, N)), cc = rdf(b_.d.get(), N, jmod(jmod(ri + 1, N) + N, N));
      double y = a + (cc - a) * fr;
      lp1_ += (y - lp1_) * k; lp2_ += (lp1_ - lp2_) * k; y = lp2_;
      hp_ += (y - hp_) * kh; y -= hp_;
      if ((i & 7) == 0) nz_ = (r_() * 2 - 1) * hiss;
      y += nz_;
      const double x = (L[i] + R[i]) * .5, v = js_tanh((x + y * fbk) * 1.2) / 1.2;
      b_[w_] = v; w_ = w_ + 1 == N ? 0 : w_ + 1;
      oL[i] = L[i] * (1 - mx * .5) + y * mx; oR[i] = R[i] * (1 - mx * .5) + y * mx;
    }
  }
 private:
  F32 b_; int w_ = 0; double d_ = -1, lp1_ = 0, lp2_ = 0, hp_ = 0, ph_ = 0, ph2_ = 0, nz_ = 0; Rng r_;
};

template <class M> std::unique_ptr<Module> mk(double sr, Seeds& s) { return std::unique_ptr<Module>(new M(sr, s)); }
const Def kDefs[] = {
  {"dline", PD(kDlIn), PD(kDlOut), PD(kDlP), mk<DelayLine>},
  {"dimension", PD(kStIn), PD(kStOut), PD(kDimP), mk<Dimension>},
  {"bbd", PD(kBbdIn), PD(kStOut), PD(kBbdP), mk<BBD>},
};
}  // namespace

void addDelayMods(std::vector<const Def*>& v) { for (const Def& d : kDefs) v.push_back(&d); }

}  // namespace modfx
}  // namespace thunder
