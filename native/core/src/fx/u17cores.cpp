// See u17cores.h. Each function keeps the JS's statements and arithmetic order (index.html: DegradeCore, ResonCore, PitchFxCore, rvMix, applyFx).
#include "fx/u17cores.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "rt/jsmath.h"

namespace thunder {
namespace {

const double kNaN = std::numeric_limits<double>::quiet_NaN();
const double kPi = 3.141592653589793;   // Math.PI

// Math.max / Math.min: NaN if either is NaN, +0 over -0 for max and -0 over +0 for min
inline double js_max(double a, double b) { if (a != a || b != b) return kNaN; if (a == 0 && b == 0) return std::signbit(a) ? b : a; return a > b ? a : b; }
inline double js_min(double a, double b) { if (a != a || b != b) return kNaN; if (a == 0 && b == 0) return std::signbit(a) ? a : b; return a < b ? a : b; }
inline int32_t i32(double x) { return js_to_int32(x); }   // `x | 0`
// A Float32Array read: an index outside the array (or NaN) is undefined in the JS, and every sum with it NaN.
inline double rdf(const float* b, int N, double i) { return i >= 0 && i < N ? (double)b[(int)i] : kNaN; }
// ...and a write there is dropped
inline void wrf(float* b, int N, double i, double v) { if (i >= 0 && i < N) b[(int)i] = (float)v; }

// rvMix(m, o): [cos(a), sin(a)^2], a = clamp(m) * PI / 2 (a NaN stays NaN)
inline void rvMix(double m, double* o) { const double a = (m < 0 ? 0 : m > 1 ? 1 : m) * kPi / 2, s = js_sin(a); o[0] = js_cos(a); o[1] = s * s; }

const int kChordN[12] = {1, 3, 4, 5, 5, 5, 5, 5, 5, 5, 5, 5};
const int kChords[12][5] = {{0}, {0, 12, 24}, {0, 7, 12, 19}, {0, 4, 7, 12, 16}, {0, 3, 7, 12, 15}, {0, 5, 7, 12, 17}, {0, 4, 7, 11, 14}, {0, 3, 7, 10, 14}, {0, 4, 7, 10, 12}, {0, 5, 10, 15, 20}, {0, 12, 19, 24, 28}, {0, 1, 2, 3, 4}};
const int kArp[6] = {0, 4, 7, 12, 7, 4};   // PitchFxCore.ARP
const double kCbn[4] = {1116, 1188, 1277, 1356};
const double kApn[2] = {556, 441};

}  // namespace

// ---- Degrade ---------------------------------------------------------------------------------------------------------------------------
void DegradeCore::process(float* L, float* R, int n, const DgDp& p) {
  const double sr = sr_, rate = js_min(p.rate, sr); const bool hold = rate < sr * 0.98; const double inc = rate / sr;
  const double bits = js_max(1, js_min(24, p.bits)); const bool crush = bits < 23.5; const double q = js_pow(2, bits - 1);
  const double nz = js_max(0, js_min(1, p.noise)), lv = nz * nz * 0.25;   // DegradeCore.level
  const double col = js_max(0, js_min(1, p.color)), k = 1 - col * 0.96, comp = std::sqrt((2 - k) / k) / 0.577;
  const double fol = js_max(0, js_min(1, p.follow)), ka = 1 - js_exp(-1 / (sr * 0.004)), kr = 1 - js_exp(-1 / (sr * 0.15)), dry = p.dry, wet = p.wet;
  for (int i = 0; i < n; i++) {
    const double xl = L[i], xr = R[i];
    const double a = js_max(std::fabs(xl), std::fabs(xr)); env_ += (a - env_) * (a > env_ ? ka : kr);
    if (hold) { ph_ += inc; if (ph_ >= 1) { ph_ -= std::floor(ph_); hl_ = xl; hr_ = xr; } } else { hl_ = xl; hr_ = xr; }
    double yl = hl_, yr = hr_;
    if (crush) { yl = js_round(yl * q) / q; yr = js_round(yr * q) / q; }
    if (lv > 0) {
      uint32_t s = s_; s ^= s << 13; s ^= s >> 17; s ^= s << 5; const double w1 = (double)s / 2147483648.0 - 1; s ^= s << 13; s ^= s >> 17; s ^= s << 5; const double w2 = (double)s / 2147483648.0 - 1; s_ = s;
      nl_ += (w1 - nl_) * k; nr_ += (w2 - nr_) * k;
      const double g = lv * comp * ((1 - fol) + fol * js_min(1, env_ * 6));
      yl += nl_ * g; yr += nr_ * g;
    }
    L[i] = (float)(xl * dry + yl * wet); R[i] = (float)(xr * dry + yr * wet);
  }
}

// ---- Resonator -------------------------------------------------------------------------------------------------------------------------
ResonCore::ResonCore(double sr) : sr_(sr) {
  for (int i = 0; i < 5; i++) { v_[i].N = (int)std::ceil(sr / 20) + 4; v_[i].b.reset(new float[(size_t)v_[i].N]()); }
}

void ResonCore::process(float* L, float* R, int n, const RsDp& p) {
  const double sr = sr_; const int ci = (int)js_max(0, js_min(11, (double)i32(p.chord))), nv = kChordN[ci]; const int* C = kChords[ci]; const bool tube = p.mode >= 0.5;
  const double t60 = 0.05 * js_pow(160, js_max(0, js_min(1, p.decay))), a = 0.08 + 0.9 * js_max(0, js_min(1, p.bright)), mix = p.mix, sp = p.spread;
  const double ing = 0.5 / std::sqrt((double)nv);
  for (int k = 0; k < nv; k++) {
    Voice& V = v_[k]; const double f = 440 * js_pow(2, (p.note + C[k] - 69) / 12);
    const double per = sr / f * (tube ? 0.5 : 1);
    V.d = js_max(2, js_min((double)V.N - 3, per - (1 - a) / a));
    V.g = js_pow(10, -3 * (per / sr) / t60) * (tube ? -1 : 1);
    const double pan = nv > 1 ? ((double)k / (nv - 1) - 0.5) * sp : 0; V.gl = js_cos((pan + 0.5) * kPi / 2); V.gr = js_sin((pan + 0.5) * kPi / 2);
  }
  for (int i = 0; i < n; i++) {
    const double x = ((double)L[i] + (double)R[i]) * 0.5 * ing; double yl = 0, yr = 0;
    for (int k = 0; k < nv; k++) {
      Voice& V = v_[k]; float* B = V.b.get(); const int N = V.N; const double r = V.w - V.d, ri = std::floor(r), fr = r - ri;
      const double i0 = ri < 0 ? ri + N : ri, i1 = i0 + 1 >= N ? 0 : i0 + 1, d = rdf(B, N, i0) + (rdf(B, N, i1) - rdf(B, N, i0)) * fr;
      V.lp += (d - V.lp) * a;
      double y = x + V.lp * V.g;
      y = y > 1.5 ? 1.5 : y < -1.5 ? -1.5 : y;
      B[V.w] = (float)y; V.w = V.w + 1 == N ? 0 : V.w + 1;
      const double o = V.lp - V.dx + 0.995 * V.dc; V.dx = V.lp; V.dc = o;
      yl += o * V.gl; yr += o * V.gr;
    }
    L[i] = (float)((double)L[i] * (1 - mix) + js_tanh(yl) * mix); R[i] = (float)((double)R[i] * (1 - mix) + js_tanh(yr) * mix);
  }
}

// ---- Pitch delay / Pitch reverb --------------------------------------------------------------------------------------------------------
PitchFxCore::PitchFxCore(double sr) : sr_(sr), D_((int)std::ceil(sr * 2.2)) {
  for (int c = 0; c < 2; c++) dl_[c].reset(new float[(size_t)D_]());
  for (int i = 0; i < 4; i++) sh_[i].b.reset(new float[8192]());
  for (int c = 0; c < 2; c++) {
    for (int k = 0; k < 4; k++) { Comb& m = cb_[c][k]; m.N = (int)std::ceil(kCbn[k] * 2.2 * sr / 44100) + 8; m.b.reset(new float[(size_t)m.N]()); }
    for (int k = 0; k < 2; k++) { Allp& m = ap_[c][k]; m.N = (int)std::ceil(kApn[k] * sr / 44100) + 8; m.b.reset(new float[(size_t)m.N]()); }
  }
}

double PitchFxCore::shift(Shifter& s, double x, double ratio, double win) {
  float* b = s.b.get(); const int N = 8192; b[s.w] = (float)x;
  const double W = js_max(64, js_min(N - 4, win)); s.ph += (1 - ratio) / W; if (s.ph >= 1) s.ph -= 1; if (s.ph < 0) s.ph += 1;
  double y = 0;
  for (int h = 0; h < 2; h++) {
    const double ph = std::fmod(s.ph + h * 0.5, 1), d = ph * W, rp = s.w - d - 1, i = std::floor(rp), f = rp - i;
    const double a = b[i32(i + N) & (N - 1)], c = b[i32(i + 1 + N) & (N - 1)]; y += (a + (c - a) * f) * js_sin(kPi * ph);
  }
  s.w = (s.w + 1) & (N - 1); return y * 0.75;
}

double PitchFxCore::verb(int ch, double x, double fb, double damp, double size) {
  double y = 0;
  for (int k = 0; k < 4; k++) {
    Comb& c = cb_[ch][k]; const double L = js_max(16, js_min((double)c.N, std::floor((kCbn[k] + ch * 23) * (0.45 + size * 1.7) * sr_ / 44100)));
    if (c.i >= L) c.i = 0; const double o = rdf(c.b.get(), c.N, c.i); c.f = o * (1 - damp) + c.f * damp; wrf(c.b.get(), c.N, c.i, x + c.f * fb); c.i++; y += o;
  }
  for (int q = 0; q < 2; q++) {
    Allp& a = ap_[ch][q]; const int L = a.N - 8;
    if (a.i >= L) a.i = 0; const double o = rdf(a.b.get(), a.N, a.i); wrf(a.b.get(), a.N, a.i, y + o * 0.5); a.i++; y = o - y * 0.5;
  }
  return y * 0.25;
}

void PitchFxCore::process(float* Lb, float* Rb, int n, const PfDp& p) {
  const double sr = sr_, mix = p.mix; double rv[2]; rvMix(mix, rv);
  const bool isRev = i32(p.kind) != 0; const double dry = isRev ? rv[0] : 1 - mix * 0.5, wgm = rv[1], r1 = js_pow(2, p.pitch / 12), r2 = js_pow(2, p.pitch2 / 12), win = sr * 0.06;
  const double tc = 1 - js_exp(-2 * kPi * (500 + p.tone * p.tone * 12000) / sr); const int mode = i32(p.mode); const int D = D_;
  if (isRev) {
    const double fb = 0.7 + p.size * 0.24, damp = 0.15 + (1 - p.tone) * 0.6, sfb = js_min(0.9, p.fb) * 0.38;
    for (int i = 0; i < n; i++) {
      for (int ch = 0; ch < 2; ch++) {
        const double x0 = ch ? Rb[i] : Lb[i]; double x = x0;
        if (mode == 1) x = shift(sh_[ch], x, r1, win);
        const double w = verb(ch, x + fbS_[ch], fb, damp, p.size) * (1 - fb) * 1.2;
        double f = 0;
        if (mode == 0) f = shift(sh_[ch], w, r1, win) * sfb;
        else if (mode == 2) { const double s1 = shift(sh_[ch], w, r1, win); const double s2 = shift(sh_[ch + 2], w, r2, win); f = (s1 + s2) * 0.5 * sfb; }
        lp_[ch] += (f - lp_[ch]) * tc;
        const double a = std::fabs(lp_[ch]); fenv_[ch] += (a - fenv_[ch]) * (a > fenv_[ch] ? 0.002 : 0.00005);
        fbS_[ch] = js_tanh(lp_[ch] * (fenv_[ch] > 0.35 ? 0.35 / fenv_[ch] : 1));
        const float o = (float)(x0 * dry + w * wgm * 1.8); if (ch) Rb[i] = o; else Lb[i] = o;
      }
    }
    return;
  }
  const double tBase = js_max(10, js_min(2000, p.time)) / 1000 * sr, fb = js_min(0.95, p.fb);
  for (int i = 0; i < n; i++) {
    wph_ += (0.3 + p.wow * 2.2) / sr; if (wph_ >= 1) wph_ -= 1;
    const double tgt = mode == 3 ? tBase * (1 + p.wow * 0.012 * js_sin(2 * kPi * wph_) + p.wow * 0.006 * js_sin(2 * kPi * wph_ * 2.7)) : tBase;
    tsm_ += (tgt - tsm_) * (mode == 3 ? 0.00006 + p.wow * 0.0002 : 0.002); if (tsm_ < 1) tsm_ = tgt;
    cnt_++; if (cnt_ >= tBase) { cnt_ = 0; step_ = (step_ + 1) % 6; }
    const double ra = mode == 2 ? js_pow(2, (kArp[step_] + p.pitch - 12) / 12) : r1;
    for (int ch = 0; ch < 2; ch++) {
      float* buf = dl_[ch].get(); const double x = ch ? Rb[i] : Lb[i];
      double rp = dw_ - (mode == 4 ? std::fmod(rev_ * 2, tBase) : tsm_) - (ch ? sr * 0.0007 : 0);
      while (rp < 0) rp += D; const double j = std::floor(rp), f = rp - j, a = rdf(buf, D, std::fmod(j, D)), b = rdf(buf, D, std::fmod(j + 1, D));
      double y = a + (b - a) * f;
      if (mode == 4 && p.pitch != 0) y = shift(sh_[ch], y, r1, win);
      double loop = y;
      if (mode == 0 || mode == 2) loop = shift(sh_[ch], y, ra, win);
      lp_[ch] += (loop - lp_[ch]) * tc;
      buf[dw_] = (float)(x + lp_[ch] * fb);
      const double wet = mode == 1 ? shift(sh_[ch], y, r1, win) : mode == 4 ? y * (0.5 - 0.5 * js_cos(2 * kPi * (std::fmod(rev_, tBase) / tBase))) : y;
      const float o = (float)(x * dry + wet * mix * 1.4); if (ch) Rb[i] = o; else Lb[i] = o;
    }
    dw_ = (dw_ + 1) % D; rev_++;
  }
}

// ---- the pads' use (applyFx) -----------------------------------------------------------------------------------------------------------
std::vector<float> pad_degrade(const PadDegrade& f, const float* a, size_t n, double sr) {
  std::vector<float> L(a, a + n), R(a, a + n); DegradeCore d(sr); DgDp pp; pp.bits = f.bits; pp.rate = f.rate; pp.noise = f.noise; pp.color = f.color; pp.follow = f.follow; pp.dry = f.dry; pp.wet = f.wet;
  for (size_t o = 0; o < n; o += 128) { const int m = (int)std::min<size_t>(128, n - o); d.process(L.data() + o, R.data() + o, m, pp); }
  return L;
}
std::vector<float> pad_reson(const PadReson& f, const float* a, size_t n, double sr) {
  std::vector<float> L(a, a + n), R(a, a + n); ResonCore d(sr); RsDp pp; pp.note = f.note; pp.chord = f.chord; pp.decay = f.decay; pp.bright = f.bright; pp.mode = f.mode; pp.spread = f.spread; pp.mix = f.mix;
  for (size_t o = 0; o < n; o += 128) { const int m = (int)std::min<size_t>(128, n - o); d.process(L.data() + o, R.data() + o, m, pp); }
  return L;
}
std::vector<float> pad_pitchfx(bool rv, const PadPitchFx& f, const float* a, size_t n, double sr) {
  const double tailD = js_round(sr * (rv ? 3 + (f.sizeNull ? 0.0 : f.size) * 4 : js_min(8, f.time / 1000 * (1 + 6 * f.fb))));
  if (!(tailD >= 0) || tailD > 1e9) return {};   // (NaN or absurd: a RangeError in the JS)
  const size_t N = n + (size_t)tailD;
  std::vector<float> L(N, 0.f), R(N, 0.f); std::copy(a, a + n, L.begin()); std::copy(a, a + n, R.begin()); PitchFxCore d(sr);
  PfDp pp; pp.kind = rv ? 1 : 0; pp.mode = f.mode; pp.time = f.time; pp.fb = f.fb; pp.pitch = f.pitch; pp.pitch2 = f.pitch2 == f.pitch2 ? f.pitch2 : -12; pp.tone = f.tone;
  pp.size = f.size == f.size ? f.size : 0.6; pp.wow = f.wow == f.wow ? f.wow : 0.2; pp.mix = f.mix;
  for (size_t o = 0; o < N; o += 128) { const int m = (int)std::min<size_t>(128, N - o); d.process(L.data() + o, R.data() + o, m, pp); }
  for (size_t i = 0; i < N; i++) L[i] = (float)(((double)L[i] + (double)R[i]) * 0.5);
  return L;
}

}  // namespace thunder
