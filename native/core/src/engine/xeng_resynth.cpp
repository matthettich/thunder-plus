// U29: ResynthCore.render / decode (XENG resynth): a sample taken apart into sines (the analysis V.rsData, made on the page by ResynthCore.analyse: not ported, it only runs when a sample is loaded) and played back from its partials:
// Speed, Position, Freeze, Harmonic, Stretch, Tilt, Formant keep, the noise band levels resynthesised as shaped random-phase spectra overlap-added, an ADSR. Without an analysis it plays a soft sine.
// index.html ResynthCore.decode and render, line for line. Bounds: parts <= 4096, frames x parts <= 2^22; a negative count (the JS's RangeError), zero frames and rat / hn entries that are not finite numbers are refused.
#include "engine/xeng_util.h"

namespace thunder {
using namespace xe;

namespace {
constexpr int kNB = 8;
const double kEdges[9] = {0, 320, 640, 1280, 2560, 5120, 9600, 14400, 22050}, kCentres[8] = {190, 453, 905, 1810, 3620, 7010, 11760, 17800};

struct Dec { int P = 0, F = 0; double hop = 0, f0 = 0, dur = 0; const float* la = nullptr; const float* ln = nullptr; const float* pc = nullptr; bool pitched = false; };

// the stable sort of an index array by key[] (Array.prototype.sort is stable): bottom-up merge, tmp the same size
void sort_idx(int* a, int* tmp, int n, const double* key) {
  for (int w = 1; w < n; w <<= 1) {
    for (int lo = 0; lo < n; lo += 2 * w) {
      const int mid = lo + w < n ? lo + w : n, hi = lo + 2 * w < n ? lo + 2 * w : n; int i = lo, j = mid, k = lo;
      while (i < mid && j < hi) { if (key[a[j]] < key[a[i]]) tmp[k++] = a[j++]; else tmp[k++] = a[i++]; }
      while (i < mid) tmp[k++] = a[i++];
      while (j < hi) tmp[k++] = a[j++];
    }
    for (int i = 0; i < n; i++) a[i] = tmp[i];
  }
}
// lq(q, ref) of decode: q ? Math.log(ref) + ((q - 1) * 90 / 254 - 90) * 0.11512925 : -30
inline double lq(int q, double lref) { return q ? lref + (((double)(q - 1) * 90 / 254) - 90) * 0.11512925 : -30; }
}  // namespace

void xeng_rs_from_json(XRsData& rs, Json o) {
  rs = XRsData(); rs.present = o.isObj(); if (!rs.present) return;
  const double nan = std::nan("");
  rs.np = o.num("np", nan); rs.nf = o.num("nf", nan); rs.hop = o.num("hop", nan); rs.f0 = o.num("f0", nan); rs.dur = o.num("dur", nan); rs.pk = o.num("pk", nan); rs.npk = o.num("npk", nan);
  { Json t = o.get("pitched"); switch (t.type()) { case JType::Number: rs.pitched = t.asNum() != 0 && t.asNum() == t.asNum(); break; case JType::Bool: rs.pitched = t.asBool(); break; case JType::String: rs.pitched = !t.asStr().empty(); break; case JType::Array: case JType::Object: rs.pitched = true; break; default: break; } }
  rs.a = std::string(o.str("a")); rs.n = std::string(o.str("n")); rs.pc = std::string(o.str("pc"));
  Json r = o.arr("rat"); for (size_t i = 0; i < r.size(); i++) { Json e = r.at(i); rs.rat.push_back(e.isNum() ? e.asNum() : e.isNull() ? 0 : e.isBool() ? (e.asBool() ? 1 : 0) : nan); }
  Json hn = o.arr("hn"); for (size_t i = 0; i < hn.size(); i++) { Json e = hn.at(i); rs.hn.push_back(e.isNum() ? js_to_int32(e.asNum()) : e.isBool() ? (e.asBool() ? 1 : 0) : 0); }
}
void xeng_wt_from_json(XWtData& wt, Json o) {
  wt = XWtData(); wt.present = o.isObj(); if (!wt.present) return;
  const double nan = std::nan("");
  wt.nf = o.num("nf", nan); wt.h = o.num("h", nan); wt.pk = o.num("pk", nan); wt.a = std::string(o.str("a"));
  wt.valid = o.isObj() && o.get("v").isNum() && o.get("v").asNum() == 1 && wt.nf >= 1 && wt.h >= 1 && o.get("a").isStr() && !wt.a.empty();   // TableCore.wtValid
}

size_t xeng_resynth(const ResynthP& p, double sr, double hold, const XRsData* rs, float* out, size_t cap) {
  const double f0 = 65.406 * js_pow(2, p.oct + orz(p.tune) / 12);
  const double atk = mx(0.0005, p.atk / 1000), dec = mx(0.001, p.dec / 1000), rel = mx(0.001, p.rel / 1000), sus = cl3(p.sus, 0, 1);
  const bool usable = rs && rs->present && rs->np > 0 && rs->nf > 0 && !rs->a.empty();   // rs && rs.np > 0 && rs.nf > 0 && rs.a
  int P = 0, F = 0;
  if (usable) {
    P = js_to_int32(rs->np); F = js_to_int32(rs->nf);   // decode: rs.np | 0, rs.nf | 0
    if (P < 0 || F < 0 || (double)P > 4096 || (double)P * (double)F > 4194304.0) return kRefuse;   // new Float32Array(P * F): a RangeError, or more than a sample's analysis can be
    if (P > 0 && F < 1) return kRefuse;   // (no frames: the JS reads past an empty array and plays NaN)
  }
  XArena& ar = xarena(); if (out) ar.reset();
  if (!usable || P == 0) {   // nothing analysed: a soft sine
    const double h = mx(0.02, orv(hold, 1)), len = mn(12, h + rel * 6 + 0.05), Nd = mx(1, std::ceil(len * sr));
    if (!okN(Nd)) return 0;
    const size_t N = (size_t)Nd;
    if (!out) return N;
    if (N > cap) return 0;
    double env = 0, st = 0;
    for (size_t i = 0; i < N; i++) {
      const double t = (double)i / sr;
      if (t < h) { if (st == 0) { env += 1 / (atk * sr); if (env >= 1) { env = 1; st = 1; } } else env += (sus - env) * 2.2 / (dec * sr); } else env -= env * 3 / (rel * sr);
      const double a = 2 * kPi * f0 * t; out[i] = (float)((js_sin(a) + 0.08 * js_sin(2 * a)) * env);
    }
    return finish(out, N, p.level, p.drive, sr, 0.003);
  }
  // the analysis unpacked (decode): log amplitudes (natural log, absolute), noise band levels, the pitch curve in cents
  const size_t nPF = (size_t)P * (size_t)F;
  Dec d; d.P = P; d.F = F; d.hop = mx(1e-4, orv(rs->hop, 0.01)); d.f0 = rs->f0 > 0 ? rs->f0 : 261.63; d.dur = orz(rs->dur); d.pitched = rs->pitched;
  const size_t K0 = (size_t)P;
  if (!out) {   // the length alone (below), the validation above
    const double dur0 = mx(d.hop, orv(d.dur, F * d.hop)), T0 = p.orig >= 0.5 ? js_pow(2, orz(p.tune) / 12) : f0 / d.f0; (void)T0;
    const double speed0 = cl3(p.speed, 0.05, 8), start0 = cl3(p.pos, 0, 0.999) * dur0, freeze0 = p.freeze >= 0.5;
    const double natural0 = freeze0 ? 1e9 : mx(0.02, (dur0 - start0) / speed0), h0 = hold > 0 ? hold : freeze0 ? 2 : natural0;
    const double len0 = mn(12, mn(h0, natural0) + rel * 6 + 0.05), Nd0 = mx(1, std::ceil(len0 * sr));
    return okN(Nd0) ? (size_t)Nd0 : 0;
  }
  size_t nb = 0;
  float* la = ar.get<float>(nPF ? nPF : 1); if (!la) return kRefuse;
  { const uint8_t* au = spec_unb64(ar, rs->a, &nb); if (!au) return kRefuse; const double lref = js_log(orv(rs->pk, 1));
    for (size_t i = 0; i < nPF; i++) la[i] = (float)lq(i < nb ? au[i] : 0, lref); }   // au[i] || 0
  d.la = la;
  if (!rs->n.empty()) {   // nu = rs.n ? unb64(rs.n) : null
    float* ln = ar.get<float>((size_t)kNB * (size_t)F); if (!ln) return kRefuse; size_t nn = 0; const uint8_t* nu = spec_unb64(ar, rs->n, &nn); if (!nu) return kRefuse; const double lref = js_log(orv(rs->npk, 1));
    for (size_t i = 0; i < (size_t)kNB * (size_t)F; i++) ln[i] = (float)lq(i < nn ? nu[i] : 0, lref);
    d.ln = ln;
  }
  if (!rs->pc.empty()) {   // the pitch curve: Int16 little-endian tenths of a cent
    size_t np2 = 0; const uint8_t* u = spec_unb64(ar, rs->pc, &np2); if (!u) return kRefuse;
    if (np2 >= 2 * (size_t)F) { float* pc = ar.get<float>((size_t)F); if (!pc) return kRefuse; for (int f = 0; f < F; f++) pc[f] = (float)((double)(int16_t)(uint16_t)(u[2 * f] | (u[2 * f + 1] << 8)) / 10); d.pc = pc; }
  }
  auto hnAt = [&](size_t k) -> int32_t { return k < rs->hn.size() ? rs->hn[k] : 0; };
  // ---- render
  const double hop = d.hop, dur = mx(hop, orv(d.dur, F * hop)), T = p.orig >= 0.5 ? js_pow(2, orz(p.tune) / 12) : f0 / d.f0, f0p = d.f0 * T;
  const double speed = cl3(p.speed, 0.05, 8), start = cl3(p.pos, 0, 0.999) * dur; const bool freeze = p.freeze >= 0.5;
  const double natural = freeze ? 1e9 : mx(0.02, (dur - start) / speed), h = hold > 0 ? hold : freeze ? 2 : natural;
  const double len = mn(12, mn(h, natural) + rel * 6 + 0.05), Nd = mx(1, std::ceil(len * sr));
  if (!okN(Nd)) return 0;
  const size_t N = (size_t)Nd;
  if (N > cap) return 0;
  KeysRnd rnd(0x4e5a);
  // the partials: snapped toward harmonics, stretched, transposed
  const int K = cidx(cl3(js_round(p.parts), 1, (double)P), 1, P); const double hm = cl3(p.harm, 0, 1), sk = cl3(p.stretch, -1, 1) * 0.12;
  double* r2 = ar.get<double>((size_t)K); double* r0 = ar.get<double>((size_t)K);
  if (!r2 || !r0) return kRefuse;
  for (int k = 0; k < K; k++) {
    if ((size_t)k >= rs->rat.size() || !std::isfinite(rs->rat[(size_t)k])) return kRefuse;   // (a missing / non-numeric ratio: NaN in the JS)
    const double r = rs->rat[(size_t)k], tgt = hnAt((size_t)k) > 0 ? (double)hnAt((size_t)k) : mx(1, js_round(r)); double q = r + (tgt - r) * hm;
    if (truthy(sk)) q = js_pow(q, 1 + sk); r0[k] = r; r2[k] = q;
  }
  // Formant keep: the original spectral envelope (through the harmonics of a pitched sound, every partial of an unpitched one, in log frequency) is read where each partial was and where it now sits
  const double fk = cl3(p.fkeep, 0, 1);
  int* ord = ar.get<int>((size_t)K); int* tmp = ar.get<int>((size_t)K); if (!ord || !tmp) return kRefuse; int no = 0;
  for (int k = 0; k < K; k++) if (!d.pitched || hnAt((size_t)k) > 0) ord[no++] = k;
  sort_idx(ord, tmp, no, r0);
  double* ex = ar.get<double>((size_t)K); double* lr0 = ar.get<double>((size_t)K); double* tq = ar.get<double>((size_t)K); double* tiltL = ar.get<double>((size_t)K);
  if (!ex || !lr0 || !tq || !tiltL) return kRefuse;
  for (int q = 0; q < no; q++) ex[q] = js_log2(mx(1e-3, r0[ord[q]]));
  for (int k = 0; k < K; k++) { lr0[k] = js_log2(mx(1e-3, r0[k])); tq[k] = js_log2(mx(1e-3, r2[k] * T)); }
  const double tilt = cl3(p.tilt, -1, 1); for (int k = 0; k < K; k++) tiltL[k] = tilt * 1.2 * js_log(mx(1e-3, r2[k]));
  const bool pcOn = d.pc && p.pmot > 0.001; const double pm = cl3(p.pmot, 0, 1);
  // phasors: Schroeder phases for a pitched sound (low crest), seeded random for an inharmonic one
  double* c = ar.get<double>((size_t)K); double* s = ar.get<double>((size_t)K); double* wc = ar.get<double>((size_t)K); double* ws = ar.get<double>((size_t)K);
  double* bc = ar.get<double>((size_t)K); double* bs = ar.get<double>((size_t)K); double* a = ar.get<double>((size_t)K); double* da = ar.get<double>((size_t)K);
  if (!c || !s || !wc || !ws || !bc || !bs || !a || !da) return kRefuse;
  for (int k = 0; k < K; k++) { const double ph0 = d.pitched && hnAt((size_t)k) > 0 ? kPi * hnAt((size_t)k) * hnAt((size_t)k) / mx(8, K) : rnd() * 2 * kPi; c[k] = js_cos(ph0); s[k] = js_sin(ph0); }
  const int CR = 32; const double nyA = sr * 0.42, nyB = sr * 0.45;
  double lastRatio = -1;
  auto setFreq = [&](double ratio) {   // ratio: the pitch curve's factor now
    for (int k = 0; k < K; k++) { const double f = f0p * ratio * r2[k], w = 2 * kPi * mn(f, nyB) / sr; wc[k] = js_cos(w); ws[k] = js_sin(w); bc[k] = js_cos(w * CR); bs[k] = js_sin(w * CR); }
    lastRatio = ratio;
  };
  auto fpos = [&](double t) { return freeze ? start / hop : (start + speed * t) / hop; };
  auto pcAt = [&](double fp) -> double {
    if (!pcOn) return 1;
    const double i = mx(0, mn(F - 1, std::floor(fp))), j = mn(F - 1, i + 1), fr = mx(0, mn(1, fp - i));
    return js_pow(2, pm * (fget(d.pc, F, i) + (fget(d.pc, F, j) - fget(d.pc, F, i)) * fr) / 1200);
  };
  const float* laP = d.la; double* envL = ar.get<double>((size_t)K); double* xs = ar.get<double>((size_t)K); double* ys = ar.get<double>((size_t)K);
  if (!envL || !xs || !ys) return kRefuse;
  auto target = [&](double t, double* out1) -> double {   // amplitudes at time t into out1, and the pitch ratio
    const double fp = fpos(t), i = std::floor(fp), fr = fp - i, ratio = pcAt(fp);
    const double fade = fp > F - 1 ? mx(0, 1 - (fp - (F - 1))) : 1, i0d = mx(0, mn(F - 1, i)), i1d = mx(0, mn(F - 1, i + 1)), frc = fp < 0 ? 0 : fr;
    const size_t i0 = (size_t)cidx(i0d, 0, F - 1), i1 = (size_t)cidx(i1d, 0, F - 1);
    for (int k = 0; k < K; k++) { const size_t b = (size_t)k * (size_t)F; envL[k] = (double)laP[b + i0] + ((double)laP[b + i1] - (double)laP[b + i0]) * frc; }
    if (fk > 0) {
      int m = 0; for (int q = 0; q < no; q++) { const int k = ord[q]; if (envL[k] > -25) { xs[m] = ex[q]; ys[m] = envL[k]; m++; } }
      // the envelope at log-ratio u: linear between the points, flat below, falling 12 dB an octave above
      auto at = [&](double u) -> double {
        if (u <= xs[0]) return ys[0]; if (u >= xs[m - 1]) return ys[m - 1] - (u - xs[m - 1]) * 1.38;
        int lo = 0, hi = m - 1; while (hi - lo > 1) { const int md = (lo + hi) >> 1; if (xs[md] <= u) lo = md; else hi = md; }
        return ys[lo] + (ys[hi] - ys[lo]) * (u - xs[lo]) / mx(1e-9, xs[hi] - xs[lo]);
      };
      for (int k = 0; k < K; k++) out1[k] = m ? envL[k] + (at(tq[k]) - at(lr0[k])) * fk : envL[k];
    } else for (int k = 0; k < K; k++) out1[k] = envL[k];
    for (int k = 0; k < K; k++) {
      const double f = f0p * ratio * r2[k]; double g = out1[k] < -24 ? 0 : js_exp(out1[k] + tiltL[k]) * fade;
      if (f > nyA) g *= f >= nyB ? 0 : (nyB - f) / (nyB - nyA); if (f < 20) g = 0;
      out1[k] = g;
    }
    return ratio;
  };
  double* A1 = ar.get<double>((size_t)K); int* live = ar.get<int>((size_t)K); if (!A1 || !live) return kRefuse;
  setFreq(target(0, a));
  // the noise (Serra's stochastic part): random-phase spectra shaped by the eight band levels, inverse FFTs overlap-added with a sine window. Two frames come out of each complex FFT.
  const bool nzOn = d.ln && p.noise > 0.001; const double Tn = js_pow(T, 1 - fk);
  float* nzBuf = nullptr;
  if (nzOn) {
    const int Ln = sr > 64000 ? 1024 : 512, hp = Ln >> 1, H = Ln >> 1; const double nfr = std::ceil((double)N / hp) + 2;
    double* w = ar.get<double>((size_t)Ln); double lc[kNB], ld[kNB];
    if (!w) return kRefuse;
    for (int j = 0; j < Ln; j++) w[j] = js_sin(kPi * (j + 0.5) / Ln);
    for (int b = 0; b < kNB; b++) lc[b] = js_log2(kCentres[b] * Tn);
    int* kl = ar.get<int>((size_t)H); double* ku = ar.get<double>((size_t)H);   // for each bin: the band centre below it and how far to the next
    double* Zr = ar.get<double>((size_t)Ln); double* Zi = ar.get<double>((size_t)Ln); double* M = ar.get<double>((size_t)H);
    nzBuf = ar.get<float>(N + (size_t)Ln);
    if (!kl || !ku || !Zr || !Zi || !M || !nzBuf) return kRefuse;
    for (int k = 1; k < H; k++) { const double lf = js_log2(k * sr / Ln); int b = 0; while (b < kNB - 2 && lf > lc[b + 1]) b++; kl[k] = b; ku[k] = mx(0, mn(1, (lf - lc[b]) / (lc[b + 1] - lc[b]))); }
    const double sc = p.noise / Ln; const int nb2 = (int)mn((double)H, std::floor(0.45 * Ln));
    const float* lnP = d.ln;
    auto shape = [&](double t, double* out2) -> bool {   // the magnitude per bin for time t (unscaled IFFT: 1/Ln folded in)
      const double fp = fpos(t), i0d = mx(0, mn(F - 1, std::floor(fp))); const size_t i0 = (size_t)cidx(i0d, 0, F - 1), i1 = (size_t)cidx(mn(F - 1, i0d + 1), 0, F - 1);
      const double fr = mx(0, mn(1, fp - i0d)), fade = fp > F - 1 ? mx(0, 1 - (fp - (F - 1))) : 1; bool any = false;
      for (int b = 0; b < kNB; b++) {
        const size_t bb = (size_t)b * (size_t)F; const double l = (double)lnP[bb + i0] + ((double)lnP[bb + i1] - (double)lnP[bb + i0]) * fr;
        // ln of the power density (power / Hz) at the band centre, transposed (the power is kept, spread over Tn x the width)
        ld[b] = l < -24 ? -80 : 2 * l + 2 * js_log(fade + 1e-9) - js_log((kEdges[b + 1] - kEdges[b]) * Tn); if (ld[b] > -79) any = true;
      }
      if (!any) return false;
      for (int k = 1; k < H; k++) { const int b = kl[k]; const double u = ku[k], lg = ld[b] + (ld[b + 1] - ld[b]) * u; out2[k] = k < nb2 && lg > -79 ? std::sqrt(0.75 * js_exp(lg) * sr * Ln) : 0; }
      return true;
    };
    // frame j covers samples j*hp ... j*hp + Ln (its centre at j*hp + hp); frame -1 makes the start full level
    for (double j = -1; j < nfr; j += 2) {
      for (int k = 0; k < Ln; k++) { Zr[k] = 0; Zi[k] = 0; } bool on = false;
      for (int q = 0; q < 2; q++) {
        if (!shape(mx(0, (j + q) * hp + hp) / sr, M)) continue; on = true;
        // frame j+q goes in the real part (q 0) or the imaginary part (q 1): Z = A + iB with A, B Hermitian
        for (int k = 1; k < H; k++) {
          const double m = M[k]; if (!truthy(m)) continue; const double u = (rnd() * 2 - 1) * m, v = (rnd() * 2 - 1) * m;
          if (q == 0) { Zr[k] += u; Zi[k] += v; Zr[Ln - k] += u; Zi[Ln - k] -= v; } else { Zr[k] -= v; Zi[k] += u; Zr[Ln - k] += v; Zi[Ln - k] += u; }
        }
      }
      if (!on) continue;
      spec_fft(Zr, Zi, Ln, true);
      for (int q = 0; q < 2; q++) { const double o = (j + q) * hp; const double* Z = q ? Zi : Zr; for (int n = 0; n < Ln; n++) { const double ix = o + n; if (ix >= 0 && ix < (double)N) { const size_t id = (size_t)ix; nzBuf[id] = (float)((double)nzBuf[id] + Z[n] * w[n] * sc); } } }
    }
  }
  double env = 0, st = 0;
  int* liveP = live;
  for (size_t i0 = 0; i0 < N; i0 += (size_t)CR) {
    const size_t i1 = i0 + (size_t)CR < N ? i0 + (size_t)CR : N, n = i1 - i0; const double ratio = target((double)i1 / sr, A1);
    for (int k = 0; k < K; k++) da[k] = (A1[k] - a[k]) / (double)n;
    if (pcOn && jabs(ratio / lastRatio - 1) > 2e-5) setFreq(ratio);
    // silent partials skip the block in one turn of their phasor
    int act = 0;
    for (int k = 0; k < K; k++) { if (a[k] < 1e-7 && A1[k] < 1e-7) { const double c0 = c[k]; c[k] = c0 * bc[k] - s[k] * bs[k]; s[k] = s[k] * bc[k] + c0 * bs[k]; a[k] = A1[k]; } else liveP[act++] = k; }
    for (size_t i = i0; i < i1; i++) {
      const double t = (double)i / sr;
      if (t < h) { if (st == 0) { env += 1 / (atk * sr); if (env >= 1) { env = 1; st = 1; } } else env += (sus - env) * 2.2 / (dec * sr); }
      else env -= env * 3 / (rel * sr);
      double y = 0;
      for (int q = 0; q < act; q++) { const int k = liveP[q]; const double c0 = c[k], s0 = s[k]; y += s0 * a[k]; a[k] += da[k]; c[k] = c0 * wc[k] - s0 * ws[k]; s[k] = s0 * wc[k] + c0 * ws[k]; }
      if (nzBuf) y += (double)nzBuf[i];
      out[i] = (float)(y * env);
    }
    if ((i0 & 4095) == 0) for (int k = 0; k < K; k++) { const double m = 1 / js_hypot2(c[k], s[k]); c[k] *= m; s[k] *= m; }
  }
  (void)K0;
  return finish(out, N, p.level, p.drive, sr, 0.003);
}

}  // namespace thunder
