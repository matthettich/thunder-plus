// U21: the generators of chains-worklet.js: Bytebeat (the 16 built-in formulas run by BBExpr, bbexpr.cpp) and Chaos (ChaosCore: Lorenz, Rossler, Chua, Thomas, Duffing, Logistic, Henon; RK4 and maps).
// Lines 4696 to 4960 of the JS. Marbles and Tides (the wasm wrappers) are in mod_mods.cpp.
#include <vector>

#include "modfx/bbexpr.h"
#include "modfx/bfutil.h"
#include "modfx/mods.h"

namespace thunder {
namespace modfx {
namespace {

#define PD(a) (int)(sizeof(a) / sizeof(a[0])), a

// ---- ChaosCore -----------------------------------------------------------------------------------------------------------------------
struct ChaosTab { double p[9], c[3][9], r[3][9]; };
#include "modfx/chaos_tab.inc"
struct ChaosSpec { double h; bool map; double p0[3]; double lim; };
const ChaosSpec kSpec[7] = {{0.02, false, {1, 1, 20}, 400}, {0.06, false, {1, -3, 0.1}, 400}, {0.03, false, {0.1, 0, 0}, 40}, {0.4, false, {0.1, 0, -0.1}, 400}, {0.08, false, {0.5, 0, 0}, 40}, {1, true, {0.31, 0, 0}, 4}, {1, true, {0.1, 0, 0}, 4}};
const double kChaosPR[7][2] = {{25, 35}, {2.6, 3.4}, {8.2, 2}, {0.32, -0.19}, {0.2, 0.3}, {3.4, 0.5999}, {1, 0.4}};
constexpr int kMaxSub = 8;
class ChaosCore {
 public:
  ChaosCore(int model, uint32_t seed) {
    seed0 = seed ? seed : 0x2545f491u; this->seed = seed0; this->model = -1;
    setModel(model);
  }
  double ox = 0, oy = 0, oz = 0; int model = -1;
  double rnd() { uint32_t s = seed; s ^= s << 13; s ^= s >> 17; s ^= s << 5; seed = s; return (double)s / 4294967296.0; }
  void setModel(int m) {
    m = m < 0 ? 0 : m > 6 ? 6 : m; if (m == model) return;
    model = m; sp = &kSpec[m]; T = &kChaosTab[m]; chaos = -1; setChaos(0.5); reset();
  }
  void setChaos(double v) {
    const double c = jmax(0, jmin(1, v == v ? v : 0)); if (c == chaos) return; chaos = c;   // +v || 0
    P = kChaosPR[model][0] + kChaosPR[model][1] * c;
    const double q = c * 8; const int i = (int)jmin(7, (double)i32(q)); const double f = q - i;
    auto Lf = [&](const double* a) { return a[i] + (a[i + 1] - a[i]) * f; };
    tp = Lf(T->p); cx = Lf(T->c[0]); cy = Lf(T->c[1]); cz = Lf(T->c[2]); rx = Lf(T->r[0]); ry = Lf(T->r[1]); rz = Lf(T->r[2]);
  }
  void reset() {
    const ChaosSpec& S = *sp; seed = seed0;
    x = S.p0[0] + (rnd() - 0.5) * 0.02; y = S.p0[1] + (rnd() - 0.5) * 0.02; z = S.p0[2] + (rnd() - 0.5) * 0.02;
    x1 = x2 = x; ph = 0; kx = rx; ky = ry; kz = rz;
    if (S.map) for (int i = 0; i < 64; i++) iter();
    else { const double h = S.h * 0.5; const int n = (int)std::ceil(8 * tp / h); for (int i = 0; i < n; i++) rk4(h); }
    norm(0);
  }
  void tick(double rate, double sr) {
    if (!(rate > 0)) return;
    const ChaosSpec& S = *sp;
    if (S.map) {
      ph += rate * tp / sr; if (ph < 1) return;
      int n = 0; while (ph >= 1 && n < 8) { ph -= 1; iter(); n++; } if (ph >= 1) ph -= std::floor(ph);
      norm(n / tp); return;
    }
    double dt = rate * tp / sr; const double MX = S.h * kMaxSub; if (dt > MX) dt = MX;
    const double n = std::ceil(dt / S.h - 1e-9), h = dt / n;
    for (int i = 0; i < n; i++) rk4(h);
    norm(dt / tp);
  }
 private:
  void deriv(double x_, double y_, double z_) {
    switch (model) {
      case 0: dx = 10 * (y_ - x_); dy = x_ * (P - z_) - y_; dz = x_ * y_ - 2.6666667 * z_; return;
      case 1: dx = -y_ - z_; dy = x_ + 0.2 * y_; dz = 0.2 + z_ * (x_ - P); return;
      case 2: dx = P * (y_ - 0.2857143 * x_ + 0.2142857 * (std::fabs(x_ + 1) - std::fabs(x_ - 1))); dy = x_ - y_ + z_; dz = -14.285714 * y_; return;
      case 3: dx = js_sin(y_) - P * x_; dy = js_sin(z_) - P * y_; dz = js_sin(x_) - P * z_; return;
      default: dx = y_; dy = x_ - 0.3 * y_ - x_ * x_ * x_ + P * js_cos(z_); dz = 1.2; return;
    }
  }
  void rk4(double h) {
    const double x_ = x, y_ = y, z_ = z, h2 = h * 0.5;
    deriv(x_, y_, z_); const double ax = dx, ay = dy, az = dz;
    deriv(x_ + h2 * ax, y_ + h2 * ay, z_ + h2 * az); const double bx = dx, by = dy, bz = dz;
    deriv(x_ + h2 * bx, y_ + h2 * by, z_ + h2 * bz); const double cx_ = dx, cy_ = dy, cz_ = dz;
    deriv(x_ + h * cx_, y_ + h * cy_, z_ + h * cz_);
    const double h6 = h / 6;
    x = x_ + h6 * (ax + 2 * (bx + cx_) + dx); y = y_ + h6 * (ay + 2 * (by + cy_) + dy); z = z_ + h6 * (az + 2 * (bz + cz_) + dz);
    if (model == 4 && z > 6.283185307179586) z -= 6.283185307179586;
    const double L = sp->lim; if (!(x < L && x > -L && y < L && y > -L && z < L && z > -L)) restart();
  }
  void iter() {
    x2 = x1; x1 = x;
    if (model == 5) {
      double xn = P * x * (1 - x);
      if (!(xn > 1e-6 && xn < 0.999999) || (xn - x < 1e-9 && x - xn < 1e-9)) xn = 0.1 + rnd() * 0.8;
      x = xn; y = x1; z = x2;
    } else {
      const double xn = 1 - P * x * x + y; y = 0.3 * x; x = xn; z = x2;
      if (!(xn < 4 && xn > -4)) restart();
    }
  }
  void restart() { const ChaosSpec& S = *sp; x = S.p0[0] + (rnd() - 0.5) * 0.02; y = S.p0[1]; z = S.p0[2]; x1 = x2 = x; }
  void norm(double cyc) {
    const double kp = cyc > 0 ? 1 - jmin(0.5, cyc * 0.02) : 1;
    double v = x - cx, a = v < 0 ? -v : v, k = rx + (kx - rx) * kp; kx = a > k ? a : k; ox = v / kx;
    v = y - cy; a = v < 0 ? -v : v; k = ry + (ky - ry) * kp; ky = a > k ? a : k; oy = v / ky;
    const double zz = model == 4 ? 0.5 * y * y + x * x * (0.25 * x * x - 0.5) : z;
    v = zz - cz; a = v < 0 ? -v : v; k = rz + (kz - rz) * kp; kz = a > k ? a : k; oz = v / kz;
  }
  uint32_t seed0 = 0, seed = 0; const ChaosSpec* sp = nullptr; const ChaosTab* T = nullptr;
  double chaos = 0.5, P = 0, x = 0, y = 0, z = 0, ph = 0, x1 = 0, x2 = 0, dx = 0, dy = 0, dz = 0, tp = 1, cx = 0, cy = 0, cz = 0, rx = 1, ry = 1, rz = 1, kx = 1, ky = 1, kz = 1;
};

// ---- Chaos ---------------------------------------------------------------------------------------------------------------------------
const PortDef kChIn[] = {{"rate", "cv"}, {"reset", "gate"}}, kChOut[] = {{"x", "cv"}, {"y", "cv"}, {"z", "cv"}, {"out", "audio"}, {"trig", "gate"}};
const ParamDef kChP[] = {{"model", 0, 0, 6, 1}, {"rate", .35, 0, 1, .001}, {"chaos", .6, 0, 1, .01}, {"smooth", 0, 0, 1, .01}, {"level", .8, 0, 1, .01}};
class Chaos : public Module {
 public:
  Chaos(double, Seeds& s) : C_(0, s.next()) {}
  void arm(Seeds& s) override { C_ = ChaosCore(0, s.next()); }   // U30
  void process(const IO& io, const Ctx& ctx) override {
    const double* p = io.p; const double sr = ctx.sr; const int m = (int)clamp((double)i32(p[0]), 0, 6); const double lv = p[4];
    if (m != m_ || C_.model != m) { m_ = m; C_.setModel(m); lx_ = C_.ox; ly_ = C_.oy; lz_ = C_.oz; }
    C_.setChaos(p[2]);
    const double base = .05 * js_pow(2, clamp(p[1], 0, 1) * 16); const bool rc = io.linked[0], sm = p[3] > .005; const double tl = jmax(1, js_round(sr * .002));
    const Out X = outp(io, 0), Y = outp(io, 1), Z = outp(io, 2), A = outp(io, 3), T = outp(io, 4); const In RC = inp(io, 0), RS = inp(io, 1);
    double g = 1;
    if (sm && !rc) g = 1 - js_exp(-2 * kPi * jmin(sr * .45, base * 64 * js_pow(2, -9 * p[3])) / sr);
    for (int i = 0; i < ctx.block; i++) {
      const bool rs = RS[i] > .5; if (rs && !r_) { C_.reset(); arm_ = 0; } r_ = rs;
      const double hz = rc ? base * js_pow(2, clamp(RC[i], -1, 1) * 4) : base;
      C_.tick(hz, sr);
      double x = C_.ox, y = C_.oy, z = C_.oz;
      if (sm) {
        if (rc) g = 1 - js_exp(-2 * kPi * jmin(sr * .45, hz * 64 * js_pow(2, -9 * p[3])) / sr);
        lx_ += (x - lx_) * g; ly_ += (y - ly_) * g; lz_ += (z - lz_) * g; x = lx_; y = ly_; z = lz_;
      }
      if (x < -.1) arm_ = 1; else if (arm_ && x > 0) { arm_ = 0; tg_ = tl; }
      T[i] = tg_ > 0 ? 1 : 0; if (tg_ > 0) tg_--;
      X[i] = x * lv; Y[i] = y * lv; Z[i] = z * lv; A[i] = (m == 0 ? z : x) * lv;
    }
  }
 private:
  ChaosCore C_; int m_ = 0; bool r_ = false; int arm_ = 0; double tg_ = 0, lx_ = 0, ly_ = 0, lz_ = 0;
};

// ---- Bytebeat ------------------------------------------------------------------------------------------------------------------------
#include "modfx/bb_list.inc"
struct BbProgs {
  BBProg p[16];
  BbProgs() { for (int i = 0; i < 16; i++) BBProg::compile(kBbList[i], &p[i]); }   // BB_PROGS = BB_LIST.map(([, src]) => BBExpr.compile(src)): the 16 are valid (the engine run proves it)
};
const BbProgs& bbProgs() { static const BbProgs P; return P; }
const PortDef kBbIn[] = {{"pitch", "cv"}, {"reset", "gate"}}, kBbOut[] = {{"out", "audio"}};
const ParamDef kBbP[] = {{"formula", 0, 0, 15, 1}, {"rate", 8000, 1000, 32000, 1}, {"a", 128, 0, 255, 1}, {"b", 128, 0, 255, 1}, {"c", 128, 0, 255, 1}, {"mode", 0, 0, 2, 1}, {"smooth", 0, 0, 1, .01}, {"level", .7, 0, 1, .01}};
class Bytebeat : public Module {
 public:
  Bytebeat(double, Seeds&) { rs_.seen = true; rs_.sn = -1; progs_ = &bbProgs(); }
  void process(const IO& io, const Ctx& ctx) override {
    const double* p = io.p; const double sr = ctx.sr; const Out o = outp(io, 0); const BBProg& P = progs_->p[(int)clamp((double)i32(p[0]), 0, 15)];
    const int32_t A = i32(clamp(js_round(p[2]), 0, 255)), B = i32(clamp(js_round(p[3]), 0, 255)), C = i32(clamp(js_round(p[4]), 0, 255)); const int mode = (int)clamp((double)i32(p[5]), 0, 2);   // i32: a NaN knob is 0, as the JS's Int32Array store of it (no (int32_t)NaN)
    const bool pc = io.linked[0]; const double base = clamp(p[1], 250, 64000) / sr, lv = p[7]; const In PI = inp(io, 0), RS = inp(io, 1);
    const bool sm = p[6] > .005; const double g = sm ? 1 - js_exp(-2 * kPi * jmin(sr * .45, 20000 * js_pow(2, -7 * p[6])) / sr) : 1, dcR = 1 - 2 * kPi * 6 / sr;
    if (rs_(ctx)) { t_ = 0; ph_ = 0; }
    for (int i = 0; i < ctx.block; i++) {
      const bool rs = RS[i] > .5; if (rs && !r_) { t_ = 0; ph_ = 1; } r_ = rs;
      ph_ += pc ? jmin(4, base * js_pow(2, clamp((PI[i] - .5) * 8, -8, 4))) : jmin(4, base);
      if (ph_ >= 1) { double acc = 0; int k = 0; while (ph_ >= 1) { ph_ -= 1; acc += P.s8(t_, A, B, C, mode); t_ = (int32_t)((uint32_t)t_ + 1u); k++; } held_ = acc / (k * 128); }
      double y = held_;
      if (sm) { s1_ += (y - s1_) * g; s2_ += (s1_ - s2_) * g; y = s2_; }
      const double d = y - dx_ + dcR * dy_; dx_ = y; dy_ = d;
      o[i] = d * lv;
    }
    if (!(dy_ > -4 && dy_ < 4)) { dx_ = dy_ = s1_ = s2_ = 0; }
  }
 private:
  const BbProgs* progs_ = nullptr; int32_t t_ = 0; double ph_ = 0, held_ = 0, s1_ = 0, s2_ = 0, dx_ = 0, dy_ = 0; bool r_ = false; Restart rs_;
};

template <class M> std::unique_ptr<Module> mk(double sr, Seeds& s) { return std::unique_ptr<Module>(new M(sr, s)); }
const Def kDefs[] = {
  {"bytebeat", PD(kBbIn), PD(kBbOut), PD(kBbP), mk<Bytebeat>},
  {"chaos", PD(kChIn), PD(kChOut), PD(kChP), mk<Chaos>},
};
}  // namespace

void addGenMods(std::vector<const Def*>& v) { for (const Def& d : kDefs) v.push_back(&d); }

}  // namespace modfx
}  // namespace thunder
