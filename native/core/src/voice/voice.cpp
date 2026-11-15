// U08: see voice.h. The JS is the specification (core/core-processor.js); JS line references are to that file.
#include "voice/voice.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "rt/jsmath.h"

namespace thunder {
namespace {

constexpr double kPi = 3.14159265358979323846;   // Math.PI
constexpr int kRdM = kRdRing - 1;
constexpr int kNT = kSongNT;   // NT: the tracks a chosen pad can be on (chokes look at 0..NT-1)

// Math.max(a, Math.min(b, v)) (NaN stays NaN)
inline double clampJ(double v, double a, double b) { const double m = v != v ? v : (b < v ? b : v); return m != m ? m : (a > m ? a : m); }
inline double cutHz(double v) { return 30 * js_pow(18000.0 / 30, clampJ(v, 0, 255) / 255); }
inline double decSec(double v) { return 0.008 * js_pow(4 / 0.008, clampJ(v, 0, 254) / 254); }
inline void acc(float* p, double v) { *p = (float)((double)*p + v); }   // L[i] += v on a Float32Array

// stBal: the StereoPannerNode's balance law for a stereo input, as [L->L, R->L, L->R, R->R]
inline void stBal(double pan, double* o) {
  if (pan <= 0) { const double a = (pan + 1) * kPi / 2; o[0] = 1; o[1] = std::cos(a); o[2] = 0; o[3] = std::sin(a); return; }
  const double a = pan * kPi / 2; o[0] = std::cos(a); o[1] = 0; o[2] = std::sin(a); o[3] = 1;
}
const double kZero4[4] = {0, 0, 0, 0};

// A curve for pitchMods' vibrato: base * 2 ** (st * sin(2 pi hz d min(1, max(0, (x - t0) / dd))) / 12); p = {base, st, hz, d, t0, dd}
double vibCurve(const double* p, double x) {
  return p[0] * js_pow(2, p[1] * std::sin(2 * kPi * p[2] * p[3] * std::min(1.0, std::max(0.0, (x - p[4]) / p[5]))) / 12);
}

}  // namespace

// ---- Rd -------------------------------------------------------------------------------------------------------------------
void VoicePool::Rd::init(const float* s, const float* s2, int length, double sr, bool hasTw, bool hasCut, double cut, double res, bool hasDec, double dec, bool reverse, double start) {
  src = s; src2 = s2; rev = reverse; len = length; N = length; tw = hasTw; f = false; tau = 0; hi = lo = 0; kd = 1; e = 1; l1 = l2 = 0;
  if (!tw) return;
  tau = hasDec ? decSec(dec) : 0; N = tau != 0 ? (int)std::min((double)len, std::ceil(tau * 9.3 * sr)) : len;
  f = hasCut;
  if (f) {
    const double fc = std::min(cutHz(cut), sr * 0.45), g = std::tan(kPi * fc / sr), k = 2 - 2 * std::min(clampJ((double)js_to_int32(res), 0, 255) / 255 * 0.97, 0.97);
    a1 = 1 / (1 + g * (g + k)); a2 = g * a1; a3 = g * a2;
  }
  l1 = 0; l2 = 0; std::memset(ring, 0, sizeof(float) * kRdRing);   // (the ring from the pool, cleared as a new Float32Array is)
  const double s0d = std::max(0.0, std::floor(start) - 4096); hi = lo = (int)s0d;   // (the filter settles in the warm-up)
  kd = tau != 0 ? js_exp(-1 / (tau * sr)) : 1; e = tau != 0 ? js_pow(kd, s0d) : 1;
}

double VoicePool::Rd::at(int c, int i) {
  if (!tw) return raw(c, i);
  if (c || i < 0 || i >= N) return 0;
  while (hi <= i) {
    const int n = hi; double x = n < len ? (double)src[n] : 0;
    if (f) { const double v3 = x - l2, v1 = a1 * l1 + a2 * v3, v2 = l2 + a2 * l1 + a3 * v3; l1 = 2 * v1 - l1; l2 = 2 * v2 - l2; x = v2; }
    if (tau != 0) { x *= e; e *= kd; const int m = N - 1 - n; if (m < 64) x *= (double)m / (double)std::min(N, 64); }
    ring[n & kRdM] = (float)x; hi = n + 1;
  }
  return i < hi - kRdRing ? 0 : (double)ring[i & kRdM];
}

// ---- pool -----------------------------------------------------------------------------------------------------------------
VoicePool::VoicePool(double sampleRate, int maxVoices) : sr_(sampleRate), cap_(maxVoices < 1 ? 1 : maxVoices) {
  V_.reset(new Voice[(size_t)cap_]); rings_.reset(new float[(size_t)cap_ * kRdRing]());
  alive_.reset(new int[(size_t)cap_]); free_.reset(new int[(size_t)cap_]);
  for (int i = 0; i < cap_; i++) { V_[(size_t)i].rd.ring = rings_.get() + (size_t)i * kRdRing; free_[(size_t)i] = cap_ - 1 - i; }
  nFree_ = cap_; std::memset(nT_, 0, sizeof nT_); std::memset(T_, 0, sizeof T_);
}
VoicePool::~VoicePool() {}

uint32_t VoicePool::autoDropped() const {
  uint32_t n = 0; for (int i = 0; i < cap_; i++) { const Voice& v = V_[(size_t)i]; n += v.r.dropped + v.g.dropped + v.f.dropped + v.pn.dropped; } return n;
}
void VoicePool::track(const Voice& v) {
  peakEv_ = std::max(peakEv_, std::max(std::max(v.r.size(), v.g.size()), std::max(v.f.size(), v.pn.size())));
}

void VoicePool::tErase(int tr, int k) { for (int q = k; q + 1 < nT_[tr]; q++) T_[tr][q] = T_[tr][q + 1]; nT_[tr]--; }   // T.splice(k, 1)
void VoicePool::tRemove(int vi) {   // the voice ended: out of its track's list (T.indexOf / splice)
  const int tr = V_[(size_t)vi].track; for (int k = 0; k < nT_[tr]; k++) if (T_[tr][k] == vi) { tErase(tr, k); return; }
}
void VoicePool::aliveErase(int ai) { const int vi = alive_[ai]; for (int q = ai; q + 1 < nAlive_; q++) alive_[q] = alive_[q + 1]; nAlive_--; free_[nFree_++] = vi; }

// kill(v, at, tc, stop) (the JS's; a live engine's branch is not here)
void VoicePool::kill(int vi, double at, double tc, bool stop) {
  Voice& v = V_[(size_t)vi];
  // STOP (stopAll): a voice booked to start at or after the stop never sounds; one already sounding fades as before
  if (stop && !v.ended && v.t0 >= at - 1e-6) { v.hasStop = true; v.stopAt = at; v.end = std::min(v.end, v.t0); return; }
  if (v.ended || (v.hasStop && v.stopAt <= at)) return;
  v.hasStop = true; v.stopAt = at; at = std::max(at, now_);
  v.f.cancel(at); v.f.set(1, at); v.f.target(0, at, tc); v.end = std::min(v.end, at + tc * 12);
}

void VoicePool::pitchMods(Voice& v, double t, double base, const SeqEvent& e, const double* arp) {
  Auto& pr = v.r;
  if (e.has & SH_ARP) {
    for (int q = 0; q < e.arpN; q++) pr.set(base * arp[2 * q + 1], std::max(arp[2 * q], t));
    if (e.arpN) pr.set(base, arp[2 * (e.arpN - 1)] + (e.arpN > 1 ? arp[2] - arp[0] : 0.02));
  } else if (e.has & SH_BEND) {
    pr.set(base, t); pr.lin(base * e.bendMul, e.bendT1); v.rate = base * e.bendMul;   // (pageTiming_: see voice.h, U09)
  } else if ((e.has & SH_VIB) && e.vibDep != 0 && e.vibDep == e.vibDep) {   // ev.vib && ev.vib.dep (truthy)
    const double d = std::max(0.02, e.vibT1 - t), hz = 1 + e.vibSpd * 1.2, st = e.vibDep / 15 * 1.5, t0 = t + 0.0005, dd = d - 0.001;
    pr.set(base, t);
    Curve c; c.fn = vibCurve; c.p[0] = base; c.p[1] = st; c.p[2] = hz; c.p[3] = d; c.p[4] = t0; c.p[5] = dd;
    pr.curve(c, t0, dd);
  }
}

void VoicePool::modVoice(int vi, const SeqEvent& e, const double* arp) {
  Voice& v = V_[(size_t)vi];
  if (e.has & SH_GAIN) v.g.set(e.gain, e.t);
  if (e.has & SH_PAN) v.pn.set(std::max(-1.0, std::min(1.0, (e.pan - 128) / 127)), e.t);
  if (e.has & SH_MUL) { v.rate = v.base * e.mul; v.r.set(v.rate, e.t); }
  pitchMods(v, e.t, v.rate, e, arp);
  if (e.has & SH_CUT) kill(vi, e.cut);
  if (e.has & SH_REL) kill(vi, e.rel, v.syn ? relTc(v) : 0.05);
  track(v);
}

void VoicePool::take(const SeqEvent& e, const VoiceKit* kv) {
  if (e.track < 0 || e.track >= kVoiceTracks) return;
  const int tr = e.track;
  for (int k = nT_[tr] - 1; k >= 0; k--) if (!live1(V_[(size_t)T_[tr][k]], e.t)) tErase(tr, k);
  if (!e.poly) { for (int k = 0; k < nT_[tr]; k++) { const Voice& v = V_[(size_t)T_[tr][k]]; kill(T_[tr][k], e.t, v.syn ? std::min(relTc(v), 0.05) : 0.004); } nT_[tr] = 0; }
  const int ck = kv ? kv->choke : 0;
  if (ck) for (int k = 0; k < kNT; k++) if (k != e.track) for (int q = 0; q < nT_[k]; q++) if (V_[(size_t)T_[k][q]].choke == ck) kill(T_[k][q], e.t);
}

// startNote
bool VoicePool::note(const SeqEvent& e, const double* arp, const VoiceBuf& c, const VoiceKit& kv, double bpm) {
  if (e.track < 0 || e.track >= kVoiceTracks || !c.data) return false;
  const int tr = e.track;
  // (STUB: `if (kv.sf) return sfNote(ev)`: SoundFont pads are not this player's; the host does not call note() for them)
  for (int k = nT_[tr] - 1; k >= 0; k--) if (!live1(V_[(size_t)T_[tr][k]], e.t)) tErase(tr, k);
  if (!e.poly) { for (int k = 0; k < nT_[tr]; k++) { const Voice& v = V_[(size_t)T_[tr][k]]; kill(T_[tr][k], e.t, v.syn ? std::min(relTc(v), 0.05) : 0.004); } nT_[tr] = 0; }
  else while (nT_[tr] >= kVoicePoly) { const int vi = T_[tr][0]; tErase(tr, 0); kill(vi, e.t, 0.006); }   // (T.shift())
  const int ck = kv.choke;
  if (ck) for (int k = 0; k < kNT; k++) if (k != e.track) for (int q = 0; q < nT_[k]; q++) if (V_[(size_t)T_[k][q]].choke == ck) kill(T_[k][q], e.t);
  if (nFree_ == 0 || nT_[tr] >= kVoiceTrackCap) { if (nFree_ == 0) dropped_++; else tOver_++; return false; }
  const bool tw = (e.has & SH_TW) && !c.noShape;   // varRec: {data, data2: null, sr, tw} when the event shapes (mono), else the pad
  const int vi = free_[--nFree_]; alive_[nAlive_++] = vi; peakV_ = std::max(peakV_, nAlive_);
  Voice& v = V_[(size_t)vi];
  const double dur = c.len / c.sr;
  v.r.reset(e.rate); v.g.reset(e.gain); v.f.reset(1); v.pn.reset(std::max(-1.0, std::min(1.0, (e.pan - 128) / 127)));
  if ((e.has & SH_SLIDE) && e.slideDur > 0) { if (pageTiming_) v.r.reset(1); v.r.set(e.slideFrom, pageTiming_ ? std::ceil(e.t * sr_ / 128) * 128 / sr_ : e.t); v.r.lin(e.rate, e.t + e.slideDur); }   // (pageTiming_: see voice.h, U09)
  const double t0n = std::max(e.t, now_);
  const int32_t trmI = js_to_int32(e.trm), apnI = js_to_int32(e.apn);
  auto hz = [&](int32_t x) { return (double)(((x >> 4) ? (x >> 4) : 1)) * bpm / 60; };
  auto dep = [](int32_t x) { return (double)(x & 15) / 15; };
  v.hasTrm = v.hasApn = false;
  if (e.fdi > 0) { v.g.set(0, t0n); v.g.lin(e.gain, t0n + e.fdi); }
  if (e.trm != 0 && dep(trmI) > 0) { const double k = dep(trmI) / 2; v.trm.hz = hz(trmI); v.trm.d = e.gain * k; v.trm.t = t0n; v.hasTrm = true; if (!(e.fdi > 0)) v.g.v0 = e.gain * (1 - k); }
  if (e.apn != 0 && dep(apnI) > 0) { v.apn.hz = hz(apnI); v.apn.d = dep(apnI); v.apn.t = t0n; v.hasApn = true; }
  v.data = c.data; v.data2 = tw ? nullptr : c.data2; v.len = c.len; v.sr = c.sr;
  v.base = e.rate; v.rate = e.rate; v.choke = ck; v.col = e.col; v.ended = false; v.hasStop = false; v.stopAt = 0;
  v.syn = kv.syn; v.rel = v.syn ? ((e.has & SH_RLO) ? e.rlo : kv.rel) : 0; v.track = tr; v.end = INFINITY; v.pos = 0; v.t0 = 0; v.hasRd = false;
  if (!(e.has & SH_SLIDE)) pitchMods(v, e.t, e.rate, e, arp);
  // (ev.ofsSec / ev.durSec: the sequencer never emits them; the JS reads them for a page that does. STUB: only the ofs form.)
  const double ofs = e.ofs == e.ofs ? e.ofs : 0, off = std::max(0.0, std::min(255.0, ofs)) / 256 * dur;
  if (off > 0.0005) { v.f.set(0, t0n); v.f.lin(1, t0n + 0.0015); }
  v.t0 = pageTiming_ ? std::ceil(t0n * sr_ - 1e-6) / sr_ : t0n; v.pos = std::min(off, std::max(0.0, dur - 0.001)) * c.sr;
  if (e.rev || tw) {   // REVERSE / SHAPED: the reader (Rd)
    v.hasRd = true;
    v.rd.init(c.data, tw ? nullptr : c.data2, c.len, c.sr, tw, (e.has & SH_TWCUT) != 0, e.twCut, e.twRes, (e.has & SH_TWDEC) != 0, e.twDec, e.rev, v.pos);
  }
  if (e.has & SH_CUT) kill(vi, e.cut);
  if (e.has & SH_REL) kill(vi, e.rel, v.syn ? relTc(v) : 0.05);
  T_[tr][nT_[tr]++] = vi;
  track(v);
  return true;
}

void VoicePool::off(const SeqEvent& e) {
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

void VoicePool::mod(const SeqEvent& e, const double* arp) {
  if (e.track < 0 || e.track >= kVoiceTracks) return;
  const int tr = e.track;
  for (int k = 0; k < nT_[tr]; k++) if (live1(V_[(size_t)T_[tr][k]], e.t)) modVoice(T_[tr][k], e, arp);
}

void VoicePool::stp(const SeqEvent& e) {
  for (int k = 0; k < kVoiceTracks; k++) if (e.flag || k == e.track) { for (int q = 0; q < nT_[k]; q++) kill(T_[k][q], e.t, 0.004); nT_[k] = 0; }
}

void VoicePool::stopAll(double at) {
  for (int ai = 0; ai < nAlive_; ai++) kill(alive_[ai], at, 0.004, true);
  for (int k = 0; k < kVoiceTracks; k++) nT_[k] = 0;
}

// ---- process1's per-voice loop ----------------------------------------------------------------------------------------------
void VoicePool::render(float* L, float* R, int frames, double t0) {
  float* ls[kVoiceTracks]; float* rs[kVoiceTracks];
  for (int k = 0; k < kVoiceTracks; k++) { ls[k] = L; rs[k] = R; }
  render(ls, rs, frames, t0);
}

void VoicePool::render(float* const* outL, float* const* outR, int N, double t0) {
  now_ = t0;
  const double dt = 1 / sr_, tb0 = t0, tb1 = t0 + N * dt;
  for (int ai = 0; ai < nAlive_;) {
    const int vi = alive_[ai]; Voice& v = V_[(size_t)vi];
    float* L = outL[v.track]; if (!L) { ai++; continue; }
    float* R = outR && outR[v.track] ? outR[v.track] : L;
    run(v, L, R, N, tb0, tb1);
    if (v.ended) { tRemove(vi); aliveErase(ai); } else ai++;   // (alive.delete, the track's list, and the reader's ring goes back with the voice)
  }
}

void VoicePool::run(Voice& v, float* L, float* R, int N, double tb0, double tb1) {
  const double dt = 1 / sr_, inc0 = v.sr / sr_;
  Rd* RD = v.hasRd ? &v.rd : nullptr; const float* D = v.data;
  const int len = RD ? RD->N : v.len;
  // FAST PATH (the usual case: a note that is just playing): gain, fade, pan and pitch don't move in this block
  double gc = 0, fc = 0, pc = 0, rc = 0;
  const bool gok = !v.hasTrm && v.g.constBlk(tb0, tb1, &gc), fok = v.f.constBlk(tb0, tb1, &fc), pok = !v.hasApn && v.pn.constBlk(tb0, tb1, &pc), rok = v.r.constBlk(tb0, tb1, &rc);
  if (gok && fok && pok && rok && v.t0 <= tb0 && v.end >= tb1) {
    const double k = gc * fc, a = (pc + 1) * kPi / 4, kl = k * std::cos(a), kr = k * std::sin(a), inc = rc * inc0; double p = v.pos;
    if (RD) {
      const bool st = !RD->tw && v.data2; double B4[4]; if (st) stBal(pc, B4); else std::memcpy(B4, kZero4, sizeof B4);
      const double ll = B4[0], rl = B4[1], lr = B4[2], rr = B4[3];
      for (int i = 0; i < N; i++) {
        if (p >= len - 1) { v.ended = true; break; }
        const int j = (int)p; const double f = p - j, d0 = RD->at(0, j), x = d0 + (RD->at(0, j + 1) - d0) * f;
        if (st) { const double e0 = RD->at(1, j), y = e0 + (RD->at(1, j + 1) - e0) * f; acc(L + i, (x * ll + y * rl) * k); acc(R + i, (x * lr + y * rr) * k); }
        else { acc(L + i, x * kl); acc(R + i, x * kr); }
        p += inc;
      }
    } else if (v.data2) {   // a stereo pad: the StereoPannerNode's balance law, as the page plays it
      const float* D2 = v.data2; double B4[4]; stBal(pc, B4); const double ll = B4[0], rl = B4[1], lr = B4[2], rr = B4[3];
      for (int i = 0; i < N; i++) {
        if (p >= len - 1) { v.ended = true; break; }
        const int j = (int)p; const double f = p - j, x = (double)D[j] + ((double)D[j + 1] - (double)D[j]) * f, y = (double)D2[j] + ((double)D2[j + 1] - (double)D2[j]) * f;
        acc(L + i, (x * ll + y * rl) * k); acc(R + i, (x * lr + y * rr) * k); p += inc;
      }
    } else {
      for (int i = 0; i < N; i++) {
        if (p >= len - 1) { v.ended = true; break; }
        const int j = (int)p; const double x = (double)D[j] + ((double)D[j + 1] - (double)D[j]) * (p - j);
        acc(L + i, x * kl); acc(R + i, x * kr); p += inc;
      }
    }
    v.pos = p;
    return;
  }
  v.g.prune(tb0); v.f.prune(tb0); v.pn.prune(tb0); v.r.prune(tb0);
  for (int i = 0; i < N; i++) {
    const double t = tb0 + i * dt;
    if (pageTiming_ ? t < v.t0 - 1e-9 : t < v.t0) continue;
    if (t >= v.end || v.pos >= len - 1) { v.ended = true; break; }
    const double p = v.pos; const int j = (int)p;
    const double d0 = RD ? RD->at(0, j) : (double)D[j], d1 = RD ? RD->at(0, j + 1) : (double)D[j + 1], x = d0 + (d1 - d0) * (p - j);
    const float* D2 = RD && RD->tw ? nullptr : v.data2;
    double g = v.g.at(t); if (v.hasTrm) g += v.trm.d * std::sin(2 * kPi * v.trm.hz * (t - v.trm.t));
    double pan = v.pn.at(t); if (v.hasApn) pan = std::max(-1.0, std::min(1.0, pan + v.apn.d * std::sin(2 * kPi * v.apn.hz * (t - v.apn.t))));
    const double gf = g * v.f.at(t);
    if (D2) {
      const double e0 = RD ? RD->at(1, j) : (double)D2[j], e1 = RD ? RD->at(1, j + 1) : (double)D2[j + 1], y2 = e0 + (e1 - e0) * (p - j); double B4[4]; stBal(pan, B4);
      acc(L + i, (x * B4[0] + y2 * B4[1]) * gf); acc(R + i, (x * B4[2] + y2 * B4[3]) * gf);
    } else {
      const double y = x * gf, a = (pan + 1) * kPi / 4;   // StereoPannerNode, mono in: equal power
      acc(L + i, y * std::cos(a)); acc(R + i, y * std::sin(a));
    }
    v.pos += (pageTiming_ ? v.r.at(std::floor((tb0 * sr_ + i + 1e-6) / 128) * 128 / sr_) : v.r.at(t)) * inc0;
  }
}

}  // namespace thunder
