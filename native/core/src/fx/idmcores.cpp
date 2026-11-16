// See idmcores.h. Each function keeps the JS's statements and arithmetic order (index.html: beatLock, bkHash, GrainsCore, ScrambleCore, GlitchCore, VSampCore).
#include "fx/idmcores.h"

#include <cmath>
#include <cstring>
#include <limits>

#include "rt/jsmath.h"

namespace thunder {
namespace {

const double kNaN = std::numeric_limits<double>::quiet_NaN();
const double kPi = 3.141592653589793;   // Math.PI

// Math.max / Math.min: NaN if either is NaN, +0 over -0 for max and -0 over +0 for min
inline double js_max(double a, double b) { if (a != a || b != b) return kNaN; if (a == 0 && b == 0) return std::signbit(a) ? b : a; return a > b ? a : b; }
inline double js_min(double a, double b) { if (a != a || b != b) return kNaN; if (a == 0 && b == 0) return std::signbit(a) ? a : b; return a < b ? a : b; }
inline bool truthy(double x) { return x != 0 && x == x; }    // a Number in `if (x)`
inline int32_t i32(double x) { return js_to_int32(x); }      // `x | 0`
// A Float32Array read: an index outside the array is undefined in the JS, and every sum with it NaN.
inline double rdf(const float* b, int N, double i) { return i >= 0 && i < N ? (double)b[(int)i] : kNaN; }

// beatLock(o, tr, n): Flex's clock lock. o: the core's ClkSpb, ph (the song position in beats, -1 stopped) and sp (the smoothed samples per beat).
double beatLock(ClkSpb& c, double& ph, double& sp, const ClkTr& tr, int n) {
  const float* bp = tr.bp; const int bpn = tr.bpn;
  const double spb = clk_spb(c, bp, bpn, n), q0 = bp && bpn > 1 ? (double)bp[0] : -1;
  if (q0 < 0) ph = -1;
  else if (!(ph >= 0)) ph = q0;
  else { double e = q0 - ph; e -= 16 * js_round(e / 16); if (std::fabs(e) > 0.004) ph = q0; else ph += e * 0.1; }
  if (!(sp > 0) || std::fabs(spb / sp - 1) > 0.02) sp = spb;
  return spb;
}

// bkHash(a, b, c): Math.imul((a | 0) * 73856093 ^ (b | 0) * 19349663 ^ (c | 0) * 83492791, 2654435761) >>> 0, then two xor-shift / imul rounds (the products are doubles that `^` brings to int32)
double bkHash(double a, double b, double c) {
  const uint32_t x = (uint32_t)i32((double)i32(a) * 73856093) ^ (uint32_t)i32((double)i32(b) * 19349663) ^ (uint32_t)i32((double)i32(c) * 83492791);
  uint32_t h = x * 2654435761u;
  h ^= h >> 15; h = h * 2246822519u; h = h ^ (h >> 13);
  return (double)h / 4294967296.0;
}

const double kSqrt2 = 1.4142135623730951;   // Math.SQRT2
const double kGrSteps[7] = {1, 0.5, 0.25, 0.125, 1.0 / 3, 1.0 / 6, 1.0 / 12};   // GrainsCore.STEPS
const double kDiv3[3] = {0.5, 0.25, 0.125};

}  // namespace

// ---- Beat grains -----------------------------------------------------------------------------------------------------------------------
GrainsCore::GrainsCore(double sr) : sr_(sr), N_((int)std::ceil(sr * 6)), bL_(new float[(size_t)N_]()), bR_(new float[(size_t)N_]()), c_(sr), sp_(sr / 2) {}

double GrainsCore::rnd() {
  uint32_t s = seed_; s ^= s << 13; s ^= s >> 17; s ^= s << 5;   // (the JS keeps each step as a uint32 or an int32 of the same bits)
  seed_ = s ? s : 1; return (double)(s >> 8) / 16777216;
}

void GrainsCore::start(const GrDp& dp, double stepS) {
  if (nv_ >= 24) { std::memmove(v_, v_ + 1, sizeof(Voice) * 23); nv_--; }   // v.shift()
  static const double RP1[3] = {-12, 0, 12}, RP2[5] = {-12, -5, 0, 7, 12};
  const int ri = (int)js_max(0, js_min(3, (double)i32(dp.rpitch)));
  double st = dp.pitch;
  if (i32(dp.rpitch)) {
    if (ri == 3) { const double r = rnd(); st += js_round((r * 2 - 1) * 12); }
    else if (ri == 1) { const double r = rnd(); st += RP1[(int)std::floor(r * 3)]; }
    else if (ri == 2) { const double r = rnd(); st += RP2[(int)std::floor(r * 5)]; }
    else { const double r = rnd(); (void)r; st += 0; }   // (ri 0 with a nonzero rpitch cannot happen: i32 < 0 clamps to 0 and the JS then reads RP[0] = [0])
  }
  const double rate = js_pow(2, st / 12), len = js_max(64, js_round((0.1 + dp.size * 3.9) * stepS));
  const double r1 = rnd(); const bool rev = r1 < dp.rev;
  const double r2 = rnd();
  const double back = js_max(0, (dp.pos * 2 + r2 * dp.spray) * sp_) + len * js_max(1, rate) + 4;
  if (back >= N_ - 8) return;
  double x = w_ - back; if (x < 0) x += N_;
  const double r3 = rnd();
  const double pan = (r3 * 2 - 1) * dp.spread, a = (pan + 1) * kPi / 4;
  Voice& g = v_[nv_++];
  g.x = x; g.rate = rev ? -rate : rate; g.len = len; g.age = 0; g.gl = js_cos(a) * kSqrt2; g.gr = js_sin(a) * kSqrt2; g.rev = rev; g.span = len * rate;
}

void GrainsCore::process(float* L, float* R, int n, const GrDp& dp, const ClkTr& tr) {
  const int N = N_; float* bL = bL_.get(); float* bR = bR_.get();
  const double spb = beatLock(c_, ph_, sp_, tr, n), sk = 1 - js_exp(-1 / (sr_ * 0.1));
  const int di = (int)js_max(0, js_min(6, (double)i32(dp.div)));
  const double stepB = kGrSteps[di], shape = dp.shape, mix = dp.mix;
  for (int i = 0; i < n; i++) {
    if (!truthy(dp.freeze) || fill_ < 2 * sp_) { bL[w_] = L[i]; bR[w_] = R[i]; w_ = w_ + 1 >= N ? 0 : w_ + 1; if (fill_ < N) fill_++; }   // FREEZE holds once two beats are in
    sp_ += (spb - sp_) * sk;
    double q = ph_;
    if (q >= 0) { ph_ += 1 / sp_; if (ph_ >= 16) ph_ -= 16; } else { q = fph_; fph_ += 1 / sp_; if (fph_ >= 16) fph_ -= 16; }
    const double k = std::floor(q / stepB + 1e-9);
    if (k != last_) {   // (NaN != NaN, as `!==`)
      last_ = k; const double stepS = stepB * sp_;
      if (rnd() < dp.density) start(dp, stepS);
      if (dp.density > 0.6 && rnd() < (dp.density - 0.6) * 2.5) start(dp, stepS);
    }
    double yl = 0, yr = 0;
    for (int j = nv_ - 1; j >= 0; j--) {
      Voice& g = v_[j]; const double t = g.age / g.len;
      if (t >= 1) { std::memmove(v_ + j, v_ + j + 1, sizeof(Voice) * (size_t)(nv_ - j - 1)); nv_--; continue; }   // v.splice(j, 1)
      const double hann = js_pow(js_sin(kPi * t), 2), perc = js_min(1, g.age / (0.002 * sr_)) * js_exp(-t * 6), env = hann + (perc - hann) * shape;
      double x = g.rev ? (g.x + g.span) - g.age * -g.rate : g.x + g.age * g.rate; x = std::fmod(x, (double)N); if (x < 0) x += N;
      const double a = std::floor(x), f = x - a, b = a + 1 >= N ? 0 : a + 1;
      const double la = rdf(bL, N, a), lb = rdf(bL, N, b), ra = rdf(bR, N, a), rb = rdf(bR, N, b);
      yl += (la + (lb - la) * f) * env * g.gl; yr += (ra + (rb - ra) * f) * env * g.gr;
      g.age++;
    }
    const int nvv = nv_; const double gn = nvv > 1 ? 1 / std::sqrt(0.5 + nvv * 0.5) : 1;
    L[i] = (float)((double)L[i] + (yl * gn - (double)L[i]) * mix); R[i] = (float)((double)R[i] + (yr * gn - (double)R[i]) * mix);
  }
}

// ---- Beat scramble ---------------------------------------------------------------------------------------------------------------------
ScrambleCore::ScrambleCore(double sr) : sr_(sr), N_((int)std::ceil(sr * 13)), bL_(new float[(size_t)N_]()), bR_(new float[(size_t)N_]()), c_(sr), sp_(sr / 2) {}

double ScrambleCore::rd(const float* b, double D) const {
  const int N = N_; double x = w_ - D; if (x < 0) x += N;
  const double i = std::floor(x), f = x - i, j = i + 1 >= N ? 0 : i + 1;
  const double bi = rdf(b, N, i); return bi + (rdf(b, N, j) - bi) * f;
}

void ScrambleCore::process(float* L, float* R, int n, const ScDp& dp, const ClkTr& tr) {
  const int N = N_; float* bL = bL_.get(); float* bR = bR_.get();
  const double spb = beatLock(c_, ph_, sp_, tr, n), sk = 1 - js_exp(-1 / (sr_ * 0.1));
  const double div = kDiv3[(int)js_max(0, js_min(2, (double)i32(dp.div)))], per = js_round(4 / div), XF = js_max(16, js_round(sr_ * dp.xf / 1000)), mix = dp.mix;
  for (int i = 0; i < n; i++) {
    bL[w_] = L[i]; bR[w_] = R[i];
    sp_ += (spb - sp_) * sk;
    const double q = ph_; if (q >= 0) { ph_ += 1 / sp_; if (ph_ >= 16) ph_ -= 16; }
    double D = 0;
    if (q >= 0) {
      if (pq_ >= 0 && q < pq_ - 8) wr_++;
      pq_ = q;
      const double bar = std::floor(q / 4), p = q - bar * 4, s = js_min(per - 1, std::floor(p / div + 1e-9));
      const double key = dp.vary == 1 ? 0 : dp.vary == 2 ? (double)(i32(bar) & 1) : wr_ * 4 + bar;
      if (s != sl_) {
        sl_ = s;
        const bool keep = truthy(dp.keep) && std::fmod(s * div, 1.0) < 1e-6;
        const bool go = !keep && bkHash(key, s, dp.seed) < dp.chance;
        src_ = go ? s - 1 - std::floor(bkHash(key, s + 97, dp.seed) * (per - 1)) : s;   // another of the last bar's slices
        rv_ = go && bkHash(key, s + 211, dp.seed) < dp.rev; if (rv_ && src_ == s) src_ = s - 1;
      }
      const double u = p - s * div, r = src_ * div + (rv_ ? div - u : u);
      D = js_max(0, js_min(N - 4, (p - r) * sp_));
    }
    if (std::fabs(D - D_) > 8) { xo_ = D_; xc_ = XF; xn_ = XF; }
    D_ = D;
    double yl = rd(bL, D), yr = rd(bR, D);
    if (xc_ > 0) { const double f = xc_ / xn_; yl += (rd(bL, xo_) - yl) * f; yr += (rd(bR, xo_) - yr) * f; xc_--; }
    L[i] = (float)((double)L[i] + (yl - (double)L[i]) * mix); R[i] = (float)((double)R[i] + (yr - (double)R[i]) * mix);
    w_ = w_ + 1 >= N ? 0 : w_ + 1;
  }
}

// ---- Glitch sequencer ------------------------------------------------------------------------------------------------------------------
GlitchCore::GlitchCore(double sr) : sr_(sr), N_((int)std::ceil(sr * 13)), bL_(new float[(size_t)N_]()), bR_(new float[(size_t)N_]()), c_(sr), sp_(sr / 2) {}

double GlitchCore::rd(const float* b, double D) const {
  const int N = N_; double x = w_ - D; if (x < 0) x += N;
  const double i = std::floor(x), f = x - i, j = i + 1 >= N ? 0 : i + 1;
  const double bi = rdf(b, N, i); return bi + (rdf(b, N, j) - bi) * f;
}

double GlitchCore::pick(double key, double s, const GlDp& dp) const {
  if (bkHash(key, s, dp.seed) >= dp.chance) return 0;
  const double W[7] = {dp.stut, dp.rev, dp.pitch * 0.5, dp.pitch * 0.5, dp.tape, dp.crush, dp.gate};
  double tot = 0; for (int k = 0; k < 7; k++) tot = tot + W[k];   // W.reduce((a, b) => a + b, 0)
  if (!(tot > 0)) return 0;
  double u = bkHash(key, s + 131, dp.seed) * tot;
  for (int k = 0; k < 7; k++) { u -= W[k]; if (u < 0) return k + 1; }
  return 0;
}

void GlitchCore::process(float* L, float* R, int n, const GlDp& dp, const ClkTr& tr) {
  const int N = N_; float* bL = bL_.get(); float* bR = bR_.get();
  const double spb = beatLock(c_, ph_, sp_, tr, n), sk = 1 - js_exp(-1 / (sr_ * 0.1));
  const double div = kDiv3[(int)js_max(0, js_min(2, (double)i32(dp.div)))], per = js_round(4 / div), XF = js_max(16, js_round(sr_ * 0.003)), gk = 1 - js_exp(-1 / (sr_ * 0.002)), mix = dp.mix;
  for (int i = 0; i < n; i++) {
    bL[w_] = L[i]; bR[w_] = R[i];
    sp_ += (spb - sp_) * sk;
    const double q = ph_; if (q >= 0) { ph_ += 1 / sp_; if (ph_ >= 16) ph_ -= 16; }
    double D = 0, g = 1, act = 0;
    if (q >= 0) {
      if (pq_ >= 0 && q < pq_ - 8) wr_++;
      pq_ = q;
      const double bar = std::floor(q / 4), p = q - bar * 4, s = js_min(per - 1, std::floor(p / div + 1e-9));
      const double key = dp.vary == 1 ? 0 : dp.vary == 2 ? (double)(i32(bar) & 1) : wr_ * 4 + bar;
      if (s != sl_) {
        sl_ = s; act_ = pick(key, s, dp);
        static const double SUB[3] = {2, 4, 8}; sub_ = SUB[(int)std::floor(bkHash(key, s + 307, dp.seed) * 3)];
      }
      act = act_;
      const double Ls = div * sp_, u = (p - s * div) * sp_;
      if (act == 1) { const double sb = Ls / sub_; D = u - std::fmod(u, sb); }
      else if (act == 2) D = 2 * u;
      else if (act == 3) D = u + Ls - std::fmod(2 * u, Ls);
      else if (act == 4) D = u / 2;
      else if (act == 5) { D = u * u / (2 * Ls); g = js_max(0, 1 - u / Ls); }
      else if (act == 7) g = 0;
      D = js_max(0, js_min(N - 4, D));
    }
    if (std::fabs(D - D_) > 8) { xo_ = D_; xc_ = XF; xn_ = XF; }
    D_ = D; g_ += (g - g_) * gk;
    double yl = rd(bL, D), yr = rd(bR, D);
    if (xc_ > 0) { const double f = xc_ / xn_; yl += (rd(bL, xo_) - yl) * f; yr += (rd(bR, xo_) - yr) * f; xc_--; }
    if (act == 6) { if (hc_ <= 0) { hl_ = js_round(yl * 24) / 24; hr_ = js_round(yr * 24) / 24; hc_ = 6; } hc_--; yl = hl_; yr = hr_; }
    yl *= g_; yr *= g_;
    L[i] = (float)((double)L[i] + (yl - (double)L[i]) * mix); R[i] = (float)((double)R[i] + (yr - (double)R[i]) * mix);
    w_ = w_ + 1 >= N ? 0 : w_ + 1;
  }
}

// ---- Vintage sampler -------------------------------------------------------------------------------------------------------------------
namespace {
// VSampCore.MODELS: [name, rate (Hz), bits, companded, anti-alias filter (fraction of the rate, 0: none), soft converter clip, hiss, hum]
struct VsModel { double rate, bits, comp, aa, soft, hiss, hum; };
const VsModel kVsModels[8] = {
  {26040, 12, 0, 0, 0, 0.6, 0.2}, {27500, 12, 0, 0, 0, 0.6, 0.2}, {40000, 12, 0, 0.42, 1, 0.5, 0.3}, {31250, 16, 0, 0.42, 0, 0.3, 0},
  {15630, 16, 0, 0.42, 0, 0.3, 0}, {7810, 16, 0, 0.42, 0, 0.35, 0}, {3910, 16, 0, 0.42, 0, 0.4, 0}, {27778, 8, 1, 0.4, 0, 0.8, 0.3}};
// VSampCore.lp: a 2-pole low-pass (RBJ, Q 0.707) as [b0, b1, b2, a1, a2], cut in Hz
void vsLp(double* o, double cut, double sr) {
  const double w = 2 * kPi * js_min(cut, sr * 0.45) / sr, al = js_sin(w) / (2 * 0.7071), c = js_cos(w), a0 = 1 + al;
  o[0] = (1 - c) / 2 / a0; o[1] = (1 - c) / a0; o[2] = (1 - c) / 2 / a0; o[3] = -2 * c / a0; o[4] = (1 - al) / a0;
}
// VSampCore.bq
inline double vsBq(const double* q, double* st, int o, double x) {
  const double y = q[0] * x + q[1] * st[o] + q[2] * st[o + 1] - q[3] * st[o + 2] - q[4] * st[o + 3];
  st[o + 1] = st[o]; st[o] = x; st[o + 3] = st[o + 2]; st[o + 2] = y; return y;
}
inline bool sameNum(double a, double b) { return a == b || (a != a && b != b); }   // the JS compares the numbers as strings of [model | 0, rate, filter].join(): equal numbers and NaN with NaN
}  // namespace

VSampCore::VSampCore(double sr) : sr_(sr) {}

void VSampCore::setup(const VsDp& p) {
  const int mi = (int)js_max(0, js_min(7, (double)i32(p.model)));
  const VsModel& M = kVsModels[mi]; const double sr = sr_;
  const double rate = js_min(sr, M.rate * js_max(0.25, js_min(1, p.rate))), f = js_max(0, js_min(255, (double)i32(p.filter)));
  const double m0 = (double)i32(p.model);
  if (kset_ && sameNum(k0_, m0) && sameNum(k1_, rate) && sameNum(k2_, f)) return;
  kset_ = true; k0_ = m0; k1_ = rate; k2_ = f;
  mi_ = mi; rate_ = rate; inc_ = rate / sr; q_ = js_pow(2, M.bits - 1);
  hasAa_ = M.aa > 0; if (hasAa_) vsLp(aa_, M.aa * rate, sr);
  // FILTER: 0 the raw DAC (no output filter), 1-255 from just under the machine's Nyquist down to a sixth of it, as two 2-pole stages
  hasOut_ = f != 0; if (hasOut_) vsLp(out_, rate * 0.48 * js_pow(2, -(f - 1) / 254 * 3.2), sr);
}

double VSampCore::quant(double x) const {
  const VsModel& M = kVsModels[mi_]; const double q = q_;
  if (M.comp != 0) {
    const double a = js_min(1, std::fabs(x)), y = js_log(1 + 255 * a) / js_log(256), z = js_round(y * q) / q;
    const double sg = x > 0 ? 1.0 : x < 0 ? -1.0 : x;   // Math.sign
    return sg * (js_pow(256, z) - 1) / 255;   // µ-law style 8-bit
  }
  return js_max(-q, js_min(q - 1, js_round(x * q))) / q;
}

void VSampCore::process(float* L, float* R, int n, const VsDp& p) {
  setup(p);
  const VsModel& M = kVsModels[mi_];
  const double dr = js_pow(10, (p.drive - 128) / 128 * 0.6), comp = 1 / js_max(1, dr), mix = js_max(0, js_min(1, p.mix));
  const double hiss = M.hiss * p.noise * p.noise * 0.01, hum = M.hum * p.noise * 0.004, hw = 2 * kPi * 60 / sr_;
  for (int ch = 0; ch < 2; ch++) {
    float* B = ch ? R : L; Chan& C = c_[ch];
    for (int i = 0; i < n; i++) {
      const double x = B[i];
      double v = x * dr;
      if (hasAa_) v = vsBq(aa_, C.a, 0, v);
      v = M.soft != 0 ? js_tanh(v * 1.2) / 1.2 * 1.05 : js_max(-1, js_min(1, v));   // the converter: a hard clip, or the MPC60's softer front end
      C.ph += inc_; if (C.ph >= 1) { C.ph -= std::floor(C.ph); C.h = quant(v); }      // sample and hold at the machine's rate, at its bit depth
      double y = C.h;
      if (hasOut_) { y = vsBq(out_, C.b, 0, y); y = vsBq(out_, C.b, 4, y); }
      if (hiss > 0 || hum > 0) {
        uint32_t s = s_; s ^= s << 13; s ^= s >> 17; s ^= s << 5; s_ = s;
        y += ((double)s / 2147483648.0 - 1) * hiss; if (ch == 0) hp_ += hw; y += js_sin(hp_) * hum;
      }
      C.dc += (y - C.dc) * 0.0005; y -= C.dc;   // the DACs' DC offset kept out
      B[i] = (float)(x * (1 - mix) + y * comp * mix);
    }
  }
  if (hp_ > 6.2832) hp_ -= 6.2832;
}

}  // namespace thunder
