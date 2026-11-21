// U26: BassCore (index.html "bass engine") and renderBass, line for line. See bass_core.h.
#include "engine/bass_core.h"

#include <cmath>
#include <cstring>

#include "engine/xeng_util.h"

namespace thunder {
using namespace xe;

namespace {
// a Float32Array element: a read outside it (or at a fractional index) is undefined (NaN in the arithmetic), a write there does nothing
inline double rd(const float* A, size_t len, double i) { return (i >= 0 && i < (double)len && i == std::floor(i)) ? (double)A[(size_t)i] : (double)NAN; }
inline void wr(float* A, size_t len, double i, double y) { if (i >= 0 && i < (double)len && i == std::floor(i)) A[(size_t)i] = (float)y; }
constexpr double kDiv[13] = {16, 8, 4, 2, 4.0 / 3, 1, 2.0 / 3, 0.75, 0.5, 1.0 / 3, 0.25, 1.0 / 6, 0.125};   // this.div
constexpr double kVow[5][2] = {{730, 1090}, {530, 1840}, {270, 2290}, {570, 840}, {300, 870}};            // this.vow: a e i o u
inline double tab(const float* T, int64_t j) { return (j >= 0 && j < 4097) ? (double)T[j] : (double)NAN; }   // T[j] (a read past the table is undefined)
}  // namespace

BassCore::BassCore(double sr) : sr_(sr) {
  for (int i = 0; i <= 4096; i++) tab_[i] = (float)js_sin(2 * kPi * i / 4096);
  std::memset(base_, 0, sizeof base_); std::memset(panL_, 0, sizeof panL_); std::memset(panR_, 0, sizeof panR_); std::memset(aA_, 0, sizeof aA_); std::memset(aT_, 0, sizeof aT_);
  std::memset(phA_, 0, sizeof phA_); std::memset(phB_, 0, sizeof phB_);
  for (int k = 1; k <= kN; k++) phB_[k] = std::fmod(k * 0.618034, 1.0);
  set(BassP());
}

void BassCore::set(const BassP& o) {
  BassP p = o;
  p.parts = mx(1, mn(kN, js_round(p.parts)));
  p_ = p;
  // the source spectrum: saw (1/k) -> square (odd 1/k) -> triangle (odd 1/k^2, alternating sign)
  const double s = mx(0, mn(1, p.shape)); double e2 = 0;
  for (int k = 1; k <= kN; k++) {
    double a = 0;
    if (k <= p.parts) {   // (a NaN count is false: no partial)
      const int odd = k & 1;
      if (s <= 0.5) a = odd ? 1.0 / k : (1 - 2 * s) / k;
      else { const double t = (s - 0.5) * 2; a = odd ? ((1 - t) / k + t / ((double)k * k)) * (t > 0.5 && (((k - 1) / 2) & 1) ? -1 : 1) : 0; }
      a *= js_pow(k, p.tilt);
      if (p.oddeven > 0 && odd && k > 1) a *= 1 - p.oddeven; else if (p.oddeven < 0 && !odd) a *= 1 + p.oddeven;
    }
    base_[k] = (float)a; e2 += a * a;
    // the low partials stay in the middle, the upper ones alternate left and right
    const double pan = (k & 1 ? -1 : 1) * p.width * mn(1, (k - 1) / 4.0);
    panL_[k] = (float)(std::sqrt((1 - pan) / 2) * 1.4142); panR_[k] = (float)(std::sqrt((1 + pan) / 2) * 1.4142);
  }
  norm_ = 0.34 / mx(0.25, std::sqrt(e2)) / (p.det > 0.5 ? 1.35 : 1);
}

void BassCore::on(bool leg) {
  const BassP& p = p_;
  const bool legato = (leg || truthy(p.legato)) && held_ && stage_ != 0 && stage_ < 4;
  held_ = true;
  if (!legato) {
    if (env_ < 0.002) { std::memset(phA_, 0, sizeof phA_); subPh_ = 0; }
    stage_ = 1; fe_ = 1; wt_ = 0;   // the wobble's delay counts from each new note (not a legato one)
    if (truthy(p.wRetrig)) { lph_ = p.wPhase; lsm_ = shape_(p.wPhase); sh_ = rand_(); }
  }
  go_ = true;   // the next block sets the pitch: glide from the last note, or jump to it
}
void BassCore::reset() {   // = a fresh core with set(p()): what the constructor does, then set()
  std::memset(aA_, 0, sizeof aA_); std::memset(aT_, 0, sizeof aT_); std::memset(phA_, 0, sizeof phA_); std::memset(phB_, 0, sizeof phB_);
  for (int k = 1; k <= kN; k++) phB_[k] = std::fmod(k * 0.618034, 1.0);
  stage_ = 0; env_ = 0; fe_ = 0; lph_ = 0; lu_ = 0; lsm_ = 0; sh_ = 0.5; rnd_ = 22222; f_ = 0; lf_ = 0; subPh_ = 0; norm_ = 0.3; wt_ = 0;
  held_ = false; go_ = false; bad_ = false; bpm = 120; wob = 0;
  const BassP keep = p_; set(keep);
}
void BassCore::off() { held_ = false; if (stage_ != 0 && stage_ < 4) stage_ = 4; }
double BassCore::rand_() { rnd_ = std::fmod(rnd_ * 16807, 2147483647.0); return rnd_ / 2147483647.0; }   // (below 2^45: exact in doubles)
double BassCore::shape_(double ph) const {
  switch (i32(p_.wShape)) {
    case 1: return ph < 0.5 ? ph * 2 : 2 - ph * 2;
    case 2: return 1 - ph;
    case 3: return ph;
    case 4: return ph < 0.5 ? 1 : 0;
    case 5: return sh_;
    default: return 0.5 - 0.5 * js_cos(2 * kPi * ph);
  }
}

void BassCore::render(float* L, float* R, size_t len, double a, double b, double rate) {
  for (double i = a; i < b && !bad_; i += kBlk) block(L, R, len, i, mn(b, i + kBlk), rate);
}

void BassCore::block(float* L, float* R, size_t len, double a, double b, double rate) {
  const double n = b - a; const BassP& p = p_; const double sr = sr_;
  if (stage_ == 0) { for (double i = a; i < b; i++) { wr(L, len, i, 0); wr(R, len, i, 0); } return; }
  const double dt = n / sr;
  // pitch (with glide)
  const double target = 65.406 * js_pow(2, p.oct + p.tune / 12) * rate, lt = js_log2(mx(1, target));
  if (go_) { go_ = false; if (!(p.glide > 0.5) || lf_ <= 0) lf_ = lt; }
  if (p.glide > 0.5) lf_ += (lt - lf_) * (1 - js_exp(-dt * 1000 / p.glide)); else lf_ = lt;
  const double f = js_pow(2, lf_); f_ = f;
  // amp envelope over this block (linear inside it)
  const double e0 = env_; double e1 = e0;
  if (stage_ == 1) { e1 = e0 + dt * 1000 / mx(0.5, p.atk); if (e1 >= 1) { e1 = 1; stage_ = 2; } }
  else if (stage_ == 2 || stage_ == 3) { e1 = p.sus + (e0 - p.sus) * js_exp(-dt * 1000 * 4.6 / mx(1, p.dec)); stage_ = jabs(e1 - p.sus) < 1e-4 ? 3 : 2; }
  else if (stage_ == 4) { e1 = e0 * js_exp(-dt * 1000 * 4.6 / mx(1, p.rel)); if (e1 < 1e-5) { e1 = 0; stage_ = 0; } }
  env_ = e1;
  fe_ *= js_exp(-dt * 1000 * 4.6 / mx(1, p.fdec));
  // wobble
  const double beats = wob > 0 ? wob : kDiv[cidx(mx(0, mn(12, (double)i32(p.wRate))), 0, 12)];
  lph_ += dt * bpm / 60 / beats;
  if (lph_ >= 1) { lph_ -= std::floor(lph_); sh_ = rand_(); }
  const double raw = shape_(lph_), sm = p.wSmooth > 0.001 ? 1 - js_exp(-dt / (p.wSmooth * 0.04)) : 1;
  lsm_ += (raw - lsm_) * sm;
  // WOBBLE DELAY (an LFO delay): no wobble for wDelay ms after the note starts, then its depth rises to full over wFade ms.
  wt_ += dt * 1000;
  const double wd = p.wDelay > 0.5 || p.wFade > 0.5 ? mx(0, mn(1, (wt_ - p.wDelay) / mx(0.5, p.wFade))) : 1;
  const double u = lsm_ * wd;
  // the spectrum for the end of this block
  const double nyq = sr * 0.5, fc = mn(nyq * 0.98, mx(20, p.cut * js_pow(2, p.fenv * fe_ + p.wCut * u) * js_pow(f / 65.406, p.key)));
  const double Q = 0.6 + p.res * p.res * 11, vp = mx(0, mn(1, p.vowel + p.wVowel * u)) * 4, vi = mn(3, std::floor(vp)), vt = vp - vi;
  // this.vow[vi][0]: a NaN index is undefined and its [0] throws a TypeError
  if (!(vi == vi)) { bad_ = true; return; }
  const int vj = (int)vi;   // 0 to 3
  const double F1 = js_log2(kVow[vj][0] * (1 - vt) + kVow[vj + 1][0] * vt), F2 = js_log2(kVow[vj][1] * (1 - vt) + kVow[vj + 1][1] * vt);
  const double form = p.form, comb = p.comb, cHz = 60 * js_pow(2, mx(0, mn(1, p.combF + p.wComb * u)) * 6);
  const double st = p.stretch * 0.0015, gain = norm_ * (1 - p.wAmp * wd * (1 - lsm_));
  for (int k = 1; k <= kN; k++) {
    const double b0 = (double)base_[k]; if (b0 == 0) { aT_[k] = 0; continue; }
    const double fk = f * k * (1 + st * k * k);
    if (fk > nyq * 0.92) { aT_[k] = 0; continue; }
    const double x = fk / fc, x2 = x * x, d1 = 1 - x2;
    double g = 1 / std::sqrt(d1 * d1 + x2 / (Q * Q)) / std::sqrt(d1 * d1 + 2 * x2);
    if (form > 0) { const double lk = js_log2(fk), g1 = (lk - F1) / 0.32, g2 = (lk - F2) / 0.32; g *= 1 - form + form * (0.1 + 2.6 * (js_exp(-g1 * g1) + 0.6 * js_exp(-g2 * g2))); }
    if (comb > 0) g *= 1 - comb * (0.5 + 0.5 * js_cos(2 * kPi * fk / cHz));
    if (fk > nyq * 0.8) g *= (nyq * 0.92 - fk) / (nyq * 0.12);
    aT_[k] = (float)(b0 * g * gain);
  }
  // synthesis: each partial's amplitude slides from last block's to this block's
  for (double i = a; i < b; i++) { wr(L, len, i, 0); wr(R, len, i, 0); }
  const double inv = 1 / n, det = p.det > 0.5 ? js_pow(2, p.det / 1200) : 0;
  for (int k = 1; k <= kN; k++) {
    const double s0 = (double)aA_[k], s1 = (double)aT_[k]; aA_[k] = (float)s1;
    if (s0 == 0 && s1 == 0) continue;
    const double fk = f * k * (1 + st * k * k), inc = mn(fk, nyq) / sr, dA = (s1 - s0) * inv, pl = (double)panL_[k], pr = (double)panR_[k];
    double ph = phA_[k], amp = s0;
    if (truthy(det)) {
      const double incB = inc * det; double phb = phB_[k];
      for (double i = a; i < b; i++) {
        amp += dA;
        double x = ph * 4096; int32_t j = i32(x); const double s = tab(tab_, j) + (x - j) * (tab(tab_, (int64_t)j + 1) - tab(tab_, j));
        x = phb * 4096; j = i32(x); const double sb = tab(tab_, j) + (x - j) * (tab(tab_, (int64_t)j + 1) - tab(tab_, j));
        wr(L, len, i, rd(L, len, i) + amp * (s * pl + sb * pr)); wr(R, len, i, rd(R, len, i) + amp * (s * pr + sb * pl));
        ph += inc; if (ph >= 1) ph -= 1; phb += incB; if (phb >= 1) phb -= 1;
      }
      phB_[k] = phb;
    } else {
      for (double i = a; i < b; i++) {
        amp += dA;
        const double x = ph * 4096; const int32_t j = i32(x); const double s = (tab(tab_, j) + (x - j) * (tab(tab_, (int64_t)j + 1) - tab(tab_, j))) * amp;
        wr(L, len, i, rd(L, len, i) + s * pl); wr(R, len, i, rd(R, len, i) + s * pr);
        ph += inc; if (ph >= 1) ph -= 1;
      }
    }
    phA_[k] = ph;
  }
  // sub, envelope, drive, level
  const double subA = p.sub * 0.45, subInc = f / js_pow(2, (double)i32(p.subOct)) / sr, de = (e1 - e0) * inv;
  const double dr = p.drive > 0.001 ? 1 + p.drive * p.drive * 12 : 0, dn = truthy(dr) ? 0.5 / js_tanh(dr * 0.5) : 1, lv = js_pow(10, p.level / 20);
  double env = e0, sp = subPh_;
  for (double i = a; i < b; i++) {
    env += de;
    double l = rd(L, len, i), r = rd(R, len, i);
    if (subA > 0) { const double x = sp * 4096; const int32_t j = i32(x); const double s = (tab(tab_, j) + (x - j) * (tab(tab_, (int64_t)j + 1) - tab(tab_, j))) * subA; l += s; r += s; sp += subInc; if (sp >= 1) sp -= 1; }
    l *= env; r *= env;
    if (truthy(dr)) { l = js_tanh(l * dr) * dn; r = js_tanh(r * dr) * dn; }
    wr(L, len, i, l * lv); wr(R, len, i, r * lv);
  }
  subPh_ = sp;
}

// ================================================================ renderBass ==================================================================================================================
// `const h = Math.max(0.05, hold || V.len || 2), N = Math.min(Math.ceil((h + p.rel / 1000 * 1.6 + 0.02) * sr), sr * 32), hN = Math.min(N, Math.round(h * sr)); const L = new Float32Array(N), R = new Float32Array(N)`.
// `new Float32Array(N)` truncates N (NaN gives 0) and a negative one throws: refused, as an empty result is (the JS returns an empty sound).
size_t xeng_bass(const BassP& P, double sr, double hold, double len, double bpm, float* out, size_t cap) {
  if (!(sr > 0 && sr <= 1e7)) return kRefuse;
  BassCore c(sr); c.set(P); c.bpm = bpm;
  const BassP& p = c.p();
  const double h = mx(0.05, orv(hold, orv(len, 2)));
  const double N = mn(std::ceil((h + p.rel / 1000 * 1.6 + 0.02) * sr), sr * 32), hN = mn(N, js_round(h * sr));
  const double tN = (N == N) ? std::trunc(N) : 0;
  if (!(tN >= 1 && tN <= 16777216.0)) return kRefuse;
  const size_t n = (size_t)tN;
  if (!out) return 2 * n;
  if (cap < 2 * n) return 0;
  float* L = out; float* R = out + n;
  std::memset(out, 0, 2 * n * sizeof(float));   // new Float32Array(N) twice (a render above 2^24 floats is refused: its two arrays would be 128 MB)
  c.on(false); c.render(L, R, n, 0, hN, 1); if (c.bad()) return kRefuse;
  c.off(); c.render(L, R, n, hN, N, 1); if (c.bad()) return kRefuse;
  // out[i] = (L[i] + R[i]) * 0.5 (a float32 store of an exact double sum), then the trim and the fade: `let end = N` is the NUMBER N (a fractional N reads undefined at end - 1 and stops the loop at once)
  for (size_t i = 0; i < n; i++) L[i] = (float)(((double)L[i] + (double)R[i]) * 0.5);
  size_t end = n; if (N == std::floor(N)) { while (end > 1 && jabs((double)L[end - 1]) < 1e-4) end--; }
  const size_t rl = end > 1 ? end : 1; const double flr = js_round(sr * 0.002); const size_t fl = (double)rl < flr ? rl : (size_t)(flr > 0 ? flr : 0);
  for (size_t i = 0; i < fl; i++) L[rl - 1 - i] = (float)((double)L[rl - 1 - i] * ((double)i / (double)fl));
  return rl;
}

}  // namespace thunder
