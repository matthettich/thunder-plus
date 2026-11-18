// U31: the …chains-only modules of chains/chains-worklet.js (outside the shared block, lines 77 to 621), line for line: VCF, VCA, Mixer, Stereo mixer, Oscillator, Noise, Repeat, Track In, Resample, Chain Out,
// Sidechain In / Comp / Out, Vocoder. (LFO, ADSR, Random and Attenuverter are the same JS as …waves' and are U30's; Morph and the pump modules are in cmods2.cpp.)
// Same rules as modfx / wmods: doubles in the JS's order, float32 only where the JS stores into a Float32Array, Math.* through js_*, no allocation in process().
#include <cmath>
#include <cstring>
#include <memory>

#include "chains/chains.h"
#include "fx/pumpfx.h"
#include "modfx/util.h"
#include "waves/graph.h"
#include "waves/waves.h"

namespace thunder {
namespace chains {
#include "chains/ctable.inc"

using namespace modfx;
using waves::WModule;
using waves::WSeeds;

const WInfo* cinfo(std::string_view t) { for (int i = 0; i < kCInfoCount; i++) if (t == kCInfo[i].type) return &kCInfo[i]; return nullptr; }
int cinfo_count() { return kCInfoCount; }
const WInfo& cinfo_at(int i) { return kCInfo[i]; }

namespace {
inline const CCtx& CC(const Ctx& c) { return static_cast<const CCtx&>(c); }
inline const float* rIn(const IO& io, int l, int r) { return io.linked[r] ? io.in[r] : io.in[l]; }   // inR(io): In R if patched, else In
inline void zeroOut(float* o) { for (int i = 0; i < kBlock; i++) o[i] = 0.f; }

// ---- VCF: the TPT state-variable, a left and a right side ----------------------------------------------------------------------------------------------------
class VCF : public WModule {
 public:
  VCF(double, Seeds&) {}
  void process(const IO& io, const Ctx& c) override {
    const int mode = i32(io.p[2]); const double q = 1 - io.p[1] * .95; const float* XR = rIn(io, 0, 1); float* oR = io.out[1];
    for (int i = 0; i < c.block; i++) {
      const double fc = jmin(.45, (.01 + js_pow(10, io.p[0] * 3) * .004) * js_pow(2, (double)io.in[2][i] * 3) * 44100 / c.sr);
      const double g = js_tan(kPi * fc), a1 = 1 / (1 + g * (g + q)), a2 = g * a1, a3 = g * a2;
      const double x = io.in[0][i], v3 = x - ic2_, bp = a1 * ic1_ + a2 * v3, lp = ic2_ + a2 * ic1_ + a3 * v3;
      ic1_ = 2 * bp - ic1_; ic2_ = 2 * lp - ic2_;
      if (!(std::fabs(ic1_) < 1e4 && std::fabs(ic2_) < 1e4)) ic1_ = ic2_ = 0;
      io.out[0][i] = (float)(mode == 0 ? lp : mode == 1 ? bp : x - q * bp - lp);
      const double xr = XR[i], w3 = xr - jc2_, bq = a1 * jc1_ + a2 * w3, lq = jc2_ + a2 * jc1_ + a3 * w3;
      jc1_ = 2 * bq - jc1_; jc2_ = 2 * lq - jc2_;
      if (!(std::fabs(jc1_) < 1e4 && std::fabs(jc2_) < 1e4)) jc1_ = jc2_ = 0;
      oR[i] = (float)(mode == 0 ? lq : mode == 1 ? bq : xr - q * bq - lq);
    }
  }
 private:
  double ic1_ = 0, ic2_ = 0, jc1_ = 0, jc2_ = 0;
};
class VCA : public WModule {
 public:
  VCA(double, Seeds&) {}
  void process(const IO& io, const Ctx& c) override {
    const double g = io.p[0]; const bool cv = io.linked[2]; const float* XR = rIn(io, 0, 1); float* oR = io.out[1];
    for (int i = 0; i < c.block; i++) { const double k = (cv ? (double)io.in[2][i] : 1) * g; io.out[0][i] = (float)((double)io.in[0][i] * k); oR[i] = (float)((double)XR[i] * k); }
  }
};
inline int mixCount(double chans) { const double r = js_round(chans); const double n = (r != 0 && r == r) ? r : 4; return (int)clamp(n, 1, 16); }
class Mixer : public WModule {
 public:
  Mixer(double, Seeds&) {}
  void process(const IO& io, const Ctx& c) override {
    const double* p = io.p; const int n = c.block, N = mixCount(p[0]);
    zeroOut(io.out[0]);
    for (int k = 0; k < N; k++) {
      if (!io.linked[k]) continue;
      const float* x = io.in[k]; const double g = p[1 + k] * p[17];
      for (int i = 0; i < n; i++) io.out[0][i] = (float)((double)io.out[0][i] + (double)x[i] * g);
    }
  }
};
class StereoMixer : public WModule {
 public:
  StereoMixer(double, Seeds&) {}
  void process(const IO& io, const Ctx& c) override {
    const double* p = io.p; const int n = c.block, N = mixCount(p[0]); float* oL = io.out[0]; float* oR = io.out[1];
    zeroOut(oL); zeroOut(oR);
    for (int k = 0; k < N; k++) {
      const int a = 2 * k, b = 2 * k + 1; const bool la = io.linked[a], lb = io.linked[b]; if (!la && !lb) continue;
      const float* L = la ? io.in[a] : io.in[b]; const float* R = lb ? io.in[b] : io.in[a];
      const double g = p[1 + 2 * k] * p[33], pn = p[2 + 2 * k], gL = g * (pn > 0 ? 1 - pn : 1), gR = g * (pn < 0 ? 1 + pn : 1);
      for (int i = 0; i < n; i++) { oL[i] = (float)((double)oL[i] + (double)L[i] * gL); oR[i] = (float)((double)oR[i] + (double)R[i] * gR); }
    }
  }
};
// ---- Oscillator, Noise ----------------------------------------------------------------------------------------------------------------------------------------
class Osc : public WModule {
 public:
  Osc(double, Seeds&) {}
  void process(const IO& io, const Ctx& c) override {
    const float* F = io.in[0]; const float* S = io.in[1]; float* O = io.out[0]; const double* p = io.p; const int w = i32(p[0]); const double f0 = p[1] / c.sr, d = p[2] * 5, lv = p[3]; const bool fm = d > 0 && io.linked[0];
    double ph = ph_; bool ls = ls_;
    for (int i = 0; i < c.block; i++) {
      const bool sy = S[i] >= .5; if (sy && !ls) ph = 0; ls = sy;
      double dt = fm ? f0 * js_pow(2, clamp((double)F[i], -4, 4) * d) : f0; if (!(dt > 0)) dt = 0; else if (dt > .49) dt = .49;
      double y;
      if (w == 0) y = js_sin(6.283185307179586 * ph);
      else if (w == 1) y = 1 - 4 * std::fabs(ph - .5);
      else {
        const double t = ph; double b = 0; if (t < dt) { const double u = t / dt; b = u + u - u * u - 1; } else if (t > 1 - dt) { const double u = (t - 1) / dt; b = u * u + u + u + 1; }
        if (w == 2) y = 2 * t - 1 - b;
        else { double t2 = t + .5; if (t2 >= 1) t2 -= 1; double b2 = 0; if (t2 < dt) { const double u = t2 / dt; b2 = u + u - u * u - 1; } else if (t2 > 1 - dt) { const double u = (t2 - 1) / dt; b2 = u * u + u + u + 1; } y = (t < .5 ? 1 : -1) + b - b2; }
      }
      O[i] = (float)(y * lv); ph += dt; if (ph >= 1) ph -= 1;
    }
    ph_ = ph; ls_ = ls;
  }
 private:
  double ph_ = 0; bool ls_ = false;
};
class NoiseSrc : public WModule {
 public:
  NoiseSrc(double, Seeds& s) : seeds_(&s) {}
  void process(const IO& io, const Ctx& c) override {
    float* O = io.out[0]; const int col = i32(io.p[0]); const double lv = io.p[1];
    uint32_t s = s_; if (!s) { s_ = seeds_->next(); s = s_ ? s_ : 1; s_ = s; }   // st.s || (st.s = nextSeed() || 1): its own run of numbers, taken the first time it sounds
    for (int i = 0; i < c.block; i++) {
      s ^= s << 13; s ^= s >> 17; s ^= s << 5; const double w = (double)s / 2147483648.0 - 1; double y;
      if (col == 0) y = w;
      else if (col == 1) {
        b0_ = .99886 * b0_ + w * .0555179; b1_ = .99332 * b1_ + w * .0750759; b2_ = .969 * b2_ + w * .153852; b3_ = .8665 * b3_ + w * .3104856;
        b4_ = .55 * b4_ + w * .5329522; b5_ = -.7616 * b5_ - w * .016898; y = (b0_ + b1_ + b2_ + b3_ + b4_ + b5_ + b6_ + w * .5362) * .11; b6_ = w * .115926;
      } else { br_ = (br_ + .02 * w) / 1.02; y = br_ * 3.5; }
      O[i] = (float)(y * lv);
    }
    s_ = s;
  }
 private:
  Seeds* seeds_; uint32_t s_ = 0; double b0_ = 0, b1_ = 0, b2_ = 0, b3_ = 0, b4_ = 0, b5_ = 0, b6_ = 0, br_ = 0;
};
// ---- Repeat ---------------------------------------------------------------------------------------------------------------------------------------------------
const double kDivB[10] = {.0625, .125, 1.0 / 6, .25, 1.0 / 3, .5, 1, 2, 4, 8};
class Repeat : public WModule {
 public:
  Repeat(double sr, Seeds&) : N_((int)js_round(sr * 4)), L_(N_), R_(N_) {}
  void process(const IO& io, const Ctx& c) override {
    const int N = N_; const float* inR = io.linked[1] ? io.in[1] : io.in[0]; const bool gl = io.linked[2];
    const double* p = io.p; const int mode = i32(p[1]); const double mix = p[5], dec = p[3], pst = p[4];
    for (int i = 0; i < c.block; i++) {
      const double l = io.in[0][i], r = inR[i];
      const bool want = p[0] >= .5 || (gl && io.in[2][i] > .5);
      if (want && !on_) {
        on_ = true; len_ = jmax(64, jmin(N - 1, js_round(kDivB[(int)clamp((double)i32(p[2]), 0, 9)] * 60 / jmax(c.tempo, 20) * c.sr)));
        at_ = mode == 2 ? jmod(w_ - 1 + N, N) : jmod(w_ - len_ + N, N); pos_ = 0; pass_ = 0; sp_ = 1;
      } else if (!want && on_) on_ = false;
      if (!on_ || mode == 2) { L_[(int)w_] = l; R_[(int)w_] = r; w_ = jmod(w_ + 1, N); }
      fade_ += ((on_ ? 1 : 0) - fade_) * .004;
      double ol = l, orr = r;
      if (fade_ > 1e-4) {
        double pp;
        if (mode == 2) { sp_ = jmax(0, sp_ - 1 / len_); pp = pos_; pos_ += sp_; }
        else {
          const double g = js_pow(2, pst * pass_ / 12); pp = mode == 1 ? len_ - 1 - pos_ : pos_; pos_ += g;
          if (pos_ >= len_) { pos_ -= len_; pass_++; }
        }
        const double j = mode == 2 ? pp : jmin(len_ - 1, jmax(0, pp)); const int k = i32(j); const double f = j - k;
        const double a = jmod(at_ + k, N), b = jmod(a + 1, N);
        const double lv = mode == 2 ? 1 : js_pow(1 - dec * .9, pass_);
        const double e = mode == 2 ? 1 : jmin(1, jmin(j, len_ - 1 - j) / 48);
        const double wl = (L_[(int)a] + (L_[(int)b] - L_[(int)a]) * f) * lv * e, wr = (R_[(int)a] + (R_[(int)b] - R_[(int)a]) * f) * lv * e;
        const double m = fade_ * mix; ol = l + (wl - l) * m; orr = r + (wr - r) * m;
      }
      io.out[0][i] = (float)ol; io.out[1][i] = (float)orr;
    }
  }
 private:
  int N_; F32 L_, R_; double w_ = 0; bool on_ = false; double at_ = 0, len_ = 1, pos_ = 0, pass_ = 0, sp_ = 1, fade_ = 0;
};
// ---- Track In, Chain Out --------------------------------------------------------------------------------------------------------------------------------------
class TrackIn : public WModule {
 public:
  TrackIn(double, Seeds&) {}
  void process(const IO& io, const Ctx& c) override {
    const CCtx& x = CC(c); const int k = (int)clamp((double)(i32(io.p[0]) - 1), 0, kChannels - 1);
    if (x.inL && x.inR && k < x.inChans) {
      std::memcpy(io.out[0], x.inL + (size_t)k * kBlock, sizeof(float) * (size_t)c.block); std::memcpy(io.out[1], x.inR + (size_t)k * kBlock, sizeof(float) * (size_t)c.block);
    } else { zeroOut(io.out[0]); zeroOut(io.out[1]); }   // (a track beyond the host's buffer: silence; the JS reads undefined there)
  }
};
class ChainOut : public WModule {
 public:
  ChainOut(double, Seeds&) {}
  void process(const IO& io, const Ctx& c) override {
    const CCtx& x = CC(c); const int k = (int)clamp((double)(i32(io.p[0]) - 1), 0, kChannels - 1); const double g = io.p[1]; float* L = x.busL(k); float* R = x.busR(k);
    const float* inR = io.linked[1] ? io.in[1] : io.in[0];
    for (int i = 0; i < c.block; i++) { L[i] = (float)((double)L[i] + (double)io.in[0][i] * g); R[i] = (float)((double)R[i] + (double)inR[i] * g); }
  }
};
// ---- Sidechain ------------------------------------------------------------------------------------------------------------------------------------------------
class SidechainIn : public WModule {
 public:
  SidechainIn(double, Seeds&) {}
  void process(const IO& io, const Ctx& c) override {
    const float* tk = c.key ? c.key(c.keyUser, io.p[3]) : nullptr; if (!tk) tk = CC(c).zero;
    const double g = io.p[0], sg = js_pow(10, io.p[1] / 20); float* oE = io.out[1]; float* oD = io.out[2];
    const double ka = 1 - js_exp(-1 / (.002 * c.sr)), kr = 1 - js_exp(-1 / jmax(1, io.p[2] / 1000 * c.sr));
    for (int i = 0; i < c.block; i++) {
      const double x = ((double)tk[i] + (double)tk[i]) * .5, ax = x < 0 ? -x : x, a = ax * sg;
      env_ += (a - env_) * (a > env_ ? ka : kr);
      const double e = env_ > 1 ? 1 : env_;
      io.out[0][i] = (float)(x * g); oE[i] = (float)e; oD[i] = (float)(1 - e);
    }
    if (!(env_ < 1e3)) env_ = 0;
  }
 private:
  double env_ = 0;
};
class SidechainComp : public WModule {
 public:
  SidechainComp(double, Seeds&) {}
  void process(const IO& io, const Ctx& c) override {
    const bool patched = io.linked[2]; const float* K = !patched && io.p[6] > 0 && c.key ? c.key(c.keyUser, io.p[6]) : nullptr;
    const double thr = -60 + io.p[0] * 60, rat = jmax(1, io.p[1]);
    const double ka = 1 - js_exp(-1 / jmax(1, io.p[2] / 1000 * c.sr)), kr = 1 - js_exp(-1 / jmax(1, io.p[3] / 1000 * c.sr));
    const double mk = io.p[4], mx = io.p[5]; const float* XR = rIn(io, 0, 1); float* oR = io.out[1];
    for (int i = 0; i < c.block; i++) {
      const double det = patched ? (double)io.in[2][i] : K ? (double)K[i] : jmax(std::fabs((double)io.in[0][i]), std::fabs((double)XR[i]));
      const double a = std::fabs(det);
      env_ += (a - env_) * (a > env_ ? ka : kr);
      const double over = 20 * js_log10(env_ + 1e-9) - thr;
      const double g = over > 0 ? js_pow(10, -over * (1 - 1 / rat) / 20) : 1;
      const double gg = 1 - mx + g * mk * mx; io.out[0][i] = (float)((double)io.in[0][i] * gg); oR[i] = (float)((double)XR[i] * gg);
    }
  }
 private:
  double env_ = 0;
};
class SidechainOut : public WModule {
 public:
  SidechainOut(double, Seeds&) {}
  void process(const IO& io, const Ctx& c) override {
    const CCtx& x = CC(c); const int n = (int)clamp(js_round(io.p[0] != 0 && io.p[0] == io.p[0] ? io.p[0] : 1), 1, kSends) - 1; float* o = x.sendOut[n]; const double g = io.p[1];
    const float* L = io.in[0]; const float* R = rIn(io, 0, 1);
    for (int i = 0; i < c.block; i++) { const double v = ((double)L[i] + (double)R[i]) * .5 * g; if (o) o[i] = (float)((double)o[i] + v); }
  }
};
// ---- Vocoder --------------------------------------------------------------------------------------------------------------------------------------------------
constexpr int FFTN = 256, FFTH = FFTN >> 1;
const int kVocBands[7] = {6, 4, 12, 16, 32, 64, 0};
void fftInPlace(float* re, float* im, bool inv) {   // the two arrays are Float32Arrays: every store rounds
  const int n = FFTN;
  for (int i = 1, j = 0; i < n; i++) {
    int bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit;
    if (i < j) { float t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
  }
  for (int len = 2; len <= n; len <<= 1) {
    const double ang = (inv ? 2 : -2) * kPi / len, wr = js_cos(ang), wi = js_sin(ang);
    for (int i = 0; i < n; i += len) {
      double cr = 1, ci = 0;
      for (int j = 0; j < len >> 1; j++) {
        const double ur = re[i + j], ui = im[i + j], vr = (double)re[i + j + (len >> 1)] * cr - (double)im[i + j + (len >> 1)] * ci, vi = (double)re[i + j + (len >> 1)] * ci + (double)im[i + j + (len >> 1)] * cr;
        re[i + j] = (float)(ur + vr); im[i + j] = (float)(ui + vi); re[i + j + (len >> 1)] = (float)(ur - vr); im[i + j + (len >> 1)] = (float)(ui - vi);
        const double ncr = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = ncr;
      }
    }
  }
  if (inv) for (int i = 0; i < n; i++) { re[i] = (float)((double)re[i] / n); im[i] = (float)((double)im[i] / n); }
}
class Vocoder : public WModule {
 public:
  Vocoder(double, Seeds&) : mc1(64), mc2(64), cc1(64), cc2(64), env(64), C(192), win(FFTN), pcar(FFTH), pmod(FFTH), acc(FFTH), re(FFTN), im(FFTN), mre(FFTN), mim(FFTN) {}
  void process(const IO& io, const Ctx& c) override {
    const int mode = (int)clamp((double)i32(io.p[0]), 0, 6); const double mix = io.p[4], lvl = io.p[5];
    const float* car = io.in[0]; const float* mod = io.linked[1] ? io.in[1] : io.in[0]; float* out = io.out[0]; float* outR = io.out[1];
    if (mode == 6) spectral(c, car, mod, out, mix, lvl); else bands(io, c, kVocBands[mode], car, mod, out, mix, lvl, mode == 0);
    for (int i = 0; i < c.block; i++) outR[i] = out[i];
  }
 private:
  void bands(const IO& io, const Ctx& c, int N, const float* car, const float* mod, float* out, double mix, double lvl, bool talk) {
    const double sr = c.sr; const int nb = (int)jmin(N, !c.full ? 16 : N), n = c.block;
    const double fmin = 90, fmax = talk ? 4200 : 6800, res = io.p[1];
    const double damp = talk ? jmax(.12, 1.2 - res * 1.05) : jmax(.28, 1.5 - res * 1.15);
    const double ka = 1 - js_exp(-1 / (jmax(.1, io.p[2]) / 1000 * sr)), kr = 1 - js_exp(-1 / (jmax(1, io.p[3]) / 1000 * sr));
    for (int k = 0; k < nb; k++) {
      const double fc = jmin(fmin * js_pow(fmax / fmin, nb > 1 ? (double)k / (nb - 1) : .5), sr * 0.45);
      const double g = js_tan(kPi * fc / sr), a1 = 1 / (1 + g * (g + damp));
      C[k * 3] = a1; C[k * 3 + 1] = g * a1; C[k * 3 + 2] = g * g * a1;
    }
    const double g2 = lvl * (talk ? 1.3 : 2.2 / std::sqrt((double)nb));
    for (int i = 0; i < n; i++) {
      const double xm = mod[i], xc = car[i]; double acc2 = 0;
      for (int k = 0; k < nb; k++) {
        const double a1 = C[k * 3], a2 = C[k * 3 + 1], a3 = C[k * 3 + 2];
        const double m3 = xm - (double)mc2[k], m1 = a1 * (double)mc1[k] + a2 * m3, m2 = (double)mc2[k] + a2 * (double)mc1[k] + a3 * m3;
        mc1[k] = 2 * m1 - (double)mc1[k]; mc2[k] = 2 * m2 - (double)mc2[k];
        const double a = std::fabs(m1); env[k] += (a - (double)env[k]) * (a > (double)env[k] ? ka : kr);
        const double c3 = xc - (double)cc2[k], c1 = a1 * (double)cc1[k] + a2 * c3, c2 = (double)cc2[k] + a2 * (double)cc1[k] + a3 * c3;
        cc1[k] = 2 * c1 - (double)cc1[k]; cc2[k] = 2 * c2 - (double)cc2[k];
        acc2 += c1 * (double)env[k];
      }
      double y = acc2 * g2; if (talk) y = js_tanh(y * 1.6) * .625;
      out[i] = (float)((double)car[i] * (1 - mix) + y * mix);
    }
  }
  void spectral(const Ctx&, const float* car, const float* mod, float* out, double mix, double lvl) {
    if (!haveWin_) { for (int i = 0; i < FFTN; i++) win[i] = .5 - .5 * js_cos(2 * kPi * i / FFTN); haveWin_ = true; }
    float* r = re.get(); float* m = im.get(); float* mr = mre.get(); float* mi = mim.get(); const float* w = win.get();
    for (int i = 0; i < FFTN; i++) {
      const double v = i < FFTH ? (double)pcar[i] : (double)car[i - FFTH]; r[i] = (float)(v * w[i]); m[i] = 0;
      const double u = i < FFTH ? (double)pmod[i] : (double)mod[i - FFTH]; mr[i] = (float)(u * w[i]); mi[i] = 0;
    }
    fftInPlace(r, m, false); fftInPlace(mr, mi, false);
    for (int b = 0; b < FFTN; b++) {
      const double cmag = js_hypot2(r[b], m[b]), mmag = js_hypot2(mr[b], mi[b]), s = mmag / (cmag + 1e-6);
      r[b] = (float)((double)r[b] * s); m[b] = (float)((double)m[b] * s);
    }
    fftInPlace(r, m, true);
    const double g = lvl;
    for (int i = 0; i < FFTH; i++) {
      const double norm = (double)w[i] * w[i] + (double)w[i + FFTH] * w[i + FFTH];
      const double dry = pcar[i];
      const double wet = norm > 1e-8 ? ((double)acc[i] + (double)r[i] * w[i]) / norm : 0;
      out[i] = (float)(dry * (1 - mix) + wet * g * mix);
    }
    for (int i = 0; i < FFTH; i++) acc[i] = (double)r[FFTH + i] * w[FFTH + i];
    for (int i = 0; i < FFTH; i++) { pcar[i] = car[i]; pmod[i] = mod[i]; }
  }
  F32 mc1, mc2, cc1, cc2, env, C, win, pcar, pmod, acc, re, im, mre, mim; bool haveWin_ = false;
};

template <class M> std::unique_ptr<Module> mk(double sr, Seeds& s) { return std::unique_ptr<Module>(new M(sr, s)); }
}  // namespace

// cmods2.cpp
std::unique_ptr<Module> makeMorph(double sr, Seeds& s);
std::unique_ptr<Module> makePump(const char* type, double sr, Seeds& s);
std::unique_ptr<Module> makeResample(double sr, Seeds& s);
const Def* pumpDef(std::string_view type);
extern const char* const kPumpTypes[];
extern const int kPumpTypeN;

namespace {
struct Own { const char* type; std::unique_ptr<Module> (*make)(double, Seeds&); };
const Own kCOwn[] = {
  {"vcf", mk<VCF>}, {"vca", mk<VCA>}, {"mixer", mk<Mixer>}, {"smixer", mk<StereoMixer>}, {"osc", mk<Osc>}, {"wnoise", mk<NoiseSrc>}, {"repeat", mk<Repeat>}, {"trackIn", mk<TrackIn>}, {"chainOut", mk<ChainOut>},
  {"scin", mk<SidechainIn>}, {"sccomp", mk<SidechainComp>}, {"scout", mk<SidechainOut>}, {"vocoder", mk<Vocoder>}, {"morph", makeMorph}, {"resample", makeResample},
};
// A build without the hosted mods (Marbles / Tides): silence, and Marbles' draw from the seed counter (the JS's create does it at the module's first block)
template <bool Draws, int NOut> class LibStub : public WModule {
 public:
  LibStub(double, Seeds&) {}
  void arm(Seeds& s) override { if (Draws) (void)s.next(); }
  void process(const IO& io, const Ctx& c) override { (void)c; for (int k = 0; k < NOut; k++) std::memset(io.out[k], 0, sizeof(float) * kBlock); }
};
std::vector<Def>& ownDefs() {
  static std::vector<Def> v = [] {
    std::vector<Def> r;
    for (const Own& o : kCOwn) { const WInfo* w = cinfo(o.type); if (w) r.push_back(Def{o.type, w->nin, w->in, w->nout, w->out, w->np, w->params, o.make}); }
    return r;
  }();
  return v;
}
}  // namespace

const Def* cdef(std::string_view type) {
  const std::string t(type);
  for (const Def& d : ownDefs()) if (t == d.type) return &d;
  if (const Def* p = pumpDef(type)) return p;
  if (const Def* d = waves::wdef(type)) return d;
  if (t == "marbles" || t == "tides") {   // a build without the hosted mods: a silent stand-in that still makes Marbles' one draw from the seed counter at its first block (the JS's create), so the modules after it draw the same seeds
    static const Def kMa = [] { const WInfo* w = cinfo("marbles"); return Def{"marbles", w->nin, w->in, w->nout, w->out, w->np, w->params, mk<LibStub<true, 7>>}; }();
    static const Def kTi = [] { const WInfo* w = cinfo("tides"); return Def{"tides", w->nin, w->in, w->nout, w->out, w->np, w->params, mk<LibStub<false, 4>>}; }();
    return t == "marbles" ? &kMa : &kTi;
  }
  return nullptr;   // modfx's registry, then the waves-only modules (the effects, LFO / ADSR / Random / Attenuverter, Expression, ...): the same JS
}
struct CMem { const char* type; long b48; long perHz; };
static const CMem kCMem[] = {
#include "chains/cmem.inc"
};
double chains_module_bytes(std::string_view type, double sr) {
  for (const CMem& m : kCMem) if (type == m.type) return (double)m.b48 + (double)m.perHz * (sr > 48000 ? sr - 48000 : 0);
  return 64.0 * 1048576.0;   // (a type the table does not know: a generous guess, so it is never counted as free)
}
bool chains_type_is_lib(std::string_view type) {
  if (waves::waves_type_is_fx(type) || type == "marbles" || type == "tides") return true;
  for (int i = 0; i < kPumpTypeN; i++) if (type == kPumpTypes[i]) return true;
  return false;
}
bool chains_type_is_pump(std::string_view type) { for (int i = 0; i < kPumpTypeN; i++) if (type == kPumpTypes[i]) return true; return false; }
bool chains_have_lib(std::string_view type) {
  if (!chains_type_is_lib(type)) return true;
  if (chains_type_is_pump(type)) return pump::available();
  return waves::Host::haveEffects();
}
const waves::ModTable& chainsTable() { static const waves::ModTable t = {&cinfo, &cdef}; return t; }

}  // namespace chains
}  // namespace thunder
