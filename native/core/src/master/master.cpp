// U11: master-worklet.js in C++. See master.h. Every statement follows the JS in order, with its float32 stores (Float32Array) kept.
#include "master/master.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

#include "rt/jsmath.h"

namespace thunder {
namespace {

const double TAU = 3.141592653589793 * 2;   // Math.PI * 2
inline double dbToLin(double d) { return js_pow(10, d / 20); }
inline double linToDb(double x) { return 20 * std::log10(std::max(x, 1e-12)); }   // (std::log10: V8's is its own fdlibm port)
inline double clampd(double v, double a, double b) { return v < a ? a : v > b ? b : v; }
inline bool finite(double v) { return (v - v) == 0; }   // the worklet's `fin` test: not NaN, not +-Infinity
inline float f32(double v) { return (float)v; }

struct Biq { double x1 = 0, x2 = 0, y1 = 0, y2 = 0; };
struct BiqC { double b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };
inline double bqRun(const BiqC& c, Biq& s, double x) {
  const double y = c.b0 * x + c.b1 * s.x1 + c.b2 * s.x2 - c.a1 * s.y1 - c.a2 * s.y2;
  s.x2 = s.x1; s.x1 = x; s.y2 = s.y1; s.y1 = y; return y;
}
inline bool dead4(const Biq& h) { return std::fabs(h.x1) < 1e-20 && std::fabs(h.x2) < 1e-20 && std::fabs(h.y1) < 1e-20 && std::fabs(h.y2) < 1e-20; }

}  // namespace

// ---- FFT (radix-2, in place, complex), N fixed at 2048 -----------------------------------------------------------------------------
struct FFTN {
  int n; std::vector<uint32_t> rev; std::vector<double> cs, sn;
  explicit FFTN(int n_) : n(n_), rev((size_t)n_), cs((size_t)n_ / 2), sn((size_t)n_ / 2) {
    int bits = 0; while ((1 << bits) < n) bits++;   // Math.log2(n) | 0 for a power of two
    for (int i = 0; i < n; i++) { uint32_t r = 0; for (int b = 0; b < bits; b++) r = (r << 1) | (uint32_t)((i >> b) & 1); rev[(size_t)i] = r; }
    for (int i = 0; i < n / 2; i++) { cs[(size_t)i] = std::cos(TAU * i / n); sn[(size_t)i] = -std::sin(TAU * i / n); }
  }
  void run(double* re, double* im, bool inverse) const {
    for (int i = 0; i < n; i++) { const int j = (int)rev[(size_t)i]; if (j > i) { double t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; } }
    for (int size = 2; size <= n; size <<= 1) {
      const int half = size >> 1, step = n / size;
      for (int i = 0; i < n; i += size) {
        for (int j = 0, k = 0; j < half; j++, k += step) {
          const double wr = cs[(size_t)k], wi = inverse ? -sn[(size_t)k] : sn[(size_t)k];
          const int a = i + j, b = a + half;
          const double tr = re[b] * wr - im[b] * wi, ti = re[b] * wi + im[b] * wr;
          re[b] = re[a] - tr; im[b] = im[a] - ti; re[a] += tr; im[a] += ti;
        }
      }
    }
    if (inverse) { for (int i = 0; i < n; i++) { re[i] /= n; im[i] /= n; } }
  }
};

// ---- Resonance suppressor ----------------------------------------------------------------------------------------------------------
class Suppressor {
 public:
  struct P { double depth = .5, sharp = .5, sel = 3, atk = 8, rel = 90, lo = 150, hi = 12000, mix = 1; bool delta = false; } p;
  explicit Suppressor(double sr)
      : sr_(sr), fft_(2048), win_((size_t)N), inL_((size_t)N), inR_((size_t)N), olaL_((size_t)(N + H)), olaR_((size_t)(N + H)),
        reL_((size_t)N), imL_((size_t)N), reR_((size_t)N), imR_((size_t)N), db_((size_t)K), env_((size_t)K), pre_((size_t)K + 1), pw_((size_t)K),
        red_((size_t)K), redS_((size_t)K), tmp_((size_t)K), fw_((size_t)K), dryL_((size_t)(N + H)), dryR_((size_t)(N + H)) {
    for (int i = 0; i < N; i++) win_[(size_t)i] = f32(std::sqrt(0.5 - 0.5 * std::cos(TAU * i / N)));
    setFocus();
  }
  void set(const P& np) { p = np; setFocus(); }
  void setFocus() {
    const double lo = p.lo, hi = p.hi, binHz = sr_ / N;
    for (int k = 0; k < K; k++) {
      const double f = std::max(1.0, k * binHz), a = std::log2(f / lo), b = std::log2(hi / f);
      fw_[(size_t)k] = f32(clampd(a * 2 + 1, 0, 1) * clampd(b * 2 + 1, 0, 1));
    }
  }
  int latency() const { return N + D; }
  void process(float* L, float* R, int n) {
    if (stage_) step();
    for (int i = 0; i < n; i++) {
      inL_[(size_t)w_] = L[i]; inR_[(size_t)w_] = R[i];
      const double dl = dryL_[(size_t)dw_], dr = dryR_[(size_t)dw_]; dryL_[(size_t)dw_] = L[i]; dryR_[(size_t)dw_] = R[i]; dw_ = (dw_ + 1) % M;
      w_ = (w_ + 1) % N;
      const double wl = olaL_[(size_t)rd_], wr = olaR_[(size_t)rd_]; olaL_[(size_t)rd_] = 0; olaR_[(size_t)rd_] = 0; rd_ = (rd_ + 1) % M;
      if (p.delta) { L[i] = f32(dl - wl); R[i] = f32(dr - wr); }
      else { L[i] = f32(dl + (wl - dl) * p.mix); R[i] = f32(dr + (wr - dr) * p.mix); }
      if (++cnt_ >= H) { cnt_ = 0; if (stage_) finish(); stage_ = 1; rdSnap_ = rd_; step(); }
    }
  }
  void finish() { while (stage_) step(); }
  void step() {
    const int st = stage_; double *reL = reL_.data(), *imL = imL_.data(), *reR = reR_.data(), *imR = imR_.data();
    if (st == 1) { for (int i = 0; i < N; i++) { const int j = (w_ + i) % N; reL[i] = inL_[(size_t)j] * (double)win_[(size_t)i]; imL[i] = 0; reR[i] = inR_[(size_t)j] * (double)win_[(size_t)i]; imR[i] = 0; } fft_.run(reL, imL, false); }
    else if (st == 2) { fft_.run(reR, imR, false); analyse(); }
    else if (st == 3) { gain(); fft_.run(reL, imL, true); }
    else { fft_.run(reR, imR, true); overlap(); stage_ = 0; return; }
    stage_ = st + 1;
  }
  void analyse() {
    const double *reL = reL_.data(), *imL = imL_.data(), *reR = reR_.data(), *imR = imR_.data();
    float* db = db_.data();
    const double av = 1 - js_exp(-((double)H / sr_) / 0.06);
    for (int k = 0; k < K; k++) {
      const double m = (reL[k] * reL[k] + imL[k] * imL[k] + reR[k] * reR[k] + imR[k] * imR[k]) * 0.5;
      pw_[(size_t)k] += (m - pw_[(size_t)k]) * av; db[k] = f32(10 * std::log10(pw_[(size_t)k] + 1e-20));
    }
    const double oct = 0.25 + (1 - p.sharp) * 1.25, half = js_pow(2, oct / 2); double* pre = pre_.data();
    pre[0] = 0; for (int k = 0; k < K; k++) pre[k + 1] = pre[k] + (double)db[k];
    for (int k = 1; k < K; k++) {
      const double a = std::max(1.0, std::floor(k / half)), b = std::min((double)(K - 1), std::ceil(k * half));
      env_[(size_t)k] = f32((pre[(int)b + 1] - pre[(int)a]) / (b - a + 1));
    }
    env_[0] = env_[1];
    const double maxCut = 18 * p.depth + 2, sc = 0.35 + 1.15 * p.depth;
    for (int k = 0; k < K; k++) { const double ex = (double)db[k] - (double)env_[(size_t)k] - p.sel; tmp_[(size_t)k] = ex > 0 ? f32(std::min(maxCut, ex * sc) * (double)fw_[(size_t)k]) : 0.0f; }
    const double radd = js_round(1 + (1 - p.sharp) * 5); const int rad = (int)radd;
    for (int k = 0; k < K; k++) {
      double m = 0; const int a = std::max(0, k - rad), b = std::min(K - 1, k + rad);
      for (int j = a; j <= b; j++) { const double v = (double)tmp_[(size_t)j] * (1 - (double)std::abs(j - k) / (radd + 1)); if (v > m) m = v; }
      red_[(size_t)k] = f32(m);
    }
    const double fr = (double)H / sr_, ca = js_exp(-fr / std::max(0.001, p.atk / 1000)), cr = js_exp(-fr / std::max(0.005, p.rel / 1000));
    for (int k = 0; k < K; k++) { const double t = red_[(size_t)k], c = t > redS_[(size_t)k] ? ca : cr; redS_[(size_t)k] = f32(t + ((double)redS_[(size_t)k] - t) * c); }
  }
  void gain() {
    double *reL = reL_.data(), *imL = imL_.data(), *reR = reR_.data(), *imR = imR_.data();
    for (int k = 0; k < K; k++) {
      const double g = js_pow(10, -(double)redS_[(size_t)k] / 20);
      reL[k] *= g; imL[k] *= g; reR[k] *= g; imR[k] *= g;
      if (k > 0 && k < N / 2) { const int m = N - k; reL[m] *= g; imL[m] *= g; reR[m] *= g; imR[m] *= g; }
    }
  }
  void overlap() {
    for (int i = 0; i < N; i++) {
      const int j = (rdSnap_ + D + i) % M;
      olaL_[(size_t)j] = f32((double)olaL_[(size_t)j] + reL_[(size_t)i] * (double)win_[(size_t)i] * 0.5);
      olaR_[(size_t)j] = f32((double)olaR_[(size_t)j] + reR_[(size_t)i] * (double)win_[(size_t)i] * 0.5);
    }
  }
  void bands(int n, float* out) const {
    const double binHz = sr_ / N;
    for (int b = 0; b < n; b++) {
      const double f0 = 20 * js_pow(1000, (double)b / n), f1 = 20 * js_pow(1000, (double)(b + 1) / n);
      const int k0 = (int)std::max(1.0, std::floor(f0 / binHz)), k1 = (int)std::min((double)(K - 1), std::max((double)k0, std::ceil(f1 / binHz)));
      double m = 0; for (int k = k0; k <= k1; k++) if ((double)redS_[(size_t)k] > m) m = redS_[(size_t)k];
      out[b] = f32(m);
    }
  }
  static constexpr int N = 2048, H = 512, K = N / 2 + 1, D = H, M = N + D;
 private:
  double sr_; FFTN fft_;
  std::vector<float> win_, inL_, inR_, olaL_, olaR_;
  std::vector<double> reL_, imL_, reR_, imR_;
  std::vector<float> db_, env_; std::vector<double> pre_, pw_;
  std::vector<float> red_, redS_, tmp_, fw_, dryL_, dryR_;
  int stage_ = 0, rdSnap_ = 0, w_ = 0, cnt_ = 0, rd_ = 0, dw_ = 0;
};

// ---- Compressor --------------------------------------------------------------------------------------------------------------------
class Compressor {
 public:
  struct P { double thr = -14, ratio = 2, atk = 10, rel = 150, knee = 6, makeup = 0, mix = 1, hpf = 80; bool autoRel = true, automk = true; } p;
  double gr = 0, load = 0, detMax = -200;
  explicit Compressor(double sr) : sr_(sr) { setHpf(); }
  void set(const P& np) { p = np; setHpf(); }
  void setHpf() {
    const double f = std::max(10.0, p.hpf != 0 ? p.hpf : 10.0), w = TAU * f / sr_, c = std::cos(w), s = std::sin(w), a = s / (2 * 0.7071067811865476);   // Math.SQRT1_2
    const double a0 = 1 + a; hc_.b0 = (1 + c) / 2 / a0; hc_.b1 = -(1 + c) / a0; hc_.b2 = (1 + c) / 2 / a0; hc_.a1 = -2 * c / a0; hc_.a2 = (1 - a) / a0;
    hpOn_ = p.hpf > 15;   // ((hpf || 0) > 15)
  }
  bool quiet() const { if (!(gr > -1e-12)) return false; for (int c = 0; c < 2; c++) if (!dead4(hp_[c])) return false; return true; }
  double curve(double x) const {
    const double thr = p.thr, ratio = p.ratio, knee = p.knee, s = 1 - 1 / ratio, o = x - thr;
    if (knee > 0 && std::fabs(o) <= knee / 2) return -s * (o + knee / 2) * (o + knee / 2) / (2 * knee);
    return o > 0 ? -s * o : 0;
  }
  void process(float* L, float* R, int n) {
    const double sr = sr_, ca = js_exp(-1 / (sr * std::max(0.0001, p.atk / 1000)));
    const double mk = p.automk ? -curve(0) * 0.5 + p.makeup : p.makeup, mkLin = dbToLin(mk);
    for (int i = 0; i < n; i++) {
      const double l = L[i], r = R[i];
      const double dl = hpOn_ ? bqRun(hc_, hp_[0], l) : l, dr = hpOn_ ? bqRun(hc_, hp_[1], r) : r;
      const double lv = linToDb(std::max(std::fabs(dl), std::fabs(dr)));
      if (lv > detMax) detMax = lv;
      const double target = curve(lv);
      if (target < gr) gr = target + (gr - target) * ca;
      else {
        const double relMs = p.autoRel ? p.rel * (0.5 + 2.5 * load) : p.rel;
        const double cr = js_exp(-1 / (sr * std::max(0.001, relMs / 1000)));
        gr = target + (gr - target) * cr;
      }
      load += ((gr < -1 ? 1.0 : 0.0) - load) * 0.00002;
      const double g = dbToLin(gr) * mkLin, m = p.mix;
      L[i] = f32(l * (1 - m) + l * g * m); R[i] = f32(r * (1 - m) + r * g * m);
    }
  }
 private:
  double sr_; Biq hp_[2]; BiqC hc_; bool hpOn_ = false;
};

// ---- Limiter -----------------------------------------------------------------------------------------------------------------------
class Limiter {
 public:
  struct P { double drive = 0, ceil = -1, rel = 120, look = 3; bool tp = true; } p;
  double env = 1, grMin = 1; int L = 1;
  Limiter(double sr, double maxLookMs) : sr_(sr), cap_(std::max(1, (int)js_round(sr * maxLookMs / 1000))),
      dl_((size_t)cap_), dr_((size_t)cap_), qv_((size_t)cap_ + 4), qi_((size_t)cap_ + 4), box_((size_t)cap_) { alloc(); }
  void set(const P& np) { const double old = p.look; p = np; if (p.look != old) alloc(); }
  void alloc() {
    L = (int)std::min((double)cap_, std::max(1.0, js_round(sr_ * p.look / 1000)));   // (a look-ahead longer than the storage is cut to it)
    std::fill(dl_.begin(), dl_.end(), 0.0f); std::fill(dr_.begin(), dr_.end(), 0.0f); di_ = 0;
    std::fill(qv_.begin(), qv_.end(), 0.0f); std::fill(qi_.begin(), qi_.end(), (int64_t)0); qh_ = qt_ = 0; t_ = 0;
    std::fill(box_.begin(), box_.end(), 1.0f); bs_ = L; bi_ = 0;
  }
  int latency() const { return L; }
  static double tpPeak(const float* h) {
    double m = std::max(std::fabs((double)h[1]), std::fabs((double)h[2]));
    const double h0 = h[0], h1 = h[1], h2 = h[2], h3 = h[3];
    for (int q = 1; q < 4; q++) {
      const double t = q * .25, t2 = t * t, t3 = t2 * t;
      const double v = 0.5 * ((2 * h1) + (-h0 + h2) * t + (2 * h0 - 5 * h1 + 4 * h2 - h3) * t2 + (-h0 + 3 * h1 - 3 * h2 + h3) * t3);
      if (std::fabs(v) > m) m = std::fabs(v);
    }
    return m;
  }
  void process(float* Lc, float* Rc, int n) {
    const double ceil_ = dbToLin(p.ceil), drive = dbToLin(p.drive); const int Lk = L; const int64_t cap = (int64_t)Lk + 4;
    const double cr = js_exp(-1 / (sr_ * std::max(0.001, p.rel / 1000)));
    double gmin = 1;
    for (int i = 0; i < n; i++) {
      const double l = (double)Lc[i] * drive, r = (double)Rc[i] * drive;
      double pk = std::max(std::fabs(l), std::fabs(r));
      if (p.tp) {
        float* a = h_[0]; float* b = h_[1];
        a[0] = a[1]; a[1] = a[2]; a[2] = a[3]; a[3] = f32(l); b[0] = b[1]; b[1] = b[2]; b[2] = b[3]; b[3] = f32(r);
        pk = std::max(pk, std::max(tpPeak(a), tpPeak(b)));   // (Math.max(pk, x, y))
      }
      const double need = pk > ceil_ ? ceil_ / pk : 1;
      const int64_t t = t_++;
      while (qt_ > qh_ && (double)qv_[(size_t)((qt_ - 1) % cap)] >= need) qt_--;
      qv_[(size_t)(qt_ % cap)] = f32(need); qi_[(size_t)(qt_ % cap)] = t; qt_++;
      while (qi_[(size_t)(qh_ % cap)] <= t - (Lk + 2)) qh_++;
      const double hmin = qv_[(size_t)(qh_ % cap)];
      bs_ += hmin - (double)box_[(size_t)bi_]; box_[(size_t)bi_] = f32(hmin); bi_ = (bi_ + 1) % Lk;
      const double G = std::min(1.0, bs_ / Lk);
      env = G < env ? G : G + (env - G) * cr;
      const double ol = dl_[(size_t)di_], orr = dr_[(size_t)di_]; dl_[(size_t)di_] = f32(l); dr_[(size_t)di_] = f32(r); di_ = (di_ + 1) % Lk;
      double yl = ol * env, yr = orr * env;
      if (yl > ceil_) yl = ceil_; else if (yl < -ceil_) yl = -ceil_;
      if (yr > ceil_) yr = ceil_; else if (yr < -ceil_) yr = -ceil_;
      Lc[i] = f32(yl); Rc[i] = f32(yr);
      if (env < gmin) gmin = env;
    }
    grMin = std::min(grMin, gmin);
  }
 private:
  double sr_; int cap_;
  std::vector<float> dl_, dr_, qv_; std::vector<int64_t> qi_; std::vector<float> box_;
  int di_ = 0, bi_ = 0; int64_t qh_ = 0, qt_ = 0, t_ = 0; double bs_ = 1;
  float h_[2][4] = {{0, 0, 0, 0}, {0, 0, 0, 0}};
};

// ---- Loudness (ITU-R BS.1770 K-weighting, short-term 3 s) ---------------------------------------------------------------------------
class Loudness {
 public:
  explicit Loudness(double sr) {
    const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
    const double K = std::tan(3.141592653589793 * f0 / sr), Vh = js_pow(10, G / 20), Vb = js_pow(Vh, 0.4996667741545416), a0 = 1 + K / Q + K * K;
    s1_.b0 = (Vh + Vb * K / Q + K * K) / a0; s1_.b1 = 2 * (K * K - Vh) / a0; s1_.b2 = (Vh - Vb * K / Q + K * K) / a0; s1_.a1 = 2 * (K * K - 1) / a0; s1_.a2 = (1 - K / Q + K * K) / a0;
    const double f1 = 38.13547087602444, Q1 = 0.5003270373238773, K1 = std::tan(3.141592653589793 * f1 / sr), a01 = 1 + K1 / Q1 + K1 * K1;
    s2_.b0 = 1; s2_.b1 = -2; s2_.b2 = 1; s2_.a1 = 2 * (K1 * K1 - 1) / a01; s2_.a2 = (1 - K1 / Q1 + K1 * K1) / a01;
    blk_ = (int)js_round(sr * 0.1);
  }
  void process(const float* L, const float* R, int n) {
    for (int i = 0; i < n; i++) {
      const double l = bqRun(s2_, st_[0][1], bqRun(s1_, st_[0][0], L[i]));
      const double r = bqRun(s2_, st_[1][1], bqRun(s1_, st_[1][0], R[i]));
      acc_ += l * l + r * r;
      if (++cnt_ >= blk_) block();
    }
  }
  bool quiet() const { for (int c = 0; c < 2; c++) for (int k = 0; k < 2; k++) if (!dead4(st_[c][k])) return false; return true; }
  void reset() { for (int c = 0; c < 2; c++) for (int k = 0; k < 2; k++) st_[c][k] = Biq{}; cnt_ = 0; hi_ = 0; filled_ = 0; acc_ = 0; for (double& h : hist_) h = 0; }   // scalars and fixed arrays only (audio thread)
  void skip(int n) { while (n > 0) { const int t = std::min(n, blk_ - cnt_); cnt_ += t; n -= t; if (cnt_ >= blk_) block(); } }
  double shortTerm() const {
    if (!filled_) return -INFINITY;
    double s = 0; for (int i = 0; i < filled_; i++) s += hist_[i];
    const double ms = s / filled_; return ms > 0 ? -0.691 + 10 * std::log10(ms) : -INFINITY;
  }
 private:
  void block() { hist_[hi_] = acc_ / cnt_; hi_ = (hi_ + 1) % 30; filled_ = std::min(30, filled_ + 1); acc_ = 0; cnt_ = 0; }
  BiqC s1_, s2_; Biq st_[2][2]; int blk_ = 1, cnt_ = 0, hi_ = 0, filled_ = 0; double acc_ = 0, hist_[30] = {};
};

// ---- MasterProcessor -----------------------------------------------------------------------------------------------------------------
MasterSet master_safe_limiter_set() {
  MasterSet s; s.hasLim = true; s.lim.on = true; s.lim.drive = 0; s.lim.ceil = -1; s.lim.rel = 120; s.lim.look = 3; s.lim.tp = true; return s;
}

MasterProcessor::MasterProcessor(double sampleRate, bool meters, double maxLookMs)
    : sr_(sampleRate), meters_(meters), sup_(new Suppressor(sampleRate)), comp_(new Compressor(sampleRate)), lim_(new Limiter(sampleRate, maxLookMs)),
      inLu_(new Loudness(sampleRate)), outLu_(new Loudness(sampleRate)) {}
MasterProcessor::~MasterProcessor() = default;

void MasterProcessor::set(const MasterSet& s) {
  if (s.hasSup) {
    on_sup_ = s.sup.on; Suppressor::P q = sup_->p; const SupSet& u = s.sup;
    auto take = [](MOpt v, double& d) { if (v && finite(*v)) d = *v; };
    take(u.depth, q.depth); take(u.sharp, q.sharp); take(u.sel, q.sel); take(u.atk, q.atk); take(u.rel, q.rel); take(u.mix, q.mix);
    if (u.lo && finite(*u.lo) && *u.lo > 0) q.lo = *u.lo;   // lo / hi go through log2: zero or less is kept out
    if (u.hi && finite(*u.hi) && *u.hi > 0) q.hi = *u.hi;
    if (u.delta) q.delta = *u.delta;
    sup_->set(q);
  }
  if (s.hasComp) {
    on_comp_ = s.comp.on; Compressor::P q = comp_->p; const CompSet& u = s.comp;
    auto take = [](MOpt v, double& d) { if (v && finite(*v)) d = *v; };
    take(u.thr, q.thr); take(u.atk, q.atk); take(u.rel, q.rel); take(u.knee, q.knee); take(u.makeup, q.makeup); take(u.mix, q.mix); take(u.hpf, q.hpf);
    if (u.ratio && finite(*u.ratio) && *u.ratio > 0) q.ratio = *u.ratio;
    if (u.autoRel) q.autoRel = *u.autoRel;
    if (u.autoMakeup) q.automk = *u.autoMakeup;
    comp_->set(q);
  }
  if (s.hasLim) {
    const bool was = on_lim_; on_lim_ = s.lim.on; Limiter::P q = lim_->p; const LimSet& u = s.lim;
    auto take = [](MOpt v, double& d) { if (v && finite(*v)) d = *v; };
    take(u.drive, q.drive); take(u.ceil, q.ceil); take(u.rel, q.rel); take(u.look, q.look);
    if (u.tp) q.tp = *u.tp;
    lim_->set(q);
    if (on_lim_ && !was) zr_ = 0;
  }
}

int MasterProcessor::latencySup() const { return on_sup_ ? Suppressor::N + Suppressor::D : 0; }
int MasterProcessor::latencyLim() const { return on_lim_ ? lim_->latency() : 0; }
int MasterProcessor::latency() const { return latencySup() + latencyLim(); }

bool MasterProcessor::popMeter(MasterMeter* out) {
  if (qn_ == 0) return false;
  *out = q_[qh_]; qh_ = (qh_ + 1) % kMeterQueue; qn_--; return true;
}

void MasterProcessor::setMeters(bool on) {
  if (on == meters_) return;
  meters_ = on;
  qh_ = 0; qn_ = 0; inPk_ = 0; outPk_ = 0; tpMax_ = 0; compGr_ = 0; nq_ = 0; lim_->grMin = 1; comp_->detMax = -200;   // (an edge either way: nothing of the other state stays to be read)
  if (on) { inLu_->reset(); outLu_->reset(); }   // lead's read (M2 Q1): the loudness window starts empty at `on` (it was not run while off: no 3 s of an older part of the song). Only the meters and the silence shortcut's quiet() read it; the audio does not
}

bool MasterProcessor::process(float* L, float* R, int frames) {
  if (dead_) { std::memset(L, 0, sizeof(float) * (size_t)std::max(frames, 0)); std::memset(R, 0, sizeof(float) * (size_t)std::max(frames, 0)); return false; }
  for (int o = 0; o < frames; o += kQuantum) quantum(L + o, R + o, std::min(kQuantum, frames - o));
  return true;
}

bool MasterProcessor::quantum(float* L, float* R, int n) {
  using clock = std::chrono::steady_clock;
  const auto t0 = clock::now();
  bool sil = false;
  if (zr_ >= lim_->L + 8 && !on_sup_ && (!on_lim_ || lim_->env > 1 - 1e-9) && (!on_comp_ || comp_->quiet()) && (!meters_ || (inLu_->quiet() && outLu_->quiet()))) {
    sil = true; for (int i = 0; i < n; i++) if (L[i] != 0 || R[i] != 0) { sil = false; break; }
  }
  double ip = 0, op = 0;
  if (sil) {
    sk_++; std::memset(L, 0, sizeof(float) * (size_t)n); std::memset(R, 0, sizeof(float) * (size_t)n); zr_ += n;
    if (on_comp_) comp_->load *= js_pow(1 - 0.00002, n);
    if (meters_) { inLu_->skip(n); outLu_->skip(n); }
  } else {
    for (int i = 0; i < n; i++) {
      double x = L[i], y = R[i];
      if (!(x > -64 && x < 64)) { L[i] = 0; x = 0; }
      if (!(y > -64 && y < 64)) { R[i] = 0; y = 0; }
      const double v = std::max(std::fabs(x), std::fabs(y)); if (v > ip) ip = v;
    }
    if (meters_) inLu_->process(L, R, n);
    if (on_sup_) sup_->process(L, R, n);
    if (on_comp_) comp_->process(L, R, n);
    if (on_lim_) lim_->process(L, R, n);
    for (int i = 0; i < n; i++) { const double v = std::max(std::fabs((double)L[i]), std::fabs((double)R[i])); if (v > op) op = v; }
    zr_ = ip == 0 ? zr_ + n : 0;
  }
  ms_ += std::chrono::duration<double, std::milli>(clock::now() - t0).count(); fr_ += n;
  if (fr_ >= sr_ / 2) { load_ = ms_ / (fr_ / sr_ * 1000); ms_ = 0; fr_ = 0; }
  if (meters_) {
    if (!sil) outLu_->process(L, R, n);
    inPk_ = std::max(inPk_, ip); outPk_ = std::max(outPk_, op); tpMax_ = std::max(tpMax_, op);
    compGr_ = std::min(compGr_, on_comp_ ? comp_->gr : 0.0);
    nq_ += n / 128.0;
    if (nq_ >= js_round(sr_ / 128 / 20)) {
      nq_ = 0;
      MasterMeter m; m.inPk = inPk_; m.outPk = outPk_; m.tp = tpMax_; m.inLu = inLu_->shortTerm(); m.outLu = outLu_->shortTerm(); m.load = load_;
      m.comp = compGr_; m.compIn = on_comp_ ? comp_->detMax : -200; m.lim = on_lim_ ? linToDb(lim_->grMin) : 0;
      m.hasSup = on_sup_; if (on_sup_) sup_->bands(kSupBands, m.sup);
      if (qn_ == kMeterQueue) { qh_ = (qh_ + 1) % kMeterQueue; qn_--; mdrop_++; }
      q_[(qh_ + qn_) % kMeterQueue] = m; qn_++;
      inPk_ = 0; outPk_ = 0; compGr_ = 0; lim_->grMin = 1; comp_->detMax = -200;
    }
  }
  return true;
}

}  // namespace thunder
