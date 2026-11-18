// MODFX modules, part 2 (chains-worklet.js 2079 to 2411): Harmonizer, Blip drum (and its BlipVoice), Life, Resonator (ResonCore) and Degrade (DegradeCore).
// ResonCore and DegradeCore are the cores the pads' engine and the track-effect worklet carry too (U17 ports them for the track-effect host); the copy here is the module's own, made from the
// ...chains text, so this unit stands alone; a later unit can make the two share one.
#include <deque>
#include <string>
#include <vector>

#include "modfx/mods.h"
#include "modfx/util.h"

namespace thunder {
namespace modfx {
namespace {

#define PD(a) (int)(sizeof(a) / sizeof(a[0])), a
const PortDef kStIn[] = {{"in", "audio"}, {"inR", "audio"}}, kStOut[] = {{"out", "audio"}, {"outR", "audio"}};
inline double u01(double v) { return clamp(v, 0, 1); }   // const u = v => clamp(v, 0, 1)
const double kDivBeats[10] = {.0625, .125, 1.0 / 6, .25, 1.0 / 3, .5, 1, 2, 4, 8};
inline double divBeats(double v) { return kDivBeats[(int)clamp((double)i32(v), 0, 9)]; }

// ---- Harmonizer ----------------------------------------------------------------------------------------------------------------------
const ParamDef kHarmP[] = {{"v1", 9, 0, 15, 1}, {"v2", 11, 0, 15, 1}, {"lv1", .7, 0, 1, .01}, {"lv2", 0, 0, 1, .01}, {"pan1", -.4, -1, 1, .01}, {"pan2", .4, -1, 1, .01}, {"detune", 0, 0, 30, .1},
  {"delay", 0, 0, .15, .001}, {"win", .5, 0, 1, .01}, {"dry", 1, 0, 1, .01}};
const double kHarmInt[16] = {-24, -19, -12, -7, -5, -4, -3, 0, 3, 4, 5, 7, 9, 12, 19, 24};
class Harmonizer : public Module {
 public:
  explicit Harmonizer(double sr, Seeds&) : b_((int)std::ceil(sr * .5) + 4) {}
  void process(const IO& io, const Ctx& ctx) override {
    const double* p = io.p; const double sr = ctx.sr; const In L = inp(io, 0), R = inR(io, 0, 1); const Out oL = outp(io, 0), oR = outp(io, 1); const int N = b_.n;
    const double W = (.02 + p[8] * .08) * sr, pre = p[7] * sr + 2, dt = p[6] / 1200, F = jmax(64, jmin(W / 3, .012 * sr)), PER = js_round(.02 * sr);
    double vs[2][3];
    harmVoice(vs[0], kHarmInt[(int)clamp((double)i32(p[0]), 0, 15)] / 12 + dt, p[2], p[4]); harmVoice(vs[1], kHarmInt[(int)clamp((double)i32(p[1]), 0, 15)] / 12 - dt, p[3], p[5]);
    for (int i = 0; i < ctx.block; i++) {
      b_[w_] = (L[i] + R[i]) * .5;
      double yl = L[i] * p[9], yr = R[i] * p[9];
      for (int k = 0; k < 2; k++) {
        const double ratio = vs[k][0], gl = vs[k][1], gr = vs[k][2]; Voice& v = v_[k];
        if (gl == 0 && gr == 0) { v.d = -1; continue; }
        const double lo = pre + 2 + F * jmax(0, ratio - 1), hi = pre + W + PER;
        if (v.d < 0) { v.d = (lo + hi) * .5; v.f = 1; }
        v.d += 1 - ratio; if (v.f < 1) v.o += 1 - ratio;
        if (v.f >= 1) {
          if (ratio > 1 && v.d < lo) { v.o = v.d; v.d = harmBest(N, W, PER, v.d, 1); v.f = 0; }
          else if (ratio < 1 && v.d > hi) { v.o = v.d; v.d = harmBest(N, W, PER, v.d, -1); v.f = 0; }
        }
        double s = ringRd(N, v.d);
        if (v.f < 1) { const double q = v.f * kPi / 2; s = s * js_sin(q) + ringRd(N, v.o) * js_cos(q); v.f += 1 / F; }
        yl += s * gl; yr += s * gr;
      }
      oL[i] = yl; oR[i] = yr;
      w_ = w_ + 1 == N ? 0 : w_ + 1;
    }
  }
 private:
  struct Voice { double d = -1, o = 0, f = 1; };
  static void harmVoice(double* o, double oct, double lv, double pan) { const double a = (clamp(pan, -1, 1) + 1) * kPi / 4; o[0] = js_pow(2, oct); o[1] = lv * js_cos(a) * kSqrt2; o[2] = lv * js_sin(a) * kSqrt2; }
  double ringRd(int N, double d) const {
    const double r = w_ - d, ri = std::floor(r), fr = r - ri;
    const double a = rdf(b_.d.get(), N, jmod(jmod(ri, N) + N, N)), b = rdf(b_.d.get(), N, jmod(jmod(ri + 1, N) + N, N));
    return a + (b - a) * fr;
  }
  double harmAt(int N, double d) const { double x = w_ - (double)i32(d + .5); if (x < 0) x += N; return rdf(b_.d.get(), N, x); }
  double harmScore(int N, double d, double dn, int step) const { double c = 0, e = 1e-9; for (int j = 0; j < 384; j += step) { const double y = harmAt(N, dn + j); c += harmAt(N, d + j) * y; e += y * y; } return c / std::sqrt(e); }
  double harmBest(int N, double W, double PER, double d, double dir) const {
    const double L0 = js_round(W * .5); double bl = L0, bv = -INFINITY;
    for (double L = L0; L < L0 + PER; L += 6) { const double v = harmScore(N, d, d + dir * L, 12); if (v > bv) { bv = v; bl = L; } }
    const double c0 = bl; bv = -INFINITY;
    // eleven candidates c0-5 .. c0+5, counted: the JS's `for (L = c0 - 5; L <= c0 + 5; L++)` never ends when c0 is infinite (a window parameter of -Infinity) or so large that L + 1 == L; the same numbers where it ends
    for (int k = -5; k <= 5; k++) { const double L = c0 + k; const double v = harmScore(N, d, d + dir * L, 3); if (v > bv) { bv = v; bl = L; } }
    return d + dir * bl;
  }
  F32 b_; int w_ = 0; Voice v_[2];
};

// ---- BlipVoice -----------------------------------------------------------------------------------------------------------------------
struct BlipP { double pitch = 180, kick = .4, kickT = 35, fm = 0, fmR = 1.5, ring = 0, ringF = 300, noise = 0, click = .2, atk = .5, decay = 220, drive = 0, filt = 0, res = .2, grain = 0, grainT = 60, vel = 1; };
class BlipVoice {
 public:
  explicit BlipVoice(double sr) : sr_(sr) { reset(); }
  void reset() { on = false; t = 0; ph = 0; mph = 0; rph = 0; lp = 0; bp = 0; rnd = 7777; g = 0; rep = 0; rg = 1; }
  void set(const BlipP& o) { p = o; }
  void trig(double vel) { on = true; t = 0; ph = 0; mph = 0; rep = 0; rg = vel; v0 = rg; }
  void render(F32& out, int a, int b, double gain) {
    if (!on) return;
    const BlipP& q0 = p; const double sr = sr_, inv = 1 / sr, kd = js_exp(-inv * 1000 / jmax(1, q0.kickT)), ad = js_exp(-inv * 1000 * 6.9 / jmax(2, q0.decay));
    const double atk = jmax(1, q0.atk / 1000 * sr), fmI = q0.fm * 8, dr = q0.drive * 8, dn = truthy(dr) ? 1 / js_tanh(dr) : 1;
    const double filt = jmax(-1, jmin(1, q0.filt)), fc = filt < 0 ? 20000 * js_pow(0.004, -filt) : 20 * js_pow(400, filt);
    const double q = 2 - 1.95 * jmax(0, jmin(1, q0.res)), gT = jmax(5, q0.grainT) / 1000 * sr, rc = std::sqrt(q / 2);
    const double f = jmin(2 * js_sin(kPi * jmin(0.45, fc / sr)), 0.98 * (std::sqrt(q * q + 4) - q));
    for (int i = a; i < b; i++) {
      const double tt = t;
      if (q0.grain >= 1 && rep < (double)i32(q0.grain) && tt >= gT * (rep + 1)) { rep++; rg = v0 * js_pow(0.72, rep); ke = 1; env = 0; ph = 0; }
      if (tt == 0) { ke = 1; env = 0; }
      const double tl = tt - rep * gT;
      env = tl < atk ? tl / atk : env * ad;
      ke *= kd;
      const double kk = q0.kick * 5 * ke, hz = kk > 1e-4 ? q0.pitch * js_exp(kk * 0.6931471805599453) : q0.pitch;
      ph += hz * inv; if (ph >= 1) ph -= std::floor(ph);
      double pm = ph;
      if (fmI > 0) { mph += hz * q0.fmR * inv; if (mph >= 1) mph -= std::floor(mph); pm += fmI * js_sin(2 * kPi * mph) * env * 0.159; }
      const double x0 = pm - std::floor(pm), uu = x0 < 0.5 ? 2 * x0 : 2 * x0 - 1;
      double y = (x0 < 0.5 ? 4 : -4) * uu * (1 - uu);
      if (q0.ring > 0) { rph += q0.ringF * inv; if (rph >= 1) rph -= std::floor(rph); y *= 1 - q0.ring + q0.ring * js_sin(2 * kPi * rph); }
      rnd = (double)js_to_uint32(rnd * 1664525 + 1013904223); const double nz = rnd / 2147483648.0 - 1;
      y = y * (1 - q0.noise * 0.7) + nz * q0.noise;
      if (tl < sr * 0.0015) y += q0.click * (1 - tl / (sr * 0.0015)) * (nz * 0.5 + 0.8);
      y *= env * rg;
      if (truthy(dr)) y = js_tanh(y * dr) * dn;
      if (filt != 0) { const double hp = y - lp - q * bp; bp += f * hp; lp += f * bp; if (!(std::fabs(lp) < 1e3)) { lp = bp = 0; } y = (filt < 0 ? lp : hp) * rc; }
      out[i] += y * gain * 0.8;
      t++;
      if (env < 1e-5 && tl > atk && (q0.grain < 1 || rep >= (double)i32(q0.grain))) { on = false; break; }
    }
  }
  bool on = false; double t = 0, ph = 0, mph = 0, rph = 0, lp = 0, bp = 0, rnd = 7777, g = 0, rep = 0, rg = 1, v0 = kNaN, ke = kNaN, env = kNaN; BlipP p;
 private:
  double sr_;
};

// ---- Blip drum -----------------------------------------------------------------------------------------------------------------------
const char kBdRows[8] = {'p', 'k', 'f', 'm', 'n', 'd', 'a', 'x'};
const double kBdDef[8][8] = {{50, 120, 220, 400, 900, 1800, 3500, 6000}, {.8, .5, .3, .2, .1, 0, 0, 0}, {0, 0, .2, 0, .3, 0, .1, 0}, {0, 0, 0, .3, 0, .5, 0, .6}, {0, .1, .3, 0, 0, .5, .8, .9},
  {600, 250, 180, 300, 120, 90, 60, 40}, {.9, .8, .7, .6, .6, .55, .5, .45}, {0, 0, 0, 0, 0, .3, .5, .6}};
const double kBdMn[8] = {20, 0, 0, 0, 0, 5, 0, -1}, kBdMx[8] = {8000, 1, 1, 1, 1, 3000, 1, 1};
enum { bdEdit, bdFmR, bdRingR, bdKickT, bdDec, bdDrive, bdLfo, bdLfoD, bdWidth, bdDly, bdDfb, bdDdiv, bdLevel, bdRows };   // row r, voice j (1..8): bdRows + r * 8 + (j - 1)
struct BdTable {
  std::deque<std::string> names; std::vector<ParamDef> p; std::vector<PortDef> in;
  BdTable() {
    auto add = [&](const std::string& n, double d, double mn, double mx, double st) { names.push_back(n); p.push_back({names.back().c_str(), d, mn, mx, st}); };
    add("edit", 0, 0, 7, 1); add("fmR", 1.5, .25, 12, .01); add("ringR", 2.3, .01, 16, .01); add("kickT", 35, 2, 400, 1); add("dec", 1, .1, 4, .01); add("drive", .15, 0, 1, .01);
    add("lfo", .5, .05, 20, .01); add("lfoD", 0, 0, 1, .01); add("width", .5, 0, 1, .01); add("dly", 0, 0, 1, .01); add("dfb", .45, 0, .95, .01); add("ddiv", 4, 0, 9, 1); add("level", .8, 0, 1.5, .01);
    for (int r = 0; r < 8; r++) for (int i = 0; i < 8; i++) add(std::string(1, kBdRows[r]) + std::to_string(i + 1), kBdDef[r][i], kBdMn[r], kBdMx[r], r == 0 || r == 5 ? 1 : .01);
    for (int i = 0; i < 8; i++) names.push_back("t" + std::to_string(i + 1));
    for (int i = 0; i < 8; i++) in.push_back({names[(size_t)(77 + i)].c_str(), "gate"});
  }
};
const BdTable& bdTable() { static const BdTable t; return t; }
const PortDef kBdOut[] = {{"out", "audio"}, {"outR", "audio"}};
class BlipDrum : public Module {
 public:
  explicit BlipDrum(double sr, Seeds&) : d_((int)std::ceil(sr * 2.2) + 4), buf_(128) { for (int k = 0; k < 8; k++) v_.emplace_back(sr); }
  void process(const IO& io, const Ctx& ctx) override {
    const double* p = io.p; const int n = ctx.block; const double sr = ctx.sr; const Out oL = outp(io, 0), oR = outp(io, 1); F32& B = buf_;
    oL.fill(kBlock, 0); oR.fill(kBlock, 0);
    lph_ += p[bdLfo] * n / sr; lph_ -= std::floor(lph_);
    for (int k = 0; k < 8; k++) {
      const In g = inp(io, k); BlipVoice& v = v_[(size_t)k];
      int at = -1; for (int i = 0; i < n; i++) { const bool on = g[i] >= .5; if (on && !lg_[k]) at = i; lg_[k] = on ? 1 : 0; if (at >= 0) break; }
      if (at >= 0) {
        const int j = k + 1; const double pj = p[bdRows + 0 * 8 + (j - 1)] * js_pow(2, p[bdLfoD] * js_sin(2 * kPi * (lph_ + k * .125)));
        BlipP q; q.pitch = pj; q.kick = p[bdRows + 1 * 8 + (j - 1)]; q.kickT = p[bdKickT]; q.fm = p[bdRows + 2 * 8 + (j - 1)]; q.fmR = p[bdFmR]; q.ring = p[bdRows + 3 * 8 + (j - 1)]; q.ringF = pj * p[bdRingR];
        q.noise = p[bdRows + 4 * 8 + (j - 1)]; q.click = .15 + p[bdRows + 1 * 8 + (j - 1)] * .3; q.decay = p[bdRows + 5 * 8 + (j - 1)] * p[bdDec]; q.drive = 0; q.filt = p[bdRows + 7 * 8 + (j - 1)]; q.res = .25;
        v.set(q); v.trig(1);
      }
      if (!v.on) continue;
      B.fill(0); v.render(B, 0, n, p[bdRows + 6 * 8 + k]);
      const double pan = (k / 7.0 - .5) * p[bdWidth], gl = js_cos((pan + .5) * kPi / 2), gr = js_sin((pan + .5) * kPi / 2);
      for (int i = 0; i < n; i++) { oL[i] += B[i] * gl; oR[i] += B[i] * gr; }
    }
    if (p[bdDly] > 0 || dOn_) {
      dOn_ = p[bdDly] > 0;
      F32& D = d_; const int N = D.n; const double dt = jmin(N / 2.0 - 2, js_round(60 / (truthy(ctx.tempo) ? ctx.tempo : 120) * divBeats(p[bdDdiv]) * sr)), fb = p[bdDfb], k = .35;
      for (int i = 0; i < n; i++) {
        const double r = dw_ - dt * 2, rl = jmod(jmod(r, N) + N, N), rr = jmod(jmod(r + 1, N) + N, N);
        lp0_ += (rdf(D.d.get(), N, rl) - lp0_) * k; lp1_ += (rdf(D.d.get(), N, rr) - lp1_) * k;
        const double l = oL[i], rgt = oR[i];
        D[dw_] = js_tanh(l + lp1_ * fb); D[(dw_ + 1) % N] = js_tanh(rgt + lp0_ * fb);
        oL[i] = l + lp0_ * p[bdDly]; oR[i] = rgt + lp1_ * p[bdDly];
        dw_ = (dw_ + 2) % N;
      }
    }
    const double lv = p[bdLevel], dr = p[bdDrive] * 6, dn = truthy(dr) ? 1 / js_tanh(dr) : 1;
    if (truthy(dr)) for (int i = 0; i < n; i++) { oL[i] = js_tanh(oL[i] * dr) * dn * lv; oR[i] = js_tanh(oR[i] * dr) * dn * lv; }
    else for (int i = 0; i < n; i++) { oL[i] = oL[i] * lv; oR[i] = oR[i] * lv; }
  }
 private:
  std::vector<BlipVoice> v_; int lg_[8] = {}; F32 d_, buf_; int dw_ = 0; double lp0_ = 0, lp1_ = 0, lph_ = 0; bool dOn_ = false;
};

// ---- Life ----------------------------------------------------------------------------------------------------------------------------
const int kLifeBorn[5][5] = {{3, -1}, {3, 6, -1}, {2, -1}, {3, -1}, {3, 6, 7, 8, -1}}, kLifeKeep[5][6] = {{2, 3, -1}, {2, 3, -1}, {-1}, {1, 2, 3, 4, 5, -1}, {3, 4, 6, 7, 8, -1}};
struct LifeTable {
  std::deque<std::string> names; std::vector<ParamDef> p; std::vector<PortDef> out;
  LifeTable() {
    auto add = [&](const std::string& n, double d, double mn, double mx, double st) { names.push_back(n); p.push_back({names.back().c_str(), d, mn, mx, st}); };
    add("w", 16, 4, 16, 1); add("div", 3, 0, 9, 1); add("gens", 8, 0, 32, 1); add("trig", 0, 0, 1, 1); add("rule", 0, 0, 4, 1); add("rot", 0, 0, 7, 1); add("len", .5, .05, .95, .01);
    const double glider[8] = {0, 0, 2, 4, 7, 0, 0, 0};
    for (int y = 0; y < 8; y++) add("b" + std::to_string(y), glider[y], 0, 65535, 1);
    for (int k = 0; k < 8; k++) names.push_back("g" + std::to_string(k + 1));
    for (int k = 0; k < 8; k++) out.push_back({names[(size_t)(15 + k)].c_str(), "gate"});
    out.push_back({"any", "gate"}); out.push_back({"count", "cv"});
  }
};
const LifeTable& lifeTable() { static const LifeTable t; return t; }
const PortDef kLifeIn[] = {{"clock", "gate"}, {"reset", "gate"}};
class Life : public Module {
 public:
  Life(double, Seeds&) { for (int y = 0; y < 8; y++) { SG_[y] = -1; } }
  void process(const IO& io, const Ctx& ctx) override {
    const double* p = io.p; const int32_t W = (int32_t)clamp((double)i32(p[0]), 4, 16); const bool ck = io.linked[0]; bool ch = false;
    for (int y = 0; y < 8; y++) { const int32_t v = i32(p[7 + y]); if (v != SG_[y]) { SG_[y] = v; ch = true; } }
    if (ch) load(p);
    if (rs_(ctx)) { load(p); x_ = -1; ph_ = 1; }
    const double inc = (truthy(ctx.tempo) ? ctx.tempo : 120) / 60 / divBeats(p[1]) / ctx.sr;
    const In RS = inp(io, 1), CK = inp(io, 0);
    for (int i = 0; i < ctx.block; i++) {
      const bool r = RS[i] >= .5; if (r && !lr_) { load(p); x_ = -1; ph_ = 1; } lr_ = r;
      bool on;
      if (ck) { const bool c = CK[i] >= .5; if (c && !lc_) lifeStep(p, W); lc_ = c; on = c; }
      else { ph_ += inc; if (ph_ >= 1) { ph_ -= std::floor(ph_); lifeStep(p, W); } on = ph_ < p[6]; }
      const int32_t f = on ? fire_ : 0;
      for (int k = 0; k < 8; k++) outp(io, k)[i] = (f >> k) & 1;
      outp(io, 8)[i] = f ? 1 : 0; outp(io, 9)[i] = cnt_ / 8.0;
    }
  }
 private:
  void load(const double* p) { for (int y = 0; y < 8; y++) { cur_[y] = (uint16_t)(i32(p[7 + y]) & 0xffff); prev_[y] = 0; } gen_ = 0; }
  void next(const double* p) {
    const int32_t W = (int32_t)clamp((double)i32(p[0]), 4, 16), m = (1 << W) - 1; const int rule = (int)clamp((double)i32(p[4]), 0, 4); const int* born = kLifeBorn[rule]; const int* keep = kLifeKeep[rule]; uint16_t nx[8] = {};
    auto at = [&](int x, int y) { return (cur_[(y + 8) % 8] >> ((x + W) % W)) & 1; };
    auto has = [](const int* l, int c) { for (; *l >= 0; l++) if (*l == c) return true; return false; };
    for (int y = 0; y < 8; y++) for (int x = 0; x < W; x++) {
      int c = 0; for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) if (dx || dy) c += at(x + dx, y + dy);
      const int a = at(x, y); if (a ? has(keep, c) : has(born, c)) nx[y] = (uint16_t)(nx[y] | (1 << x));
    }
    for (int y = 0; y < 8; y++) prev_[y] = cur_[y];
    for (int y = 0; y < 8; y++) cur_[y] = (uint16_t)(nx[y] & m);
    gen_++;
  }
  // (the JS's `1 << n` in lifeStep: the count is taken mod 32, and an out-of-range rot gives a negative n; a plain C++ shift of it is undefined)
  void lifeStep(const double* p, int32_t W) {
    x_++;
    if (x_ >= W) { x_ = 0; if (i32(p[2]) > 0 && gen_ + 1 >= i32(p[2])) load(p); else next(p); }
    int32_t f = 0, c = 0;
    for (int y = 0; y < 8; y++) { const int a = (cur_[y] >> x_) & 1, b = (prev_[y] >> x_) & 1; c += a; if (p[3] >= .5 ? (a && !b) : a) f |= (int32_t)(1u << (((y + i32(p[5])) % 8) & 31)); }
    fire_ = f; cnt_ = c;
  }
  uint16_t cur_[8] = {}, prev_[8] = {}; int32_t SG_[8]; int32_t x_ = -1, gen_ = 0, fire_ = 0, cnt_ = 0; double ph_ = 1; bool lc_ = false, lr_ = false; Restart rs_;
};

// ---- Resonator -----------------------------------------------------------------------------------------------------------------------
const PortDef kRsIn[] = {{"in", "audio"}, {"inR", "audio"}, {"note", "cv"}};
const ParamDef kRsP[] = {{"note", 48, 24, 96, 1}, {"chord", 3, 0, 11, 1}, {"mode", 0, 0, 1, 1}, {"decay", .5, 0, 1, .01}, {"bright", .6, 0, 1, .01}, {"spread", .5, 0, 1, .01}, {"mix", .5, 0, 1, .01}};
const int kChordN[12] = {1, 3, 4, 5, 5, 5, 5, 5, 5, 5, 5, 5};
const double kChords[12][5] = {{0}, {0, 12, 24}, {0, 7, 12, 19}, {0, 4, 7, 12, 16}, {0, 3, 7, 12, 15}, {0, 5, 7, 12, 17}, {0, 4, 7, 11, 14}, {0, 3, 7, 10, 14}, {0, 4, 7, 10, 12}, {0, 5, 10, 15, 20}, {0, 12, 19, 24, 28}, {0, 1, 2, 3, 4}};
class Reson : public Module {
 public:
  explicit Reson(double sr, Seeds&) { for (int i = 0; i < 5; i++) v_[i].b.init((int)std::ceil(sr / 20) + 4); }
  void process(const IO& io, const Ctx& ctx) override {
    const double* p = io.p; const Out L = outp(io, 0), R = outp(io, 1);
    { const In a = inp(io, 0), b = inR(io, 0, 1); for (int i = 0; i < kBlock; i++) { L[i] = a[i]; R[i] = b[i]; } }   // L.set(io.in.in); R.set(inR(io))
    const double note = io.linked[2] ? 69 + (inp(io, 2)[0] - .5) * 96 + (p[0] - 48) : p[0], chord = p[1], mode = p[2], decay = p[3], bright = p[4], spread = p[5], mix = p[6];
    const int n = ctx.block; const double sr = ctx.sr; const int ci = (int)jmax(0, jmin(11, (double)i32(chord))); const double* C = kChords[ci]; const int nv = kChordN[ci]; const bool tube = mode >= 0.5;
    const double t60 = 0.05 * js_pow(160, jmax(0, jmin(1, decay))), a = 0.08 + 0.9 * jmax(0, jmin(1, bright)), sp = spread;
    const double ing = 0.5 / std::sqrt((double)nv);
    for (int k = 0; k < nv; k++) {
      V& V_ = v_[k]; const double f = 440 * js_pow(2, (note + C[k] - 69) / 12);
      const double per = sr / f * (tube ? 0.5 : 1);
      V_.d = jmax(2, jmin(V_.b.n - 3, per - (1 - a) / a));
      V_.g = js_pow(10, -3 * (per / sr) / t60) * (tube ? -1 : 1);
      const double pan = nv > 1 ? (k / (double)(nv - 1) - 0.5) * sp : 0; V_.gl = js_cos((pan + 0.5) * kPi / 2); V_.gr = js_sin((pan + 0.5) * kPi / 2);
    }
    for (int i = 0; i < n; i++) {
      const double x = (L[i] + R[i]) * 0.5 * ing; double yl = 0, yr = 0;
      for (int k = 0; k < nv; k++) {
        V& V_ = v_[k]; F32& B = V_.b; const int N = B.n; const double r = V_.w - V_.d, ri = std::floor(r), fr = r - ri;
        const double i0 = ri < 0 ? ri + N : ri, i1 = i0 + 1 >= N ? 0 : i0 + 1, d = rdf(B.d.get(), N, i0) + (rdf(B.d.get(), N, i1) - rdf(B.d.get(), N, i0)) * fr;
        V_.lp += (d - V_.lp) * a;
        double y = x + V_.lp * V_.g;
        y = y > 1.5 ? 1.5 : y < -1.5 ? -1.5 : y;
        B[V_.w] = y; V_.w = V_.w + 1 == N ? 0 : V_.w + 1;
        const double o = V_.lp - V_.dx + 0.995 * V_.dc; V_.dx = V_.lp; V_.dc = o;
        yl += o * V_.gl; yr += o * V_.gr;
      }
      L[i] = L[i] * (1 - mix) + js_tanh(yl) * mix; R[i] = R[i] * (1 - mix) + js_tanh(yr) * mix;
    }
  }
 private:
  struct V { F32 b; int w = 0; double lp = 0, dc = 0, dx = 0, d = 0, g = 0, gl = 0, gr = 0; };
  V v_[5];
};

// ---- Degrade -------------------------------------------------------------------------------------------------------------------------
const ParamDef kDgP[] = {{"bits", 24, 1, 24, .1}, {"rate", 1, 0, 1, .001}, {"noise", .15, 0, 1, .01}, {"color", .3, 0, 1, .01}, {"follow", .6, 0, 1, .01}, {"dry", 0, 0, 1, .01}, {"wet", 1, 0, 1, .01}};
class Degrade : public Module {
 public:
  explicit Degrade(double, Seeds&) {}
  void process(const IO& io, const Ctx& ctx) override {
    const double* p = io.p; const Out L = outp(io, 0), R = outp(io, 1);
    { const In a = inp(io, 0), b = inR(io, 0, 1); for (int i = 0; i < kBlock; i++) { L[i] = a[i]; R[i] = b[i]; } }
    const double qbits = p[0], qrate = p[1] >= .999 ? 48000 : 300 * js_pow(160, p[1]), qnoise = p[2], qcolor = p[3], qfollow = p[4], dry = p[5], wet = p[6];
    const int n = ctx.block; const double sr = ctx.sr, rate = jmin(qrate, sr); const bool hold = rate < sr * 0.98; const double inc = rate / sr;
    const double bits = jmax(1, jmin(24, qbits)); const bool crush = bits < 23.5; const double q = js_pow(2, bits - 1);
    const double nn = jmax(0, jmin(1, qnoise)), lv = nn * nn * 0.25, col = jmax(0, jmin(1, qcolor)), k = 1 - col * 0.96, comp = std::sqrt((2 - k) / k) / 0.577;
    const double fol = jmax(0, jmin(1, qfollow)), ka = 1 - js_exp(-1 / (sr * 0.004)), kr = 1 - js_exp(-1 / (sr * 0.15));
    for (int i = 0; i < n; i++) {
      const double xl = L[i], xr = R[i];
      const double a = jmax(std::fabs(xl), std::fabs(xr)); env_ += (a - env_) * (a > env_ ? ka : kr);
      if (hold) { ph_ += inc; if (ph_ >= 1) { ph_ -= std::floor(ph_); hl_ = xl; hr_ = xr; } } else { hl_ = xl; hr_ = xr; }
      double yl = hl_, yr = hr_;
      if (crush) { yl = js_round(yl * q) / q; yr = js_round(yr * q) / q; }
      if (lv > 0) {
        uint32_t s = s_; s ^= s << 13; s ^= s >> 17; s ^= s << 5; const double w1 = (double)s / 2147483648.0 - 1; s ^= s << 13; s ^= s >> 17; s ^= s << 5; const double w2 = (double)s / 2147483648.0 - 1; s_ = s;
        nl_ += (w1 - nl_) * k; nr_ += (w2 - nr_) * k;
        const double g = lv * comp * ((1 - fol) + fol * jmin(1, env_ * 6));
        yl += nl_ * g; yr += nr_ * g;
      }
      L[i] = xl * dry + yl * wet; R[i] = xr * dry + yr * wet;
    }
  }
 private:
  double ph_ = 1, hl_ = 0, hr_ = 0, nl_ = 0, nr_ = 0, env_ = 0; uint32_t s_ = 0x2545f491u;
};

template <class M> std::unique_ptr<Module> mk(double sr, Seeds& s) { return std::unique_ptr<Module>(new M(sr, s)); }
const Def* defs(int* n) {
  static const Def kD[] = {
    {"harm", PD(kStIn), PD(kStOut), PD(kHarmP), mk<Harmonizer>},
    {"blipdrum", 8, nullptr, PD(kBdOut), 0, nullptr, mk<BlipDrum>},
    {"life", PD(kLifeIn), 10, nullptr, 0, nullptr, mk<Life>},
    {"reson", PD(kRsIn), PD(kStOut), PD(kRsP), mk<Reson>},
    {"degrade", PD(kStIn), PD(kStOut), PD(kDgP), mk<Degrade>},
  };
  static Def D[5]; static bool init = false;
  if (!init) {
    for (int i = 0; i < 5; i++) D[i] = kD[i];
    D[1].in = bdTable().in.data(); D[1].np = (int)bdTable().p.size(); D[1].params = bdTable().p.data();
    D[2].out = lifeTable().out.data(); D[2].np = (int)lifeTable().p.size(); D[2].params = lifeTable().p.data();
    init = true;
  }
  *n = 5; return D;
}
}  // namespace

void addMiscMods(std::vector<const Def*>& v) { int n; const Def* d = defs(&n); for (int i = 0; i < n; i++) v.push_back(&d[i]); }

}  // namespace modfx
}  // namespace thunder
