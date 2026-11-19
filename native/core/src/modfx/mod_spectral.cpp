// MODFX modules B (1 of 2): Spectral and Extreme stretch (chains-worklet.js 2989 to 3414), with the helpers they share (fxSafe, sinR / cosR, wrapPi, tiltGain). Line for line from the JS:
// doubles in the JS's order, float32 where the JS keeps a Float32Array (F32 / FRef make a store round and a read give the double), Math.* through js_*, xorshift32 on uint32 (the JS keeps an
// int32 and reads it with >>> 0: the same bits). Everything is allocated in the constructors (the JS does it in create()), the FFT tables come from the shared cache (fftr.h).
#include <limits>
#include <vector>

#include "modfx/fftr.h"
#include "modfx/mods_b.h"
#include "modfx/util.h"

namespace thunder {
namespace modfx {
namespace {

#define PD(a) (int)(sizeof(a) / sizeof(a[0])), a
const PortDef kStIn[] = {{"in", "audio"}, {"inR", "audio"}}, kStOut[] = {{"out", "audio"}, {"outR", "audio"}};
inline double u01(double v) { return clamp(v, 0, 1); }
const double kTwoPi = 2 * kPi, kHalfPi = kPi / 2, kInv2Pi = 0.15915494309189535, kU32 = 2.3283064365386963e-10;

// the last word on a wet signal: untouched below 2, rounds off towards 4 above
inline double fxSafe(double y) { const double a = y < 0 ? -y : y; if (a < 2) return y; const double e = a - 2; return (y < 0 ? -1 : 1) * (2 + e / (1 + e * .5)); }
inline double sinR(double a) { return sinT(a * kInv2Pi); }
inline double cosR(double a) { return sinT(a * kInv2Pi + .25); }
inline double wrapPi(double a) { return a - kTwoPi * js_round(a * kInv2Pi); }
inline double tiltGain(int k, int n, double sr, double tone) { const double f = jmax(30, k * sr / n); return clamp(js_pow(f * .001, tone * .75), .063, 4); }
inline void xs(uint32_t& r) { r ^= r << 13; r ^= r >> 17; r ^= r << 5; }   // r ^= r << 13; r ^= r >>> 17; r ^= r << 5

// ---- Spectral ------------------------------------------------------------------------------------------------------------------------------------------------
constexpr int SPX_N = 2048, SPX_H = 512, SPX_K = SPX_N / 2 + 1;
const ParamDef kSpxP[] = {{"mode", 0, 0, 5, 1}, {"amt", .5, 0, 1, .01}, {"shift", .15, -1, 1, .01}, {"tone", 0, -1, 1, .01}, {"freeze", 0, 0, 1, 1}, {"mix", .5, 0, 1, .01}};

struct SpxCh {
  F32 fin, acc, oq, x, re, im, prev, B, as, q, A, F, fq, fa, fg, gg, sm, sp, sa, yp, yn, ao, an, qs;
  uint32_t r; bool cap = false; double pkA = 0, fpk = 0; bool fbOn = false;
  explicit SpxCh(uint32_t seed) : r(seed) {
    const int K = SPX_K;
    fin.init(SPX_N); acc.init(SPX_N); oq.init(SPX_H); x.init(SPX_N);
    for (F32* a : {&re, &im, &prev, &B, &as, &q, &A, &F, &fq, &fa, &fg, &gg, &sm, &sp, &sa, &yp, &yn, &ao, &an, &qs}) a->init(K);
  }
};

class Spectral : public Module {
 public:
  Spectral(double, Seeds&) : c0_(0x9e3779b9u), c1_(0x2545f491u), cr_(SPX_H >> 1), fft_(FFTR::get(SPX_N)), W_(FFTR::hann(SPX_N)), job_(SPX_N) { tone_.init(SPX_K); }
  void process(const IO& io, const Ctx& ctx) override {
    const double* p = io.p; const In L = inp(io, 0), R = inR(io, 0, 1); const Out oL = outp(io, 0), oR = outp(io, 1); const double sr = ctx.sr; const int H = SPX_H, N = SPX_N;
    const int mode = (int)clamp((double)i32(p[0]), 0, 5); const double amt = u01(p[1]), mx = u01(p[5]), sv = clamp(p[2], -1, 1); const bool frz = p[4] >= .5;
    const double tone = clamp(p[3], -1, 1);
    if (tone != toneV_) { toneV_ = tone; for (int k = 0; k < SPX_K; k++) tone_[k] = tiltGain(k, N, sr, tone); }   // (a NaN tone: always unequal, as in the JS)
    float* f0 = c0_.fin.get(); float* f1 = c1_.fin.get(); const float* q0 = c0_.oq.get(); const float* q1 = c1_.oq.get();
    int cl = cl_, cr = cr_; double m = mix_ < 0 ? mx : mix_;
    for (int i = 0; i < ctx.block; i++) {
      // a NaN, an infinity or an absurd sample (|x| >= 8) in is a 0, on the dry path too
      const double l0 = L[i], r0 = R[i], l = l0 > -8 && l0 < 8 ? l0 : 0, r = r0 > -8 && r0 < 8 ? r0 : 0;
      f0[N - H + cl] = (float)l; f1[N - H + cr] = (float)r;
      m += (mx - m) * .002;   // Mix glides, so it never clicks
      oL[i] = l + (fxSafe((double)q0[cl]) - l) * m; oR[i] = r + (fxSafe((double)q1[cr]) - r) * m;
      if (++cl == H) {
        cl = 0;
        fz_ = frz ? jmin(1, fz_ + H / (.08 * sr)) : jmax(0, fz_ - H / (.3 * sr));
        frame(c0_, mode, amt, sv, frz, sr);
      }
      if (++cr == H) { cr = 0; frame(c1_, mode, amt, sv, frz, sr); }
    }
    cl_ = cl; cr_ = cr; mix_ = m;
  }
 private:
  static void reset(SpxCh& c) {
    for (F32* a : {&c.fin, &c.acc, &c.oq, &c.prev, &c.B, &c.as, &c.q, &c.A, &c.F, &c.fq, &c.fa, &c.gg, &c.yp, &c.yn, &c.ao, &c.an, &c.qs}) a->fill(0);
    c.fg.fill(1); c.cap = false; c.pkA = 0; c.fpk = 0;
  }
  // One frame of one channel: analyse, change, resynthesise, overlap-add, hand the next 512 samples to the output.
  void frame(SpxCh& c, int mode, double amt, double sv, bool frz, double sr) {
    const int N = SPX_N, H = SPX_H, K = SPX_K; const float* W = W_; F32& x = c.x; float* re = c.re.get(); float* im = c.im.get(); F32& fin = c.fin; F32& T = tone_;
    F32& sm = c.sm; F32& sp = c.sp; F32& sa = c.sa; F32& prev = c.prev; F32& Bm = c.B; F32& AS = c.as; F32& Q = c.q; F32& A = c.A;
    double inPk = 0;
    for (int i = 0; i < N; i++) { const double v = fin[i] * (double)W[i]; x[i] = v; const double a = v < 0 ? -v : v; if (a > inPk) inPk = a; }
    fft_->forward(job_, x.get(), re, im);
    c.pkA += (inPk - c.pkA) * .4;
    const bool blur = mode == 0 && amt > .001;
    // Blur: each bin's power follows the input through a one-pole smoother
    const double cb = blur ? 1 - js_exp(-H / (sr * .15 * js_pow(80, amt) / 13.8)) : 1, jb = .7 * amt;
    uint32_t r = c.r;
    for (int k = 0; k < K; k++) {
      const double a = re[k], b = im[k], M = std::sqrt(a * a + b * b), phi = js_atan2(b, a);
      // the bin's phase advance since the last frame: its expected k pi/2 (hop N/4) plus the measured deviation
      const double e = (k & 3) * kHalfPi; double d = phi - prev[k] - e; d -= kTwoPi * js_round(d * kInv2Pi);
      const double adv = e + d; prev[k] = phi;
      if (blur) {
        double Pv = Bm[k] + (M * M - Bm[k]) * cb; if (Pv < 1e-30) Pv = 0; Bm[k] = Pv;
        const double Bv = std::sqrt(Pv);
        double dd = adv - AS[k]; dd -= kTwoPi * js_round(dd * kInv2Pi);
        AS[k] = wrapPi(AS[k] + dd * .5 * (M >= Bv ? 1 : M / (Bv + 1e-20)));
        // a bin the input still feeds keeps the input's phase; a fading one runs on at its own frequency, drifting a little
        if (M >= .7 * Bv) Q[k] = phi;
        else { xs(r); Q[k] = wrapPi(Q[k] + AS[k] + ((double)r * kU32 - .5) * jb); }
        sm[k] = Bv; sp[k] = Q[k]; sa[k] = AS[k];
      } else { Bm[k] = M * M; AS[k] = adv; Q[k] = phi; sm[k] = M; sp[k] = phi; sa[k] = adv; }
      const double av = A[k] + (sm[k] - A[k]) * .4; A[k] = av < 1e-15 ? 0 : av;
    }
    // Freeze: catch the (lightly averaged) spectrum, then keep each bin turning at its own frequency, plus drift
    const double fz = fz_;
    if (frz && !c.cap) { c.cap = true; for (int k = 0; k < K; k++) { c.F[k] = (double)A[k]; c.fq[k] = (double)sp[k]; c.fa[k] = (double)sa[k]; } c.fg.fill(1); c.fpk = c.pkA; }
    if (fz > 0 && c.cap) {
      F32& F = c.F; F32& FQ = c.fq; F32& FA = c.fa; F32& FG = c.fg; const double jit = mode == 1 ? .08 + amt * 1.1 : .35, mot = mode == 1 ? amt * .05 : 0;
      for (int k = 0; k < K; k++) {
        xs(r); const double u1 = (double)r * kU32 - .5;
        FQ[k] = wrapPi(FQ[k] + FA[k] + u1 * jit);
        double g = 1;
        if (mot > 0) { xs(r); g = FG[k] + (1 - FG[k]) * .03 + ((double)r * kU32 - .5) * mot; g = g < .3 ? .3 : g > 1.7 ? 1.7 : g; FG[k] = g; }
        const double fm = F[k] * g;
        if (fz >= 1) { sm[k] = fm; sp[k] = FQ[k]; sa[k] = FA[k]; }
        else {
          const double w0 = 1 - fz, yr = w0 * sm[k] * cosR(sp[k]) + fz * fm * cosR(FQ[k]), yi = w0 * sm[k] * sinR(sp[k]) + fz * fm * sinR(FQ[k]);
          sm[k] = std::sqrt(yr * yr + yi * yi); sp[k] = js_atan2(yi, yr); if (fz > .5) sa[k] = FA[k];
        }
      }
    }
    if (!frz && fz == 0) c.cap = false;
    // the mode
    if (mode == 5) {
      const double hz = jsign(sv) * sv * sv * 2000, dB = hz * N / sr, dphi = kTwoPi * hz * H / sr, fb = amt * .8 * jmin(1, std::fabs(hz) / 50);
      F32* yp = &c.yp; F32* yn = &c.yn; F32* ao = &c.ao; F32* an = &c.an; F32& QS = c.qs;
      if (!c.fbOn) { yp->fill(0); ao->fill(0); c.fbOn = true; }
      re[0] = 0; im[0] = 0; re[K - 1] = 0; im[K - 1] = 0; (*yn)[0] = 0; (*yn)[K - 1] = 0;
      for (int j = 1; j < K - 1; j++) {
        const double s = j - dB;
        if (s < 1 || s > K - 2) { (*yn)[j] = 0; (*an)[j] = 0; re[j] = 0; im[j] = 0; continue; }
        const int i0 = i32(s); const double fr = s - i0; const int i1 = i0 + 1;
        const double m0 = sm[i0] + (sm[i1] - sm[i0]) * fr, m1 = fb * ((*yp)[i0] + ((*yp)[i1] - (*yp)[i0]) * fr); const int ks = fr < .5 ? i0 : i1;
        // the fed-back spiral adds as power (a different partial each time round), so it can't pile up
        const double av = (m0 >= m1 ? (double)sa[ks] : (double)(*ao)[ks]) + dphi, m = std::sqrt(m0 * m0 + m1 * m1);
        (*yn)[j] = m > 1e-15 ? m : 0; (*an)[j] = wrapPi(av);
        const double ph = wrapPi(QS[j] + av); QS[j] = ph; const double g = m * T[j];
        re[j] = (float)(g * cosR(ph)); im[j] = (float)(g * sinR(ph));
      }
      std::swap(c.yp, c.yn); std::swap(c.ao, c.an);   // c.yp = yn; c.yn = yp; c.ao = an; c.an = ao
    } else {
      c.fbOn = false;
      double thr = 0;
      if (mode == 2) { double mxm = 0; for (int k = 0; k < K; k++) if (sm[k] > mxm) mxm = sm[k]; thr = jmax(mxm * js_pow(10, (-70 + amt * 64) / 20), 1e-9); }
      F32& GG = c.gg;
      for (int k = 0; k < K; k++) {
        double m = sm[k], ph = sp[k];
        if (mode == 2) { double g = m >= thr ? 1 : (m / thr) * (m / thr) * (m / thr); const double h = GG[k] * .55; if (g < h) g = h; if (g < 1e-9) g = 0; GG[k] = g; m *= g; }
        else if (mode == 3) ph += amt * wrapPi(kPi * (k & 1) - ph);   // phase pi k: each frame a pulse at its centre
        else if (mode == 4) { xs(r); ph += amt * kTwoPi * ((double)r * kU32 - .5); }
        m *= T[k]; re[k] = (float)(m * cosR(ph)); im[k] = (float)(m * sinR(ph));
      }
      im[0] = 0; im[K - 1] = 0;
    }
    c.r = r;
    // resynthesis: the inverse, the window again, /1.5 (the overlapped squared Hann windows sum to 1.5)
    fft_->inverse(job_, re, im, x.get(), 0, 0, 1 / 1.5);
    // Frames whose phases no longer line up add as power, not amplitude: 3.1 dB quieter with this window and overlap
    const double inc = mode == 4 ? amt : 0, fzi = c.cap ? fz * (mode == 1 ? jmin(1, .3 + amt) : .6) : 0;
    double g = 1 + .434 * jmax(inc, fzi);
    if (mode == 3 && amt > 0) {
      // Robot puts each frame's energy into one pulse
      double e = 0, pk = 0, ei = 0;
      for (int i = 0; i < N; i++) { const double v = x[i] * (double)W[i], a = v < 0 ? -v : v; e += v * v; if (a > pk) pk = a; const double w = fin[i] * (double)W[i] * (double)W[i]; ei += w * w; }
      ei = (ei / 2.25 * (1 - fz) + fz * e) * 2.06;
      double ge = e > 1e-20 ? std::sqrt(ei / e) : 1; ge = 1 + (jmin(4, ge) - 1) * amt;
      const double ref = jmax(inPk, fz * c.fpk) * 2.5;
      g *= ge; if (pk * g > ref && pk > 0) g = ref / pk;
    }
    F32& acc = c.acc; bool bad = false;
    for (int i = 0; i < N; i++) { const double v = acc[i] + x[i] * (double)W[i] * g; acc[i] = v; }
    for (int i = 0; i < H; i++) { const double v = acc[i]; if (!(v > -64 && v < 64)) { bad = true; break; } }
    if (bad) { reset(c); return; }   // something went wrong: start this channel afresh
    for (int i = 0; i < H; i++) c.oq[i] = (double)acc[i];
    for (int i = 0; i < N - H; i++) acc[i] = (double)acc[i + H];   // acc.copyWithin(0, H) (forward, as the source is ahead of the target)
    for (int i = N - H; i < N; i++) acc[i] = 0;                   // acc.fill(0, N - H)
    for (int i = 0; i < N - H; i++) fin[i] = (double)fin[i + H];   // fin.copyWithin(0, H)
  }
  SpxCh c0_, c1_; int cl_ = 0, cr_; double fz_ = 0, mix_ = -1, toneV_ = kNaN; F32 tone_; const FFTR* fft_; const float* W_; FFTR::Job job_;
};

// ---- Extreme stretch -----------------------------------------------------------------------------------------------------------------------------------------
const int kXstSizes[5] = {2048, 4096, 8192, 16384, 32768};
const ParamDef kXstP[] = {{"amt", .5, 0, 1, .01}, {"win", 2, 0, 4, 1}, {"freeze", 0, 0, 1, 1}, {"tone", 0, -1, 1, .01}, {"mix", .5, 0, 1, .01}};

class XStretch : public Module {
 public:
  XStretch(double sr, Seeds&) : sr_(sr), NM_(kXstSizes[4]), K_(NM_ / 2 + 1), cap_((int)js_round(sr * 12)), J_(NM_ / 16), job_(NM_) {
    for (int n : kXstSizes) { FFTR::hann(n); FFTR::get(n); }   // made now, not on the audio thread later
    for (int i = 0; i < 5; i++) { fft_[i] = FFTR::get(kXstSizes[i]); hw_[i] = FFTR::hann(kXstSizes[i]); }
    for (Ch& c : c_) { c.ring.init(cap_); c.hold.init(NM_ + 2 * J_); c.acc.init(NM_ + NM_ / 4 + 8); }
    ln_.init(K_); for (int k = 0; k < K_; k++) ln_[k] = js_log(jmax(k, 1));
    x_.init(NM_); re_.init(K_); im_.init(K_); pw_.init(K_); tilt_.init(K_);
    H_ = kXstSizes[2] / 4; fr_.N = kXstSizes[2];
  }
  void process(const IO& io, const Ctx& ctx) override {
    const double* p = io.p; const In Lin = inp(io, 0), R = inR(io, 0, 1); const Out oL = outp(io, 0), oR = outp(io, 1); const int B = ctx.block, cap = cap_;
    Ch& c0 = c_[0]; Ch& c1 = c_[1]; F32& r0 = c0.ring; F32& r1 = c1.ring; F32& a0 = c0.acc; F32& a1 = c1.acc; const double mx = u01(p[4]);
    if (restart_(ctx) && wpos_ > 0) jumpReq_ = true;   // the song started again: on to the newest sound
    if (mix_ < 0) mix_ = mx;
    int done = 0;
    while (done < B) {
      // this hop's share of the frame's work: it must be finished when the hop ends
      const int left = H_ - cnt_, n = left < B - done ? left : B - done; const double want = std::ceil((double)(nu_ - u_) / jmax(1, std::ceil((double)left / B)));
      for (int k = 0; k < want && u_ < nu_; k++) unit();
      int w = (int)(wpos_ % cap); double m = mix_;
      for (int i = done, j = cnt_; i < done + n; i++, j++) {
        // a NaN, an infinity or an absurd sample (|x| >= 8) in is a 0, on the dry path too
        const double l0 = Lin[i], q0 = R[i], l = l0 > -8 && l0 < 8 ? l0 : 0, r = q0 > -8 && q0 < 8 ? q0 : 0;
        r0[w] = l; r1[w] = r; if (++w == cap) w = 0;
        m += (mx - m) * .002;   // Mix glides, so it never clicks
        oL[i] = l + (fxSafe(a0[j]) - l) * m; oR[i] = r + (fxSafe(a1[j]) - r) * m;
      }
      mix_ = m; wpos_ += n; cnt_ += n; done += n;
      if (cnt_ >= H_) { while (u_ < nu_) unit(); hop(p); }
    }
  }
 private:
  struct Ch { F32 ring, hold, acc; };
  struct Fr { int N = 0, P = 0, ns = 1; double tu = 0; bool two = false; double wa = 0; int sa = 0; double a = 0; int sb = 0; double b = 0, tone = 0; };
  double pos(int s, int N) {   // where a source reads; the frozen read point wanders +-N/16, so the drone moves
    if (s != 1) return s == 2 ? oldCen_ : cen_;
    uint32_t r = r_; xs(r); r_ = r;
    return hc_ + ((double)r * kU32 * 2 - 1) * N / 16;
  }
  // End of a hop: the frame made during it is in the accumulators; move them on, move the read head, plan the next frame.
  void hop(const double* p) {
    const double sr = sr_; const int H = H_;
    for (Ch& c : c_) { F32& a = c.acc; const int len = a.n; for (int i = 0; i < len - H; i++) a[i] = (double)a[i + H]; for (int i = len - H; i < len; i++) a[i] = 0; }
    cnt_ = 0;
    const int wi = (int)clamp((double)i32(p[1]), 0, 4); const int N = kXstSizes[wi], Hn = N / 4; const double cap = cap_; const int NM = NM_;
    const double stretch = 1.5 * js_pow(50 / 1.5, u01(p[0])); const bool frz = p[2] >= .5; const double adv = H / stretch;
    const double xfF = clamp(js_round(.4 * sr / Hn), 2, 40), xfJ = clamp(js_round(sr / Hn), 2, 100);
    // how old the head may get: the room a 1 s crossfade and the largest window need, inside the 12 s
    const double oldest = (double)wpos_ - cap + js_round(1.2 * sr) + NM + 2 * J_, newest = (double)wpos_ - N / 2;
    // Reading silence: wait at the newest sound. When sound comes, read at normal speed until the head reaches where it came in, then slow down.
    Fr& f0 = fr_; const bool fre = !frozen_ && srcB_ == 0 && xf_ >= xfn_;
    if (fre) {
      if (fe_[0] + fe_[1] < 7.5e-10 * f0.N) quiet_ = true;
      else if (quiet_) { quiet_ = false; catchTo_ = f0.b + f0.N; }
    }
    if (!frozen_) cen_ = fre && quiet_ ? newest : jmin(cen_ + (cen_ < catchTo_ ? H : adv), newest);   // frozen, it stays put
    if (srcA_ == 2 && xf_ < xfn_) oldCen_ += adv;
    if (frz && !frozen_) {
      // catch the moment: copy the largest window (and room to jitter) around the read head
      frozen_ = true; hc_ = cen_;
      const double h0 = js_round(hc_ - NM / 2.0 - J_);
      for (Ch& c : c_) { F32& hd = c.hold; F32& rg = c.ring; for (int i = 0; i < hd.n; i++) { const double q = h0 + i; hd[i] = q >= 0 && q < (double)wpos_ && q >= (double)wpos_ - cap ? (double)rg[(int)jmod(q, cap)] : 0.0; } }
      srcA_ = srcB_ == 2 ? 0 : srcB_; srcB_ = 1; xf_ = 0; xfn_ = xfF;
    } else if (!frz && frozen_) {
      frozen_ = false;
      if (jumpReq_ || cen_ - N / 2 < oldest) { cen_ = newest; jumpReq_ = false; catchTo_ = -inf(); }
      srcA_ = 1; srcB_ = 0; xf_ = 0; xfn_ = xfF;
    } else if (!frz && xf_ >= xfn_ && (jumpReq_ || cen_ - N / 2 < oldest)) {
      // a jump to the newest sound: the old head keeps reading (fading out) while the new one fades in
      oldCen_ = cen_; cen_ = newest; jumpReq_ = false; catchTo_ = -inf(); srcA_ = 2; srcB_ = 0; xf_ = 0; xfn_ = xfJ;
    }
    Fr& f = fr_; const bool two = xf_ < xfn_ && srcA_ != srcB_;
    f.N = N; f.P = fft_[wi]->passes; f.two = two; f.ns = two ? 2 : 1;
    f.wa = two ? 1 - (xf_ + 1) / (xfn_ + 1) : 0; if (xf_ < xfn_) xf_++;
    f.tone = clamp(p[3], -1, 1); f.tu = !(std::fabs(f.tone - tiltV_) <= .004) || N != tiltN_ ? 1 : 0;
    f.sa = srcA_; f.a = two ? js_round(pos(srcA_, N) - N / 2) : 0;
    f.sb = srcB_; f.b = js_round(pos(srcB_, N) - N / 2);
    H_ = Hn; u_ = 0; nu_ = (int)f.tu + 2 * ((f.ns + 1) * (f.P + 2));
  }
  // One window of a source into x, Hann-windowed (zeros where nothing was recorded); returns its energy.
  double grab(Ch& c, int s, double at, int N, int wi) {
    F32& x = x_; const float* w = hw_[wi]; const double cap = cap_; double e = 0;
    if (s == 1) {
      F32& h = c.hold; const double o = at - js_round(hc_ - NM_ / 2.0 - J_); const int hl = h.n;
      for (int i = 0; i < N; i++) { const double q = o + i; x[i] = q >= 0 && q < hl ? h[(int)q] * (double)w[i] : 0.0; }
    } else {
      F32& rg = c.ring; const double lo = jmax(0, (double)wpos_ - cap), hi = (double)wpos_;
      double q = at; const double kd = jmod(jmod(at, cap) + cap, cap); int k = kd == kd ? (int)kd : 0;   // (a NaN `at`: q is NaN, no sample is read: the JS reads none either)
      for (int i = 0; i < N; i++, q++) { const double v = q >= lo && q < hi ? rg[k] * (double)w[i] : 0.0; x[i] = v; e += v * v; if (++k == cap_) k = 0; }
    }
    return e;
  }
  // One unit of the frame's work. In order: [the Tone table], then for each channel: each source (grab and start, the passes, the magnitudes), then the resynthesis.
  void unit() {
    Fr& f = fr_; const int N = f.N, K = N / 2 + 1; int wi = 0; while (kXstSizes[wi] != N) wi++;
    const FFTR* fft = fft_[wi]; const int P = f.P; float* re = re_.get(); float* im = im_.get(); F32& pw = pw_;
    int v = u_++;
    if (f.tu != 0) {
      if (v == 0) {
        // Tone: +-4.5 dB an octave about 1 kHz (+12 / -24 dB at most), from a table of ln k
        F32& T = tilt_; const double a = f.tone * .75, cb = js_log(sr_ / N / 1000), l30 = js_log(.03);
        for (int k = 0; k < K; k++) { double e = ln_[k] + cb; if (e < l30) e = l30; e *= a; T[k] = e > 1.386 ? 4 : e < -2.765 ? .063 : js_exp(e); }
        tiltV_ = f.tone; tiltN_ = N; return;
      }
      v--;
    }
    const int per = (f.ns + 1) * (P + 2), ci = v >= per ? 1 : 0; Ch& ch = c_[ci];
    v -= ci * per;
    if (v < f.ns * (P + 2)) {
      const int si = v >= P + 2 ? 1 : 0, w = v - si * (P + 2); const bool first = f.two && si == 0;
      if (w == 0) { const double e = grab(ch, first ? f.sa : f.sb, first ? f.a : f.b, N, wi); fe_[ci] = first ? 1 : e; fft->fwdStart(job_, x_.get()); }
      else if (w <= P) fft->pass(job_);
      else {
        fft->fwdEnd(job_, re, im);
        if (!f.two) for (int k = 0; k < K; k++) pw[k] = (double)re[k] * re[k] + (double)im[k] * im[k];
        else if (first) { const double g = f.wa; for (int k = 0; k < K; k++) pw[k] = ((double)re[k] * re[k] + (double)im[k] * im[k]) * g; }
        else { const double g = 1 - f.wa; for (int k = 0; k < K; k++) pw[k] += ((double)re[k] * re[k] + (double)im[k] * im[k]) * g; }
      }
      return;
    }
    const int w = v - f.ns * (P + 2);
    if (w == 0) {
      // keep each bin's loudness, give it a random phase
      F32& T = tilt_; uint32_t r = r_;
      for (int k = 1; k < K - 1; k++) {
        double m = std::sqrt((double)pw[k]) * T[k]; if (m < 1e-20) m = 0;
        xs(r); const double ph = (double)r * kU32;
        re[k] = (float)(m * sinT(ph + .25)); im[k] = (float)(m * sinT(ph));
      }
      xs(r); r_ = r;
      re[0] = 0; im[0] = 0; re[K - 1] = 0; im[K - 1] = 0;   // no DC, no Nyquist
      fft->invStart(job_, re, im);
    } else if (w <= P) fft->pass(job_);
    else {
      // random phases scatter each frame's energy, so overlapped frames add as power: x4/3 keeps the level
      fft->invEnd(job_, x_.get(), 4.0 / 3);
      F32& x = x_; const float* hw = hw_[wi]; F32& a = ch.acc; const int o = H_;
      for (int i = 0; i < N; i++) a[o + i] += x[i] * (double)hw[i];
    }
  }
  static double inf() { return std::numeric_limits<double>::infinity(); }
  double sr_; int NM_, K_, cap_, J_; Ch c_[2]; int cnt_ = 0, H_ = 0; int64_t wpos_ = 0; double cen_ = 0, oldCen_ = 0, hc_ = 0; int srcA_ = 0, srcB_ = 0; double xf_ = 0, xfn_ = 0;
  bool frozen_ = false, jumpReq_ = false; F32 x_, re_, im_, pw_, tilt_, ln_; double tiltN_ = 0, tiltV_ = kNaN; FFTR::Job job_; int u_ = 0, nu_ = 0; double fe_[2] = {0, 0};
  bool quiet_ = true; double catchTo_ = -std::numeric_limits<double>::infinity(); Fr fr_; uint32_t r_ = 0x6d2b79f5u; Restart restart_; double mix_ = -1;
  const FFTR* fft_[5]; const float* hw_[5];
};

template <class M> std::unique_ptr<Module> mk(double sr, Seeds& s) { return std::unique_ptr<Module>(new M(sr, s)); }
}  // namespace

void addSpectralMods(std::vector<const Def*>& v) {
  static const Def kD[] = {{"spectral", PD(kStIn), PD(kStOut), PD(kSpxP), mk<Spectral>}, {"stretch", PD(kStIn), PD(kStOut), PD(kXstP), mk<XStretch>}};
  for (const Def& d : kD) v.push_back(&d);
}

}  // namespace modfx
}  // namespace thunder
