// U32: see sf_live.h. The JS is the specification (core/core-processor.js: sfNote, sfRun, kill, pitchMods, modVoice, the off / stp handlers); the page's own sfNote (index.html) is the same logic on Web Audio nodes.
#include "sf/sf_live.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "rt/jsmath.h"
#include "sf/sf_math.h"

namespace thunder {
namespace sf {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kNT = kSongNT;
inline void acc(float* p, double v) { *p = (float)((double)*p + v); }   // L[i] += v on a Float32Array

double vibCurve(const double* p, double x) {   // as voice.cpp: base * 2 ** (st * sin(2 pi hz d min(1, max(0, (x - t0) / dd))) / 12)
  return p[0] * js_pow(2, p[1] * js_sin(2 * kPi * p[2] * p[3] * jmin(1, jmax(0, (x - p[4]) / p[5]))) / 12);
}
double expCurve(const double* p, double x) {   // expTo's curve: from * Math.pow(to / from, Math.min(1, Math.max(0, (x - t) / d))); p = {from, to, t, d}
  return p[0] * js_pow(p[1] / p[0], jmin(1, jmax(0, (x - p[2]) / p[3])));
}
// expTo(A, from, to, t, d)
void expTo(Auto& A, double from, double to, double t, double d) {
  if (!(from > 0) || !(to > 0)) { A.lin(to, t + d); return; }
  Curve c; c.fn = expCurve; c.p[0] = from; c.p[1] = to; c.p[2] = t; c.p[3] = d; A.curve(c, t, d);
}

}  // namespace

SfPool::SfPool(double sampleRate, int maxVoices) : sr_(sampleRate), dt_(1 / sampleRate), cap_(maxVoices < 1 ? 1 : maxVoices) {
  V_.reset(new Voice[(size_t)cap_]); alive_.reset(new int[(size_t)cap_]); free_.reset(new int[(size_t)cap_]);
  for (int i = 0; i < cap_; i++) free_[(size_t)i] = cap_ - 1 - i;
  nFree_ = cap_; std::memset(nT_, 0, sizeof nT_); std::memset(T_, 0, sizeof T_); std::memset(sL_, 0, sizeof sL_); std::memset(sR_, 0, sizeof sR_);
}
SfPool::~SfPool() {}

double SfPool::timeOf(uint64_t n) const { return (double)(n & ~(uint64_t)127) / sr_ + (double)(n & 127) * dt_; }

uint32_t SfPool::autoDropped() const {
  uint32_t n = 0;
  for (int i = 0; i < cap_; i++) { const Voice& v = V_[(size_t)i]; n += v.r.dropped + v.g.dropped + v.f.dropped + v.pn.dropped;
    for (int k = 0; k < kZones; k++) n += v.z[k].r.dropped + v.z[k].env.dropped + v.z[k].flt.fr.dropped; }
  return n;
}

void SfPool::tErase(int tr, int k) { for (int q = k; q + 1 < nT_[tr]; q++) T_[tr][q] = T_[tr][q + 1]; nT_[tr]--; }
void SfPool::tRemove(int vi) { const int tr = V_[(size_t)vi].track; for (int k = 0; k < nT_[tr]; k++) if (T_[tr][k] == vi) { tErase(tr, k); return; } }
void SfPool::aliveErase(int ai) { const int vi = alive_[ai]; for (int q = ai; q + 1 < nAlive_; q++) alive_[q] = alive_[q + 1]; nAlive_--; free_[nFree_++] = vi; }

void SfPool::kill(int vi, double at, double tc, bool stop) {
  Voice& v = V_[(size_t)vi];
  if (stop && !v.ended && v.t0 >= at - 1e-6) { v.hasStop = true; v.stopAt = at; v.end = std::min(v.end, v.t0); return; }
  if (v.ended || (v.hasStop && v.stopAt <= at)) return;
  v.hasStop = true; v.stopAt = at; at = jmax(at, now_);
  v.f.cancel(at); v.f.set(1, at); v.f.target(0, at, tc); v.end = jmin(v.end, at + tc * 12);
}

void SfPool::pitchMods(Voice& v, double t, double base, const SeqEvent& e, const double* arp) {
  Auto& pr = v.r;
  if (e.has & SH_ARP) {
    if (pr.size() + e.arpN + 1 > pr.capacity()) {   // as VoicePool::pitchMods (N1 review B5): never drop the "back to base" event
      const double t1 = e.arpN ? std::max(arp[0], t) : t;
      pr.prune(now_);
      if (pr.size() + e.arpN + 1 > pr.capacity()) { pr.prune(t1); pr.cancel(t1); }
    }
    for (int q = 0; q < e.arpN; q++) pr.set(base * arp[2 * q + 1], std::max(arp[2 * q], t));
    if (e.arpN) pr.set(base, arp[2 * (e.arpN - 1)] + (e.arpN > 1 ? arp[2] - arp[0] : 0.02));
  } else if (e.has & SH_BEND) {
    pr.set(base, t); pr.lin(base * e.bendMul, e.bendT1); v.rate = base * e.bendMul;
  } else if ((e.has & SH_VIB) && e.vibDep != 0 && e.vibDep == e.vibDep) {
    const double d = std::max(0.02, e.vibT1 - t), hz = 1 + e.vibSpd * 1.2, st = e.vibDep / 15 * 1.5, t0 = t + 0.0005, dd = d - 0.001;
    pr.set(base, t);
    Curve c; c.fn = vibCurve; c.p[0] = base; c.p[1] = st; c.p[2] = hz; c.p[3] = d; c.p[4] = t0; c.p[5] = dd;
    pr.curve(c, t0, dd);
  }
}

void SfPool::take(const SeqEvent& e, const VoiceKit* kv) {
  if (e.track < 0 || e.track >= kVoiceTracks) return;
  const int tr = e.track;
  for (int k = nT_[tr] - 1; k >= 0; k--) if (!live1(V_[(size_t)T_[tr][k]], e.t)) tErase(tr, k);
  if (!e.poly) { for (int k = 0; k < nT_[tr]; k++) kill(T_[tr][k], e.t, std::min(relTc(V_[(size_t)T_[tr][k]]), 0.05)); nT_[tr] = 0; }
  const int ck = kv ? kv->choke : 0;
  if (ck) for (int k = 0; k < kNT; k++) if (k != e.track) for (int q = 0; q < nT_[k]; q++) if (V_[(size_t)T_[k][q]].choke == ck) kill(T_[k][q], e.t);
}

bool SfPool::covers(const SeqEvent& e, const SfPad& pad) const {
  if (e.track < 0 || e.track >= kVoiceTracks || !pad.font || !pad.pcm) return false;
  if (!(e.rate > 0 && std::isfinite(e.rate)) || !std::isfinite(e.t) || !std::isfinite(e.gain) || !std::isfinite(e.pan)) return false;
  const PadParams& P = pad.P;
  const double lr = js_log2(jmax(1e-4, e.rate));
  const double key = jclamp(js_round(60 + 12 * lr + P.trans), 0, 127), vel = jclamp(js_round(127 * ((1 - P.velo) + P.velo * std::sqrt(jclamp(e.gain, 0, 1)))), 1, 127);
  Pick picks[4];
  return sf_pick(*pad.font, pad.pr, key, vel, picks) > 0;
}

bool SfPool::note(const SeqEvent& e, const double* arp, const SfPad& pad, const VoiceKit& kv) {
  if (e.track < 0 || e.track >= kVoiceTracks || !pad.font || !pad.pcm) return false;
  if (!(e.rate > 0 && std::isfinite(e.rate)) || !std::isfinite(e.t) || !std::isfinite(e.gain) || !std::isfinite(e.pan)) { refused_++; return false; }
  const PadParams& P = pad.P; const Font& font = *pad.font;
  const double lr = js_log2(jmax(1e-4, e.rate));
  const double key = jclamp(js_round(60 + 12 * lr + P.trans), 0, 127), vel = jclamp(js_round(127 * ((1 - P.velo) + P.velo * std::sqrt(jclamp(e.gain, 0, 1)))), 1, 127);
  Pick picks[4]; const int np = sf_pick(font, pad.pr, key, vel, picks);
  if (!np) return false;
  const int tr = e.track;
  for (int k = nT_[tr] - 1; k >= 0; k--) if (!live1(V_[(size_t)T_[tr][k]], e.t)) tErase(tr, k);
  if (!e.poly) { for (int k = 0; k < nT_[tr]; k++) kill(T_[tr][k], e.t, std::min(relTc(V_[(size_t)T_[tr][k]]), 0.05)); nT_[tr] = 0; }
  else while (nT_[tr] >= kVoicePoly) { const int vi = T_[tr][0]; tErase(tr, 0); kill(vi, e.t, 0.006); }
  const int ck = kv.choke;
  if (ck) for (int k = 0; k < kNT; k++) if (k != e.track) for (int q = 0; q < nT_[k]; q++) if (V_[(size_t)T_[k][q]].choke == ck) kill(T_[k][q], e.t);
  // the zones (built in a voice taken from the pool only once we know there is one: a note with no sample at all leaves the track as the takeover left it, as the JS)
  if (nFree_ == 0 || nT_[tr] >= kVoiceTrackCap) { if (nFree_ == 0) dropped_++; else tOver_++; return false; }
  const int vi = free_[--nFree_]; Voice& v = V_[(size_t)vi];
  const double st0 = jmax(e.t, now_), sr = sr_;
  auto hz = [&](double c) { return jclamp(8.176 * js_pow(2, c / 1200), 20, sr * 0.45); };
  v.nz = 0; bool looped = false, stAny = false; double relMax = 0.01, base = e.rate;
  for (int pi = 0; pi < np; pi++) {
    const Zone& zone = *picks[pi].z; const bool stereo = picks[pi].stereo;
    VoiceParams vo; sf_voice(font, zone, key, vel, vo);
    const Pcm* a = pad.pcm->get(zone.s); if (!a) continue;
    const float* L = a->d.data(); const float* R = nullptr; int n = (int)std::min<size_t>(a->d.size(), 0x7fffffff);
    if (stereo) { const SampleHdr& s0 = font.samples[zone.s]; const Pcm* b = pad.pcm->get(s0.link);
      if (b) { const bool left = (s0.type & 4) != 0; L = left ? a->d.data() : b->d.data(); R = left ? b->d.data() : a->d.data();
        n = (int)std::min<size_t>(std::min(a->d.size(), b->d.size()), 0x7fffffff); } }
    Zn& z = v.z[v.nz];
    const double F = js_pow(2, ((60 + P.trans - vo.root) * vo.scale / 100 + vo.tune) / 12), rate = F * js_pow(e.rate, vo.scale / 100);
    if (!(rate > 0 && std::isfinite(rate))) continue;   // (the JS would play a zone whose rate is 0 / NaN / inf as silence or garbage: refused here)
    z.r.reset(rate);
    if ((e.has & SH_SLIDE) && e.slideDur > 0 && vo.scale == 100) { z.r.set(e.slideFrom * F, e.t); z.r.lin(rate, e.t + e.slideDur); }
    if (!v.nz) base = rate;
    const bool lp = (vo.modes == 1 || vo.modes == 3) && a->le > a->ls + 8 && a->le <= n && a->ls >= 0; if (lp) looped = true;
    const auto& E = vo.v; const double lg = js_pow(10, -vo.atten / 200), sus = jmax(1e-5, lg * js_pow(10, -E.sus / 200)), atk = E.atk * P.atk;
    const double t1 = st0 + E.delay, t2 = t1 + jmax(0.0015, atk), t3 = t2 + E.hold;
    z.env.reset(0); z.env.set(0, st0); z.env.set(0, t1); z.env.lin(lg, t2); z.env.set(lg, t3);
    if (E.sus > 0) expTo(z.env, lg, sus, t3, jmax(0.005, E.dec * jmin(1, E.sus / 1000)));
    z.hasFlt = false;
    if (vo.fc < 13490 || std::fabs(vo.modEnvToFc) > 1) {
      const auto& M = vo.m; const double f0 = hz(vo.fc), fp = hz(vo.fc + vo.modEnvToFc), fs = hz(vo.fc + vo.modEnvToFc * (1 - M.sus));
      const double m1 = st0 + M.delay, m2 = m1 + jmax(0.001, M.atk), m3 = m2 + M.hold;
      Flt& F2 = z.flt; F2.b0 = 1; F2.b1 = F2.b2 = F2.a1 = F2.a2 = F2.x1 = F2.x2 = F2.y1 = F2.y2 = F2.u1 = F2.u2 = F2.w1 = F2.w2 = 0; F2.k = 0; F2.fr.reset(f0);
      F2.fr.set(f0, st0); F2.fr.set(f0, m1); expTo(F2.fr, f0, fp, m1, m2 - m1); F2.fr.set(fp, m3); expTo(F2.fr, fp, fs, m3, jmax(0.005, M.dec * jmax(0.001, M.sus)));
      F2.q = vo.q / 10; z.hasFlt = true;
    }
    z.hasZp = !R && vo.pan != 0; z.zp = z.hasZp ? vo.pan / 500 : 0; if (R || z.hasZp) stAny = true;
    z.L = L; z.R = R; z.n = n; z.sr = a->sr; z.ls = a->ls; z.le = a->le; z.lp = lp; z.done = false; z.pos = 0; z.rate = rate;
    v.nz++; relMax = jmax(relMax, jmin(8, E.rel * P.rel));
  }
  if (!v.nz) { free_[nFree_++] = vi; return false; }
  const double g0 = e.gain * js_pow(10, (orv(P.level, 0) + 6) / 20);
  v.st = stAny; v.r.reset(base); v.g.reset(g0); v.f.reset(1); v.pn.reset(jclamp((e.pan - 128) / 127, -1, 1));
  v.base = base; v.rate = base; v.choke = ck; v.col = e.col; v.ended = false; v.hasStop = false; v.stopAt = 0; v.rel = relMax * 0.4; v.track = tr; v.ser = stamp_; v.end = INFINITY; v.t0 = st0; v.ep = pad.epoch;
  if (!(e.has & SH_SLIDE)) pitchMods(v, e.t, base, e, arp);
  alive_[nAlive_++] = vi;
  if (e.has & SH_CUT) kill(vi, e.cut);
  if (e.has & SH_REL) kill(vi, e.rel, relTc(v));
  else if (!kv.syn && looped) kill(vi, st0 + jmax(0.1, orv(P.len, 2)), relTc(v));   // a drum pad: a looping zone lets go after the pad's Max len
  T_[tr][nT_[tr]++] = vi;
  return true;
}

int SfPool::liveOnTrack(int tr, double t) {
  if (tr < 0 || tr >= kVoiceTracks) return 0;
  for (int k = nT_[tr] - 1; k >= 0; k--) if (!live1(V_[(size_t)T_[tr][k]], t)) tErase(tr, k);
  return nT_[tr];
}
bool SfPool::oldestOnTrack(int tr, uint64_t& serial) const {
  if (tr < 0 || tr >= kVoiceTracks || nT_[tr] == 0) return false;
  serial = V_[(size_t)T_[tr][0]].ser; return true;
}
void SfPool::releaseOldest(int tr, double t) {
  if (tr < 0 || tr >= kVoiceTracks || nT_[tr] == 0) return;
  const int vi = T_[tr][0]; tErase(tr, 0); kill(vi, t, 0.006);   // (T.shift())
}

void SfPool::off(const SeqEvent& e) {
  if (e.track < 0 || e.track >= kVoiceTracks) return;
  const int tr = e.track;
  if (e.has & SH_COLS) {
    for (int k = nT_[tr] - 1; k >= 0; k--) { const Voice& v = V_[(size_t)T_[tr][k]];
      if (v.col >= 0 && v.col < 8 && ((e.cols >> v.col) & 1)) { const int vi = T_[tr][k]; kill(vi, e.t, relTc(v)); tErase(tr, k); } }
    return;
  }
  for (int k = 0; k < nT_[tr]; k++) kill(T_[tr][k], e.t, relTc(V_[(size_t)T_[tr][k]]));
  nT_[tr] = 0;
}

void SfPool::mod(const SeqEvent& e, const double* arp) {
  if (e.track < 0 || e.track >= kVoiceTracks) return;
  const int tr = e.track;
  for (int k = 0; k < nT_[tr]; k++) {
    const int vi = T_[tr][k]; Voice& v = V_[(size_t)vi]; if (!live1(v, e.t)) continue;
    if (e.has & SH_GAIN) v.g.set(e.gain, e.t);
    if (e.has & SH_PAN) v.pn.set(jmax(-1, jmin(1, (e.pan - 128) / 127)), e.t);
    if (e.has & SH_MUL) { v.rate = v.base * e.mul; v.r.set(v.rate, e.t); }
    pitchMods(v, e.t, v.rate, e, arp);
    if (e.has & SH_CUT) kill(vi, e.cut);
    if (e.has & SH_REL) kill(vi, e.rel, relTc(v));
  }
}

void SfPool::stp(const SeqEvent& e) {
  for (int k = 0; k < kVoiceTracks; k++) if (e.flag || k == e.track) { for (int q = 0; q < nT_[k]; q++) kill(T_[k][q], e.t, 0.004); nT_[k] = 0; }
}

void SfPool::stopTrack(int track, double t) {
  if (!(track >= 0 && track < kNT)) return;
  const double at = jmax(now_, t == t ? t : 0.0);
  for (int q = 0; q < nT_[track]; q++) kill(T_[track][q], at, 0.004, true);
  nT_[track] = 0;
}

void SfPool::stopAll(double at) {
  for (int ai = 0; ai < nAlive_; ai++) kill(alive_[ai], at, 0.004, true);
  for (int k = 0; k < kVoiceTracks; k++) nT_[k] = 0;
}

void SfPool::render(float* L, float* R, int frames, uint64_t frame0) {
  float* ls[kVoiceTracks]; float* rs[kVoiceTracks];
  for (int k = 0; k < kVoiceTracks; k++) { ls[k] = L; rs[k] = R; }
  render(ls, rs, frames, frame0);
}

void SfPool::render(float* const* outL, float* const* outR, int frames, uint64_t frame0) {
  now_ = timeOf(frame0);
  int done = 0;
  while (done < frames) {   // pieces that never cross a 128-frame edge of the host's absolute frame count
    const uint64_t f = frame0 + (uint64_t)done; const int n = std::min(frames - done, (int)(128 - (f & 127)));
    for (int ai = 0; ai < nAlive_;) {
      const int vi = alive_[ai]; Voice& v = V_[(size_t)vi];
      float* L = outL[v.track]; if (!L) { ai++; continue; }
      float* R = outR && outR[v.track] ? outR[v.track] : L;
      run(v, L + done, R + done, n, f);
      if (v.ended) { tRemove(vi); aliveErase(ai); } else ai++;
    }
    done += n;
  }
}

// sfRun: one piece (at most 128 frames, inside one quantum) of a voice into its track's output (zones -> [zone pan] -> gain -> fade -> pan)
void SfPool::run(Voice& v, float* L, float* R, int N, uint64_t f0) {
  const double tq = (double)(f0 & ~(uint64_t)127) / sr_, t0 = timeOf(f0); const int o0 = (int)(f0 & 127);
  const bool edge = o0 == 0;   // the web copy folds its automation at the start of every 128-frame block
  if (edge) { v.g.prune(t0); v.f.prune(t0); v.pn.prune(t0); v.r.prune(t0); }
  float* sL = sL_; float* sR = sR_;
  for (int i = 0; i < N; i++) { sL[i] = 0; sR[i] = 0; }
  // The fast path's decision (constant from here on?) is taken at the START OF THE QUANTUM, with the automation as it is now: the web copy asks at the start of its 128-frame block, with every event of the block already in the list; a piece in the middle
  // of a quantum (a host block of another size) must ask the same question, or an event booked inside the quantum (whose time is after the quantum's start) would be seen differently. (Auto::constFrom looks at the last event only: a vibrato curve
  // followed by a set reads as constant once the set's time has passed, where at() still follows the curve: so the question must not depend on the time asked.)
  double rcv = 0; const bool rcb = v.r.constFrom(tq, &rcv); bool any = false;
  for (int zi = 0; zi < v.nz; zi++) {
    Zn& z = v.z[zi]; if (z.done) continue;
    if (edge) { z.env.prune(t0); z.r.prune(t0); if (z.hasFlt) z.flt.fr.prune(t0); }
    double ecv = 0, zcv = 0; const bool ecb = z.env.constFrom(tq, &ecv), zcb = z.r.constFrom(tq, &zcv);
    const float* D = z.L; const float* E = z.R; const int n = z.n; const double k0 = z.sr / sr_;
    const double za = z.hasZp ? (z.zp + 1) * kPi / 4 : 0, zl = z.hasZp ? js_cos(za) : 1, zrr = z.hasZp ? js_sin(za) : 1;
    Flt* F = z.hasFlt ? &z.flt : nullptr;
    for (int i = 0; i < N; i++) {
      const double t = tq + (double)(o0 + i) * dt_;
      if (t < v.t0) continue;
      if (z.lp) { int guard = 0; while (z.pos >= z.le && guard++ < 1024) z.pos -= z.le - z.ls; if (z.pos >= z.le) { z.done = true; break; } }
      else if (!(z.pos < (double)(n - 1))) { z.done = true; break; }
      if (!(z.pos >= 0)) { z.done = true; break; }   // (a negative or NaN position would index outside the sample)
      const double p = z.pos; const int j = (int)p; const double fr = p - j; const int j1 = z.lp && j + 1 >= z.le ? (int)z.ls : j + 1;
      const double e = ecb ? ecv : z.env.at(t);
      double x = ((double)D[j] + ((double)D[j1] - (double)D[j]) * fr) * e, y = E ? ((double)E[j] + ((double)E[j1] - (double)E[j]) * fr) * e : x;
      if (F) {   // Web Audio's low-pass (Q in dB), its cutoff read every 16 samples
        if ((F->k++ & 15) == 0) {
          const double f = F->fr.at(t), w = 2 * kPi * jmin(f, sr_ * 0.49) / sr_, c = js_cos(w), al = js_sin(w) / (2 * js_pow(10, F->q / 20)), a0 = 1 + al;
          F->b0 = (1 - c) / 2 / a0; F->b1 = (1 - c) / a0; F->b2 = F->b0; F->a1 = -2 * c / a0; F->a2 = (1 - al) / a0;
        }
        const double ox = F->b0 * x + F->b1 * F->x1 + F->b2 * F->x2 - F->a1 * F->y1 - F->a2 * F->y2; F->x2 = F->x1; F->x1 = x; F->y2 = F->y1; F->y1 = ox; x = ox;
        if (E) { const double oy = F->b0 * y + F->b1 * F->u1 + F->b2 * F->u2 - F->a1 * F->w1 - F->a2 * F->w2; F->u2 = F->u1; F->u1 = y; F->w2 = F->w1; F->w1 = oy; y = oy; } else y = x;
      }
      if (z.hasZp) { sL[i] = (float)((double)sL[i] + x * zl); sR[i] = (float)((double)sR[i] + x * zrr); } else { sL[i] = (float)((double)sL[i] + x); sR[i] = (float)((double)sR[i] + y); }
      const double ratio = (rcb ? rcv : v.r.at(t)) / v.base;
      z.pos += (zcb ? zcv : z.r.at(t)) * ratio * k0;
    }
    if (!z.done) any = true;
  }
  double gcv = 0, fcv = 0, pcv = 0; const bool gcb = v.g.constFrom(tq, &gcv), fcb = v.f.constFrom(tq, &fcv), pcb = v.pn.constFrom(tq, &pcv);
  for (int i = 0; i < N; i++) {
    const double t = tq + (double)(o0 + i) * dt_;
    if (t < v.t0) continue;
    if (t >= v.end) { v.ended = true; break; }
    const double G = (gcb ? gcv : v.g.at(t)) * (fcb ? fcv : v.f.at(t)), p = pcb ? pcv : v.pn.at(t);
    if (!v.st) { const double m = (double)sL[i] * G, a = (p + 1) * kPi / 4; acc(L + i, m * js_cos(a)); acc(R + i, m * js_sin(a)); continue; }   // StereoPannerNode, mono in
    const double l = (double)sL[i] * G, r = (double)sR[i] * G;   // stereo in
    if (p <= 0) { const double x = (p + 1) * kPi / 2; acc(L + i, l + r * js_cos(x)); acc(R + i, r * js_sin(x)); }
    else { const double x = p * kPi / 2; acc(L + i, l * js_cos(x)); acc(R + i, r + l * js_sin(x)); }
  }
  if (!any) v.ended = true;
}

}  // namespace sf
}  // namespace thunder
