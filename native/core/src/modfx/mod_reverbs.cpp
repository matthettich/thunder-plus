// MODFX reverbs (chains-worklet.js 1143 to 1346): Spring, Plate (Dattorro) and Lite reverb. See modfx.h for the rules; each process() keeps the JS's statements.
#include <vector>

#include "modfx/mods.h"
#include "modfx/util.h"

namespace thunder {
namespace modfx {
namespace {

const PortDef kStIn[] = {{"in", "audio"}, {"inR", "audio"}}, kStOut[] = {{"out", "audio"}, {"outR", "audio"}};

// ---- Spring reverb -------------------------------------------------------------------------------------------------------------------
constexpr int SPR_N = 18;
const ParamDef kSpringP[] = {{"decay", .6, 0, 1, .01}, {"tone", .5, 0, 1, .01}, {"chirp", .6, 0, 1, .01}, {"length", .5, 0, 1, .01}, {"drip", .4, 0, 1, .01}, {"mix", .35, 0, 1, .01}};
class Spring : public Module {
 public:
  Spring(double sr, Seeds& sd) {
    for (int c = 0; c < 2; c++) { s_[c].d.init((int)std::ceil(sr * .2) + 4); s_[c].x.init(SPR_N * 2); s_[c].y.init(SPR_N * 2); }
    r_ = Rng(sd.next());
  }
  void arm(Seeds& sd) override { r_ = Rng(sd.next()); }   // U30
  void process(const IO& io, const Ctx& ctx) override {
    const double* p = io.p; const double sr = ctx.sr; const In L = inp(io, 0), R = inR(io, 0, 1); const Out oL = outp(io, 0), oR = outp(io, 1);
    const double g = .55 + p[0] * .43, a = .3 + p[2] * .45, kl = 1 - js_exp(-2 * kPi * (1500 * js_pow(6, p[1])) / sr);
    const double kh = 1 - js_exp(-2 * kPi * 140 / sr), kp = 1 - js_exp(-2 * kPi * 900 / sr), drip = p[4] * 3, mx = p[5]; double rv[2]; rvMix(mx, rv); const double dg = rv[0], wg = rv[1];
    const double base = (.028 + p[3] * .05) * sr;
    for (int c = 0; c < 2; c++) {
      Tank& s = s_[c]; const In X = c ? R : L; const Out O = c ? oR : oL; F32& D = s.d; const int N = D.n; const double len = base * (c ? 1.13 : 1);
      double t = t_;
      for (int i = 0; i < ctx.block; i++) {
        const double x = X[i];
        s.pre += (x - s.pre) * kp; const double xin = x + (x - s.pre) * drip;
        { const int32_t ti = i32(t); t++; if ((ti & 255) == 0) s.wob = r_() * 2 - 1; }
        const double rd = s.w - len * (1 + s.wob * .0015); const double ri = std::floor(rd), fr = rd - ri;
        const double d0 = rdf(D.get(), N, jmod(jmod(ri, N) + N, N)), d1 = rdf(D.get(), N, jmod(jmod(ri + 1, N) + N, N)), dl = d0 + (d1 - d0) * fr;
        s.lp += (dl - s.lp) * kl; s.hp += (s.lp - s.hp) * kh;
        double v = xin * .5 + (s.lp - s.hp) * g;
        const int k = i32(t) & 1; F32& X2 = s.x; F32& Y2 = s.y;
        for (int j = 0; j < SPR_N; j++) {
          const int o = j * 2 + k; const double y = a * v + X2[o] - a * Y2[o];
          X2[o] = v; Y2[o] = y; v = y;
        }
        if (!(v > -8 && v < 8)) { v = 0; X2.fill(0); Y2.fill(0); s.lp = s.hp = 0; }
        D[s.w] = v; s.w = s.w + 1 == N ? 0 : s.w + 1;
        O[i] = x * dg + v * wg * 1.4;
      }
      if (c) t_ = t;
    }
  }
 private:
  struct Tank { F32 d; int w = 0; F32 x, y; double lp = 0, hp = 0, pre = 0, fb = 0, wob = 0; };
  Tank s_[2]; double t_ = 0; Rng r_;
};

// ---- Plate reverb --------------------------------------------------------------------------------------------------------------------
const double PL_sr = 29761;
const double PL_ind[4] = {142, 107, 379, 277}, PL_ap1[2] = {672, 908}, PL_dl1[2] = {4453, 4217}, PL_ap2[2] = {1800, 2656}, PL_dl2[2] = {3720, 3163};
// taps: [sign, tank node, offset]
const double PL_tapsL[7][3] = {{1, 0, 266}, {1, 0, 2974}, {-1, 1, 1913}, {1, 2, 1996}, {-1, 3, 1990}, {-1, 4, 187}, {-1, 5, 1066}};
const double PL_tapsR[7][3] = {{1, 3, 353}, {1, 3, 3627}, {-1, 4, 1228}, {1, 5, 2673}, {-1, 0, 2111}, {-1, 1, 335}, {-1, 2, 121}};
struct PlBuf {
  F32 b; int n = 0, w = 0;
  void init(int n_) { b.init(n_); n = n_; w = 0; }
  // plAt(d, k): the sample written k samples ago (an index outside the array is undefined: NaN)
  double at(double k) const { double i = w - k; if (i < 0) i += n; return rdf(b.d.get(), n, i); }
  void push(double v) { b[w] = v; if (++w == n) w = 0; }
};
const ParamDef kPlateP[] = {{"size", .7, .2, 1, .01}, {"decay", .6, 0, .97, .01}, {"damp", .4, 0, 1, .01}, {"pre", .01, 0, .25, .001}, {"mod", .3, 0, 1, .01}, {"mix", .3, 0, 1, .01}};
class Plate : public Module {
 public:
  Plate(double sr, Seeds&) {
    s_ = sr / PL_sr; const double s = s_;
    auto mkn = [&](double n) { return (int)std::ceil(n * s) + 40; };
    pre_.init((int)std::ceil(sr * .26) + 2);
    for (int j = 0; j < 4; j++) { ind_[j].init(mkn(PL_ind[j])); indK_[j] = js_round(PL_ind[j] * s); }
    for (int j = 0; j < 2; j++) { ap1_[j].init(mkn(PL_ap1[j] + 32)); dl1_[j].init(mkn(PL_dl1[j])); ap2_[j].init(mkn(PL_ap2[j])); dl2_[j].init(mkn(PL_dl2[j])); }
    PlBuf* node[6] = {&dl1_[1], &ap2_[1], &dl2_[1], &dl1_[0], &ap2_[0], &dl2_[0]};
    for (int j = 0; j < 7; j++) { nl_[j] = node[(int)PL_tapsL[j][1]]; nr_[j] = node[(int)PL_tapsR[j][1]]; }
  }
  void process(const IO& io, const Ctx& ctx) override {
    const double* p = io.p; const double s = s_; const In L = inp(io, 0), R = inR(io, 0, 1); const Out oL = outp(io, 0), oR = outp(io, 1); const double mx = p[5]; double rv[2]; rvMix(mx, rv); const double dg = rv[0], wg = rv[1];
    const double sz = p[0], dec = p[1], dd2 = clamp(dec + .15, .25, .5), kd = 1 - p[2] * .9, sc = s * sz;
    const double pre = jmin(pre_.n - 1, js_round(p[3] * ctx.sr) + 1), exc = p[4] * 16 * s;
    const double a1k0 = PL_ap1[0] * sc, a1k1 = PL_ap1[1] * sc, d1k0 = js_round(PL_dl1[0] * sc), d1k1 = js_round(PL_dl1[1] * sc);
    const double a2k0 = js_round(PL_ap2[0] * sc), a2k1 = js_round(PL_ap2[1] * sc), d2k0 = js_round(PL_dl2[0] * sc), d2k1 = js_round(PL_dl2[1] * sc);
    int32_t tl[7], tr[7];
    for (int j = 0; j < 7; j++) { tl[j] = i32(jmax(1, js_round(PL_tapsL[j][2] * sc))); tr[j] = i32(jmax(1, js_round(PL_tapsR[j][2] * sc))); }
    const double dph = .9 / ctx.sr;
    double lp0 = lp0_, lp1 = lp1_, fb0 = fb0_, fb1 = fb1_, bw = bw_, ph = ph_;
    for (int i = 0; i < ctx.block; i++) {
      pre_.push((L[i] + R[i]) * .5);
      bw += (pre_.at(pre) - bw) * .9995;
      double x = bw;
      for (int j = 0; j < 4; j++) { const double g = j < 2 ? .75 : .625, y = -g * x + ind_[j].at(indK_[j]); ind_[j].push(x + g * y); x = y; }
      ph += dph; if (ph >= 1) ph -= 1;
      double v = x + fb1 * dec;
      { const double k = a1k0 + exc * (1 + sinT(ph)); const int32_t ki = i32(k); const double fr = k - ki, z = ap1_[0].at(ki) * (1 - fr) + ap1_[0].at(ki + 1.0) * fr, y = .7 * v + z; ap1_[0].push(v - .7 * y); v = y; }
      dl1_[0].push(v); v = dl1_[0].at(d1k0);
      lp0 += (v - lp0) * kd; v = lp0 * dec;
      { const double y = -dd2 * v + ap2_[0].at(a2k0); ap2_[0].push(v + dd2 * y); v = y; }
      dl2_[0].push(v); v = dl2_[0].at(d2k0); if (!(v > -8 && v < 8)) v = 0;
      const double nf0 = v;
      v = x + fb0 * dec;
      { const double k = a1k1 + exc * (1 + sinT(ph + .25)); const int32_t ki = i32(k); const double fr = k - ki, z = ap1_[1].at(ki) * (1 - fr) + ap1_[1].at(ki + 1.0) * fr, y = .7 * v + z; ap1_[1].push(v - .7 * y); v = y; }
      dl1_[1].push(v); v = dl1_[1].at(d1k1);
      lp1 += (v - lp1) * kd; v = lp1 * dec;
      { const double y = -dd2 * v + ap2_[1].at(a2k1); ap2_[1].push(v + dd2 * y); v = y; }
      dl2_[1].push(v); v = dl2_[1].at(d2k1); if (!(v > -8 && v < 8)) v = 0;
      fb0 = nf0; fb1 = v;
      const double yl = (nl_[0]->at(tl[0]) + nl_[1]->at(tl[1]) - nl_[2]->at(tl[2]) + nl_[3]->at(tl[3]) - nl_[4]->at(tl[4]) - nl_[5]->at(tl[5]) - nl_[6]->at(tl[6])) * .6;
      const double yr = (nr_[0]->at(tr[0]) + nr_[1]->at(tr[1]) - nr_[2]->at(tr[2]) + nr_[3]->at(tr[3]) - nr_[4]->at(tr[4]) - nr_[5]->at(tr[5]) - nr_[6]->at(tr[6])) * .6;
      oL[i] = L[i] * dg + yl * wg; oR[i] = R[i] * dg + yr * wg;
    }
    lp0_ = lp0; lp1_ = lp1; fb0_ = fb0; fb1_ = fb1; bw_ = bw; ph_ = ph;
  }
 private:
  double s_ = 1; PlBuf pre_, ind_[4], ap1_[2], dl1_[2], ap2_[2], dl2_[2]; double indK_[4] = {}; PlBuf* nl_[7]; PlBuf* nr_[7];
  double lp0_ = 0, lp1_ = 0, fb0_ = 0, fb1_ = 0, bw_ = 0, ph_ = 0;
};

// ---- Lite reverb ---------------------------------------------------------------------------------------------------------------------
const double LV_base[4] = {1423, 1709, 2017, 2411}, LV_ap[2] = {142, 107}, LV_apg = .6, LV_tail = 3.2e-5, LV_wake = 1e-7;
inline int lvPow2(double n) { int p = 256; while (p < n) p <<= 1; return p; }
const ParamDef kLverbP[] = {{"size", .5, 0, 1, .01}, {"decay", .5, 0, 1, .01}, {"tone", .6, 0, 1, .01}, {"pre", 0, 0, .1, .001}, {"mix", .3, 0, 1, .01}};
class Lverb : public Module {
 public:
  explicit Lverb(double sr, Seeds&) {
    const double k = jmax(sr, 96000) / 48000; const int n = lvPow2(std::ceil(2411 * k) + 8), pn = lvPow2(std::ceil(.1 * jmax(sr, 96000)) + 8), an = lvPow2(std::ceil(142 * k) + 8);
    m_ = n - 1; pm_ = pn - 1; am_ = an - 1;
    for (int j = 0; j < 4; j++) B_[j].init(n);
    P_.init(pn); A_[0].init(an); A_[1].init(an);
    len_.init(4); lenT_.init(4); g_.init(4); gT_.init(4); lp_.init(4); apk_.init(2);
  }
  void process(const IO& io, const Ctx& ctx) override {
    const double* p = io.p; const In L = inp(io, 0), R = inR(io, 0, 1); const Out oL = outp(io, 0), oR = outp(io, 1); const int N = ctx.block; const double sr = ctx.sr; const int32_t m = m_, pm = pm_, am = am_;
    double rv[2]; rvMix(p[4], rv); const double dgT = rv[0], wgT = rv[1];
    if (p[0] != ps_ || p[1] != pd_ || p[2] != pt_ || p[3] != pp_ || sr != sr_) {
      const double k = sr / 48000, sc = (.15 + .85 * clamp(p[0], 0, 1)) * k, rt = .2 * js_pow(60, clamp(p[1], 0, 1)), lim = m - 2;
      for (int i = 0; i < 4; i++) {
        const double l = jmin(lim, (double)(i32(js_round(LV_base[i] * sc)) | 1)); lenT_[i] = i32(l);
        gT_[i] = jmin(.9995, js_exp(-6.9078 * l / (sr * rt)));
      }
      kdT_ = 1 - js_exp(-6.2832 * 1200 * js_pow(11.7, clamp(p[2], 0, 1)) / sr);
      preT_ = jmin(pm - 2, js_round(clamp(p[3], 0, .1) * sr));
      if (sr != sr_) { apk_[0] = i32(js_round(LV_ap[0] * k)); apk_[1] = i32(js_round(LV_ap[1] * k)); }
      qmax_ = 2 * (double)lenT_[3] + preT_;
      ps_ = p[0]; pd_ = p[1]; pt_ = p[2]; pp_ = p[3]; sr_ = sr;
      if (!init_) { init_ = true; for (int j = 0; j < 4; j++) { len_[j] = lenT_[j]; g_[j] = gT_[j]; } kd_ = kdT_; pre_ = preT_; dg_ = dgT; wg_ = wgT; }
    }
    dg_ += (dgT - dg_) * .35; wg_ += (wgT - wg_) * .35;
    F32& B0 = B_[0]; F32& B1 = B_[1]; F32& B2 = B_[2]; F32& B3 = B_[3]; F32& PB = P_; F32& A0 = A_[0]; F32& A1 = A_[1];
    const double dg = dg_, wg = wg_;
    if (asleep_) {
      double pk = 0; for (int i = 0; i < N; i++) { double a = L[i], b = R[i]; a = a > -8 && a < 8 ? a : 0; b = b > -8 && b < 8 ? b : 0; oL[i] = a * dg; oR[i] = b * dg; const double x = (a < 0 ? -a : a) + (b < 0 ? -b : b); if (x > pk) pk = x; }
      if (pk >= LV_wake) { asleep_ = false; for (int j = 0; j < 4; j++) len_[j] = lenT_[j]; pre_ = preT_; }
      else return;
    }
    for (int i = 0; i < 4; i++) g_[i] += (gT_[i] - g_[i]) * .35;
    kd_ += (kdT_ - kd_) * .35;
    const double g0 = g_[0], g1 = g_[1], g2 = g_[2], g3 = g_[3], kd = kd_; const int32_t k0 = apk_[0], k1 = apk_[1]; const double ag = LV_apg;
    const int32_t o0 = len_[0], o1 = len_[1], o2 = len_[2], o3 = len_[3], n0 = lenT_[0], n1 = lenT_[1], n2 = lenT_[2], n3 = lenT_[3]; const double po = pre_, pn = preT_;
    const bool fd = o0 != n0 || o1 != n1 || o2 != n2 || o3 != n3 || po != pn; const double iN = 1.0 / N;
    int32_t w = w_, pw = pw_, aw = aw_; double lp0 = lp_[0], lp1 = lp_[1], lp2 = lp_[2], lp3 = lp_[3], dn = 1e-18, ipk = 0, opk = 0;
    const bool pnNaN = pn != pn, poNaN = po != po; const int32_t pwi_n = i32(pn), pwi_o = i32(po);   // JS: (pw - NaN) & pm is 0, not pw & pm
    for (int i = 0; i < N; i++) {
      double dl = L[i], dr = R[i]; dl = dl > -8 && dl < 8 ? dl : 0; dr = dr > -8 && dr < 8 ? dr : 0;
      const double a = (dl + dr) * .5, aa = a < 0 ? -a : a; if (aa > ipk) ipk = aa;
      PB[pw & pm] = a;
      double x = PB[pnNaN ? 0 : (pw - pwi_n) & pm], r0 = B0[(w - n0) & m], r1 = B1[(w - n1) & m], r2 = B2[(w - n2) & m], r3 = B3[(w - n3) & m];
      if (fd) {
        const double u = (i + 1) * iN, v = 1 - u;
        x = x * u + PB[poNaN ? 0 : (pw - pwi_o) & pm] * v; r0 = r0 * u + B0[(w - o0) & m] * v; r1 = r1 * u + B1[(w - o1) & m] * v;
        r2 = r2 * u + B2[(w - o2) & m] * v; r3 = r3 * u + B3[(w - o3) & m] * v;
      }
      pw++;
      double y = -ag * x + A0[(aw - k0) & am]; A0[aw & am] = x + ag * y; x = y;
      y = -ag * x + A1[(aw - k1) & am]; A1[aw & am] = x + ag * y; x = y;
      aw++;
      const double s = (r0 + r1 + r2 + r3) * .5, inj = x * .5 + dn; dn = -dn;
      lp0 += ((r0 - s) * g0 - lp0) * kd; lp1 += ((r1 - s) * g1 - lp1) * kd; lp2 += ((r2 - s) * g2 - lp2) * kd; lp3 += ((r3 - s) * g3 - lp3) * kd;
      B0[w & m] = lp0 + inj; B1[w & m] = lp1 + inj; B2[w & m] = lp2 + inj; B3[w & m] = lp3 + inj; w++;
      double yl = (r0 + r1 - r2 - r3) * .75, yr = (r0 - r1 + r2 - r3) * .75;
      yl = yl > 4 ? 4 : yl < -4 ? -4 : yl; yr = yr > 4 ? 4 : yr < -4 ? -4 : yr;
      const double al = yl < 0 ? -yl : yl, ar = yr < 0 ? -yr : yr; if (al > opk) opk = al; if (ar > opk) opk = ar;
      oL[i] = dl * dg + yl * wg; oR[i] = dr * dg + yr * wg;
    }
    w_ = w & m; pw_ = pw & pm; aw_ = aw & am; lp_[0] = lp0; lp_[1] = lp1; lp_[2] = lp2; lp_[3] = lp3;
    for (int j = 0; j < 4; j++) len_[j] = lenT_[j];
    pre_ = pn;
    if (!(lp0 > -1e3 && lp0 < 1e3 && lp1 > -1e3 && lp1 < 1e3 && lp2 > -1e3 && lp2 < 1e3 && lp3 > -1e3 && lp3 < 1e3)) {
      lp_.fill(0); B0.fill(0); B1.fill(0); B2.fill(0); B3.fill(0); PB.fill(0); A0.fill(0); A1.fill(0); q_ = 0; return;
    }
    q_ = ipk < LV_wake && opk < LV_tail ? q_ + N : 0;
    if (q_ >= qmax_) { lp_.fill(0); B0.fill(0); B1.fill(0); B2.fill(0); B3.fill(0); PB.fill(0); A0.fill(0); A1.fill(0); q_ = 0; asleep_ = true; }
  }
 private:
  int32_t m_ = 0, pm_ = 0, am_ = 0, w_ = 0, pw_ = 0, aw_ = 0;
  F32 B_[4], P_, A_[2]; Arr<int32_t> len_, lenT_, apk_; Arr<double> g_, gT_, lp_;
  double sr_ = 0, ps_ = -1, pd_ = -1, pt_ = -1, pp_ = -1, kd_ = .5, kdT_ = .5, pre_ = 0, preT_ = 0, dg_ = 1, wg_ = 0, q_ = 0, qmax_ = 0; bool asleep_ = false, init_ = false;
};

template <class M> std::unique_ptr<Module> mk(double sr, Seeds& s) { return std::unique_ptr<Module>(new M(sr, s)); }
#define PD(a) (int)(sizeof(a) / sizeof(a[0])), a
const Def kDefs[] = {
  {"spring", PD(kStIn), PD(kStOut), PD(kSpringP), mk<Spring>},
  {"plate", PD(kStIn), PD(kStOut), PD(kPlateP), mk<Plate>},
  {"lverb", PD(kStIn), PD(kStOut), PD(kLverbP), mk<Lverb>},
};
}  // namespace

void addReverbMods(std::vector<const Def*>& v) { for (const Def& d : kDefs) v.push_back(&d); }

}  // namespace modfx
}  // namespace thunder
