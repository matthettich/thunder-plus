// U21: the basic studio effects of chains-worklet.js (the "Basic studio effects (Oct 2026)" block, lines 5148 to 5476): Simple delay, Filter, Low and high cut, Classic compressor, Bus comp,
// Glue comp, Noise gate, Auto-pan and Pump (the tempo-locked ducker; not the pump plugin suite). Line for line from the JS. show() (the meters) is not ported.
#include <vector>

#include "modfx/bfutil.h"
#include "modfx/mods.h"

namespace thunder {
namespace modfx {
namespace {

#define PD(a) (int)(sizeof(a) / sizeof(a[0])), a
const PortDef kStIn[] = {{"in", "audio"}, {"inR", "audio"}}, kStOut[] = {{"out", "audio"}, {"outR", "audio"}}, kScIn[] = {{"in", "audio"}, {"inR", "audio"}, {"sc", "audio"}};
const double kSdDivs[12] = {.125, 1.0 / 6, .25, .375, 1.0 / 3, .5, .75, 2.0 / 3, 1, 1.5, 2, 4};
const double kLfoDivs[7] = {.25, .5, 1, 2, 4, 8, 16};
const double kPumpDivs[4] = {.5, 1, 2, 4};
const double kSqrt2d = kSqrt2;

// ---- Simple delay --------------------------------------------------------------------------------------------------------------------
const ParamDef kSdP[] = {{"sync", 1, 0, 1, 1}, {"div", 8, 0, 11, 1}, {"time", .375, .01, 2, .001}, {"fb", .35, 0, 1, .01}, {"mode", 0, 0, 1, 1}, {"lowcut", 80, 20, 2000, 1}, {"highcut", 9000, 1000, 20000, 1}, {"mix", .3, 0, 1, .01}};
class SimpleDelay : public Module {
 public:
  SimpleDelay(double sr, Seeds&) { const int N = 1 << (int)std::ceil(js_log2(sr * 4.2)); L_.init(N); R_.init(N); M_ = N - 1; }
  void process(const IO& io, const Ctx& c) override {
    const double* p = io.p; const double sr = c.sr; const int B = c.block; const In L = inp(io, 0), R = inR(io, 0, 1); const Out oL = outp(io, 0), oR = outp(io, 1); const int32_t M = M_;
    const double sec = p[0] >= .5 ? 60 / bfTempo(c) * kSdDivs[tidx(js_round(p[1]), 12)] : p[2];
    const double D = clamp(sec * sr, 4, M - 4), kd = bfK(.06, sr), fb = clamp(p[3], 0, 1) * .95; const bool ping = p[4] >= .5; const double mx = clamp(p[7], 0, 1);
    const double dryG = mx <= .5 ? 1 : 2 - 2 * mx, wetG = mx >= .5 ? 1 : 2 * mx;
    const double kh = 1 - js_exp(-2 * kPi * clamp(p[5], 5, sr * .2) / sr), kl = 1 - js_exp(-2 * kPi * clamp(p[6], 200, sr * .45) / sr);
    double d = d_ < 0 ? D : d_, ll = ll_, lr = lr_, hl = hl_, hr = hr_; int32_t w = w_;
    for (int i = 0; i < B; i++) {
      d += (D - d) * kd;
      const double pos = (double)w - d + M + 1, i0 = std::floor(pos), f = pos - i0; const int32_t ii = i32(i0), a = ii & M, b = (ii + 1) & M;
      const double yl = L_[a] + (L_[b] - L_[a]) * f, yr = R_[a] + (R_[b] - R_[a]) * f;
      ll += (yl - ll) * kl; lr += (yr - lr) * kl;
      hl += (ll - hl) * kh; hr += (lr - hr) * kh;
      const double el = ll - hl, er = lr - hr;
      double xl = L[i], xr = R[i]; if (!(xl > -8 && xl < 8)) xl = 0; if (!(xr > -8 && xr < 8)) xr = 0;
      if (ping) { L_[w] = fxClean(fxSat((xl + xr) * .5 + er * fb)); R_[w] = fxClean(fxSat(el * fb)); }
      else { L_[w] = fxClean(fxSat(xl + el * fb)); R_[w] = fxClean(fxSat(xr + er * fb)); }
      w = (w + 1) & M;
      oL[i] = xl * dryG + el * wetG; oR[i] = xr * dryG + er * wetG;
    }
    d_ = d; w_ = w; ll_ = fxClean(ll); lr_ = fxClean(lr); hl_ = fxClean(hl); hr_ = fxClean(hr);
  }
 private:
  F32 L_, R_; int32_t M_ = 0, w_ = 0; double d_ = -1, ll_ = 0, lr_ = 0, hl_ = 0, hr_ = 0;
};

// ---- Filter --------------------------------------------------------------------------------------------------------------------------
const ParamDef kFiP[] = {{"mode", 0, 0, 3, 1}, {"slope", 0, 0, 1, 1}, {"cutoff", 2000, 20, 20000, 1}, {"res", .2, 0, 1, .01}, {"drive", 0, 0, 1, .01}, {"lfo", 0, 0, 1, .01}, {"rate", 4, 0, 6, 1}, {"shape", 0, 0, 5, 1}, {"mix", 1, 0, 1, .01}};
class Filter : public Module {
 public:
  Filter(double, Seeds& s) : nr_(s.next()) {}
  void process(const IO& io, const Ctx& c) override {
    const double* p = io.p; const double sr = c.sr; const int B = c.block; const In L = inp(io, 0), R = inR(io, 0, 1); const Out oL = outp(io, 0), oR = outp(io, 1);
    if (rs_(c)) ph_ = 0;
    const int mode = (int)clamp(js_round(p[0]), 0, 3); const bool s24 = p[1] >= .5; const double r = clamp(p[3], 0, 1), dep = clamp(p[5], 0, 1) * 4; const int shape = (int)clamp(js_round(p[7]), 0, 5);
    const double k12 = 1.414 - 1.38 * r, k2 = mode < 2 ? .765 - .73 * r : k12, k1 = mode < 2 ? 1.848 : k12;
    const double inc = bfTempo(c) / 60 / kLfoDivs[tidx(js_round(p[6]), 7)] / sr;
    const double base = js_log2(clamp(p[2], 10, sr * .45)), top = js_log2(sr * .45), ks = bfK(.005, sr);
    const double drv = clamp(p[4], 0, 1), dr = .25 + drv * 8, dn = 1 / js_tanh(dr), mx = clamp(p[8], 0, 1);
    Svf &A = a_, &Bq = b_, &C = c_, &D = d_;
    double lc = lc_ < 0 ? base : lc_, ph = ph_;
    for (int i = 0; i < B; i++) {
      ph += inc; if (ph >= 1) { ph -= std::floor(ph); rv_ = nr_() * 2 - 1; }
      double lv = 0;
      if (dep > 0) {
        lv = shape == 0 ? js_sin(2 * kPi * ph) : shape == 1 ? (ph < .5 ? 4 * ph - 1 : 3 - 4 * ph) : shape == 2 ? 2 * ph - 1 : shape == 3 ? 1 - 2 * ph : shape == 4 ? (ph < .5 ? 1 : -1) : rv_;
      }
      lc += (jmin(top, jmax(3.4, base + lv * dep)) - lc) * ks;
      const double g = js_tan(kPi * js_pow(2, lc) / sr);
      double xl = L[i], xr = R[i]; if (!(xl > -8 && xl < 8)) xl = 0; if (!(xr > -8 && xr < 8)) xr = 0;
      const double il = drv > .001 ? js_tanh(xl * dr) * dn : xl, ir = drv > .001 ? js_tanh(xr * dr) * dn : xr;
      double yl, yr;
      if (!s24) {
        bfStep(A, il, g, k12); bfStep(C, ir, g, k12);
        yl = mode == 0 ? A.lp : mode == 1 ? A.hp : mode == 2 ? k12 * A.bp : il - k12 * A.bp;
        yr = mode == 0 ? C.lp : mode == 1 ? C.hp : mode == 2 ? k12 * C.bp : ir - k12 * C.bp;
      } else {
        bfStep(A, il, g, k1); bfStep(C, ir, g, k1);
        const double ml = mode == 0 ? A.lp : mode == 1 ? A.hp : mode == 2 ? k1 * A.bp : il - k1 * A.bp, mr = mode == 0 ? C.lp : mode == 1 ? C.hp : mode == 2 ? k1 * C.bp : ir - k1 * C.bp;
        bfStep(Bq, ml, g, k2); bfStep(D, mr, g, k2);
        yl = mode == 0 ? Bq.lp : mode == 1 ? Bq.hp : mode == 2 ? k2 * Bq.bp : ml - k2 * Bq.bp;
        yr = mode == 0 ? D.lp : mode == 1 ? D.hp : mode == 2 ? k2 * D.bp : mr - k2 * D.bp;
      }
      oL[i] = xl + (fxGuard(yl) - xl) * mx; oR[i] = xr + (fxGuard(yr) - xr) * mx;
    }
    lc_ = lc; ph_ = ph; bfTidy(A); bfTidy(Bq); bfTidy(C); bfTidy(D);
  }
 private:
  Svf a_, b_, c_, d_; double lc_ = -1, ph_ = 0, rv_ = 0; Restart rs_; Rng nr_;
};

// ---- Low and high cut ----------------------------------------------------------------------------------------------------------------
const ParamDef kCutP[] = {{"low", 20, 20, 2000, 1}, {"high", 20000, 1000, 20000, 1}, {"slope", 1, 0, 2, 1}};
class LoHiCut : public Module {
 public:
  LoHiCut(double, Seeds&) {}
  void process(const IO& io, const Ctx& c) override {
    const double* p = io.p; const double sr = c.sr; const int B = c.block; const In L = inp(io, 0), R = inR(io, 0, 1); const Out oL = outp(io, 0), oR = outp(io, 1); const int sl = (int)clamp(js_round(p[2]), 0, 2);
    const bool loOn = p[0] > 20.5, hiOn = p[1] < 19900;
    const double lt = js_log2(clamp(p[0], 10, sr * .45)), ht = js_log2(clamp(p[1], 10, sr * .45));
    lo_ = lo_ < 0 ? lt : lo_ + (lt - lo_) * .3; hi_ = hi_ < 0 ? ht : hi_ + (ht - hi_) * .3;
    const double flo = js_pow(2, lo_), fhi = js_pow(2, hi_), gl = js_tan(kPi * flo / sr), gh = js_tan(kPi * fhi / sr);
    const double cl = 1 - js_exp(-2 * kPi * flo / sr), ch = 1 - js_exp(-2 * kPi * fhi / sr);
    Svf* H = h_; Svf* Lw = l_; double* P = p1_;
    for (int i = 0; i < B; i++) {
      double xl = L[i], xr = R[i]; if (!(xl > -8 && xl < 8)) xl = 0; if (!(xr > -8 && xr < 8)) xr = 0;
      if (loOn) {
        if (sl == 0) { P[0] += (xl - P[0]) * cl; P[1] += (xr - P[1]) * cl; xl -= P[0]; xr -= P[1]; }
        else if (sl == 1) { bfStep(H[0], xl, gl, 1.414); bfStep(H[1], xr, gl, 1.414); xl = H[0].hp; xr = H[1].hp; }
        else { bfStep(H[0], xl, gl, 1.848); bfStep(H[1], xr, gl, 1.848); bfStep(H[2], H[0].hp, gl, .765); bfStep(H[3], H[1].hp, gl, .765); xl = H[2].hp; xr = H[3].hp; }
      }
      if (hiOn) {
        if (sl == 0) { P[2] += (xl - P[2]) * ch; P[3] += (xr - P[3]) * ch; xl = P[2]; xr = P[3]; }
        else if (sl == 1) { bfStep(Lw[0], xl, gh, 1.414); bfStep(Lw[1], xr, gh, 1.414); xl = Lw[0].lp; xr = Lw[1].lp; }
        else { bfStep(Lw[0], xl, gh, 1.848); bfStep(Lw[1], xr, gh, 1.848); bfStep(Lw[2], Lw[0].lp, gh, .765); bfStep(Lw[3], Lw[1].lp, gh, .765); xl = Lw[2].lp; xr = Lw[3].lp; }
      }
      oL[i] = xl; oR[i] = xr;
    }
    for (int j = 0; j < 4; j++) bfTidy(H[j]); for (int j = 0; j < 4; j++) bfTidy(Lw[j]); for (int j = 0; j < 4; j++) P[j] = fxClean(P[j]);
  }
 private:
  Svf h_[4], l_[4]; double lo_ = -1, hi_ = -1, p1_[4] = {0, 0, 0, 0};
};

// ---- the compressors' shared parts ---------------------------------------------------------------------------------------------------
inline double gainRed(double over, double sl, double kn) {   // the Classic compressor's soft knee
  return over <= -kn / 2 ? 0 : over < kn / 2 ? sl * (over + kn / 2) * (over + kn / 2) / (2 * kn) : sl * over;
}

// ---- Classic compressor --------------------------------------------------------------------------------------------------------------
const ParamDef kCoP[] = {{"thresh", -18, -60, 0, .1}, {"ratio", 4, 1, 20, .1}, {"attack", .01, .0001, .25, .0001}, {"release", .15, .01, 2, .001}, {"knee", 6, 0, 24, .1}, {"makeup", 0, 0, 24, .1}, {"sck", 0, 0, 136, 1}, {"mix", 1, 0, 1, .01}};
class ClassicComp : public Module {
 public:
  ClassicComp(double, Seeds&) {}
  void process(const IO& io, const Ctx& c) override {
    const double* p = io.p; const double sr = c.sr; const int B = c.block; const In L = inp(io, 0), R = inR(io, 0, 1); const Out oL = outp(io, 0), oR = outp(io, 1);
    const bool scP = io.linked[2]; const float* K = !scP && p[6] > 0 ? keyOf(c, p[6]) : nullptr;
    const In SL = K ? In{K} : L, SR = K ? In{K} : R, SC = inp(io, 2);
    const double thr = p[0], sl = 1 - 1 / jmax(1, p[1]), kn = jmax(0, p[4]), mk = p[5], ka = bfK(p[2], sr), kr = bfK(p[3], sr), mx = clamp(p[7], 0, 1);
    double g = g_;
    for (int i = 0; i < B; i++) {
      double xl = L[i], xr = R[i]; if (!(xl > -8 && xl < 8)) xl = 0; if (!(xr > -8 && xr < 8)) xr = 0;
      double det; if (scP) det = std::fabs(SC[i]); else { const double a = std::fabs(SL[i]), b = std::fabs(SR[i]); det = a > b ? a : b; }
      const double xdB = det > 1e-6 ? 8.685889638 * js_log(det) : -120, over = xdB - thr;
      const double t = gainRed(over, sl, kn);
      g += (t - g) * (t > g ? ka : kr);
      const double gain = js_exp((mk - g) * .1151292546);
      oL[i] = xl + (fxGuard(xl * gain) - xl) * mx; oR[i] = xr + (fxGuard(xr * gain) - xr) * mx;
    }
    g_ = g > 1e-6 && g < 200 ? g : 0;
  }
 private:
  double g_ = 0;
};

// ---- Bus comp and Glue comp ----------------------------------------------------------------------------------------------------------
const double kBcRatio[3] = {2, 4, 10}, kBcAtt[6] = {.0001, .0003, .001, .003, .01, .03}, kBcRel[5] = {.1, .3, .6, 1.2, -1}, kBcHpf[5] = {0, 60, 90, 150, 250};
inline double bcHp(double* z, double x, double g, double k) {
  const double hp = (x - (k + g) * z[0] - z[1]) / (1 + k * g + g * g), bp = g * hp + z[0]; z[0] = g * hp + bp; const double lp = g * bp + z[1]; z[1] = g * bp + lp; return hp;
}
struct BcState { double sus = 0, hz = -1, hg = 0, zl[2] = {0, 0}, zr[2] = {0, 0}; };
inline double bcRel(BcState& st, double g, double sr, double rel) {
  if (rel > 0) return bfK(rel, sr);
  st.sus += ((g > 1 ? 1 : 0) - st.sus) * bfK(.6, sr);
  return bfK(.1 + 1.1 * st.sus, sr);
}
// bcKey's corner: the detector's high-pass (st.hz / st.hg), set when the stepped Sc HPF changes
inline void bcCorner(BcState& st, double schpf, double sr) {
  const double hz = kBcHpf[tidx(js_round(truthy(schpf) ? schpf : 0), 5)];
  if (hz != st.hz) { st.hz = hz; st.hg = hz != 0 ? js_tan(kPi * jmin(hz, sr * .45) / sr) : 0; }
}
inline void bcTidy(BcState& st) { st.zl[0] = fxClean(st.zl[0]); st.zl[1] = fxClean(st.zl[1]); st.zr[0] = fxClean(st.zr[0]); st.zr[1] = fxClean(st.zr[1]); }

const ParamDef kBcP[] = {{"thresh", -12, -40, 0, .1}, {"ratio", 1, 0, 2, 1}, {"attack", 4, 0, 5, 1}, {"release", 4, 0, 4, 1}, {"makeup", 0, 0, 20, .1}, {"schpf", 0, 0, 4, 1}, {"sck", 0, 0, 136, 1}, {"mix", 1, 0, 1, .01}};
class BusComp : public Module {
 public:
  BusComp(double, Seeds&) {}
  void process(const IO& io, const Ctx& c) override {
    const double* p = io.p; const double sr = c.sr; const int B = c.block; const In L = inp(io, 0), R = inR(io, 0, 1); const Out oL = outp(io, 0), oR = outp(io, 1);
    const bool scP = io.linked[2]; const float* Kb = !scP && p[6] > 0 ? keyOf(c, p[6]) : nullptr;
    const In SL = Kb ? In{Kb} : L, SR = Kb ? In{Kb} : R, SC = inp(io, 2);
    bcCorner(st_, p[5], sr);
    const double hg = st_.hg, K = kSqrt2d;
    const double thr = p[0], sl = 1 - 1 / kBcRatio[tidx(js_round(p[1]), 3)], kn = 2, mk = p[4], mx = clamp(p[7], 0, 1);
    const double ka = bfK(kBcAtt[tidx(js_round(p[2]), 6)], sr), rel = kBcRel[tidx(js_round(p[3]), 5)];
    double g = g_, kr = bcRel(st_, g, sr, rel);
    for (int i = 0; i < B; i++) {
      double xl = L[i], xr = R[i]; if (!(xl > -8 && xl < 8)) xl = 0; if (!(xr > -8 && xr < 8)) xr = 0;
      double a, b;
      if (scP) { a = SC[i]; if (hg != 0) a = bcHp(st_.zl, a, hg, K); a = std::fabs(a); b = 0; }
      else { a = SL[i]; b = SR[i]; if (hg != 0) { a = bcHp(st_.zl, a, hg, K); b = bcHp(st_.zr, b, hg, K); } a = std::fabs(a); b = std::fabs(b); }
      const double det = a > b ? a : b, xdB = det > 1e-6 ? 8.685889638 * js_log(det) : -120, over = xdB - thr;
      const double t = over <= -kn / 2 ? 0 : over < kn / 2 ? sl * (over + kn / 2) * (over + kn / 2) / (2 * kn) : sl * over;
      if ((i & 31) == 0) kr = bcRel(st_, g, sr, rel);
      g += (t - g) * (t > g ? ka : kr);
      const double gain = js_exp((mk - g) * .1151292546);
      oL[i] = xl + (fxGuard(xl * gain) - xl) * mx; oR[i] = xr + (fxGuard(xr * gain) - xr) * mx;
    }
    g_ = g > 1e-6 && g < 200 ? g : 0; bcTidy(st_);
  }
 private:
  BcState st_; double g_ = 0;
};

const ParamDef kGlP[] = {{"thresh", -15, -40, 0, .1}, {"ratio", 2, 1, 10, .1}, {"attack", .01, .0001, .03, .0001}, {"release", .3, .05, 1.5, .01}, {"auto", 1, 0, 1, 1}, {"knee", 8, 0, 18, .1}, {"range", 20, 1, 40, .5},
  {"makeup", 0, 0, 20, .1}, {"clip", 0, 0, 1, 1}, {"schpf", 0, 0, 4, 1}, {"sck", 0, 0, 136, 1}, {"mix", 1, 0, 1, .01}};
class GlueComp : public Module {
 public:
  GlueComp(double, Seeds&) {}
  void process(const IO& io, const Ctx& c) override {
    const double* p = io.p; const double sr = c.sr; const int B = c.block; const In L = inp(io, 0), R = inR(io, 0, 1); const Out oL = outp(io, 0), oR = outp(io, 1);
    const bool scP = io.linked[2]; const float* Kb = !scP && p[10] > 0 ? keyOf(c, p[10]) : nullptr;
    const In SL = Kb ? In{Kb} : L, SR = Kb ? In{Kb} : R, SC = inp(io, 2);
    bcCorner(st_, p[9], sr);
    const double hg = st_.hg, K = kSqrt2d;
    const double thr = p[0], sl = 1 - 1 / jmax(1, p[1]), kn = jmax(0, p[5]), rg = jmax(.5, p[6]), mk = p[7], mx = clamp(p[11], 0, 1); const bool clip = p[8] >= .5;
    const double ka = bfK(p[2], sr), rel = p[4] >= .5 ? -1 : p[3], km = bfK(.006, sr);
    double g = g_, ms = ms_, kr = bcRel(st_, g, sr, rel);
    for (int i = 0; i < B; i++) {
      double xl = L[i], xr = R[i]; if (!(xl > -8 && xl < 8)) xl = 0; if (!(xr > -8 && xr < 8)) xr = 0;
      double a, b;
      if (scP) { a = SC[i]; if (hg != 0) a = bcHp(st_.zl, a, hg, K); b = a; }
      else { a = SL[i]; b = SR[i]; if (hg != 0) { a = bcHp(st_.zl, a, hg, K); b = bcHp(st_.zr, b, hg, K); } }
      const double sq = a * a > b * b ? a * a : b * b; ms += (sq - ms) * km;
      const double xdB = ms > 1e-12 ? 4.342944819 * js_log(ms * 2) : -120, over = xdB - thr;
      double t = over <= -kn / 2 ? 0 : kn > 0 && over < kn / 2 ? sl * (over + kn / 2) * (over + kn / 2) / (2 * kn) : sl * over;
      if (t > rg) t = rg;
      if ((i & 31) == 0) kr = bcRel(st_, g, sr, rel);
      g += (t - g) * (t > g ? ka : kr);
      const double gain = js_exp((mk - g) * .1151292546);
      double yl = xl * gain, yr = xr * gain; if (clip) { yl = soft(yl); yr = soft(yr); }
      oL[i] = xl + (fxGuard(yl) - xl) * mx; oR[i] = xr + (fxGuard(yr) - xr) * mx;
    }
    g_ = g > 1e-6 && g < 200 ? g : 0; ms_ = fxClean(ms); bcTidy(st_);
  }
 private:
  BcState st_; double g_ = 0, ms_ = 0;
};

// ---- Noise gate ----------------------------------------------------------------------------------------------------------------------
const ParamDef kNgP[] = {{"thresh", -45, -80, 0, .1}, {"range", 90, 0, 90, 1}, {"attack", .0005, .0001, .05, .0001}, {"hold", .03, 0, .5, .001}, {"release", .12, .005, 2, .001}, {"sck", 0, 0, 136, 1}};
class NoiseGate : public Module {
 public:
  NoiseGate(double, Seeds&) {}
  void process(const IO& io, const Ctx& c) override {
    const double* p = io.p; const double sr = c.sr; const int B = c.block; const In L = inp(io, 0), R = inR(io, 0, 1); const Out oL = outp(io, 0), oR = outp(io, 1);
    const bool scP = io.linked[2]; const float* K = !scP && p[5] > 0 ? keyOf(c, p[5]) : nullptr;
    const In SL = K ? In{K} : L, SR = K ? In{K} : R, SC = inp(io, 2);
    const double on = js_pow(10, p[0] / 20), off = on * .6, floor = p[1] >= 89.5 ? 0 : js_pow(10, -p[1] / 20);
    const double ka = bfK(p[2], sr), kr = bfK(p[4], sr), ke = bfK(.005, sr), hn = js_round(p[3] * sr);
    double e = e_, hold = hold_, g = g_; bool open = open_;
    for (int i = 0; i < B; i++) {
      double xl = L[i], xr = R[i]; if (!(xl > -8 && xl < 8)) xl = 0; if (!(xr > -8 && xr < 8)) xr = 0;
      double a; if (scP) a = std::fabs(SC[i]); else { const double x = std::fabs(SL[i]), y = std::fabs(SR[i]); a = x > y ? x : y; }
      e = a > e ? a : e + (a - e) * ke;
      if (e > on) { open = true; hold = hn; } else if (open && e < off) { if (hold > 0) hold--; else open = false; }
      const double t = open ? 1 : floor; g += (t - g) * (t > g ? ka : kr);
      oL[i] = xl * g; oR[i] = xr * g;
    }
    e_ = fxClean(e); open_ = open; hold_ = hold; g_ = g > 1e-7 ? g : 0;
  }
 private:
  double e_ = 0, hold_ = 0, g_ = 0; bool open_ = false;
};

// ---- Auto-pan ------------------------------------------------------------------------------------------------------------------------
const ParamDef kApP[] = {{"sync", 1, 0, 1, 1}, {"div", 2, 0, 6, 1}, {"rate", 2, .05, 20, .01}, {"depth", .8, 0, 1, .01}, {"shape", 0, 0, 2, 1}};
class AutoPan : public Module {
 public:
  AutoPan(double, Seeds&) {}
  void process(const IO& io, const Ctx& c) override {
    const double* p = io.p; const double sr = c.sr; const int B = c.block; const In L = inp(io, 0), R = inR(io, 0, 1); const Out oL = outp(io, 0), oR = outp(io, 1); const bool sync = p[0] >= .5;
    if (rs_(c) && sync) ph_ = 0;
    const double hz = sync ? bfTempo(c) / 60 / kLfoDivs[tidx(js_round(p[1]), 7)] : clamp(p[2], .01, 40);
    const double inc = hz / sr, dep = clamp(p[3], 0, 1); const int shape = (int)clamp(js_round(p[4]), 0, 2); const double sq = 1 / js_tanh(4);
    double ph = ph_;
    for (int i = 0; i < B; i++) {
      ph += inc; if (ph >= 1) ph -= std::floor(ph);
      const double s = js_sin(2 * kPi * ph), v = shape == 0 ? s : shape == 1 ? (ph < .25 ? 4 * ph : ph < .75 ? 2 - 4 * ph : 4 * ph - 4) : js_tanh(4 * s) * sq;
      const double th = (v * dep + 1) * kPi / 4, gl = jmin(1, kSqrt2d * js_cos(th)), gr = jmin(1, kSqrt2d * js_sin(th));
      double xl = L[i], xr = R[i]; if (!(xl > -8 && xl < 8)) xl = 0; if (!(xr > -8 && xr < 8)) xr = 0;
      oL[i] = xl * gl; oR[i] = xr * gr;
    }
    ph_ = ph;
  }
 private:
  double ph_ = 0; Restart rs_;
};

// ---- Pump ----------------------------------------------------------------------------------------------------------------------------
const ParamDef kPuP[] = {{"div", 1, 0, 3, 1}, {"depth", .8, 0, 1, .01}, {"release", .7, .05, 1, .01}, {"curve", .5, 0, 1, .01}};
class PumpDuck : public Module {
 public:
  PumpDuck(double, Seeds&) {}
  void process(const IO& io, const Ctx& c) override {
    const double* p = io.p; const double sr = c.sr; const int B = c.block; const In L = inp(io, 0), R = inR(io, 0, 1); const Out oL = outp(io, 0), oR = outp(io, 1);
    if (rs_(c)) ph_ = 0;
    const double inc = bfTempo(c) / 60 / kPumpDivs[tidx(js_round(p[0]), 4)] / sr;
    const double dep = clamp(p[1], 0, 1), rel = clamp(p[2], .02, 1), ex = 1 + clamp(p[3], 0, 1) * 3, kg = bfK(.002, sr);
    double ph = ph_, g = g_;
    for (int i = 0; i < B; i++) {
      ph += inc; if (ph >= 1) ph -= std::floor(ph);
      const double x = ph / rel, env = x >= 1 ? 0 : js_pow(1 - x, ex), t = 1 - dep * env;
      g += (t - g) * (t < g ? kg : 1);
      double xl = L[i], xr = R[i]; if (!(xl > -8 && xl < 8)) xl = 0; if (!(xr > -8 && xr < 8)) xr = 0;
      oL[i] = xl * g; oR[i] = xr * g;
    }
    ph_ = ph; g_ = g;
  }
 private:
  double ph_ = 0, g_ = 1; Restart rs_;
};

template <class M> std::unique_ptr<Module> mk(double sr, Seeds& s) { return std::unique_ptr<Module>(new M(sr, s)); }
const Def kDefs[] = {
  {"sdelay", PD(kStIn), PD(kStOut), PD(kSdP), mk<SimpleDelay>},
  {"filter", PD(kStIn), PD(kStOut), PD(kFiP), mk<Filter>},
  {"cut", PD(kStIn), PD(kStOut), PD(kCutP), mk<LoHiCut>},
  {"compressor", PD(kScIn), PD(kStOut), PD(kCoP), mk<ClassicComp>},
  {"buscomp", PD(kScIn), PD(kStOut), PD(kBcP), mk<BusComp>},
  {"glue", PD(kScIn), PD(kStOut), PD(kGlP), mk<GlueComp>},
  {"ngate", PD(kScIn), PD(kStOut), PD(kNgP), mk<NoiseGate>},
  {"autopan", PD(kStIn), PD(kStOut), PD(kApP), mk<AutoPan>},
  {"pump", PD(kStIn), PD(kStOut), PD(kPuP), mk<PumpDuck>},
};
}  // namespace

void addBasicMods(std::vector<const Def*>& v) { for (const Def& d : kDefs) v.push_back(&d); }

}  // namespace modfx
}  // namespace thunder
