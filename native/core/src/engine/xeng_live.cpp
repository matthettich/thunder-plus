// U27: AcidCore, MachCore and renderLiveCore (XENG acid, mach), index.html line for line. THE JS IS THE SPECIFICATION: operation order, doubles everywhere (the cores' state is doubles; the only float32
// values are the ones written into the Float32Arrays L / R), every Math.* is a js_* function, Math.max / Math.min keep NaN. Allocation: none.
#include "engine/xeng_live.h"

#include <cmath>
#include <cstring>

#include "engine/xeng_util.h"

namespace thunder {
using namespace xe;

namespace {
constexpr double kBLK = 16;
constexpr double kMaxLive = 33554432;   // 2^25 floats (a 12 minute render at 48 kHz): the longest render of these engines; the JS would allocate whatever the hold and the rate say

// a write into a Float32Array: an index that is out of range or not a whole number is ignored (no property is made on a typed array)
inline void put(float* L, float* R, size_t len, double i, double y) {
  if (i >= 0 && i < (double)len && i == std::floor(i)) { L[(size_t)i] = (float)y; if (R) R[(size_t)i] = (float)y; }
}
}  // namespace

// ================================================================ AcidCore ====================================================================================================================
AcidCore::AcidCore(double sr) : sr_(sr) { set(AcidP()); }

void AcidCore::set(const AcidP& o) {
  const AcidP d;   // `for (const k in p) if (typeof p[k] !== 'number' || !isFinite(p[k])) p[k] = AcidCore.defaults()[k] || 0`
#define XP_SAN(n, dd) p_.n = std::isfinite(o.n) ? o.n : d.n;
  XP_ACID(XP_SAN)
#undef XP_SAN
}
void AcidCore::on(bool leg, bool acc) {
  const bool fresh = !gate_ && env_ < 0.003; go_ = fresh;
  leg_ = leg && !fresh; gate_ = true; accOn_ = acc ? 1 : 0;
  if (!leg_) { fe_ = 1; stage_ = 1; if (acc) accE_ = 1; }
}
void AcidCore::off() { gate_ = false; stage_ = 4; }
double AcidCore::pblep(double t, double dt) { if (t < dt) { t /= dt; return t + t - t * t - 1; } if (t > 1 - dt) { t = (t - 1) / dt; return t * t + t + t + 1; } return 0; }
double AcidCore::ft(double x) { if (x > 3) return 1; if (x < -3) return -1; const double x2 = x * x; return x * (27 + x2) / (27 + 9 * x2); }

void AcidCore::render(float* L, float* R, size_t len, double a, double b, double rate) { for (double i = a; i < b; i += kBLK) block(L, R, len, i, mn(b, i + kBLK), rate); }

void AcidCore::block(float* L, float* R, size_t len, double a, double b, double rate) {
  const AcidP& p = p_; const double sr = sr_, n = b - a, dt = n / sr;
  if (stage_ == 0 && env_ == 0) { for (double i = a; i < b; i++) put(L, R, len, i, 0); return; }
  const double target = js_log2(65.406 * mx(1e-3, rate)) + p.oct + p.tune / 12;
  if (go_ || lf_ == 0) { lf_ = target; go_ = false; }
  else if (leg_ && p.slide > 0.5) lf_ += (target - lf_) * (1 - js_exp(-dt * 1000 / p.slide));
  else lf_ = target;
  const double f = js_pow(2, lf_), inc = mn(0.45, f / sr);
  // amp: a quick attack, then down toward Sustain over Amp decay while held; Release after
  const double e0 = env_; double e1 = e0;
  if (stage_ == 1) { e1 = e0 + dt * 1000 / 2; if (e1 >= 1) { e1 = 1; stage_ = 2; } }
  else if (stage_ == 2) e1 = p.sus + (e0 - p.sus) * js_exp(-dt * 1000 * 4.6 / mx(1, p.ampDec));
  else if (stage_ == 4) { e1 = e0 * js_exp(-dt * 1000 * 4.6 / mx(1, p.rel)); if (e1 < 1e-5) { e1 = 0; stage_ = 0; } }
  env_ = e1;
  // filter envelope (accented notes decay faster), and the accent "sweep"
  const double fd = truthy(accOn_) ? mn(p.dec, 200) : p.dec;
  fe_ *= js_exp(-dt * 1000 * 4.6 / mx(10, fd));
  accE_ *= js_exp(-dt * 1000 * 4.6 / 200);
  accC_ += (accE_ - accC_) * (1 - js_exp(-dt / 0.05));
  const double acc = p.accent * accOn_;
  const double fc = mn(sr * 0.42, mx(25, p.cut * js_pow(2, p.envAmt * 5 * fe_ + acc * 3.2 * accC_ + p.key * (lf_ - 7.03))));
  const double g = 1 - js_exp(-2 * kPi * fc / sr), k = p.res * 4.1, comp = 1 + p.res * 1.1;
  const double pw = 0.5 - mx(0, mn(0.45, (p.pw - 0.5) * 0.9 + 0)), wv = p.wave, sub = p.sub, nz = p.noise;
  const double sat = 1 + p.sat * 4, lv = js_pow(10, orz(p.level) / 20) * 1.1 * (1 + acc * 0.6), dr = p.drive > 0.001 ? 1 + p.drive * p.drive * 10 : 0, dn = truthy(dr) ? 1 / js_tanh(dr) : 1;
  const double de = (e1 - e0) / n; double env = e0;
  for (double i = a; i < b; i++) {
    env += de;
    const double t = ph_; ph_ += inc; if (ph_ >= 1) ph_ -= 1;
    const double saw = 2 * t - 1 - pblep(t, inc);
    double sq = (t < pw ? 1 : -1) + pblep(t, inc); { double t2 = t - pw; if (t2 < 0) t2 += 1; sq -= pblep(t2, inc); }
    double x = saw * (1 - wv) + sq * wv;
    if (sub > 0) { subPh_ += inc * 0.5; if (subPh_ >= 1) subPh_ -= 1; x += (subPh_ < 0.5 ? 1 : -1) * sub * 0.7; }
    if (nz > 0) { rnd_ = rnd_ * 1664525u + 1013904223u; x += ((double)rnd_ / 2147483648.0 - 1) * nz * 0.6; }   // (rnd * 1664525 + 1013904223) >>> 0, exact below 2^53
    // the ladder: four one-pole stages with soft saturation, resonance fed back from the last
    const double u = ft((x - k * s3_) * 0.8);
    s0_ += g * (u - s0_); s1_ += g * (s0_ - s1_); s2_ += g * (s1_ - s2_); s3_ += g * (ft(s2_ * sat) / sat - s3_);
    double y = s3_ * comp * env;
    const double o = y - dcx_ + 0.997 * dc_; dcx_ = y; dc_ = o; y = o * lv;
    if (truthy(dr)) y = js_tanh(y * dr) * dn;
    put(L, R, len, i, y);
  }
}

// ================================================================ MachCore ====================================================================================================================
MachCore::MachCore(double sr) : sr_(sr) { for (int i = 0; i < 10; i++) ph_[i] = std::fmod(i * 0.618034, 1.0); set(MachP()); }

void MachCore::set(const MachP& o) {
  const MachP d;
#define XP_SAN(n, dd) p_.n = std::isfinite(o.n) ? o.n : d.n;
  XP_MACH(XP_SAN)
#undef XP_SAN
}
void MachCore::on(bool leg) { const bool fresh = !gate_ && env_ < 0.003; go_ = fresh; leg_ = leg && !fresh; gate_ = true; if (!leg_) { stage_ = 1; fe_ = 1; } }
void MachCore::off() { gate_ = false; stage_ = 4; }
double MachCore::pblep(double t, double dt) { if (t < dt) { t /= dt; return t + t - t * t - 1; } if (t > 1 - dt) { t = (t - 1) / dt; return t * t + t + t + 1; } return 0; }
// this.ph[j]: a Float64Array(10): a read past it is undefined (the arithmetic gives NaN), a write past it is ignored
double MachCore::saw(int j, double inc) {
  const double t = (j >= 0 && j < 10) ? ph_[j] : (double)NAN;
  const double v = 2 * t - 1 - pblep(t, inc); if (j >= 0 && j < 10) ph_[j] = t + inc >= 1 ? t + inc - 1 : t + inc; return v;
}
double MachCore::gof(double fc) const { return js_tan(kPi * mn(fc, sr_ * 0.45) / sr_); }
double MachCore::svf(double x, double g, double r, double* st) {   // a state-variable filter step: returns the low-pass; oh_ / ob_ hold the high- and band-pass
  const double h = (x - (r + g) * st[0] - st[1]) / (1 + r * g + g * g), bp = g * h + st[0]; st[0] = g * h + bp;
  const double lp = g * bp + st[1]; st[1] = g * bp + lp; oh_ = h; ob_ = bp; return lp;
}

void MachCore::render(float* L, float* R, size_t len, double a, double b, double rate) { for (double i = a; i < b && !bad_; i += kBLK) block(L, R, len, i, mn(b, i + kBLK), rate); }

void MachCore::block(float* L, float* R, size_t len, double a, double b, double rate) {
  static const int kChN[10] = {2, 2, 3, 3, 3, 4, 4, 4, 4, 4};
  static const int kCh[10][4] = {{0, 12, 0, 0}, {0, 7, 0, 0}, {0, 4, 7, 0}, {0, 3, 7, 0}, {0, 5, 7, 0}, {0, 4, 7, 11}, {0, 3, 7, 10}, {0, 4, 7, 10}, {0, 5, 10, 15}, {0, 7, 12, 19}};   // MachCore.CHORDS
  const MachP& p = p_; const double sr = sr_, n = b - a, dt = n / sr;
  if (stage_ == 0 && env_ == 0) { for (double i = a; i < b; i++) put(L, R, len, i, 0); return; }
  // LFO (sine) and where it goes: 0 pitch, 1 low-pass, 2 the machine's A, 3 level
  lfo_ += dt * p.lfoRate; if (lfo_ >= 1) lfo_ -= std::floor(lfo_);
  const double lv0 = js_sin(2 * kPi * lfo_) * p.lfoDepth; const int32_t dst = i32(p.lfoDest);
  const double target = js_log2(65.406 * mx(1e-3, rate)) + p.oct + p.tune / 12 + (dst == 0 ? lv0 * 0.5 : 0);
  if (go_ || lf_ == 0) { lf_ = target; go_ = false; }
  else if (p.glide > 0.5 && leg_) lf_ += (target - lf_) * (1 - js_exp(-dt * 1000 / p.glide));
  else if (p.glide > 0.5) lf_ += (target - lf_) * (1 - js_exp(-dt * 1000 / p.glide));
  else lf_ = target;
  const double f = js_pow(2, lf_), inc = mn(0.45, f / sr);
  const double e0 = env_; double e1 = e0;
  if (stage_ == 1) { e1 = e0 + dt * 1000 / mx(0.5, p.atk); if (e1 >= 1) { e1 = 1; stage_ = 2; } }
  else if (stage_ == 2) e1 = p.sus + (e0 - p.sus) * js_exp(-dt * 1000 * 4.6 / mx(1, p.dec));
  else if (stage_ == 4) { e1 = e0 * js_exp(-dt * 1000 * 4.6 / mx(1, p.rel)); if (e1 < 1e-5) { e1 = 0; stage_ = 0; } }
  env_ = e1; fe_ *= js_exp(-dt * 1000 * 4.6 / mx(5, p.fdec));
  const double A = mx(0, mn(1, p.a + (dst == 2 ? lv0 * 0.5 : 0))), B = p.b, C = p.c; const int32_t m = i32(p.machine);
  const double lpF = mx(30, mn(sr * 0.45, p.lp * js_pow(2, p.fenv * 6 * fe_ + (dst == 1 ? lv0 * 3 : 0)))), q = 0.6 + p.res * p.res * 14;
  const double amp = js_pow(10, orz(p.level) / 20) * 0.35 * (dst == 3 ? 1 - jabs(lv0) * 0.9 : 1);
  const double dr = p.drive > 0.001 ? 1 + p.drive * p.drive * 10 : 0, dn = truthy(dr) ? 1 / js_tanh(dr) : 1, de = (e1 - e0) / n;
  const double gH = gof(p.hp), gL = gof(lpF), rL = 1 / q, gN = gof(f * 2), rN = 1 / (0.6 + (1 - A) * 30);
  // The JS would throw here (`MachCore.CHORDS[NaN].length`): Ensemble with A = NaN (the LFO phase ran to infinity, then Math.sin gave NaN). And a Swarm of more than 1e4 voices loops that often per sample: it never finishes.
  const double nvS = 2 + js_round(B * 5);
  if (m == 4 && !(A == A)) { bad_ = true; return; }
  if (m == 3 && nvS > 1e4) { bad_ = true; return; }
  double env = e0;
  for (double i = a; i < b; i++) {
    env += de; double x = 0;
    switch (m) {
      case 0: {   // Dual VCO: A saw->pulse, B second oscillator mix, C its interval (0-24 st), Detune
        const double r2 = js_pow(2, (js_round(C * 24) + p.det * 0.5) / 12), inc2 = mn(0.45, inc * r2), pw = 0.5 - A * 0.45;
        const double t = ph_[0]; double s1 = 2 * t - 1 - pblep(t, inc);
        if (A > 0.02) { double sq = (t < pw ? 1 : -1) + pblep(t, inc), t2 = t - pw; if (t2 < 0) t2 += 1; sq -= pblep(t2, inc); s1 = s1 * (1 - A) + sq * A; }
        ph_[0] = t + inc >= 1 ? t + inc - 1 : t + inc;
        x = s1 * (1 - B * 0.5) + saw(1, inc2) * B * 0.8; break; }
      case 1: {   // Chip: A pulse width, B wave (pulse, saw, triangle, noise), C ring mod; 12-bit steps
        const double t = ph_[0]; ph_[0] = t + inc >= 1 ? t + inc - 1 : t + inc;
        const double zone = mn(3, std::floor(B * 4)), pw = 0.08 + A * 0.84;
        if (zone == 0) x = t < pw ? 1 : -1;
        else if (zone == 1) x = 2 * t - 1;
        else if (zone == 2) x = 4 * jabs(t - 0.5) - 1;
        else { if (ph_[0] < t) { rnd_ = rnd_ * 1664525u + 1013904223u; hold_ = (double)rnd_ / 2147483648.0 - 1; } x = hold_; }
        if (C > 0.01) { const double t2 = ph_[1]; ph_[1] = t2 + inc * 1.5 >= 1 ? t2 + inc * 1.5 - 1 : t2 + inc * 1.5; x *= 1 - C + C * (4 * jabs(t2 - 0.5) - 1); x *= 1 + C; }
        x = js_round(x * 2047) / 2047 * 0.8; break; }
      case 2: {   // FM+: A ratio, B index (follows the filter envelope too), C feedback
        const double ratio = 0.5 + js_round(A * 15) * 0.5, idx = B * 8 * (0.4 + 0.6 * fe_);
        const double tm = ph_[1]; ph_[1] = tm + inc * ratio >= 1 ? std::fmod(tm + inc * ratio, 1.0) : tm + inc * ratio;
        const double mod = js_sin(2 * kPi * tm + C * 2.5 * fbm_); fbm_ = mod;
        const double tc = ph_[0]; ph_[0] = tc + inc >= 1 ? tc + inc - 1 : tc + inc;
        x = js_sin(2 * kPi * tc + idx * mod) * 0.9; break; }
      case 3: {   // Swarm: A spread, B voices (2-7), C sub. (ph has 10 entries: a voice from the 11th on reads undefined, so s is NaN from then on: the loop need not run past it)
        const double nv = nvS; double s = 0;
        for (int j = 0; j < 11 && j < nv; j++) { const double off = (j / (nv - 1) - 0.5) * 2 * A * 0.06; s += saw(j, mn(0.45, inc * (1 + off))); }
        x = s / std::sqrt(nv) * 0.75;
        if (C > 0) { const double t = ph_[9]; ph_[9] = t + inc * 0.5 >= 1 ? t + inc * 0.5 - 1 : t + inc * 0.5; x += js_sin(2 * kPi * t) * C * 0.8; }
        break; }
      case 4: {   // Ensemble: A chord, B detune, C saw->square
        const int ci = (int)mn(9, std::floor(A * 10)); const int chn = kChN[ci]; const int* ch = kCh[ci]; double s = 0;   // A is a number in 0..1 here (NaN was refused above)
        for (int j = 0; j < chn; j++) {
          const double r = js_pow(2, ch[j] / 12.0) * (1 + (j % 2 ? 1 : -1) * B * 0.008 * (j + 1)), ij = mn(0.45, inc * r), t = ph_[j];
          double v = 2 * t - 1 - pblep(t, ij); if (C > 0.02) { double sq = (t < 0.5 ? 1 : -1) + pblep(t, ij), t2 = t - 0.5; if (t2 < 0) t2 += 1; sq -= pblep(t2, ij); v = v * (1 - C) + sq * C; }
          ph_[j] = t + ij >= 1 ? t + ij - 1 : t + ij; s += v; }
        x = s / std::sqrt((double)chn) * 0.7; break; }
      case 5: {   // Noise: A band width, B colour (white to dark), C pitched resonance
        rnd_ = rnd_ * 1664525u + 1013904223u; double w = (double)rnd_ / 2147483648.0 - 1;
        n1_ += (w - n1_) * (1 - B * 0.95); w = n1_;
        svf(w, gN, rN, fn_); const double bp = ob_;
        x = (w * A + bp * (1 - A) * 3) * (1 - C) + bp * C * 6; x *= 0.3; break; }
      default: {   // Additive: A partials (1-16), B odd/even, C tilt (bright)
        const double np = 1 + js_round(A * 15), t = ph_[0]; ph_[0] = t + inc >= 1 ? t + inc - 1 : t + inc;
        double s = 0, norm = 0;
        for (int k = 1; k <= np; k++) { if (f * k > sr * 0.45) break; const double g = js_pow(k, -1.6 + C * 1.4) * (k % 2 ? 1 : 1 - B); s += js_sin(2 * kPi * t * k) * g; norm += g; }
        x = truthy(norm) ? s / std::sqrt(norm) * 0.8 : 0; break; }
    }
    // high-pass, then the resonant low-pass
    svf(x, gH, 1.4, fh_); const double hpo = oh_;
    const double lpo = svf(hpo, gL, rL, fl_);
    double y = lpo * env * amp;
    if (truthy(dr)) y = js_tanh(y * dr) * dn;
    put(L, R, len, i, y);
  }
}

// ================================================================ renderLiveCore ==============================================================================================================
// The live engines' pad render: one note, held for `hold` seconds, then released. Returns the trimmed, faded length (the finished sound is in L), kRefuse where the JS throws, 0 if `cap` is too small.
// out == nullptr: only the length of the render before the trim.
namespace {
struct LiveLen { double N, hN; size_t len; bool ok; };
// Math.max(0.05, hold || 1.5), tail, N, hN. `new Float32Array(N)`: N is truncated (NaN gives 0), a negative one throws RangeError; an empty result is refused too (the JS returns an empty sound).
LiveLen liveLen(double rel, double sr, double hold) {
  LiveLen r{0, 0, 0, false};
  const double h = mx(0.05, orv(hold, 1.5)), tail = (orv(rel, 20) / 1000) * 1.6 + 0.03;
  r.N = mn(std::ceil((h + tail) * sr), sr * 32); r.hN = mn(r.N, js_round(h * sr));
  const double tN = (r.N == r.N) ? std::trunc(r.N) : 0;
  if (!(tN >= 1 && tN <= kMaxLive)) return r;
  r.len = (size_t)tN; r.ok = true; return r;
}
// `let end = N; while (end > 1 && Math.abs(L[end - 1]) < 1e-4) end--; res = L.slice(0, Math.max(end, 1)); fade of round(sr * 0.002)`. A fractional N reads L[N - 1] = undefined: the loop stops at once.
size_t liveTail(float* L, size_t len, double N, double sr) {
  size_t end = len; if (N == std::floor(N)) { while (end > 1 && jabs((double)L[end - 1]) < 1e-4) end--; }
  const size_t rl = end > 1 ? end : 1; const double flr = js_round(sr * 0.002); const size_t fl = (double)rl < flr ? rl : (size_t)(flr > 0 ? flr : 0);
  for (size_t i = 0; i < fl; i++) L[rl - 1 - i] = (float)((double)L[rl - 1 - i] * ((double)i / (double)fl));
  return rl;
}
}  // namespace

size_t xeng_acid(const AcidP& P, double sr, double hold, float* out, size_t cap, bool acc) {
  AcidCore c(sr); c.set(P);
  const LiveLen ll = liveLen(c.p().rel, sr, hold);
  if (!ll.ok) return kRefuse;
  if (!out) return ll.len;
  if (ll.len > cap) return 0;
  std::memset(out, 0, ll.len * sizeof(float));   // new Float32Array(N)
  c.on(false, acc); c.render(out, nullptr, ll.len, 0, ll.hN, 1); c.off(); c.render(out, nullptr, ll.len, ll.hN, ll.N, 1);
  return liveTail(out, ll.len, ll.N, sr);
}

size_t xeng_mach(const MachP& P, double sr, double hold, float* out, size_t cap) {
  MachCore c(sr); c.set(P);
  const LiveLen ll = liveLen(c.p().rel, sr, hold);
  if (!ll.ok) return kRefuse;
  if (!out) return ll.len;
  if (ll.len > cap) return 0;
  std::memset(out, 0, ll.len * sizeof(float));
  c.on(false); c.render(out, nullptr, ll.len, 0, ll.hN, 1); if (c.bad()) return kRefuse; c.off(); c.render(out, nullptr, ll.len, ll.hN, ll.N, 1); if (c.bad()) return kRefuse;
  return liveTail(out, ll.len, ll.N, sr);
}

}  // namespace thunder
