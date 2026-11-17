// U21: the Oct 2026 voice effects of chains-worklet.js (lines 3984 to 4452): Pitch correct (autotune), Vowel filter, Rotary speaker, Granular delay, Reverse echo and Envelope filter. Line for line from the JS.
// show() is not ported (none of these has one).
#include <vector>

#include "modfx/bfutil.h"
#include "modfx/mods.h"

namespace thunder {
namespace modfx {
namespace {

#define PD(a) (int)(sizeof(a) / sizeof(a[0])), a
const PortDef kStIn[] = {{"in", "audio"}, {"inR", "audio"}}, kStOut[] = {{"out", "audio"}, {"outR", "audio"}};
const double kPpDivs[7] = {.25, 1.0 / 3, .5, 2.0 / 3, 1, 2, 4};   // PP_DIVS
const double kLn2 = 0.6931471805599453;                            // Math.LN2

// ---- Pitch correct -------------------------------------------------------------------------------------------------------------------
const int kScales[9][12] = {{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}, {0, 2, 4, 5, 7, 9, 11, -1}, {0, 2, 3, 5, 7, 8, 10, -1}, {0, 2, 3, 5, 7, 8, 11, -1}, {0, 2, 4, 7, 9, -1}, {0, 3, 5, 7, 10, -1}, {0, 3, 5, 6, 7, 10, -1}, {0, 2, 3, 5, 7, 9, 10, -1}, {0, 2, 4, 5, 7, 9, 10, -1}};
inline int32_t pcMask(int s) { int32_t m = 0; for (int k = 0; k < 12 && kScales[s][k] >= 0; k++) m |= 1 << kScales[s][k]; return m; }
constexpr int PC_N = 8192, PC_W = 200, PC_WF = 384; constexpr double PC_FMIN = 65, PC_FMAX = 1100, PC_THR = .15, PC_GATE = .006;
inline int pcIn(int32_t mask, double n, double key) { return (mask >> (int)jmod(jmod(n - key, 12) + 12, 12)) & 1; }
inline double pcNearest(double m, double key, int32_t mask) {
  const double c = js_round(m); double best = c, bd = 1e9;
  for (double n = c - 6; n <= c + 6; n++) { if (!pcIn(mask, n, key)) continue; const double d = std::fabs(n - m); if (d < bd) { bd = d; best = n; } }
  return best;
}
const ParamDef kPcP[] = {{"key", 0, 0, 11, 1}, {"scale", 0, 0, 8, 1}, {"speed", .025, 0, .4, .001}, {"humanize", 0, 0, 1, .01}, {"trans", 0, -12, 12, 1}, {"mix", 1, 0, 1, .01}};
const PortDef kPcIn[] = {{"in", "audio"}, {"inR", "audio"}, {"note", "cv"}}, kPcOut[] = {{"out", "audio"}, {"outR", "audio"}, {"pitch", "cv"}, {"gate", "gate"}};
class PitchCorrect : public Module {
 public:
  explicit PitchCorrect(double sr, Seeds&) : bL(PC_N), bR(PC_N), m(PC_N), db(1024), lin(512), yd(320) {
    D = (int)jmax(1, js_round(sr / 12000)); c = 1 - js_exp(-2 * kPi / (5 * D));
    for (int s = 0; s < 9; s++) mask[s] = pcMask(s);
  }
  void process(const IO& io, const Ctx& ctx) override {
    const double* p = io.p; const double sr = ctx.sr; const int B = ctx.block; const In L = inp(io, 0), R = inR(io, 0, 1); const Out oL = outp(io, 0), oR = outp(io, 1);
    const int32_t M = PC_N - 1; const int32_t w0 = w;
    // 1. store the block and the low-passed, decimated mono the detector uses
    double pk = 0;
    for (int i = 0; i < B; i++) {
      double l = L[i], r = R[i]; if (!(l > -8 && l < 8)) l = 0; if (!(r > -8 && r < 8)) r = 0;
      const double x = (l + r) * .5; const int j = (w0 + i) & M; bL[j] = l; bR[j] = r; m[j] = x; oL[i] = l; oR[i] = r;
      lp1 += (x - lp1) * c; lp2 += (lp1 - lp2) * c;
      if (++dec >= D) { dec = 0; db[dw] = lp2; dw = (dw + 1) & 1023; if (dn < 1024) dn++; }
      const double a = x < 0 ? -x : x; if (a > pk) pk = a;
    }
    if (lp1 < 1e-15 && lp1 > -1e-15) lp1 = 0;
    if (lp2 < 1e-15 && lp2 > -1e-15) lp2 = 0;
    env += (pk - env) * (pk > env ? .6 : .08);
    // 2. detect
    bool ran = ctx.full; if (!ran) { k ^= 1; ran = k != 0; }
    if (ran) {
      const double T_ = env > PC_GATE ? detect(sr, w0 + B) : 0;
      if (T_ > 0) { voiced = 1; miss = 0; T = T_; midi = 69 + 12 * js_log2(sr / T_ / 440); }
      else { miss = missInc(miss); if (miss > 2) voiced = 0; }   // JS: `++st.miss > 2` on a Number that keeps counting; here missInc saturates (util.h)
    }
    // 3. the correction
    const double kB = B / sr;
    if (voiced) {
      double tgt;
      if (io.linked[2]) tgt = 69 + (inp(io, 2)[B - 1] - .5) * 96;
      else {
        const double key = clamp(js_round(p[0]), 0, 11); const int32_t mk = mask[tidx(js_round(p[1]), 9)];
        const double n = pcNearest(midi, key, mk), cur = note;
        if (cur < 0 || !pcIn(mk, cur, key) || (n != cur && std::fabs(midi - cur) > std::fabs(midi - n) + .2)) note = n;
        tgt = note;
      }
      if (std::fabs(tgt - tgtv) > .5) held = 0; else held += kB;
      tgtv = tgt;
      const double tau = jmax(0, p[2]) + clamp((held - .08) / .2, 0, 1) * clamp(p[3], 0, 1) * .3;
      corr += (clamp(tgt - midi, -24, 24) - corr) * (tau < 5e-4 ? 1 : 1 - js_exp(-kB / tau));
    } else { note = -1; tgtv = -99; held = 0; corr -= corr * (1 - js_exp(-kB / .03)); }
    tr += (clamp(p[4], -12, 12) - tr) * (1 - js_exp(-kB / .02));
    if (corr < 1e-9 && corr > -1e-9) corr = 0;
    const double r = js_pow(2, clamp(corr + tr, -24, 24) / 12), dr = 1 - r, ar = dr < 0 ? -dr : dr;
    // 4. the splicing delay
    const bool vcd = voiced && T > 0; const double Tj = vcd ? T : js_round(.012 * sr);
    double F = clamp(Tj, .0015 * sr, .01 * sr);
    if (ar > 1e-6) F = jmin(F, .9 * Tj / ar);
    F = jmax(8, std::floor(F));
    const double lo = 4 + r + F * jmax(0, r - 1), hi = lo + Tj; const bool still = !vcd && ar < 1e-4; const double mx = clamp(p[5], 0, 1);
    if (d < 0) d = lo + Tj * .5;
    for (int i = 0; i < B; i++) {
      const double now = w0 + i;
      if (xf >= 1) {
        double nd = d;
        if (still) { if (nd < lo || nd > .03 * sr) nd = lo + Tj * .5; }
        else if (nd < lo) nd += std::ceil((lo - nd) / Tj) * Tj;
        else if (nd > hi) nd -= std::ceil((nd - hi) / Tj) * Tj;
        if (nd != d) { od = d; d = nd; xf = 0; xs = 1 / F; pw = vcd ? 0 : 1; }
      }
      double yl = fxHermAt(bL.get(), M, now - d), yr = fxHermAt(bR.get(), M, now - d);
      if (xf < 1) {
        const double q = xf, ol = fxHermAt(bL.get(), M, now - od), orr = fxHermAt(bR.get(), M, now - od);
        double ga, gb;
        if (pw) { const double e = q * q * (3 - 2 * q); ga = sinT(e * .25); gb = sinT(e * .25 + .25); } else { ga = .5 - .5 * sinT(q * .5 + .25); gb = 1 - ga; }
        yl = yl * ga + ol * gb; yr = yr * ga + orr * gb;
        xf += xs; od += dr;
      }
      d += dr;
      if (!(d > 2 && d < PC_N - 64)) { d = lo + Tj * .5; xf = 1; }
      const double l = oL[i], rr = oR[i];
      oL[i] = fxGuard(l + (yl - l) * mx); oR[i] = fxGuard(rr + (yr - rr) * mx);
    }
    w = (w0 + B) & M;
    if (voiced) cv = clamp(.5 + (midi - 69) / 96, 0, 1);
    outp(io, 2).fill(kBlock, cv);
    outp(io, 3).fill(kBlock, voiced);
  }
 private:
  // pcDetect: the period of the latest audio in full-rate samples (fractional), or 0 when it is not clearly pitched
  double detect(double sr, double now) {
    const double rs = sr / D; const int W = PC_W;
    const double tMin = jmax(2, std::floor(rs / PC_FMAX)), tMax = jmin(jmin(512 - W - 2, 320 - 2), std::ceil(rs / PC_FMIN));
    const double nn = W + tMax + 2; if (dn < nn) return 0;
    const int n = (int)nn;
    for (int j = 0, o = dw - n; j < n; j++) lin[j] = db[(o + j) & 1023];
    double run = 0, gmin = 2; int best = -1, gt = -1; yd[0] = 1;
    for (int t = 1; t <= (int)tMax; t++) {
      double s = 0; for (int j = 0; j < W; j++) { const double q = lin[j] - lin[j + t]; s += q * q; }
      run += s; const double v = run > 1e-20 ? s * t / run : 1; yd[t] = v;
      if (t > tMin + 1) {
        const double pv = yd[t - 1];
        if (v >= pv && pv <= yd[t - 2]) { if (pv < PC_THR) { best = t - 1; break; } if (pv < gmin) { gmin = pv; gt = t - 1; } }
      }
    }
    if (best < 0) { if (gmin < .35) best = gt; else return 0; }
    const double a = yd[best - 1], mm = yd[best], z = yd[best + 1], den = a - 2 * mm + z;
    const double tau = (best + (den > 1e-12 ? .5 * (a - z) / den : 0)) * D;
    // refine at the full rate: the squared difference at +-4 samples around the coarse lag
    const double t0 = js_round(tau); const int32_t M = PC_N - 1; const int Wf = PC_WF; const double s0 = now - Wf - t0 - 6;
    int bq = -1; double bv = INFINITY;
    for (int q = 0; q < 9; q++) {
      const double t = t0 - 4 + q; double s = 0;
      for (int j = 0; j < Wf; j++) { const double e = m[i32(s0 + j) & M] - m[i32(s0 + j + t) & M]; s += e * e; }
      rf[q] = s; if (s < bv) { bv = s; bq = q; }
    }
    if (bq <= 0 || bq >= 8) return tau;
    const double A = rf[bq - 1], C = rf[bq + 1], d2 = A - 2 * bv + C;
    return t0 - 4 + bq + (d2 > 1e-20 ? .5 * (A - C) / d2 : 0);
  }
  F32 bL, bR, m, db, lin, yd; int D = 1; double c = 0; int32_t w = 0, dw = 0, dn = 0, dec = 0; double lp1 = 0, lp2 = 0, rf[9] = {};
  double env = 0; int k = 0, voiced = 0, miss = 9; double T = 0, midi = 69, note = -1, tgtv = -99, held = 0, corr = 0, tr = 0, cv = .5, d = -1, od = 0, xf = 1, xs = .01; int pw = 0; int32_t mask[9];
};

// ---- Vowel filter --------------------------------------------------------------------------------------------------------------------
const double kVfF[3][5][3] = {
  {{730, 1090, 2440}, {530, 1840, 2480}, {270, 2290, 3010}, {570, 840, 2410}, {300, 870, 2240}},
  {{850, 1220, 2810}, {610, 2330, 2990}, {310, 2790, 3310}, {590, 920, 2710}, {370, 950, 2670}},
  {{1030, 1370, 3170}, {690, 2610, 3570}, {370, 3200, 3730}, {680, 1060, 3180}, {430, 1170, 3260}}};
const double kVfDb[5][3] = {{-1, -5, -28}, {-2, -17, -24}, {-4, -24, -28}, {0, -7, -34}, {-3, -19, -43}};
struct VfLf { double v[3][5][3]; VfLf() { for (int a = 0; a < 3; a++) for (int b = 0; b < 5; b++) for (int f = 0; f < 3; f++) v[a][b][f] = js_log(kVfF[a][b][f]); } };   // VF_LF: f.map(Math.log)
const VfLf kVfLf;
const double kVfBw[3] = {80, 100, 140}, kVfBref[3] = {500, 1500, 2500}, kVfGain = 3.2;
const ParamDef kVfP[] = {{"vowel", 0, 0, 4, .01}, {"voice", 0, 0, 2, .01}, {"shift", 0, -1, 1, .01}, {"res", .5, 0, 1, .01}, {"rate", .5, .05, 10, .01}, {"depth", 0, 0, 1, .01}, {"env", 0, -1, 1, .01}, {"drive", 0, 0, 1, .01}, {"mix", 1, 0, 1, .01}};
const PortDef kVfIn[] = {{"in", "audio"}, {"inR", "audio"}, {"vowel", "cv"}};
class VowelFilter : public Module {
 public:
  VowelFilter(double, Seeds&) {}
  void process(const IO& io, const Ctx& ctx) override {
    const double* p = io.p; const double sr = ctx.sr; const int B = ctx.block; const In L = inp(io, 0), R = inR(io, 0, 1); const Out oL = outp(io, 0), oR = outp(io, 1);
    double* const s = s_; double* const A = a_;
    const bool cv = io.linked[2]; const double dr = .25 + clamp(p[7], 0, 1) * 10, dn = 1 / js_tanh(dr), mx = clamp(p[8], 0, 1);
    const double voice = clamp(p[1], 0, 2), vi = jmin(1, std::floor(voice)), vf = voice - vi, sh = js_pow(2, clamp(p[2], -1, 1));
    const double bw = js_pow(2.5, 1 - 2 * clamp(p[3], 0, 1)), ea = 1 - js_exp(-1 / (sr * .005)), er = 1 - js_exp(-1 / (sr * .12));
    const double lfoD = clamp(p[5], 0, 1) * 2, envD = clamp(p[6], -1, 1) * 4, base = clamp(p[0], 0, 4), dph = clamp(p[4], 0, 40) * 8 / sr;
    const int v0 = (int)vi; const double (*F0)[3] = kVfLf.v[v0], (*F1)[3] = kVfLf.v[v0 + 1];
    for (int i0 = 0; i0 < B; i0 += 8) {
      double q = base + lfoD * sinT(ph_) + envD * jmin(1, env_ * 2.5) + (cv ? inp(io, 2)[i0] * 4 : 0);
      q = jmod(q, 8); if (q < 0) q += 8; if (q > 4) q = 8 - q; if (!(q >= 0 && q <= 4)) q = 0;
      const double k0d = jmin(3, std::floor(q)); const int k0 = (int)k0d; const double kf = q - k0d;
      for (int f = 0; f < 3; f++) {
        const double lf = (F0[k0][f] * (1 - kf) + F0[k0 + 1][f] * kf) * (1 - vf) + (F1[k0][f] * (1 - kf) + F1[k0 + 1][f] * kf) * vf;
        const double fc = jmin(sr * .45, js_exp(lf) * sh), g = js_tan(kPi * fc / sr);
        const double k = clamp(kVfBw[f] * std::sqrt(fc / kVfBref[f]) * bw / fc, .02, 2), a1 = 1 / (1 + g * (g + k));
        A[f * 4] = a1; A[f * 4 + 1] = g * a1; A[f * 4 + 2] = g * g * a1;
        A[f * 4 + 3] = k * dbLin((kVfDb[k0][f] * (1 - kf) + kVfDb[k0 + 1][f] * kf) * .5) * kVfGain;
      }
      ph_ += dph; if (ph_ >= 1) ph_ -= 1;
      const int i1 = B < i0 + 8 ? B : i0 + 8;
      for (int i = i0; i < i1; i++) {
        double l0 = L[i], r0 = R[i]; if (!(l0 > -8 && l0 < 8)) l0 = 0; if (!(r0 > -8 && r0 < 8)) r0 = 0;
        const double m = (l0 + r0) * .5, a = m < 0 ? -m : m;
        env_ += (a - env_) * (a > env_ ? ea : er);
        const double xl = js_tanh(l0 * dr) * dn, xr = js_tanh(r0 * dr) * dn;
        double yl = 0, yr = 0;
        for (int f = 0, j = 0; f < 3; f++, j += 4) {
          const double a1 = A[j], a2 = A[j + 1], a3 = A[j + 2];
          double v3 = xl - s[j + 1], v1 = a1 * s[j] + a2 * v3, v2 = s[j + 1] + a2 * s[j] + a3 * v3;
          s[j] = 2 * v1 - s[j]; s[j + 1] = 2 * v2 - s[j + 1]; yl += v1 * A[j + 3];
          v3 = xr - s[j + 3]; v1 = a1 * s[j + 2] + a2 * v3; v2 = s[j + 3] + a2 * s[j + 2] + a3 * v3;
          s[j + 2] = 2 * v1 - s[j + 2]; s[j + 3] = 2 * v2 - s[j + 3]; yr += v1 * A[j + 3];
        }
        oL[i] = fxGuard(l0 + (yl - l0) * mx); oR[i] = fxGuard(r0 + (yr - r0) * mx);
      }
    }
    for (int j = 0; j < 12; j++) { const double v = s[j]; if (!(v > -1e4 && v < 1e4) || (v < 1e-20 && v > -1e-20)) s[j] = 0; }
    if (!(env_ > 1e-20 && env_ < 1e4)) env_ = 0;
  }
 private:
  double s_[12] = {}, a_[12] = {}, ph_ = 0, env_ = 0;
};

// ---- Rotary speaker ------------------------------------------------------------------------------------------------------------------
const double kRsHorn[3] = {0, .8, 6.8}, kRsDrum[3] = {0, .7, 5.9}, kRsHg = 1.15, kRsDg = 1.25;
const ParamDef kRoP[] = {{"speed", 1, 0, 2, 1}, {"accel", .5, 0, 1, .01}, {"drive", .2, 0, 1, .01}, {"balance", 0, -1, 1, .01}, {"spread", .8, 0, 1, .01}, {"mix", 1, 0, 1, .01}};
const PortDef kRoIn[] = {{"in", "audio"}, {"inR", "audio"}, {"fast", "gate"}};
class RotarySpk : public Module {
 public:
  explicit RotarySpk(double sr, Seeds&) { const int n = 1 << (int)std::ceil(js_log2(sr * .006 + 16)); h_.init(n); d_.init(n); m_ = n - 1; }
  void process(const IO& io, const Ctx& ctx) override {
    const double* p = io.p; const double sr = ctx.sr; const int B = ctx.block; const In L = inp(io, 0), R = inR(io, 0, 1); const Out oL = outp(io, 0), oR = outp(io, 1); const int32_t m = m_; double* T = t_;
    int sp = (int)clamp(js_round(p[0]), 0, 2);
    if (io.linked[2] && inp(io, 2)[B - 1] > .5) sp = 2;
    const double th = .08 + (1 - clamp(p[1], 0, 1)) * .9, hT = kRsHorn[sp], dT = kRsDrum[sp];
    if (!go_) { go_ = 1; vh_ = hT; vd_ = dT; }
    vh_ += (hT - vh_) * (1 - js_exp(-B / (sr * th * (hT < vh_ ? 1.4 : 1))));
    vd_ += (dT - vd_) * (1 - js_exp(-B / (sr * th * 4 * (dT < vd_ ? 1.4 : 1))));
    if (vh_ < 1e-3) vh_ = 0;
    if (vd_ < 1e-3) vd_ = 0;
    const double dv = clamp(p[2], 0, 1), dr = .3 + dv * 8, bias = dv * .3, tb = js_tanh(bias), nr = 1 / ((js_tanh(.5 * dr + bias) - js_tanh(bias - .5 * dr)) * (1 + dv * 1.2));
    const double g = js_tan(kPi * jmin(800, sr * .45) / sr), a1 = 1 / (1 + g * (g + 2)), a2 = g * a1, a3 = g * a2;
    const double dA = .00044 * sr, d0 = dA + 3, dRef = .0011 * sr, eA = .00022 * sr, e0 = eA + 3;
    const double kt = 1 - js_exp(-2 * kPi * 2200 / sr), kdc = 1 - js_exp(-2 * kPi * 20 / sr);
    const double phi = clamp(p[4], 0, 1) * .25, bal = clamp(p[3], -1, 1), gh = jmin(1, 1 + bal) * kRsHg, gd = jmin(1, 1 - bal) * kRsDg, mx = clamp(p[5], 0, 1);
    const double ih = vh_ / sr, id = vd_ / sr;
    int32_t w = w_;
    for (int i = 0; i < B; i++) {
      const double l0 = L[i], r0 = R[i]; double x0 = (l0 + r0) * .5; if (!(x0 > -8 && x0 < 8)) x0 = 0;
      double x = (js_tanh(x0 * dr + bias) - tb) * nr;
      dc_ += (x - dc_) * kdc; x -= dc_;
      const double v3 = x - c2_, v1 = a1 * c1_ + a2 * v3, v2 = c2_ + a2 * c1_ + a3 * v3;
      c1_ = 2 * v1 - c1_; c2_ = 2 * v2 - c2_;
      h_[w] = x - v2; d_[w] = v2;
      ph_ += ih; if (ph_ >= 1) ph_ -= 1; pd_ += id; if (pd_ >= 1) pd_ -= 1;
      double yl = 0, yr = 0;
      for (int s = 0; s < 2; s++) {
        const double off = s ? phi : -phi;
        const double c = sinT(ph_ - off + .25), b = .5 + .5 * c;
        double hn = fxHermAt(h_.get(), m, w - (d0 - dA * c));
        T[s] += (hn - T[s]) * kt; hn = (T[s] + (hn - T[s]) * (.25 + .75 * b)) * (.3 + .7 * b);
        const double rf = fxHermAt(h_.get(), m, w - (d0 + dRef + dA * c));
        T[s + 2] += (rf - T[s + 2]) * kt;
        const double hr = T[s + 2] * (.3 + .7 * (1 - b)) * .35;
        const double cd = sinT(pd_ - off + .25), dm = fxHermAt(d_.get(), m, w - (e0 - eA * cd)) * (1 - .4 * (.5 - .5 * cd));
        const double y = (hn + hr) * gh + dm * gd;
        if (s) yr = y; else yl = y;
      }
      w = (w + 1) & m;
      oL[i] = fxGuard(l0 + (yl - l0) * mx); oR[i] = fxGuard(r0 + (yr - r0) * mx);
    }
    w_ = w;
    c1_ = fxClean(c1_); c2_ = fxClean(c2_); dc_ = fxClean(dc_); for (int j = 0; j < 4; j++) T[j] = fxClean(T[j]);
  }
 private:
  F32 h_, d_; int32_t m_ = 0, w_ = 0; double ph_ = 0, pd_ = .37, vh_ = 0, vd_ = 0, c1_ = 0, c2_ = 0, dc_ = 0, t_[4] = {}; int go_ = 0;
};

// ---- Granular delay ------------------------------------------------------------------------------------------------------------------
constexpr int GD_MAX = 32, GD_WN = 1024;
struct GdWin { float w[GD_WN + 2]; GdWin() { for (int i = 0; i <= GD_WN + 1; i++) { const double x = jmin(1, (double)i / GD_WN), e = jmin(x, 1 - x) / .3; w[i] = (float)(e >= 1 ? 1 : .5 - .5 * js_cos(kPi * e)); } } };
const GdWin kGdWin;
const ParamDef kGdP[] = {{"time", .3, .02, 2, .001}, {"sync", 0, 0, 1, 1}, {"div", 4, 0, 6, 1}, {"size", .12, .01, .5, .001}, {"density", 10, 1, 60, .1}, {"pitch", 0, -24, 24, 1}, {"jitter", 0, 0, 1, .01},
  {"spray", .15, 0, 1, .01}, {"reverse", 0, 0, 1, .01}, {"spread", .5, 0, 1, .01}, {"fb", .3, 0, 1, .01}, {"freeze", 0, 0, 1, 1}, {"mix", .4, 0, 1, .01}};
class GrainDelay : public Module {
 public:
  GrainDelay(double sr, Seeds& s) : r_(s.next()) { const int N = 1 << (int)std::ceil(js_log2(sr * 4.5)); L_.init(N); R_.init(N); M_ = N - 1; }
  void process(const IO& io, const Ctx& ctx) override {
    const double* p = io.p; const double sr = ctx.sr; const int B = ctx.block; const In L = inp(io, 0), R = inR(io, 0, 1); const Out oL = outp(io, 0), oR = outp(io, 1); const int32_t M = M_; const double N = (double)M + 1;
    const bool frz = p[11] >= .5; const int cap = ctx.full ? GD_MAX : 12;
    const double Ts = p[1] >= .5 ? 60 / jmax(truthy(ctx.tempo) ? ctx.tempo : 120, 20) * kPpDivs[tidx(js_round(p[2]), 7)] : p[0];
    const double T = clamp(Ts, .005, 2.5) * sr, G = clamp(p[3], .005, .5) * sr, dens = clamp(p[4], .1, 100) / sr;
    const double spray = clamp(p[7], 0, 1), jit = clamp(p[6], 0, 1), rev = clamp(p[8], 0, 1), spr = clamp(p[9], 0, 1), fb = clamp(p[10], 0, 1) * .9, mx = clamp(p[12], 0, 1), pitch = clamp(p[5], -24, 24);
    for (int i = 0; i < B; i++) {
      sp_ += dens;
      if (sp_ >= 1) { sp_ -= 1 + (r_() - .5) * spray * .8; if (n_ < cap) spawn(T, G, pitch, jit, spray, rev, spr, frz, N, sr); }
      double aL = 0, aR = 0, fL = 0, fR = 0, ws = 0;
      if (n_ > 0) for (int g = 0; g < GD_MAX; g++) {
        if (!on_[g]) continue;
        const double e0 = ge_[g] * GD_WN; const int32_t ei = i32(e0); const double e = (double)kGdWin.w[ei] + ((double)kGdWin.w[ei + 1] - (double)kGdWin.w[ei]) * (e0 - ei);
        const double pos = gp_[g], ip = std::floor(pos), f = pos - ip; const int32_t ii = i32(ip), a = ii & M, b = (ii + 1) & M;
        const double sl = (L_[a] + (L_[b] - L_[a]) * f) * e, sr2 = (R_[a] + (R_[b] - R_[a]) * f) * e;
        fL += sl; fR += sr2; aL += sl * gl_[g]; aR += sr2 * gr_[g]; ws += e;
        gp_[g] = pos + gi_[g]; ge_[g] += gd_[g];
        if (ge_[g] >= 1) { on_[g] = 0; n_--; }
      }
      const double w2 = ws * ws, q6 = 1 + w2 * w2 * w2, nf = js_pow(q6, -1.0 / 6), no = js_pow(q6, -.7 / 6);
      double xl = L[i], xr = R[i]; if (!(xl > -8 && xl < 8)) xl = 0; if (!(xr > -8 && xr < 8)) xr = 0;
      if (!frz) { L_[w_] = fxClean(fxSat(xl + fL * nf * fb)); R_[w_] = fxClean(fxSat(xr + fR * nf * fb)); w_ = (w_ + 1) & M; }
      oL[i] = xl * (1 - mx) + fxGuard(aL * no) * mx; oR[i] = xr * (1 - mx) + fxGuard(aR * no) * mx;
    }
  }
 private:
  void spawn(double T, double G, double pitch, double jit, double spray, double rev, double spr, bool frz, double N, double sr) {
    int k = -1; for (int g = 0; g < GD_MAX; g++) if (!on_[g]) { k = g; break; }
    if (k < 0) return;
    const double r = js_pow(2, (pitch + (r_() * 2 - 1) * jit * jit * 12) / 12); const bool back = r_() < rev; const double adv = frz ? 0 : 1;
    G = jmin(G, N * .45 / (r + 1));
    const double lo = 4 + (back ? 0 : jmax(0, G * (r - adv))), hi = N - 8 - (back ? G * (r + adv) : jmax(0, G * (adv - r)));
    double D = T + r_() * spray * .5 * sr; D = D < lo ? lo : D > hi ? hi : D;
    const double a = ((r_() * 2 - 1) * spr + 1) * kPi / 4;
    on_[k] = 1; n_++; gp_[k] = w_ - D; gi_[k] = back ? -r : r; ge_[k] = 0; gd_[k] = 1 / G;
    gl_[k] = js_cos(a) * kSqrt2; gr_[k] = js_sin(a) * kSqrt2;
  }
  F32 L_, R_; int32_t M_ = 0, w_ = 0; double sp_ = 1; int n_ = 0; Rng r_;
  uint8_t on_[GD_MAX] = {}; double gp_[GD_MAX] = {}, gi_[GD_MAX] = {}, ge_[GD_MAX] = {}, gd_[GD_MAX] = {}, gl_[GD_MAX] = {}, gr_[GD_MAX] = {};
};

// ---- Reverse echo --------------------------------------------------------------------------------------------------------------------
const double kReAp[3] = {.0047, .0036, .00127}, kReFdn[4] = {.0297, .0371, .0411, .0437}, kReSg[4] = {1, -1, 1, -1}, kReVg = .45;
const ParamDef kRePrm[] = {{"mode", 0, 0, 1, 1}, {"time", .4, .05, 2, .001}, {"sync", 0, 0, 1, 1}, {"div", 4, 0, 6, 1}, {"fb", .3, 0, 1, .01}, {"size", .5, 0, 1, .01}, {"tone", .6, 0, 1, .01}, {"mix", .5, 0, 1, .01}};
class ReverseEcho : public Module {
 public:
  explicit ReverseEcho(double sr, Seeds&) {
    const int N = 1 << (int)std::ceil(js_log2(sr * 4.3)); L_.init(N); R_.init(N); M_ = N - 1;
    for (int j = 0; j < 3; j++) ap_[j].init((int)jmax(2, js_round(kReAp[j] * sr)));
    for (int j = 0; j < 4; j++) fd_[j].init((int)std::ceil(kReFdn[j] * 2 * sr) + 8);
  }
  void process(const IO& io, const Ctx& ctx) override {
    const double* p = io.p; const double sr = ctx.sr; const int B = ctx.block; const In L = inp(io, 0), R = inR(io, 0, 1); const Out oL = outp(io, 0), oR = outp(io, 1); const int64_t M = M_;
    const int mode = p[0] >= .5 ? 1 : 0; const bool sync = p[2] >= .5;
    if (rs_(ctx) && sync) k_ = 0;
    const double Ts = sync ? 60 / jmax(truthy(ctx.tempo) ? ctx.tempo : 120, 20) * kPpDivs[tidx(js_round(p[3]), 7)] : p[1];
    const double Tn = js_round(clamp(Ts, .02, (M + 1) / sr * .5 - .05) * sr);
    const double fb = clamp(p[4], 0, 1) * .9, mx = clamp(p[7], 0, 1);
    const double kl = 1 - js_exp(-2 * kPi * jmin(400 * js_pow(40, clamp(p[6], 0, 1)), sr * .45) / sr), kh = 1 - js_exp(-2 * kPi * 40 / sr);
    if (mode) {
      const double z = clamp(p[5], 0, 1); sz_ = sz_ < 0 ? z : sz_ + (z - sz_) * .1;
      const double sc = .5 + 1.5 * sz_, rt = .4 + sz_ * 4.6;
      for (int j = 0; j < 4; j++) { const double len = kReFdn[j] * sc * sr; fl_[j] = jmin(len, fd_[j].n - 4); fg_[j] = js_pow(10, -3 * fl_[j] / (rt * sr)); }
      kd_ = 1 - js_exp(-2 * kPi * 5500 / sr);
    }
    for (int i = 0; i < B; i++) {
      if (k_ <= 0) {
        T_ = Tn; F_ = jmax(16, jmin(js_round(.012 * sr), (double)(i32(Tn) >> 2)));
        b_ = a_; bOn_ = aOn_; a_ = w_ - 1; aOn_ = 1; k_ = T_; x_ = 0;
      }
      const int ia = (int)(a_ & M); double yl = L_[ia], yr = R_[ia];
      if (x_ < F_) {
        const double q = x_ / F_, e = q * q * (3 - 2 * q), ga = sinT(e * .25), gb = bOn_ ? sinT(e * .25 + .25) : 0; const int ib = (int)(b_ & M);
        yl = yl * ga + L_[ib] * gb; yr = yr * ga + R_[ib] * gb; b_--; x_++;
      }
      a_--; k_--;
      lp_ += (yl - lp_) * kl; lpR_ += (yr - lpR_) * kl; hp_ += (lp_ - hp_) * kh; hpR_ += (lpR_ - hpR_) * kh;
      yl = lp_ - hp_; yr = lpR_ - hpR_;
      double xl = L[i], xr = R[i]; if (!(xl > -8 && xl < 8)) xl = 0; if (!(xr > -8 && xr < 8)) xr = 0;
      double wl, wr;
      if (mode) { tank((xl + xr) * .5); wl = ol_ + yl * fb; wr = or_ + yr * fb; }
      else { wl = xl + yl * fb; wr = xr + yr * fb; }
      L_[w_] = fxClean(fxSat(wl)); R_[w_] = fxClean(fxSat(wr)); w_ = (int)((w_ + 1) & M);
      oL[i] = xl * (1 - mx) + yl * mx; oR[i] = xr * (1 - mx) + yr * mx;
    }
    lp_ = fxClean(lp_); lpR_ = fxClean(lpR_); hp_ = fxClean(hp_); hpR_ = fxClean(hpR_);
    for (int j = 0; j < 4; j++) dm_[j] = fxClean(dm_[j]);
  }
 private:
  void tank(double x) {
    for (int j = 0; j < 3; j++) { F32& b = ap_[j]; const int wi = aw_[j]; const double v = b[wi], y = v - .62 * x; b[wi] = fxClean(x + .62 * y); aw_[j] = wi + 1 == b.n ? 0 : wi + 1; x = y; }
    double* s = fs_; double* dm = dm_; const double kd = kd_;
    for (int j = 0; j < 4; j++) {
      F32& b = fd_[j]; const int n = b.n; double rp = fw_[j] - fl_[j]; if (rp < 0) rp += n;
      const int i0 = i32(rp); const double f = rp - i0; const int i1 = i0 + 1 == n ? 0 : i0 + 1;
      dm[j] += (rdf(b.get(), n, i0) + (rdf(b.get(), n, i1) - rdf(b.get(), n, i0)) * f - dm[j]) * kd; s[j] = dm[j];
    }
    const double h0 = (s[0] + s[1] + s[2] + s[3]) * .5, h1 = (s[0] - s[1] + s[2] - s[3]) * .5, h2 = (s[0] + s[1] - s[2] - s[3]) * .5, h3 = (s[0] - s[1] - s[2] + s[3]) * .5;
    double* h = h4_; h[0] = h0; h[1] = h1; h[2] = h2; h[3] = h3;
    for (int j = 0; j < 4; j++) { F32& b = fd_[j]; b[fw_[j]] = fxClean(x * kReSg[j] + fg_[j] * h[j]); fw_[j] = fw_[j] + 1 == b.n ? 0 : fw_[j] + 1; }
    ol_ = (s[0] + s[2] * .6) * kReVg; or_ = (s[1] + s[3] * .6) * kReVg;
  }
  F32 L_, R_, ap_[3], fd_[4]; int32_t M_ = 0; int32_t w_ = 0; double k_ = 0, T_ = 0, F_ = 1; int64_t a_ = 0, b_ = 0; double aOn_ = 0, bOn_ = 0, x_ = 0, lp_ = 0, lpR_ = 0, hp_ = 0, hpR_ = 0;
  Restart rs_; int aw_[3] = {}, fw_[4] = {}; double fl_[4] = {}, fg_[4] = {}, fs_[4] = {}, dm_[4] = {}, h4_[4] = {}, kd_ = 1, sz_ = -1, ol_ = 0, or_ = 0;
};

// ---- Envelope filter -----------------------------------------------------------------------------------------------------------------
const ParamDef kEfP[] = {{"mode", 1, 0, 2, 1}, {"sck", 0, 0, 136, 1}, {"freq", .35, 0, 1, .001}, {"depth", .7, -1, 1, .01}, {"range", 4, 1, 8, .1}, {"res", .6, 0, 1, .01}, {"attack", .005, .0005, .2, .0001},
  {"release", .15, .01, 2, .001}, {"sens", .5, 0, 1, .01}, {"drive", 0, 0, 1, .01}, {"mix", 1, 0, 1, .01}};
const PortDef kEfIn[] = {{"in", "audio"}, {"inR", "audio"}, {"sc", "audio"}}, kEfOut[] = {{"out", "audio"}, {"outR", "audio"}, {"env", "cv"}};
class EnvFilter : public Module {
 public:
  EnvFilter(double, Seeds&) {}
  void process(const IO& io, const Ctx& ctx) override {
    const double* p = io.p; const double sr = ctx.sr; const int B = ctx.block; const In L = inp(io, 0), R = inR(io, 0, 1); const Out oL = outp(io, 0), oR = outp(io, 1), oE = outp(io, 2);
    const bool scP = io.linked[2]; const float* K = !scP && p[1] > 0 ? keyOf(ctx, p[1]) : nullptr;
    const In SL = K ? In{K} : L, SR = K ? In{K} : R, SC = inp(io, 2);
    const int mode = (int)clamp(js_round(p[0]), 0, 2); const double base = 40 * js_pow(400, clamp(p[2], 0, 1)), oct = clamp(p[3], -1, 1) * clamp(p[4], 0, 10) * kLn2;
    const double k = 2 - 1.92 * clamp(p[5], 0, 1), comp = mode == 1 ? js_pow(k, .6) * 1.32 : js_pow(k / 2, .3);
    const double ka = 1 - js_exp(-1 / (sr * jmax(p[6], 1e-4))), kr = 1 - js_exp(-1 / (sr * jmax(p[7], 1e-3)));
    const double sg = .25 * js_pow(64, clamp(p[8], 0, 1)), dr = .25 + clamp(p[9], 0, 1) * 8, dn = 1 / js_tanh(dr), mx = clamp(p[10], 0, 1), fMax = sr * .45, pf = kPi / sr;
    double e = e_, l1 = l1_, l2 = l2_, r1 = r1_, r2 = r2_;
    for (int i = 0; i < B; i++) {
      double l0 = L[i], r0 = R[i]; if (!(l0 > -8 && l0 < 8)) l0 = 0; if (!(r0 > -8 && r0 < 8)) r0 = 0;
      const double det = scP ? SC[i] : (SL[i] + SR[i]) * .5, a = (det < 0 ? -det : det) * sg;
      if (a < 64) e += (a - e) * (a > e ? ka : kr);
      const double en = 1 - js_exp(-2 * e), fc = clamp(base * js_exp(en * oct), 10, fMax), g = js_tan(pf * fc);
      const double a1 = 1 / (1 + g * (g + k)), a2 = g * a1, a3 = g * a2;
      const double xl = js_tanh(l0 * dr) * dn, xr = js_tanh(r0 * dr) * dn;
      double v3 = xl - l2, v1 = a1 * l1 + a2 * v3, v2 = l2 + a2 * l1 + a3 * v3;
      l1 = 2 * v1 - l1; l2 = 2 * v2 - l2;
      const double yl = mode == 0 ? v2 : mode == 1 ? v1 : xl - k * v1 - v2;
      v3 = xr - r2; v1 = a1 * r1 + a2 * v3; v2 = r2 + a2 * r1 + a3 * v3;
      r1 = 2 * v1 - r1; r2 = 2 * v2 - r2;
      const double yr = mode == 0 ? v2 : mode == 1 ? v1 : xr - k * v1 - v2;
      oL[i] = l0 + (fxGuard(yl * comp) - l0) * mx; oR[i] = r0 + (fxGuard(yr * comp) - r0) * mx;
      oE[i] = en;
    }
    if (!(l1 > -1e4 && l1 < 1e4 && l2 > -1e4 && l2 < 1e4 && r1 > -1e4 && r1 < 1e4 && r2 > -1e4 && r2 < 1e4)) { l1 = l2 = r1 = r2 = 0; }
    e_ = fxClean(e); l1_ = fxClean(l1); l2_ = fxClean(l2); r1_ = fxClean(r1); r2_ = fxClean(r2);
  }
 private:
  double e_ = 0, l1_ = 0, l2_ = 0, r1_ = 0, r2_ = 0;
};

template <class M> std::unique_ptr<Module> mk(double sr, Seeds& s) { return std::unique_ptr<Module>(new M(sr, s)); }
const Def kDefs[] = {
  {"autotune", PD(kPcIn), PD(kPcOut), PD(kPcP), mk<PitchCorrect>},
  {"vowel", PD(kVfIn), PD(kStOut), PD(kVfP), mk<VowelFilter>},
  {"rotary", PD(kRoIn), PD(kStOut), PD(kRoP), mk<RotarySpk>},
  {"gdelay", PD(kStIn), PD(kStOut), PD(kGdP), mk<GrainDelay>},
  {"reverse", PD(kStIn), PD(kStOut), PD(kRePrm), mk<ReverseEcho>},
  {"envfilt", PD(kEfIn), PD(kEfOut), PD(kEfP), mk<EnvFilter>},
};
}  // namespace

void addVoiceMods(std::vector<const Def*>& v) { for (const Def& d : kDefs) v.push_back(&d); }

}  // namespace modfx
}  // namespace thunder
