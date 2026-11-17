// MODFX filters and audio building blocks (chains-worklet.js 892 to 916 and 1647 to 1714): Blades (dual multimode filter), One-pole, Allpass, DC blocker, Mid / side.
#include <vector>

#include "modfx/mods.h"
#include "modfx/util.h"

namespace thunder {
namespace modfx {
namespace {

#define PD(a) (int)(sizeof(a) / sizeof(a[0])), a
const PortDef kStOut[] = {{"out", "audio"}, {"outR", "audio"}}, kAudIn[] = {{"in", "audio"}}, kAudOut[] = {{"out", "audio"}};

// ---- Blades --------------------------------------------------------------------------------------------------------------------------
const PortDef kBlIn[] = {{"in", "audio"}, {"inR", "audio"}, {"cutoff", "cv"}, {"res", "cv"}};
const ParamDef kBlP[] = {{"cutoff", .5, 0, 1, .001}, {"res", .3, 0, 1, .001}, {"drive", 0, 0, 1, .01}, {"mode", 0, 0, 3, 1}, {"slope", 2, 2, 4, 2}};
struct BladeZ { double ic1 = 0, ic2 = 0, jc1 = 0, jc2 = 0; };
// one side through one pair of stages (state z): bladeSide
inline double bladeSide(BladeZ& z, double xin, double a1, double a2, double a3, double k, int32_t mode, bool four, double dr, double norm) {
  const double x = js_tanh(xin * dr) / norm;
  const double v3 = x - z.ic2, v1 = a1 * z.ic1 + a2 * v3, v2 = z.ic2 + a2 * z.ic1 + a3 * v3;
  z.ic1 = 2 * v1 - z.ic1; z.ic2 = 2 * v2 - z.ic2;
  double y = mode == 0 ? v2 : mode == 1 ? v1 : mode == 2 ? (x - k * v1 - v2) : (x - k * v1);
  if (four) {
    const double x2 = mode == 1 ? v1 : v2;
    const double u3 = x2 - z.jc2, u1 = a1 * z.jc1 + a2 * u3, u2 = z.jc2 + a2 * z.jc1 + a3 * u3;
    z.jc1 = 2 * u1 - z.jc1; z.jc2 = 2 * u2 - z.jc2;
    y = mode == 0 ? u2 : mode == 1 ? u1 : mode == 2 ? (x2 - k * u1 - u2) : (x2 - k * u1);
  }
  return y;
}
inline void bladeGuard(BladeZ& z) { if (!(std::fabs(z.ic1) < 1e4 && std::fabs(z.ic2) < 1e4 && std::fabs(z.jc1) < 1e4 && std::fabs(z.jc2) < 1e4)) { z.ic1 = z.ic2 = z.jc1 = z.jc2 = 0; } }
class Blades : public Module {
 public:
  Blades(double, Seeds&) {}
  void process(const IO& io, const Ctx& c) override {
    const double* p = io.p; const int32_t mode = i32(p[3]); const bool four = i32(p[4]) >= 4; const double dr = 1 + p[2] * 6, nt = js_tanh(dr); const double norm = truthy(nt) ? nt : 1;
    const In X = inp(io, 0), XR = inR(io, 0, 1), CUT = inp(io, 2), RES = inp(io, 3); const Out O = outp(io, 0), oR = outp(io, 1);
    for (int i = 0; i < c.block; i++) {
      const double fc = jmin(.49, (.01 + js_pow(10, (p[0] + CUT[i]) * 3) * .004) * 44100 / c.sr);
      const double g = js_tan(kPi * fc), k = 2 - 2 * jmin(clamp(p[1] + RES[i], 0, 1), .98);
      const double a1 = 1 / (1 + g * (g + k)), a2 = g * a1, a3 = g * a2;
      O[i] = bladeSide(z_, X[i], a1, a2, a3, k, mode, four, dr, norm);
      oR[i] = bladeSide(zR_, XR[i], a1, a2, a3, k, mode, four, dr, norm);
    }
    bladeGuard(z_); bladeGuard(zR_);
  }
 private:
  BladeZ z_, zR_;
};

// ---- One-pole ------------------------------------------------------------------------------------------------------------------------
const PortDef kOpOut[] = {{"lp", "audio"}, {"hp", "audio"}};
const ParamDef kOpP[] = {{"freq", 1000, 5, 20000, 1}};
inline double settle(double y) { return y == y && (y > 1e-20 || y < -1e-20) && y > -1e6 && y < 1e6 ? y : 0; }
class OnePole : public Module {
 public:
  OnePole(double, Seeds&) {}
  void process(const IO& io, const Ctx& c) override {
    const In X = inp(io, 0); const Out L = outp(io, 0), Hh = outp(io, 1); const double k = 1 - js_exp(-6.283185307179586 * clamp(io.p[0], 1, c.sr * .49) / c.sr); double y = y_;
    for (int i = 0; i < c.block; i++) { const double x = X[i]; y += (x - y) * k; L[i] = y; Hh[i] = x - y; }
    y_ = settle(y);
  }
 private:
  double y_ = 0;
};

// ---- Allpass -------------------------------------------------------------------------------------------------------------------------
const ParamDef kApP[] = {{"mode", 0, 0, 1, 1}, {"freq", 1000, 20, 20000, 1}, {"time", .01, .0005, .1, .0001}, {"gain", .5, -.95, .95, .01}};
class AllpassM : public Module {
 public:
  explicit AllpassM(double sr, Seeds&) : b_((int)std::ceil(sr * .1) + 4) {}
  void process(const IO& io, const Ctx& c) override {
    const In X = inp(io, 0); const Out O = outp(io, 0); const double* p = io.p;
    if (p[0] < .5) {
      const double t = js_tan(kPi * clamp(p[1], 1, c.sr * .49) / c.sr), a = (t - 1) / (t + 1); double x1 = x1_, y1 = y1_;
      for (int i = 0; i < c.block; i++) { const double x = X[i], y = a * x + x1 - a * y1; x1 = x; y1 = y; O[i] = y; }
      x1_ = x1; y1_ = settle(y1);
    } else {
      const int M = b_.n; const double D = clamp(js_round(p[2] * c.sr), 1, M - 1), g = clamp(p[3], -.95, .95); int w = w_;
      for (int i = 0; i < c.block; i++) {
        double r = w - D; if (r < 0) r += M; const double d = rdf(b_.d.get(), M, r); double v = X[i] + g * d;
        if (!(v == v && v > -1e6 && v < 1e6)) v = 0; else if (v < 1e-20 && v > -1e-20) v = 0;
        b_[w] = v; O[i] = d - g * v; if (++w >= M) w = 0;
      }
      w_ = w;
    }
  }
 private:
  double x1_ = 0, y1_ = 0; F32 b_; int w_ = 0;
};

// ---- DC blocker ----------------------------------------------------------------------------------------------------------------------
const ParamDef kDcP[] = {{"freq", 10, 1, 200, 1}};
class DCBlock : public Module {
 public:
  DCBlock(double, Seeds&) {}
  void process(const IO& io, const Ctx& c) override {
    const In X = inp(io, 0); const Out O = outp(io, 0); const double R = 1 - 6.283185307179586 * clamp(io.p[0], 1, 200) / c.sr; double x1 = x1_, y1 = y1_;
    for (int i = 0; i < c.block; i++) { const double x = X[i], y = x - x1 + R * y1; x1 = x; y1 = y; O[i] = y; }
    x1_ = x1 == x1 ? x1 : 0; y1_ = settle(y1);
  }
 private:
  double x1_ = 0, y1_ = 0;
};

// ---- Mid / side ----------------------------------------------------------------------------------------------------------------------
const PortDef kMsIn[] = {{"in", "audio"}, {"inR", "audio"}};
const ParamDef kMsP[] = {{"mode", 0, 0, 1, 1}};
class MidSide : public Module {
 public:
  MidSide(double, Seeds&) {}
  void process(const IO& io, const Ctx& c) override {
    const In A = inp(io, 0), B = inp(io, 1); const Out O = outp(io, 0), P = outp(io, 1);
    if (io.p[0] < .5) { const In R = io.linked[1] ? B : A; for (int i = 0; i < c.block; i++) { const double l = A[i], r = R[i]; O[i] = (l + r) * .5; P[i] = (l - r) * .5; } }
    else for (int i = 0; i < c.block; i++) { const double m = A[i], sd = B[i]; O[i] = m + sd; P[i] = m - sd; }
  }
};

template <class M> std::unique_ptr<Module> mk(double sr, Seeds& s) { return std::unique_ptr<Module>(new M(sr, s)); }
const Def kDefs[] = {
  {"blades", PD(kBlIn), PD(kStOut), PD(kBlP), mk<Blades>},
  {"onepole", PD(kAudIn), PD(kOpOut), PD(kOpP), mk<OnePole>},
  {"allpass", PD(kAudIn), PD(kAudOut), PD(kApP), mk<AllpassM>},
  {"dcblock", PD(kAudIn), PD(kAudOut), PD(kDcP), mk<DCBlock>},
  {"midside", PD(kMsIn), PD(kStOut), PD(kMsP), mk<MidSide>},
};
}  // namespace

void addFilterMods(std::vector<const Def*>& v) { for (const Def& d : kDefs) v.push_back(&d); }

}  // namespace modfx
}  // namespace thunder
