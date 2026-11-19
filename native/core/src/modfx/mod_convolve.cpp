// MODFX modules B (2 of 2): Convolution reverb (chains-worklet.js 3415 to 3990), line for line: the uniformly partitioned FFT convolution in three stages, the IR maker (rooms by the image
// method, plate, spring, gated, reverse, cab, telephone, and the song's own IRs) and the crossfade between IRs. Doubles in the JS's order, float32 where the JS keeps a Float32Array (F32 / FRef),
// Math.* through js_* (Math.hypot is js_hypot3). Everything is allocated in the constructor (as create()), the FFT tables come from the shared cache. The song's IRs are read from ctx.irs
// (globalThis.__thunderIR): see SongIR in modfx.h.
#include <algorithm>
#include <cstdlib>
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
const ParamDef kCnvP[] = {{"type", 1, 0, 13, 1}, {"size", .5, 0, 1, .01}, {"decay", 2, .1, 10, .01}, {"pre", .01, 0, .25, .001}, {"tone", .5, 0, 1, .01}, {"early", .5, 0, 1, .01}, {"width", 1, 0, 1.5, .01}, {"mix", .3, 0, 1, .01}};
inline double u01(double v) { return clamp(v, 0, 1); }
inline double fxSafe(double y) { const double a = y < 0 ? -y : y; if (a < 2) return y; const double e = a - 2; return (y < 0 ? -1 : 1) * (2 + e / (1 + e * .5)); }
const double kU32 = 2.3283064365386963e-10;
inline void xs(uint32_t& r) { r ^= r << 13; r ^= r >> 17; r ^= r << 5; }
inline double jmin3(double a, double b, double c) { return jmin(jmin(a, b), c); }

const int CNV_KIND[14] = {0, 0, 0, 1, 0, 2, 3, 4, 5, 6, 7, 7, 7, 7};   // room, plate, spring, gated, reverse, cab, telephone, song IR
// The JS indexes these tables with the (clamped, rounded) type, which is NaN for a NaN parameter: CNV_KIND[NaN] is undefined (no kind matches) and CNV_ROOM[Math.min(NaN, 9)] is undefined.
// Native: a type that is not an integer 0..13 gets kind -1 (matches nothing) and the room row 0; never an out-of-range index (an (int) cast of NaN is undefined behaviour).
inline int cnvKind(double t) { return (t >= 0 && t <= 13) ? CNV_KIND[(int)t] : -1; }
inline int cnvRoomIdx(double t) { return (t >= 0 && t <= 13) ? (int)(t < 9 ? t : 9) : 0; }
const double CNV_ROOM[10][11] = {{6.2, 4.4, 2.9, .8, 4, .4, .035, 1500, 1.1, .9, 50}, {30, 21, 13, .86, 3, .25, .1, 400, 1.3, .85, 35},
  {8, 5.5, 4, .93, 4, .3, .02, 3000, 1, 1.15, 60}, {0, 0, 0, 0, 0, 0, .006, 15000, .7, 1.25, 140},
  {60, 30, 32, .9, 3, .2, .25, 150, 1.5, .7, 28}, {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 60},
  {10, 7, 4.5, .85, 3, .3, .012, 3000, 1, 1.1, 70}, {20, 14, 9, .86, 3, .2, .05, 2000, 1.1, .9, 50},
  {3.5, 3, 2.5, .6, 3, .6, .01, 3000, 1, .8, 0}, {2.4, 2, 2.2, .7, 3, .6, .008, 3000, 1, .8, 0}};
const int CNV_S[3] = {128, 1024, 8192}, CNV_D[3] = {0, 2048, 16384}, CNV_SPR = 36, CNV_FADE = 8192;
const double CNV_BUDGET = 6, CNV_E = .09;   // about -6 dB from .35

struct Spec { F32 re, im; };
struct SpecD { Arr<double> re, im; };   // (Y: Float64Arrays)
struct Slot {
  Spec H[3][2]; Arr<uint8_t> nz[3]; double g = 1, gt = 1; F32 ring[3][2]; F32 o0[2]; bool empty = false;
};
struct Syn {
  uint32_t r = 1; double l1 = 0, l2 = 0, l3 = 0, e[4] = {0, 0, 0, 0}, d[4] = {0, 0, 0, 0}, lp = 0, hp = 0, hx = 0, bq[24] = {};
  F32 dl; int w = 0; double D = 1; double ap[CNV_SPR * 2] = {}; int sl = 0;
};
struct JobS { bool on = false; int64_t S = 0; int64_t j = 0; int need = 0, u = 0, nu = 0, left = 0, ph = 0, c = 0, sub = 0, pair = 0; int pairs[4] = {0, 0, 0, 0}; int np = 0; FFTR::Job fj; explicit JobS(int maxN) : fj(maxN) {} };

struct Gen {
  bool on = false, init = false; int slot = 0, ph = 0; int n = 0; double type = -1, size = 0, decay = 0, pre = 0, tone = 0, early = 0; const SongIR* obj = nullptr; bool hasId = false; uint64_t id = 0;
  int kind = 0, len = 0, pre0 = 0; double tr = 1, tb = 1, rho0 = 0, gate = 0, cut = 0, kr = 0, fc0 = 0, th = 1, ab = 0, qd = 0, qb = 0, tail0 = 0, kt = 1, kh = 0, kp = 0, ls = 1, drip = 0;
  double C[30] = {}, E[2] = {0, 0}, g = 0, ready = 0; int s = 0, p = 0, c = 0, sub = 0;
  double step = 1, ka = 0, hg = 1, kx = 0, ge = 1, geo[12] = {}; int ord = 0; bool er = false; double erEnd = 0, erR = 1; int ex = 0; double clr = 0, eest = 1, et = 0, rm = 0, ef = 0;
  const float* srcL = nullptr; const float* srcR = nullptr; int srcN = 0; double sr = 0; Syn sy[2]; F32 ir[2]; Arr<double> x; FFTR::Job job;
  explicit Gen(int maxN) : job(maxN) {}
};

class Convolve : public Module {
 public:
  Convolve(double sr_, Seeds&) : sr(sr_), cap((int)std::ceil(sr_ * 6.25)), G(2 * CNV_S[2]), job1(2 * CNV_S[1]), job2(2 * CNV_S[2]), scr0(256), scr1(2 * CNV_S[1]) {
    P[0] = 16; P[1] = 14; P[2] = (int)jmax(1, std::ceil((double)(cap - CNV_D[2]) / CNV_S[2]));
    for (int s = 0; s < 3; s++) K[s] = CNV_S[s] + 1;
    for (int n : CNV_S) FFTR::get(2 * n);   // the transforms' tables, made now rather than on the audio thread
    for (int i = 0; i < 3; i++) F[i] = FFTR::get(2 * CNV_S[i]);
    for (int k = 0; k < 2; k++) {
      Slot& Z = sl[k];
      for (int s = 0; s < 3; s++) { for (int c = 0; c < 2; c++) { Z.H[s][c].re.init(P[s] * K[s]); Z.H[s][c].im.init(P[s] * K[s]); } Z.nz[s].init(P[s]); }
      for (int s = 1; s < 3; s++) for (int c = 0; c < 2; c++) Z.ring[s][c].init(3 * CNV_S[s]);
      for (int c = 0; c < 2; c++) Z.o0[c].init(128);
    }
    for (int c = 0; c < 2; c++) { G.sy[c].dl.init((int)std::ceil(sr * .09) + 4); G.ir[c].init(cap); }
    G.x.init(2 * CNV_S[2]); G.sr = sr;
    for (int s = 0; s < 3; s++) for (int c = 0; c < 2; c++) { X[s][c].re.init(P[s] * K[s]); X[s][c].im.init(P[s] * K[s]); hist[s][c].init(2 * CNV_S[s]); }
    for (int c = 0; c < 2; c++) { snap[c].init(2 * CNV_S[2]); fin[c].init(128); wq[c].init(128); }
    for (int s = 0; s < 3; s++) { Y[s].re.init(K[s]); Y[s].im.init(K[s]); x[s].init(2 * CNV_S[s]); }
  }
  void process(const IO& io, const Ctx& ctx) override {
    const double* p = io.p; const In L = inp(io, 0), R = inR(io, 0, 1); const Out oL = outp(io, 0), oR = outp(io, 1); const int B = ctx.block;
    watch(p, ctx);
    if (G.on) { double w = 0; while (G.on && w < CNV_BUDGET) w += gen(); }
    // with no IR at all (a Song IR that isn't there), the dry sound passes at full level
    const double mx = sl[act].empty && !fd ? 0 : u01(p[7]), wd = clamp(p[6], 0, 1.5); F32& fL = fin[0]; F32& fR = fin[1]; F32& qL = wq[0]; F32& qR = wq[1];
    if (mix < 0) mix = mx;
    double m = mix;
    if (B == 128 && fpos == 0) {
      // the usual case: one block is one frame, so the wet sound is not late at all
      for (int i = 0; i < 128; i++) { const double l = L[i], r = R[i]; fL[i] = l > -8 && l < 8 ? l : 0; fR[i] = r > -8 && r < 8 ? r : 0; }
      frame();
      for (int i = 0; i < 128; i++) {
        const double l = fL[i], r = fR[i], wl = qL[i], wr = qR[i], mid = (wl + wr) * .5, sd = (wl - wr) * .5 * wd;
        m += (mx - m) * .002;   // Mix glides, so it never clicks
        oL[i] = l * (1 - m) + fxSafe(mid + sd) * m; oR[i] = r * (1 - m) + fxSafe(mid - sd) * m;
      }
    } else {
      // any other block size: 128-sample frames, the wet path 128 samples late
      for (int i = 0; i < B; i++) {
        const double l0 = L[i], r0 = R[i], l = l0 > -8 && l0 < 8 ? l0 : 0, r = r0 > -8 && r0 < 8 ? r0 : 0; const int j = fpos; const double wl = qL[j], wr = qR[j], mid = (wl + wr) * .5, sd = (wl - wr) * .5 * wd;
        m += (mx - m) * .002;
        oL[i] = l * (1 - m) + fxSafe(mid + sd) * m; oR[i] = r * (1 - m) + fxSafe(mid - sd) * m;
        fL[j] = l; fR[j] = r;
        if (++fpos == 128) { fpos = 0; frame(); }
      }
    }
    mix = m;
  }

 private:
  // ---- the engine -------------------------------------------------------------------------------------------------------------------------------------------
  // Has the shape changed? Then a new IR, once it has been still for 150 ms (or a second has passed).
  void watch(const double* p, const Ctx& ctx) {
    double* w = want;
    const double type = clamp(js_round(p[0]), 0, 13);
    const double v1 = u01(p[1]), v2 = clamp(p[2], .1, 10), v3 = clamp(p[3], 0, .25), v4 = u01(p[4]), v5 = u01(p[5]);
    const SongIR* obj = nullptr; bool hasId = false; uint64_t id = 0;
    if (cnvKind(type) == 7) { const SongIR* const* T = ctx.irs; obj = T ? T[(int)type - 10] : nullptr; hasId = obj != nullptr; id = obj ? obj->id : 0; }
    if (w[0] != type || w[1] != v1 || w[2] != v2 || w[3] != v3 || w[4] != v4 || w[5] != v5 || wobj != obj || whasId != hasId || wid != id) {
      if (n - chg >= .15 * sr) chg0 = n;   // a new run of changes starts
      w[0] = type; w[1] = v1; w[2] = v2; w[3] = v3; w[4] = v4; w[5] = v5; wobj = obj; whasId = hasId; wid = id; chg = n;
    }
    if (!started) { started = true; start(true); gen(); return; }   // the first IR: set up now, made as it is needed
    if (G.on || fd || n < free) return;
    if (G.type == type && G.size == v1 && G.decay == v2 && G.pre == v3 && G.tone == v4 && G.early == v5 && G.obj == obj && G.hasId == hasId && G.id == id) return;
    if ((double)(n - chg) >= .15 * sr || (double)(n - std::max(last, chg0)) >= sr) start(false);
  }
  void start(bool init) {
    double* w = want;
    G.on = true; G.init = init; G.slot = init ? act : 1 - act; G.ph = 0; G.ready = 0; last = n;
    G.type = w[0]; G.size = w[1]; G.decay = w[2]; G.pre = w[3]; G.tone = w[4]; G.early = w[5]; G.obj = wobj; G.hasId = whasId; G.id = wid;
    Slot& Z = sl[G.slot];
    for (int s = 0; s < 3; s++) Z.nz[s].fill(0);
    if (!init) for (int s = 1; s < 3; s++) { Z.ring[s][0].fill(0); Z.ring[s][1].fill(0); }
  }
  // The IR slots a stretch of output [t, t + n) needs: the playing one, and while crossfading the new one.
  int need(int64_t t, int64_t nn) const { if (!fd) return 1 << act; return (t < fe ? 1 << fa : 0) | (t + nn > fs ? 1 << fb : 0); }
  // While the first IR is still being turned into spectra: finish partition p of stage s before it is used.
  void ready(int slot, int s, int p) {
    if (!G.on || !G.init || G.slot != slot) return;
    const double e = CNV_D[s] + (p + 1) * CNV_S[s];
    while (G.on && G.ready < e) gen();
  }
  // Y += sum X(newest - p) H(p) for partitions p0 ... p1 - 1 (those with anything in them).
  void mac(int slot, int s, int c, int p0, int p1) {
    const int PP = P[s], KK = K[s]; Spec& Xs = X[s][c]; Spec& Hs = sl[slot].H[s][c]; Arr<uint8_t>& nzz = sl[slot].nz[s]; Arr<double>& Yr = Y[s].re; Arr<double>& Yi = Y[s].im;
    for (int p = p0; p < p1; p++) {
      ready(slot, s, p);
      if (!nzz[p]) continue;
      const int xo = ((head[s] - p + PP) % PP) * KK, ho = p * KK;
      for (int k = 0; k < KK; k++) { const double a = Xs.re[xo + k], b = Xs.im[xo + k], c2 = Hs.re[ho + k], d = Hs.im[ho + k]; Yr[k] += a * c2 - b * d; Yi[k] += a * d + b * c2; }
    }
  }
  // One 128-sample frame: stage 0 in full, the bigger stages' input and this frame's share of their work, the mix.
  void frame() {
    const int64_t t0 = n; const FFTR* f0 = F[0]; F32 (&h0)[2] = hist[0];
    // what the IRs hear: each side with a quarter of the other, so a sound on one side still fills the room
    for (int i = 0; i < 128; i++) { const double l = fin[0][i], r = fin[1][i]; h0[0][128 + i] = l * .75 + r * .25; h0[1][128 + i] = r * .75 + l * .25; }
    // stage 0: [last frame, this frame] -> the newest spectrum, and this frame's output at once
    head[0] = (head[0] + 1) % P[0]; ticks[0]++;
    for (int c = 0; c < 2; c++) f0->forward(scr0, h0[c].get(), X[0][c].re.get(), X[0][c].im.get(), head[0] * K[0]);
    const int need0 = need(t0, 128); const int64_t pm0 = std::min<int64_t>(ticks[0], P[0]);
    for (int k = 0; k < 2; k++) {
      if (!(need0 >> k & 1)) continue;
      for (int c = 0; c < 2; c++) {
        Arr<double>& Yr = Y[0].re; Arr<double>& Yi = Y[0].im; Yr.fill(0); Yi.fill(0);
        mac(k, 0, c, 0, (int)pm0);
        double* xx = x[0].get(); f0->inverse(scr0, Yr.get(), Yi.get(), xx);
        F32& o = sl[k].o0[c]; for (int i = 0; i < 128; i++) o[i] = xx[128 + i];
      }
    }
    // stages 1 and 2: gather the input; when a partition's worth is in, start the job that makes its output, which is due one partition later
    for (int s = 1; s < 3; s++) {
      const int N = CNV_S[s]; F32 (&H0)[2] = hist[s];
      for (int c = 0; c < 2; c++) for (int i = 0; i < 128; i++) H0[c][N + hp[s] + i] = (double)h0[c][128 + i];
      hp[s] += 128;
      if (hp[s] < N) continue;
      JobS& J = job(s);
      while (J.on) jobStep(s);   // never needed (a job ends within its partition), but safe
      hp[s] = 0; head[s] = (head[s] + 1) % P[s]; ticks[s]++;
      if (s == 1) { const FFTR* f1 = F[1]; for (int c = 0; c < 2; c++) f1->forward(scr1, H0[c].get(), X[1][c].re.get(), X[1][c].im.get(), head[1] * K[1]); }
      else for (int c = 0; c < 2; c++) for (int i = 0; i < 2 * N; i++) snap[c][i] = (double)H0[c][i];
      for (int c = 0; c < 2; c++) for (int i = 0; i < N; i++) H0[c][i] = (double)H0[c][N + i];
      J.S = t0 + 128 + N; J.j = ticks[s] - 1; J.need = need(J.S, N); J.np = 0;
      for (int k = 0; k < 2; k++) if (J.need >> k & 1) { J.pairs[J.np++] = k * 2; J.pairs[J.np++] = k * 2 + 1; }
      const int passes = F[s]->passes, per = s == 2 ? P[2] + passes + 2 : (int)std::ceil(P[1] / 7.0) + 1;
      J.nu = (s == 2 ? 2 * (passes + 2) : 0) + J.np * per; J.u = 0; J.left = N / 128; J.ph = s == 2 ? 0 : 1; J.c = 0; J.sub = 0; J.pair = 0;
      J.on = J.nu > 0;
    }
    for (int c = 0; c < 2; c++) for (int i = 0; i < 128; i++) h0[c][i] = (double)h0[c][128 + i];
    // this frame's share of the jobs (each ends a frame or two before its output is due)
    for (int s = 1; s < 3; s++) {
      JobS& J = job(s); if (!J.on) continue;
      const double nn = std::ceil((double)(J.nu - J.u) / jmax(1, J.left)); J.left--;
      for (int k = 0; k < nn && J.on; k++) jobStep(s);
    }
    // sum the stages, crossfading between two IRs when there are two; each read sample is cleared for its next turn
    F32& qL = wq[0]; F32& qR = wq[1]; const int N1 = 3 * CNV_S[1], N2 = 3 * CNV_S[2], use = fd ? 3 : 1 << act;
    int i1 = (int)(t0 % N1), i2 = (int)(t0 % N2);
    for (int i = 0; i < 128; i++) {
      double wl = 0, wr = 0, ga = 1, gb = 1;
      if (fd) { const double th = clamp((double)(t0 + i - fs) / (double)(fe - fs), 0, 1); ga = sinT(th * .25 + .25); gb = sinT(th * .25); }
      for (int k = 0; k < 2; k++) {
        if (!(use >> k & 1)) continue;
        Slot& Z = sl[k]; F32 (&r1)[2] = Z.ring[1]; F32 (&r2)[2] = Z.ring[2];
        Z.g += (Z.gt - Z.g) * .0005;   // the level (each IR is scaled to the same energy), glided
        const double g = Z.g * (!fd ? 1 : k == fa ? ga : gb);
        wl += (Z.o0[0][i] + r1[0][i1] + r2[0][i2]) * g; wr += (Z.o0[1][i] + r1[1][i1] + r2[1][i2]) * g;
        r1[0][i1] = 0; r1[1][i1] = 0; r2[0][i2] = 0; r2[1][i2] = 0;
      }
      if (!(wl > -64 && wl < 64 && wr > -64 && wr < 64)) { wl = 0; wr = 0; }
      qL[i] = wl; qR[i] = wr;
      if (++i1 == N1) i1 = 0;
      if (++i2 == N2) i2 = 0;
    }
    n = t0 + 128;
    if (fd && n >= fe) { act = fb; fd = false; free = n + 128; }
  }
  JobS& job(int s) { return s == 1 ? job1 : job2; }
  // One unit of a stage-1/2 job: [stage 2: the two forward transforms, a pass at a time], then for each IR slot and side: the partitions (stage 2: one a unit; stage 1: seven) and the
  // inverse transform, whose second half is the output for the partition starting at J.S.
  void jobStep(int s) {
    JobS& J = job(s); const int N = CNV_S[s]; const FFTR* f = F[s]; const int PP = P[s], KK = K[s];
    J.u++;
    if (J.ph == 0) {
      if (J.sub == 0) f->fwdStart(J.fj, snap[J.c].get());
      else if (J.sub <= f->passes) f->pass(J.fj);
      else { f->fwdEnd(J.fj, X[2][J.c].re.get(), X[2][J.c].im.get(), head[2] * KK); J.sub = -1; if (++J.c == 2) J.ph = 1; }
      J.sub++;
      if (J.ph == 1 && J.np == 0) J.on = false;
      return;
    }
    const int pr = J.pairs[J.pair], slot = pr >> 1, c = pr & 1; SpecD& Ys = Y[s]; const int nM = s == 2 ? PP : (int)std::ceil(PP / 7.0); const int64_t pm = std::min<int64_t>(J.j + 1, PP);
    if (J.sub < nM) {
      if (J.sub == 0) { Ys.re.fill(0); Ys.im.fill(0); }
      const int a = s == 2 ? J.sub : J.sub * 7; const int b = (int)std::min<int64_t>(pm, s == 2 ? a + 1 : a + 7);
      if (a < b) mac(slot, s, c, a, b);
      J.sub++; return;
    }
    double* xx = x[s].get(); const int v = J.sub - nM;
    bool done = false;
    if (s == 1) { f->inverse(scr1, Ys.re.get(), Ys.im.get(), xx); done = true; }
    else if (v == 0) f->invStart(J.fj, Ys.re.get(), Ys.im.get());
    else if (v <= f->passes) f->pass(J.fj);
    else { f->invEnd(J.fj, xx); done = true; }
    J.sub++;
    if (done) {
      F32& ring = sl[slot].ring[s][c]; const int o = (int)(J.S % (3 * N));
      for (int i = 0; i < N; i++) ring[o + i] = xx[N + i];
      J.sub = 0; if (++J.pair >= J.np) J.on = false;
    }
  }
  // ---- the IR maker -----------------------------------------------------------------------------------------------------------------------------------------
  // One unit of work; returns its rough cost.
  double gen() {
    if (G.ph == 0) { setup(); G.ph = 1; G.clr = G.init ? cap : 0; return 4; }   // the first IR's buffers are new (zero)
    if (G.ph == 1) {
      // clear the new IR, a stretch at a time
      const double e = jmin(cap, G.len + 1), b = jmin(e, G.clr + 32768);
      for (double i = G.clr; i < b; i++) { G.ir[0][(int)i] = 0; G.ir[1][(int)i] = 0; }
      G.clr = b;
      if (b >= e) { G.ph = G.er ? 2 : 4; G.ex = -G.ord; }
      return .5;
    }
    if (G.ph == 2) { early(G.ex); if (++G.ex > G.ord) G.ph = 3; return .3; }
    if (G.ph == 3) { earlyScale(); G.ph = 4; return 2.5; }
    const int s = G.s, N = CNV_S[s], o = CNV_D[s] + G.p * N; Slot& Z = sl[G.slot];
    if (G.p >= P[s] || o >= G.len) {
      if (s < 2) { G.s++; G.p = 0; G.c = 0; G.sub = 0; return .05; }
      return finish();
    }
    const int e = std::min(o + N, G.len);
    if (G.n < e) return synth();
    const int KK = K[s], ho = G.p * KK;
    if (s == 0) {
      // (its own buffers: this can run in the middle of the stage-0 convolution, while the first IR is being made)
      const FFTR* f = F[0]; double* xx = G.x.get();
      for (int c = 0; c < 2; c++) {
        for (int i = 0; i < 128; i++) { xx[i] = o + i < e ? (double)G.ir[c][o + i] : 0.0; xx[128 + i] = 0; }
        Spec& H = Z.H[0][c]; f->fwdStart(G.job, xx); while (f->pass(G.job)) {} f->fwdEnd(G.job, H.re.get(), H.im.get(), ho);
      }
      part(Z, 0, o, N); return .2;
    }
    double* xx = G.x.get(); FFTR::Job& J = G.job; const FFTR* f = F[s]; const int c = G.c; Spec& H = Z.H[s][c];
    if (G.sub == 0) { F32& a = G.ir[c]; for (int i = 0; i < N; i++) { xx[i] = o + i < e ? (double)a[o + i] : 0.0; xx[N + i] = 0; } f->fwdStart(J, xx); G.sub = 1; return s == 1 ? .2 : 1.6; }
    if (G.sub <= f->passes) { const int k = s == 1 ? f->passes : 1; for (int j = 0; j < k; j++) f->pass(J); G.sub += k; return s == 1 ? .6 : .5; }
    f->fwdEnd(J, H.re.get(), H.im.get(), ho); G.sub = 0;
    if (++G.c == 2) { G.c = 0; part(Z, s, o, N); }
    return s == 1 ? .15 : .8;
  }
  // A partition is ready: is anything in it (else the convolution skips it), and move on.
  void part(Slot& Z, int s, int o, int N) {
    F32& a = G.ir[0]; F32& b = G.ir[1]; const int e = std::min(o + N, G.len); int nz = 0;
    for (int i = o; i < e; i++) if ((double)a[i] != 0 || (double)b[i] != 0) { nz = 1; break; }
    Z.nz[s][G.p] = (uint8_t)nz; G.p++; G.ready = o + N;
  }
  double finish() {
    Slot& Z = sl[G.slot]; const double E = G.E[0] + G.E[1];
    G.on = false; G.ph = 5; G.ready = std::numeric_limits<double>::infinity();
    Z.empty = !(E > 1e-20); Z.gt = Z.empty ? 0 : std::sqrt(2 * CNV_E / E);
    if (G.init) return .1;
    // a new IR: crossfade to it once both bigger stages have started making its output
    Z.g = Z.gt;
    fd = true; fa = act; fb = G.slot;
    fs = n + (CNV_S[2] - hp[2]) + CNV_S[2]; fe = fs + CNV_FADE;
    return .1;
  }
  // RBJ cookbook biquad into C[o ... o+4]: t 0 low-pass, 1 high-pass, 2 peak (g dB).
  static void bq(double* C, int o, int t, double f, double q, double g, double srr) {
    const double w = 2 * kPi * clamp(f, 10, srr * .45) / srr, cs = js_cos(w), al = js_sin(w) / (2 * q), A = js_pow(10, g / 40);
    double b0, b1, b2, a0, a1, a2;
    if (t == 0) { b1 = 1 - cs; b0 = b2 = b1 / 2; a0 = 1 + al; a1 = -2 * cs; a2 = 1 - al; }
    else if (t == 1) { b1 = -(1 + cs); b0 = b2 = (1 + cs) / 2; a0 = 1 + al; a1 = -2 * cs; a2 = 1 - al; }
    else { b0 = 1 + al * A; b1 = -2 * cs; b2 = 1 - al * A; a0 = 1 + al / A; a1 = -2 * cs; a2 = 1 - al / A; }
    C[o] = b0 / a0; C[o + 1] = b1 / a0; C[o + 2] = b2 / a0; C[o + 3] = a1 / a0; C[o + 4] = a2 / a0;
  }
  // power gain of two one-pole low-passes in a row (pole a) on white noise
  static double pg2(double a) { const double b = 1 - a * a; return js_pow(1 - a, 4) * (1 + a * a) / (b * b * b); }
  // the fade at the end of the IR (and the gate's cut)
  double fade(double i, double nn) const {
    if (G.kind == 3) { const double j = i - G.gate; return j <= 0 ? 1 : j >= G.cut ? 0 : .5 + .5 * js_cos(kPi * j / G.cut); }
    if (G.kind == 4) { const double j = nn - i, f = js_round((.002 + .04 * (1 - G.early)) * G.sr); return j >= f ? 1 : j <= 0 ? 0 : j / f; }   // Early: how sharply the swell stops
    const double f = jmax(64, js_round(nn * .08)), j = i - (nn - f);
    return j <= 0 ? 1 : j >= f ? 0 : .5 + .5 * js_cos(kPi * j / f);
  }
  // a sample through the six biquads (transposed direct form II; z holds two states each)
  static double cascade(const double* C, double* z, double x) {
    for (int b = 0, o = 0, q = 0; b < 6; b++, o += 5, q += 2) {
      const double y = C[o] * x + z[q]; z[q] = C[o + 1] * x - C[o + 3] * y + z[q + 1]; z[q + 1] = C[o + 2] * x - C[o + 4] * y; x = y;
    }
    return x;
  }
  // two biquads (the Song IR's anti-alias low-pass, the same section twice)
  static double cascade2(const double* C, double* z, double x) {
    for (int q = 0; q < 4; q += 2) { const double y = C[0] * x + z[q]; z[q] = C[1] * x - C[3] * y + z[q + 1]; z[q + 1] = C[2] * x - C[4] * y; x = y; }
    return x;
  }
  // The first IR plays while it is made: start it at the level its energy will have.
  void level(double E) {
    Slot& Z = sl[G.slot];
    if (!(E > 1e-20)) E = 1;
    if (G.init) { Z.g = Z.gt = std::sqrt(CNV_E / E); Z.empty = G.len <= G.pre0; }
  }
  // Works out the IR: its length, the early reflections (written now), and everything the tail needs.
  void setup() {
    const double srr = sr; const int kind = cnvKind(G.type); const double* R = CNV_ROOM[cnvRoomIdx(G.type)];
    const double sc = js_pow(2, 2 * G.size - 1), tone = G.tone, erly = G.early, RT = G.decay, pre = js_round(G.pre * srr);
    G.kind = kind; G.n = 0; G.E[0] = G.E[1] = 0; G.ready = 0; G.s = 0; G.p = 0; G.c = 0; G.sub = 0; G.pre0 = (int)pre;
    for (int c = 0; c < 2; c++) {
      Syn& y = G.sy[c]; const uint32_t r0 = (uint32_t)js_to_uint32((G.type + 1) * 7919 + c * 104729 + 12345); y.r = r0 ? r0 : 1;
      y.l1 = y.l2 = y.l3 = y.lp = y.hp = y.hx = 0; for (double& v : y.d) v = 0; for (double& v : y.e) v = 0; for (double& v : y.bq) v = 0; y.dl.fill(0); y.w = 0; for (double& v : y.ap) v = 0; y.sl = 0;
    }
    double body = 0, Eest = 1;
    const bool er = kind == 0 || kind == 3 || ((kind == 5 || kind == 6) && erly > 0);
    // the tail's shape (rooms, plate, gated, reverse; and the little room around a cab or phone)
    if (kind != 2 && kind != 7) {
      // (Cab and Telephone: the room around the mic rings a tenth of Decay, 30 ... 600 ms)
      const bool small = kind == 5 || kind == 6; const double trt = small ? clamp(RT * .1, .03, .6) : kind == 3 ? jmax(1, 6 * clamp(RT, .1, 1.5)) : kind == 4 ? clamp(RT, .1, 6) : RT;
      body = kind == 3 ? clamp(RT, .1, 1.5) : kind == 4 ? clamp(RT, .1, 6) : small ? (erly > 0 ? jmax(trt, .08) : .08) : RT * jmax(1, R[8]);
      G.tr = trt; G.gate = kind == 3 ? js_round(clamp(RT, .1, 1.5) * srr) : 0; G.cut = js_round((kind == 3 ? clamp(.05 * body, .01, .03) : jmax(.012, .08 * body)) * srr);   // Gated: a 10-30 ms close
      if (kind == 3) body += G.cut / srr;
      G.rho0 = small ? 3000 : R[7] / sc; G.tb = jmax(.002, (small ? .01 : R[6]) * sc);
      G.kr = 1 - js_exp(-1 / (srr * (kind == 1 ? .0005 + (1 - erly) * .03 : kind == 4 ? .001 : G.tb * .7)));
      // Tone: the tail starts at a cutoff of 800 Hz (dull) ... 20 kHz (bright), which halves every th seconds
      G.fc0 = small ? 6000 : 800 * js_pow(10, tone * 1.4); G.th = trt * (small ? 1 : R[9]) * (.15 + .5 * tone);
      G.ab = R[8] > 1 && !small && kind != 4 ? jmin(.9, 1.2 * (R[8] - 1)) : 0;   // the longer bass (not in Reverse)
      G.qd = js_exp(-6.91 / (trt * srr)); G.qb = js_exp(-6.91 / (trt * jmax(1, R[8]) * srr));
      G.kh = R[10] > 0 ? 1 - js_exp(-2 * kPi * R[10] / std::sqrt(sc) / srr) : 0;
      G.tail0 = kind == 0 || kind == 3 ? js_round(jmin3(R[0], R[1], R[2]) * sc * .5 / 343 * srr) : 0;   // it starts near the first reflection
      // its energy (unit-variance noise, through the damping filter, times the envelope), summed every 128th sample
      const double nn = jmin(js_round(6 * srr), js_round(body * srr)); const double q = 128, ma = js_pow(G.qd, 2 * q), mb = js_pow(G.qb, 2 * q), mr = js_pow(1 - G.kr, q);
      double Et = 0, ea = kind == 4 ? js_pow(G.qd, 2 * nn) : 1, eb = 1, rr = 1;
      for (double i = 0; i < nn; i += q) {
        const double ts = i / srr, fc = clamp(kind == 4 ? G.fc0 * js_pow(2, -(nn / srr - ts) / G.th) : G.fc0 * js_pow(2, -ts / G.th), 150, srr * .45);
        const double rp = 1 - rr, fd2 = fade(i, nn), a = (kind == 4 ? ea : rp * rp * ea) * pg2(js_exp(-2 * kPi * fc / srr)), b = G.ab * G.ab * rp * rp * eb;
        Et += (a + b) * fd2 * fd2 * q;
        ea = kind == 4 ? ea / ma : ea * ma; eb *= mb; rr *= mr;
      }
      G.et = Et;
    }
    if (kind == 2) {
      // Spring: a loop of Length with 18 stretched all-passes (the chirp), damping (Tone) and drip (Early)
      body = jmin(6, RT);
      G.ls = (.028 + G.size * .05) * srr; G.g = js_pow(10, -3 * G.ls / (RT * srr));
      G.kt = 1 - js_exp(-2 * kPi * (1500 * js_pow(6, tone)) / srr); G.kh = 1 - js_exp(-2 * kPi * 140 / srr); G.kp = 1 - js_exp(-2 * kPi * 900 / srr);
      G.drip = erly * 3;
      // its energy: the click (with drip) through the loop, averaged over the loop's comb, 1/(1 - g^2|loop|^2) at each frequency
      { const double ap = 1 - G.kp, at = 1 - G.kt, ah = 1 - G.kh, g2 = G.g * G.g; double e = 0;
        for (int j = 0; j < 256; j++) {
          const double w = kPi * (j + .5) / 256, cw = js_cos(w), sw = js_sin(w), hp = 2 * (1 - cw);
          // drip: X = 1 + drip ap(1 - z^-1)/(1 - ap z^-1)
          const double dp = 1 - 2 * ap * cw + ap * ap, xr = 1 + G.drip * ap * ((1 - cw) * (1 - ap * cw) + ap * sw * sw) / dp, xi = G.drip * ap * sw * (1 - ap) / dp;
          const double lp = G.kt * G.kt / (1 - 2 * at * cw + at * at), hh = ah * ah * hp / (1 - 2 * ah * cw + ah * ah);
          e += .25 * (xr * xr + xi * xi) / jmax(.001, 1 - g2 * lp * hh);
        }
        Eest = e / 256; }
    }
    if (kind == 7) {
      // Song IR: resampled (cubic), stretched by Size, cut to Decay, darker or brighter with Tone, Early the first 50 ms
      const SongIR* o = G.obj; const float* L = o ? o->L : nullptr; const bool ok = L && o->nL > 1 && o->sr > 1000 && o->sr < 400000;
      G.srcL = ok ? L : nullptr; const bool hasR = ok && o->R && o->nR > 1; G.srcR = ok ? (hasR ? o->R : L) : nullptr;
      G.step = ok ? o->sr / srr / sc : 1; G.srcN = ok ? std::min(o->nL, hasR ? o->nR : o->nL) : 0;
      body = ok ? jmin3((G.srcN - 1) / G.step / srr, RT, 6) : 0;
      G.ka = G.step > 1.02 ? 1 : 0;
      if (G.ka != 0) bq(G.C, 0, 0, .42 * o->sr / G.step, .7, 0, o->sr);
      { const double k0 = 1 - js_exp(-2 * kPi * 1500 / srr), q = jmin(1, 2 * tone); G.kt = k0 + (1 - k0) * q * q; }   // dark: down to 1.5 kHz
      G.hg = tone > .5 ? (tone - .5) * 2 : 0; G.kx = 1 - js_exp(-2 * kPi * 3000 / srr);
      G.ge = erly < .5 ? dbLin(-24 * (1 - 2 * erly)) : 1 + (erly - .5) * 2;
      for (Syn& y : G.sy) { y.sl = 0; y.D = 0; for (double& v : y.d) v = 0; for (double& v : y.e) v = 0; }
      // its energy, from at most 8192 of the samples it will use (the level is measured properly once it is made)
      double e = 0; const double nU = ok ? jmin(G.srcN, std::ceil(body * srr * G.step) + 1) : 0, q = jmax(1, std::ceil(nU / 8192));
      for (double i = 0; i < nU; i += q) { const double a = L[(int)i], b = G.srcR[(int)i]; if (a == a && b == b) e += (a * a + b * b) * q; }
      const double ev = e / jmax(1e-9, G.step) / 2; Eest = (ev == 0 || ev != ev) ? 1 : ev;   // `|| 1`
    }
    if (kind == 5 || kind == 6) {
      // the filter: a speaker cab (low thump, presence, cone break-up, steep top) or a telephone line (band-pass, honk)
      double* C = G.C;
      if (kind == 5) {
        const double r = std::sqrt(sc);
        bq(C, 0, 1, 70 / r, .8, 0, srr); bq(C, 5, 2, 110 / sc, 1.3, 4, srr); bq(C, 10, 2, (1800 + 1400 * tone) / r, 1.2, 3 + 3 * tone, srr);
        bq(C, 15, 2, 4200 / r, 2, -6, srr); bq(C, 20, 0, (2600 + 4400 * tone) / js_pow(sc, .3), .9, 0, srr); bq(C, 25, 0, (2600 + 4400 * tone) / js_pow(sc, .3), .6, 0, srr);
      } else {
        const double lo = 450 / sc, hi = 3400 * js_pow(sc, .6);
        bq(C, 0, 1, lo, .7, 0, srr); bq(C, 5, 1, lo, 1, 0, srr); bq(C, 10, 2, 1100, 1.5, 3 + 6 * tone, srr);
        bq(C, 15, 2, 2500, 1.2, 2 * tone, srr); bq(C, 20, 0, hi, .7, 0, srr); bq(C, 25, 0, hi, 1, 0, srr);
      }
      // the filter's own energy (its response to a click), and its gain on the room sound, which is darker than white noise
      double* z = G.sy[0].bq; double ef = 0, ei = 0, eo = 0, l1 = 0, l2 = 0; uint32_t r = 0x1234567;
      for (int i = 0; i < 4096; i++) { const double y = cascade(C, z, i == 0 ? 1 : 0); ef += y * y; }
      for (int i = 0; i < 24; i++) z[i] = 0;
      const double kl = 1 - js_exp(-2 * kPi * 3000 / srr);
      for (int i = 0; i < 4096; i++) {
        xs(r); l1 += (((double)r * kU32 - .5) - l1) * kl; l2 += (l1 - l2) * kl;
        const double y = cascade(C, z, l2); if (i >= 1024) { ei += l2 * l2; eo += y * y; }
      }
      for (int i = 0; i < 24; i++) z[i] = 0;
      G.ef = ef; G.rm = .4 * erly;
      // the click, plus the room (its tail rm^2 et and its reflections, 1.5 times that) through the filter
      Eest = ef + (eo / jmax(1e-20, ei)) * G.rm * G.rm * G.et * 2.5;
    }
    G.len = (int)jmin(cap, pre + jmax(0, jmin(js_round(6 * srr), js_round(body * srr))));
    // the early reflections (written in early(), one slice of mirror images at a time): a shoebox room's mirror images (Allen & Berkley), two microphones a little apart
    G.er = er && G.len > pre;
    if (G.er) {
      const double Lx = R[0] * sc, Ly = R[1] * sc, Lz = R[2] * sc; double* g = G.geo;
      g[0] = Lx; g[1] = Ly; g[2] = Lz; g[3] = .27 * Lx; g[4] = .41 * Ly; g[5] = .36 * Lz; g[6] = .68 * Lx; g[7] = .57 * Ly; g[8] = .45 * Lz; g[9] = .1 + .08 * sc;
      g[10] = js_hypot3(g[6] - g[3], g[7] - g[4], g[8] - g[5]); g[11] = R[3];
      G.ord = (int)R[4]; G.erEnd = pre; { const uint32_t e0 = js_to_uint32(G.type * 7919 + 77); G.erR = e0 ? e0 : 1; }
    }
    G.eest = Eest;
    if (!G.er) level(kind != 2 && kind != 5 && kind != 6 && kind != 7 ? G.et : Eest);
  }
  // One slice (mirror images with x index nx) of the early reflections, each bounce a little duller.
  void early(int nx) {
    const double srr = sr; const double* g = G.geo; const int ord = G.ord; const int pre = G.pre0; const double tone = G.tone;
    const double Lx = g[0], Ly = g[1], Lz = g[2], sx = g[3], sy = g[4], sz = g[5], mx = g[6], my = g[7], mz = g[8], dm = g[9], d0 = g[10], beta = g[11];
    uint32_t r = (uint32_t)G.erR; double end = G.erEnd;
    for (int ny = -ord; ny <= ord; ny++) for (int nz = -ord; nz <= ord; nz++) {
      const int k = std::abs(nx) + std::abs(ny) + std::abs(nz); if (!k || k > ord) continue;
      const double ix = nx * Lx + (nx & 1 ? Lx - sx : sx), iy = ny * Ly + (ny & 1 ? Ly - sy : sy), iz = nz * Lz + (nz & 1 ? Lz - sz : sz);
      const double fc = jmin(srr * .45, (1200 + 10000 * tone) * js_pow(.75, k - 1)), a = js_exp(-2 * kPi * fc / srr);
      for (int c = 0; c < 2; c++) {
        xs(r); const double jit = ((double)r * kU32 - .5) * .0006 * k;
        const double d = js_hypot3(ix - mx, iy - (my + (c ? dm : -dm)), iz - mz), t = (d - d0) / 343 + jit;
        if (t < 0) continue;
        const double pos = pre + t * srr, i0 = std::floor(pos), fr = pos - i0, gn = js_pow(beta, k) * d0 / d * (1 - a); F32& A = G.ir[c];
        double v = gn;
        for (int j = 0; j < 160 && i0 + j + 1 < G.len; j++) { const int ii = (int)i0 + j; A[ii] += v * (1 - fr); A[ii + 1] += v * fr; v *= a; if (v < gn * 1e-3) break; }
        if (i0 + 162 > end) end = jmin(G.len, i0 + 162);
      }
    }
    G.erR = r; G.erEnd = end;
  }
  // Scale the reflections to their share of the energy (Early: up to twice it; Cab and Telephone: the room's level).
  void earlyScale() {
    const int pre = G.pre0; const int end = (int)G.erEnd; const int kind = G.kind; const double* R = CNV_ROOM[cnvRoomIdx(G.type)]; const double erly = G.early;
    double e = 0; for (int c = 0; c < 2; c++) { F32& A = G.ir[c]; for (int i = pre; i < end; i++) e += A[i] * A[i]; }
    e *= .5;
    const bool small = kind == 5 || kind == 6; const double share = R[5], wantE = small ? G.rm * G.rm * G.et * share / (1 - share) : G.et * share / (1 - share) * (2 * erly) * (2 * erly);
    const double g = e > 1e-20 ? std::sqrt(wantE / e) : 0;
    for (int c = 0; c < 2; c++) { F32& A = G.ir[c]; for (int i = pre; i < end; i++) A[i] = (double)A[i] * g; }
    level(small ? G.eest : G.et + wantE);
  }
  // Makes the next stretch of the IR (both sides), adding the tail to the reflections already written.
  double synth() {
    const double srr = sr; const int kind = G.kind, pre = G.pre0, n0 = G.n; const int chunk = kind == 2 ? 256 : kind == 5 || kind == 6 ? 512 : 1024, n1 = std::min(G.len, n0 + chunk); const int body = G.len - pre;
    for (int c = 0; c < 2; c++) {
      Syn& y = G.sy[c]; F32& A = G.ir[c]; uint32_t r = y.r; double e = 0;
      if (kind == 2) {
        // the spring: an impulse (with drip) into the loop (two springs: the right one 13% longer and a little more dispersive, so the sides differ)
        F32& D = y.dl; const int N = D.n; const double ls = G.ls * (c ? 1.13 : 1), g = G.g, kt = G.kt, kh = G.kh, kp = G.kp; double* X2 = y.ap; const double a = c ? .6 : .55;
        for (int i = n0; i < n1; i++) {
          double v = 0;
          if (i >= pre) {
            const double x = i == pre ? 1 : 0; y.l1 += (x - y.l1) * kp; const double xin = x + (x - y.l1) * G.drip;
            const double rd = y.w - ls, ri = std::floor(rd), fr = rd - ri, d0 = D[(int)jmod(jmod(ri, N) + N, N)], d1 = D[(int)jmod(jmod(ri + 1, N) + N, N)], dl = d0 + (d1 - d0) * fr;
            y.lp += (dl - y.lp) * kt; y.hp += (y.lp - y.hp) * kh;
            v = xin * .5 + (y.lp - y.hp) * g;
            const int k = y.sl & 1; y.sl++;
            for (int j = 0; j < 18; j++) { const int o = j * 2 + k; const double w = a * v + X2[o] - a * X2[36 + o]; X2[o] = v; X2[36 + o] = w; v = w; }
            if (!(v > -8 && v < 8)) { v = 0; for (int q = 0; q < CNV_SPR * 2; q++) X2[q] = 0; D.fill(0); y.lp = y.hp = 0; }
            D[y.w] = v; y.w = y.w + 1 == N ? 0 : y.w + 1;
            const double fd2 = fade(i - pre, body); v *= fd2;
          }
          A[i] = v; e += v * v;
        }
      } else if (kind == 7) {
        const float* src = c ? G.srcR : G.srcL; const int M = G.srcN; const double step = G.step; double* h = y.d; double* C = G.C; double* z = y.bq; const int nE = (int)js_round(.05 * srr), nX = (int)js_round(.01 * srr);
        for (int i = n0; i < n1; i++) {
          double v = 0;
          if (i >= pre && src) {
            const double pos = (i - pre) * step; const int64_t j = (int64_t)std::floor(pos); const double fr = pos - (double)j;
            // filter the source up to j + 2 (a low-pass first when it is being squeezed), keep the last eight
            while (y.D <= (double)(j + 2)) { double x = y.D < M ? (double)src[(int)y.D] : 0.0; x = x == x ? clamp(x, -64, 64) : 0; if (G.ka != 0) x = cascade2(C, z, x); h[(int64_t)y.D & 3] = x; y.D++; }
            const double p0 = j > 0 ? h[(j - 1) & 3] : 0, p1 = h[j & 3], p2 = h[(j + 1) & 3], p3 = h[(j + 2) & 3];
            // cubic Hermite
            v = p1 + .5 * fr * (p2 - p0 + fr * (2 * p0 - 5 * p1 + 4 * p2 - p3 + fr * (3 * (p1 - p2) + p3 - p0)));
            // Tone: darker (one pole) or a high shelf up to +6 dB
            y.lp += (v - y.lp) * G.kt; v = y.lp;
            if (G.hg > 0) { y.hx += (v - y.hx) * G.kx; v += (v - y.hx) * G.hg; }
            const int t = i - pre; if (t < nE + nX) v *= t < nE ? G.ge : G.ge + (1 - G.ge) * (t - nE) / nX;
            v *= fade(t, body);
          }
          A[i] = v; e += v * v;
        }
      } else {
        // rooms, plate, gated, reverse; and the room around a cab or phone: velvet noise (sparse at first, then dense) through two one-pole low-passes whose cutoff falls as it decays
        const bool small = kind == 5 || kind == 6; double* C = G.C; double* z = y.bq; const int t0 = pre + (int)G.tail0; const double rho0 = G.rho0, tb = G.tb * srr, kr = G.kr, kh = G.kh;
        const double qd = G.qd, qb = G.qb, ab = G.ab, ab2 = 1 - js_exp(-2 * kPi * 220 / srr), nb2 = 1 / std::sqrt(pg2(1 - ab2)), rm = small ? G.rm : 1;
        double l1 = y.l1, l2 = y.l2, b1 = y.e[0], b2 = y.e[1], rp = y.e[2], dd = y.e[3], db = y.d[0], hx = y.hx, cs = y.d[1], ce = y.d[2], vp = y.d[3], amp = y.l3, a = y.lp, nm = y.hp;
        if (n0 == 0) { dd = kind == 4 ? js_exp(-6.91) : 1; db = 1; rp = 0; cs = 0; ce = 0; vp = -1; }
        for (int i = n0; i < n1; i++) {
          double v = 0;
          const int t = i - t0;
          if (t >= 0 && t < body) {
            if ((t & 31) == 0) {
              // the cutoff for the next 32 samples
              const double ts = t / srr, fc = clamp(kind == 4 ? G.fc0 * js_pow(2, -(body / srr - ts) / G.th) : G.fc0 * js_pow(2, -ts / G.th), 150, srr * .45);
              a = js_exp(-2 * kPi * fc / srr); nm = 1;   // not renormalised: what the filter takes away is gone
            }
            double x = 0;
            if (t >= ce) {
              const double ts = t / tb, rho = jmin(6000, rho0 * (1 + ts) * (1 + ts)), Td = jmax(2, srr / rho);
              cs = ce; ce = cs + Td; xs(r); vp = std::floor(cs + (double)r * kU32 * Td);
              amp = std::sqrt(Td) * (r & 1 ? 1 : -1);
            }
            if (t == vp) x = amp;
            l1 += (x - l1) * (1 - a); l2 += (l1 - l2) * (1 - a);
            rp += (1 - rp) * kr;
            double w = kind == 4 ? l2 * nm * dd : l2 * nm * rp * dd;
            if (ab > 0) { xs(r); const double wn = ((double)r * kU32 * 2 - 1) * 1.7320508; b1 += (wn - b1) * ab2; b2 += (b1 - b2) * ab2; w += b2 * nb2 * ab * rp * db; }
            dd *= kind == 4 ? 1 / qd : qd; db *= qb;
            if (dd < 1e-30) dd = 0;
            if (db < 1e-30) db = 0;
            v = w * fade(i - pre, body) * rm;
          }
          v += A[i];
          if (kh > 0) { hx += (v - hx) * kh; v -= hx; }
          if (small) v = cascade(C, z, v + (i == pre ? 1 : 0));
          A[i] = v; e += v * v;
        }
        y.l1 = l1; y.l2 = l2; y.e[0] = b1; y.e[1] = b2; y.e[2] = rp; y.e[3] = dd; y.d[0] = db; y.hx = hx; y.d[1] = cs; y.d[2] = ce; y.d[3] = vp; y.l3 = amp; y.lp = a; y.hp = nm;
      }
      y.r = r; G.E[c] += e;
    }
    G.n = n1;
    return kind == 2 ? 3 : kind == 7 ? 1.5 : kind == 5 || kind == 6 ? 1.2 : 2;
  }

  // ---- state (the JS's create() return value) ---------------------------------------------------------------------------------------------------------------
  double sr; int cap; int P[3], K[3]; const FFTR* F[3] = {nullptr, nullptr, nullptr};
  Slot sl[2]; Gen G; JobS job1, job2; FFTR::Job scr0, scr1;
  int64_t n = 0; int fpos = 0; int act = 0; bool fd = false; int fa = 0, fb = 1; int64_t fs = 0, fe = 0, free = 0, last = 0;
  Spec X[3][2]; int head[3] = {0, 0, 0}; int64_t ticks[3] = {0, 0, 0}; F32 hist[3][2]; int hp[3] = {0, 0, 0}; F32 snap[2]; SpecD Y[3]; Arr<double> x[3];
  F32 fin[2], wq[2];
  double want[6] = {0, 0, 0, 0, 0, 0}; const SongIR* wobj = nullptr; bool whasId = false; uint64_t wid = 0; int64_t chg = 0, chg0 = 0; bool started = false; double mix = -1;
};

template <class M> std::unique_ptr<Module> mk(double sr, Seeds& s) { return std::unique_ptr<Module>(new M(sr, s)); }
}  // namespace

void addConvolveMods(std::vector<const Def*>& v) {
  static const Def kD = {"convolve", PD(kStIn), PD(kStOut), PD(kCnvP), mk<Convolve>};
  v.push_back(&kD);
}

}  // namespace modfx
}  // namespace thunder
