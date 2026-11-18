// U31: Morph, Resample and the 13 pump modules of chains/chains-worklet.js, line for line.
//  - Morph: two stereo sounds combined frame by frame in the frequency domain (FFTR of U20 does the transforms; its tables are made in the constructor).
//  - Resample: records Bars bars of the playing song and hands the take to the host. The JS allocates the take's buffers when the recording starts and posts it with onResample; here the two
//    buffer pairs are made with the module (32 s at most, per pair) and a take is posted to the engine's queue (the control thread reads it with popTake / releaseTake). If both pairs are
//    still lent to the host when a recording would start, it does not start (the JS has no such limit: named choice).
//  - Pump modules (PUMP_CH, written into cpump.inc by tools/chains-gen.mjs): each runs its plugin's code in pump through the pump_* C entry points. THE SPARE (decision (e) of U18, the lead's text:
//    "spare instances in U31"): the JS makes the instance at the first block (pump_new on the audio thread) and again after a block whose output went bad (pump_free + pump_new on the audio thread).
//    Here the instance is made when the patch is prepared (prepare(): the control thread, with the values the patch sets, as the JS stages them at the first block), and a SPARE made the same way is kept;
//    when the output goes bad the audio thread swaps the spare in (no allocation, no lock: an atomic exchange) and the control thread (service()) frees the dead one and makes the next spare. Where
//    the values equal the ones the spare was made with, the module is what the JS builds at its next block; where they differ the spare is brought up to date by pump_values + pump_commit
//    (the instance is not what the JS would have made, only for the block after a failure, which is the web's own recovery path). With no spare ready the block is silent and the swap happens when
//    one is.
#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>

#include "chains/chains.h"
#include "fx/pumpfx.h"
#include "modfx/fftr.h"
#include "modfx/util.h"
#include "waves/graph.h"

namespace thunder {
namespace chains {

using namespace modfx;
using waves::WModule;
namespace {
inline const CCtx& CC(const Ctx& c) { return static_cast<const CCtx&>(c); }
inline void zeroOut(float* o) { for (int i = 0; i < kBlock; i++) o[i] = 0.f; }

// ---- Morph ------------------------------------------------------------------------------------------------------------------------------------------------------
const int kMorphSizes[3] = {512, 1024, 2048};
struct MorphChan {
  F32 fa{2048}, fb{2048}, acc{2048}, oq{512}, x{2048}, ar{1025}, ai{1025}, br{1025}, bi{1025}, yr{1025}, yi{1025}, ma{1025}, mb{1025}, pa{1025}, pb{1025}, da{1025}, db{1025}, qa{1025}, qb{1025}, ta{1025}, tb{1025}, ea{1026}, eb{1026};
  double g = 1; int has = 0; double gm = -1e300; bool gmSet = false;   // gm: c.gm (undefined until the first Convolve / Shape frame)
};
class Morph : public WModule {
 public:
  Morph(double, Seeds&) : job_(2048) { for (int i = 0; i < 3; i++) { fft_[i] = FFTR::get(kMorphSizes[i]); win_[i] = FFTR::hann(kMorphSizes[i]); } }
  void process(const IO& io, const Ctx& c) override {
    const double* p = io.p; const int fi = (int)clamp((double)i32(p[4]), 0, 2); const int N = kMorphSizes[fi], H = N >> 2; MorphChan& c0 = c_[0]; MorphChan& c1 = c_[1];
    if (N_ != N) {
      N_ = N; W_ = win_[fi]; fft_cur_ = fft_[fi]; cl_ = 0; cr_ = H >> 1;
      for (MorphChan& ch : c_) {
        ch.fa.fill(0); ch.fb.fill(0); ch.acc.fill(0); ch.oq.fill(0); ch.ma.fill(0); ch.mb.fill(0); ch.pa.fill(1); ch.pb.fill(1); ch.da.fill(1); ch.db.fill(1); ch.ta.fill(0); ch.tb.fill(0); ch.qa.fill(0); ch.qb.fill(0); ch.g = 1; ch.has = 0;
      }
    }
    const float* A = io.in[0]; const float* Bn = io.in[2]; const bool sA = io.linked[1], sB = io.linked[3], stereo = sA || sB;
    const float* AR = sA ? io.in[1] : A; const float* BR = sB ? io.in[3] : Bn; float* oL = io.out[0]; float* oR = io.out[1]; const double lv = dbLin(clamp(p[6], -24, 24));
    const int mode = p[0] >= .5 ? 1 : 0; const double mo = clamp(p[1], 0, 1), sh = clamp(p[2], 0, 1); const int fz = (int)clamp((double)i32(p[5]), 0, 2);
    const double ks = p[3] < .005 ? 1 : 1 - js_exp(-H / (c.sr * (.004 + p[3] * p[3] * .6)));
    int cl = cl_, cr = cr_;
    for (int i = 0; i < c.block; i++) {
      const double a = A[i], b = Bn[i];
      c0.fa[N - H + cl] = a > -8 && a < 8 ? a : 0; c0.fb[N - H + cl] = b > -8 && b < 8 ? b : 0;
      double y = (double)c0.oq[cl] * lv; if (!(y > -1 && y < 1)) { if (y != y) y = 0; else if (y < 0) y = -1 - js_tanh(-y - 1); else y = 1 + js_tanh(y - 1); }
      oL[i] = (float)y;
      if (++cl == H) { cl = 0; frame(c0, N, mode, mo, sh, ks, fz); }
      if (stereo) {
        const double ar = AR[i], br = BR[i];
        c1.fa[N - H + cr] = ar > -8 && ar < 8 ? ar : 0; c1.fb[N - H + cr] = br > -8 && br < 8 ? br : 0;
        double z = (double)c1.oq[cr] * lv; if (!(z > -1 && z < 1)) { if (z != z) z = 0; else if (z < 0) z = -1 - js_tanh(-z - 1); else z = 1 + js_tanh(z - 1); }
        oR[i] = (float)z;
        if (++cr == H) { cr = 0; frame(c1, N, mode, mo, sh, ks, fz); }
      } else oR[i] = (float)y;
    }
    cl_ = cl; cr_ = cr;
  }
 private:
  void frame(MorphChan& c, int N, int mode, double mo, double sh, double ks, int fz) {
    const int H = N >> 2, K = (N >> 1) + 1; const float* W = W_; F32& x = c.x; F32& ar = c.ar; F32& ai = c.ai; F32& br = c.br; F32& bi = c.bi; F32& yr = c.yr; F32& yi = c.yi;
    F32& ma = c.ma; F32& mb = c.mb; F32& ua = c.pa; F32& va = c.qa; F32& ra = c.da; F32& sa = c.ta; F32& ub = c.pb; F32& vb = c.qb; F32& rb = c.db; F32& sb = c.tb;
    for (int i = 0; i < N; i++) x[i] = (double)c.fa[i] * W[i];
    fft_cur_->forward(job_, x.get(), ar.get(), ai.get());
    for (int i = 0; i < N; i++) x[i] = (double)c.fb[i] * W[i];
    fft_cur_->forward(job_, x.get(), br.get(), bi.get());
    std::memmove(c.fa.get(), c.fa.get() + H, sizeof(float) * (size_t)(N - H)); std::memmove(c.fb.get(), c.fb.get() + H, sizeof(float) * (size_t)(N - H));
    const bool first = !c.has, holdA = fz == 1 && !first, holdB = fz == 2 && !first; c.has = 1;
    double pkA = 0, pkB = 0, EA = 0, EB = 0;
    for (int k = 0; k < K; k++) {
      if (holdA) { const double u = (double)ua[k] * ra[k] - (double)va[k] * sa[k], v = (double)ua[k] * sa[k] + (double)va[k] * ra[k], n = u * u + v * v, q = n > 1e-30 ? 1 / std::sqrt(n) : 0; ua[k] = u * q; va[k] = v * q; }
      else {
        const double re = ar[k], im = ai[k], a = std::sqrt(re * re + im * im), q = a > 1e-20 ? 1 / a : 0, u = q ? re * q : 1, v = im * q;
        ra[k] = u * (double)ua[k] + v * (double)va[k]; sa[k] = v * (double)ua[k] - u * (double)va[k]; ua[k] = u; va[k] = v;
        double m = (double)ma[k] + (a - (double)ma[k]) * ks; if (m < 1e-15) m = 0; ma[k] = m;
      }
      if (holdB) { const double u = (double)ub[k] * rb[k] - (double)vb[k] * sb[k], v = (double)ub[k] * sb[k] + (double)vb[k] * rb[k], n = u * u + v * v, q = n > 1e-30 ? 1 / std::sqrt(n) : 0; ub[k] = u * q; vb[k] = v * q; }
      else {
        const double re = br[k], im = bi[k], b = std::sqrt(re * re + im * im), q = b > 1e-20 ? 1 / b : 0, u = q ? re * q : 1, v = im * q;
        rb[k] = u * (double)ub[k] + v * (double)vb[k]; sb[k] = v * (double)ub[k] - u * (double)vb[k]; ub[k] = u; vb[k] = v;
        double m = (double)mb[k] + (b - (double)mb[k]) * ks; if (m < 1e-15) m = 0; mb[k] = m;
      }
      const double A2 = ma[k], B2 = mb[k]; if (A2 > pkA) pkA = A2; if (B2 > pkB) pkB = B2; EA += A2 * A2; EB += B2 * B2;
    }
    const double wA = mo < .5 ? 1 - mo * 2 : 0, wB = mo > .5 ? mo * 2 - 1 : 0, wC = 1 - wA - wB;
    double g = 0;
    if (wC > 0) {
      if (mode == 0) {
        const double e = .5 + sh, ref = pkA * pkB;
        if (!(ref > 1e-30)) for (int k = 0; k < K; k++) yr[k] = 0;
        else if (e > .999 && e < 1.001) { const double nrm = 1 / std::sqrt(ref); for (int k = 0; k < K; k++) yr[k] = (double)ma[k] * mb[k] * nrm; }
        else { const double nrm = js_pow(ref, .5 - e); for (int k = 0; k < K; k++) { const double m = (double)ma[k] * mb[k]; yr[k] = m > 1e-30 ? js_pow(m, e) * nrm : 0; } }
        g = 1;
      } else {
        const int hw = (int)jmax(1, js_round((2 + (1 - sh) * 30) * N / 1024)); F32& ea = c.ea; F32& eb = c.eb;
        ea[0] = 0; eb[0] = 0; for (int k = 0; k < K; k++) { ea[k + 1] = (double)ea[k] + ma[k]; eb[k + 1] = (double)eb[k] + mb[k]; }
        double Er = 0; const bool useB = EB > 1e-18; const double fla = (double)ea[K] / K * .02 + 1e-12, flb = (double)eb[K] / K * .02;
        for (int k = 0; k < K; k++) {
          const int lo = k - hw < 0 ? 0 : k - hw, hi = k + hw + 1 > K ? K : k + hw + 1, n = hi - lo; const double oa = ((double)ea[hi] - ea[lo]) / n, ob = ((double)eb[hi] - eb[lo]) / n;
          const double v = useB ? (double)ma[k] / (oa + fla) * (ob + flb) : (double)ma[k]; yr[k] = v; Er += (double)yr[k] * yr[k];
        }
        g = Er > 1e-30 ? jmin(32, std::sqrt(EA / Er)) : 0;
      }
      if (!c.gmSet || c.gm != mode) { c.gm = mode; c.gmSet = true; c.g = g; }
      c.g += (g - c.g) * .5; if (!(c.g <= 32)) c.g = c.g > 32 ? 32 : 0; g = c.g * wC;
    }
    for (int k = 0; k < K; k++) {
      double re = 0, im = 0;
      if (wC > 0) { const double m = (double)yr[k] * g; re = m * ua[k]; im = m * va[k]; }
      if (wA > 0) { if (holdA) { const double m = (double)ma[k] * wA; re += m * ua[k]; im += m * va[k]; } else { re += (double)ar[k] * wA; im += (double)ai[k] * wA; } }
      if (wB > 0) { if (holdB) { const double m = (double)mb[k] * wB; re += m * ub[k]; im += m * vb[k]; } else { re += (double)br[k] * wB; im += (double)bi[k] * wB; } }
      yr[k] = re; yi[k] = im;
    }
    fft_cur_->inverse(job_, yr.get(), yi.get(), x.get());
    F32& acc = c.acc; F32& oq = c.oq; const double OLA = 1 / 1.5;
    for (int i = 0; i < N; i++) { const double v = (double)acc[i] + (double)x[i] * W[i] * OLA; acc[i] = v == v && v > -1e6 && v < 1e6 ? v : 0; }
    for (int i = 0; i < H; i++) oq[i] = (double)acc[i];
    std::memmove(acc.get(), acc.get() + H, sizeof(float) * (size_t)(N - H)); for (int i = N - H; i < N; i++) acc[i] = 0;
  }
  MorphChan c_[2]; FFTR::Job job_; const FFTR* fft_[3]; const float* win_[3]; const FFTR* fft_cur_ = nullptr; const float* W_ = nullptr; int N_ = 0, cl_ = 0, cr_ = 0;
};

// ---- Resample -------------------------------------------------------------------------------------------------------------------------------------------------
const int kRsBars[4] = {1, 2, 4, 8}, kRsSlices[5] = {0, 4, 8, 16, 32};
class Resample : public WModule {
 public:
  Resample(double sr, Seeds&) : sr_(sr), cap_((int)jmin(sr * 32, 1 << 26)) { for (int k = 0; k < 2; k++) { L_[k].init(cap_); R_[k].init(cap_); } }
  void process(const IO& io, const Ctx& c) override {
    const CCtx& x = CC(c); const int B = c.block; const float* inL = io.in[0]; const float* inR = io.linked[1] ? io.in[1] : io.in[0];
    std::memcpy(io.out[0], inL, sizeof(float) * (size_t)B); std::memcpy(io.out[1], inR, sizeof(float) * (size_t)B);
    if (restarted_(c)) t_ = 0;
    const int rec = i32(io.p[0]);
    if (!rec) { arm_ = false; done_ = false; cur_ = -1; n_ = 0; }
    else if (!arm_ && cur_ < 0 && !done_) arm_ = true;
    if (!c.playing) { if (cur_ >= 0) { cur_ = -1; n_ = 0; arm_ = rec ? true : false; } return; }
    const double barLen = jmax(1, js_round(4 * 60 / jmax(20, c.tempo) * c.sr));
    int from = 0;
    if (arm_) {
      const double nb = std::ceil(t_ / barLen) * barLen, off = nb - t_;
      if (off < B) {
        int free = -1; for (int k = 0; k < 2; k++) if (!lent_[k]) { free = k; break; }
        if (free >= 0) {
          arm_ = false; len_ = (int)jmin(c.sr * 32, barLen * kRsBars[(int)clamp((double)i32(io.p[1]), 0, 3)]); if (len_ > cap_) len_ = cap_;
          cur_ = free; std::memset(L_[cur_].get(), 0, sizeof(float) * (size_t)len_); std::memset(R_[cur_].get(), 0, sizeof(float) * (size_t)len_); n_ = 0; from = (int)off;
        }
      }
    }
    if (cur_ >= 0) {
      const int k = (int)jmin(B - from, len_ - n_);
      if (k > 0) { std::memcpy(L_[cur_].get() + n_, inL + from, sizeof(float) * (size_t)k); std::memcpy(R_[cur_].get() + n_, inR + from, sizeof(float) * (size_t)k); n_ += k; }
      if (n_ >= len_) {
        if (x.onTake) {
          ResampleTake t; std::memset(&t, 0, sizeof t); std::snprintf(t.id, sizeof t.id, "%s", x.curId); t.L = L_[cur_].get(); t.R = R_[cur_].get(); t.n = len_; t.sr = c.sr;
          t.bars = kRsBars[(int)clamp((double)i32(io.p[1]), 0, 3)]; t.tempo = c.tempo; t.pad = i32(io.p[2]); t.slices = kRsSlices[(int)clamp((double)i32(io.p[3]), 0, 4)]; t.owner = this; t.which = cur_;
          lent_[cur_] = true; x.onTake(x.takeUser, t);
        }
        cur_ = -1; n_ = 0; done_ = true;
      }
    }
    t_ += B;
  }
  void release(int which) { if (which >= 0 && which < 2) lent_[which] = false; }
 private:
  double sr_; int cap_; F32 L_[2], R_[2]; bool lent_[2] = {false, false}; int cur_ = -1, n_ = 0, len_ = 0; bool arm_ = false, done_ = false; double t_ = 0; Restart restarted_;
};

// ---- the pump modules -------------------------------------------------------------------------------------------------------------------------------------------
struct PumpQ { const char* name; int idx; char kind; double mn, mx; char curve; double exp; double def, step; };
struct PumpCh { const char* key; int t, np, nps; const PumpQ* ps; };
#include "chains/cpump.inc"

double pumpCurve(const PumpQ& q, double v) { v = v < 0 ? 0 : v > 1 ? 1 : v; return q.curve == 'g' ? q.mn * js_pow(q.mx / q.mn, v) : q.mn + (q.mx - q.mn) * js_pow(v, q.exp > 0 ? q.exp : 1); }
double pumpValue(const PumpQ& q, double raw) { const double v = raw; if (!(v == v)) return q.def; return q.kind == 'n' ? pumpCurve(q, v) : v; }

std::atomic<uint64_t> g_pumpSwaps{0}, g_pumpBad{0}, g_pumpDry{0};   // spares taken, blocks that went bad, blocks with no spare ready (counted for the tests and the notes)
class PumpMod : public WModule {
 public:
  PumpMod(const PumpCh* d, double sr) : d_(d), sr_(sr) { for (int i = 0; i < 128; i++) last_[i] = NAN; }
  ~PumpMod() override { pump_free_ctl(h_); pump_free_ctl(spare_.load()); for (auto& d : dead_) pump_free_ctl(d.load()); }
  void prepare(const double* p) override {
    if (!pump::available()) return;
    float pv[512]; for (float& q : pv) q = std::nanf("");   // unwritten = the parameter's default (pump_new: NaN in the stage)
    for (int i = 0; i < d_->nps; i++) { pv[d_->ps[i].idx] = (float)pumpValue(d_->ps[i], p[i]); stagedRaw_[i] = p[i]; }
    h_ = pump_make_staged(d_->t, pv, d_->np, sr_); if (h_ < 0) dead_state_ = true;
    for (int i = 0; i < d_->nps; i++) last_[i] = p[i];   // the JS's st.last is set when it stages the first block
    makeSpare(p);
  }
  void service() override {
    for (auto& d : dead_) { const int dd = d.exchange(-1); if (dd >= 0) pump_free_ctl(dd); }
    if (spare_.load() < 0 && wantSpare_.load()) { double p[128]; for (int i = 0; i < d_->nps; i++) p[i] = rawNow_[i].load(std::memory_order_relaxed); makeSpare(p); }
  }
  void process(const IO& io, const Ctx& c) override;
 private:
  void makeSpare(const double* p) {
    if (!pump::available() || dead_state_) return;
    float pv[512]; for (float& q : pv) q = std::nanf("");   // unwritten = the parameter's default (pump_new: NaN in the stage)
    for (int i = 0; i < d_->nps; i++) { pv[d_->ps[i].idx] = (float)pumpValue(d_->ps[i], p[i]); spareRaw_[i] = p[i]; rawNow_[i].store(p[i], std::memory_order_relaxed); }
    const int s = pump_make_staged(d_->t, pv, d_->np, sr_); spare_.store(s); wantSpare_.store(false);
  }
  const PumpCh* d_; double sr_; int h_ = -1; bool dead_state_ = false; double last_[128]; double stagedRaw_[128] = {}; double spareRaw_[128] = {}; std::atomic<double> rawNow_[128];
  std::atomic<int> spare_{-1}; std::atomic<int> dead_[4] = {{-1}, {-1}, {-1}, {-1}}; std::atomic<bool> wantSpare_{false}; double pos_ = 0, sn_ = -1; bool haveSn_ = false;
  friend class PumpModProc;
};
void PumpMod::process(const IO& io, const Ctx& c) {
  const float* L0 = io.in[0]; const float* R0 = io.linked[1] ? io.in[1] : io.in[0]; const int n = c.block;
  auto pass = [&]() { std::memcpy(io.out[0], L0, sizeof(float) * kBlock); std::memcpy(io.out[1], R0, sizeof(float) * kBlock); };
  if (!pump::available() || dead_state_ || h_ < 0) { if (h_ < 0 && !dead_state_ && pump::available()) { int s = spare_.exchange(-1); if (s >= 0) { g_pumpSwaps++; h_ = s; for (int i = 0; i < d_->nps; i++) last_[i] = spareRaw_[i]; wantSpare_.store(true); } } if (h_ < 0) { if (dead_state_ || !pump::available()) pass(); else { g_pumpDry++; zeroOut(io.out[0]), zeroOut(io.out[1]); } return; } }
  PumpGate gate(true); if (!gate.ok) { pass(); return; }   // the control thread is in pump (making a spare): this block passes the sound, nothing waits
  { const int np = pump::nparams(d_->t); float* V = nullptr;
    for (int i = 0; i < d_->nps; i++) { const PumpQ& q = d_->ps[i]; const double raw = io.p[i]; rawNow_[i].store(raw, std::memory_order_relaxed);
      if (raw != last_[i]) { last_[i] = raw; if (!V) V = pump::values(h_); if (q.idx < np) V[q.idx] = (float)pumpValue(q, raw); } }
    if (V) pump::commit(h_); }
  const double bpm = c.tempo > 0 ? c.tempo : 120;
  if (!c.playing || !(haveSn_ && sn_ == c.startN)) { sn_ = c.startN; haveSn_ = true; pos_ = 0; }
  pump::transport(h_, bpm, pos_ * bpm / 60 / c.sr, c.playing ? 1 : 0);
  if (c.playing) pos_ += n;
  float* L = pump::io(0); float* R = pump::io(1);
  for (int i = 0; i < n; i++) { const double a = L0[i], b = R0[i], dn = (i & 1) ? 1e-18 : -1e-18; L[i] = (float)((a > -8 && a < 8 ? a : 0) + dn); R[i] = (float)((b > -8 && b < 8 ? b : 0) + dn); }
  pump::process(h_, n, 0);
  bool bad = false; for (int i = 0; i < n; i++) { const float a = L[i], b = R[i]; if (!(a > -8 && a < 8 && b > -8 && b < 8)) { bad = true; break; } }
  if (bad) { g_pumpBad++;   // gone bad: afresh next block. The JS frees and makes the instance on the audio thread; here the dead one goes to the control thread and the spare takes over.
    const int old = h_; h_ = -1; { bool put = false; for (auto& d : dead_) { int e = -1; if (d.compare_exchange_strong(e, old)) { put = true; break; } } (void)put; /* (four failures between two services: the handle is kept by nobody; it is freed with the process) */ }
    zeroOut(io.out[0]); zeroOut(io.out[1]); wantSpare_.store(true); return;
  }
  std::memcpy(io.out[0], L, sizeof(float) * (size_t)n); std::memcpy(io.out[1], R, sizeof(float) * (size_t)n);
}
}  // namespace

uint64_t chains_pump_swaps() { return g_pumpSwaps.load(); }
uint64_t chains_pump_bad() { return g_pumpBad.load(); }
uint64_t chains_pump_dry() { return g_pumpDry.load(); }
void resample_release(const ResampleTake& t) { if (t.owner) static_cast<Resample*>(t.owner)->release(t.which); }
std::unique_ptr<Module> makeMorph(double sr, Seeds& s) { return std::unique_ptr<Module>(new Morph(sr, s)); }
std::unique_ptr<Module> makeResample(double sr, Seeds& s) { return std::unique_ptr<Module>(new Resample(sr, s)); }
template <int I> std::unique_ptr<Module> mkPump(double sr, Seeds&) { return std::unique_ptr<Module>(new PumpMod(&kPumpCh[I], sr)); }
typedef std::unique_ptr<Module> (*Fac)(double, Seeds&);
const Fac kFac[13] = {mkPump<0>, mkPump<1>, mkPump<2>, mkPump<3>, mkPump<4>, mkPump<5>, mkPump<6>, mkPump<7>, mkPump<8>, mkPump<9>, mkPump<10>, mkPump<11>, mkPump<12>};
extern const char* const kPumpTypes[] = {"ptube", "psnap", "pgate", "pecho", "pspring", "pverb", "pgrain", "pvox", "ptune", "pcrush", "pswirl", "pstutter", "ptwang"};
extern const int kPumpTypeN = 13;
const Def* pumpDef(std::string_view type) {
  static std::vector<Def> defs = [] {
    std::vector<Def> r; r.reserve(13);
    for (int i = 0; i < kPumpChN && i < 13; i++) { const WInfo* w = cinfo(kPumpCh[i].key); if (w) r.push_back(Def{kPumpCh[i].key, w->nin, w->in, w->nout, w->out, w->np, w->params, kFac[i]}); }
    return r;
  }();
  for (const Def& d : defs) if (type == d.type) return &d;
  return nullptr;
}
}  // namespace chains
}  // namespace thunder
