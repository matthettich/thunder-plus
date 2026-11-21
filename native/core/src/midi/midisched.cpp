// U35: see midisched.h. Each function says which index.html function it is (line numbers: the web app of 19 Nov, `native` 1b7b99d) and which
// ruling (B1..B9) changes it. Audio thread: no allocation, no lock after the constructor.
#include "midi/midisched.h"

#include <algorithm>
#include <limits>

#include "rt/jsmath.h"

namespace thunder {

namespace {
constexpr double kInf = std::numeric_limits<double>::infinity();
inline double clampd(double v, double a, double b) { return v < a ? a : v > b ? b : v; }
inline int clampi(int v, int a, int b) { return v < a ? a : v > b ? b : v; }
inline int rnd(double x) { const double r = js_round(x); return r != r ? 0 : (int)(r < -1e9 ? -1e9 : r > 1e9 ? 1e9 : r); }   // JS Math.round, then an int that cannot overflow
inline int ceilClamp(double x, int lo, int hi) { const double c = std::ceil(x); return c != c ? lo : (int)(c < lo ? lo : c > hi ? hi : c); }
const double kLfoRates[14] = {1.0 / 6, 1.0 / 4, 1.0 / 3, 1.0 / 2, 2.0 / 3, 3.0 / 4, 1, 4.0 / 3, 3.0 / 2, 2, 4, 8, 16, 32};   // LFO_RATES :17133
}  // namespace

MidiSched::MidiSched(double sampleRate) : sr_(sampleRate > 1000 && sampleRate < 1e6 ? sampleRate : 48000.0) {
  pend_ = new PendMsg[kMidiPendCap];
  cnt_ = new uint8_t[kMidiPorts * 16 * 128]();
  owner_ = new int8_t[kMidiPorts * 16 * 128];
  wire_ = new uint8_t[kMidiPorts * 16 * 128]();
  held_ = new Held[(size_t)kMidiTracks * kMidiHeldCap]();
  pendRet_ = new PendRet[(size_t)kMidiTracks * 8]();
  ccv_ = new int16_t[kMidiPorts * 16 * 128];
  std::memset(owner_, -1, kMidiPorts * 16 * 128);
  for (int i = 0; i < kMidiPorts * 16 * 128; i++) ccv_[i] = -1;
  for (auto& b : bend_) b = {false, 8192, 0};
  std::memset(lastTickFrame_, 0, sizeof lastTickFrame_);
}

MidiSched::~MidiSched() { delete[] pend_; delete[] cnt_; delete[] owner_; delete[] wire_; delete[] held_; delete[] pendRet_; delete[] ccv_; }

// ---- configuration ---------------------------------------------------------------------------------------------------------------
void MidiSched::setPort(int p, bool connected, bool clockOn, double gearMs, int cable) {
  if (p < 0 || p >= kMidiPorts) { st_.badArgs++; return; }
  conn_[p] = connected; clkOn_[p] = clockOn;
  gear_[p] = std::isfinite(gearMs) ? clampd(std::round(gearMs), -50, 200) : 0;   // :17695 clamp(round(v), -50, 200)
  cable_[p] = cable;
}
void MidiSched::setLead(double sec) { lead_ = std::isfinite(sec) ? clampd(sec, 0.005, 0.5) : kDefaultLeadSec; }
void MidiSched::setFxLatency(double sec) { fxLat_ = std::isfinite(sec) ? clampd(sec, 0, 2) : 0; }
void MidiSched::setSampleRate(double sr) { if (sr > 1000 && sr < 1e6) sr_ = sr; }
void MidiSched::setBlock(double sec) { block_ = std::isfinite(sec) ? clampd(sec, 0, 1) : 0; }

// ---- output ------------------------------------------------------------------------------------------------------------------------
// F4: what the ring gives up first when the consumer stalls. 0 = clock ticks, CCs, bends, pressure, program; 1 = note-ons, Start / SPP / Continue; 2 = NOTE-OFF,
// CLEAR, All Notes Off / All Sound Off, Stop and everything a release or panic sends (a dropped one can leave the gear sounding).
static int prioOf(const uint8_t* b, int len, uint8_t kind, uint8_t flags) {
  if ((flags & MIDI_F_CLEAR) || kind == MK_RELEASE || kind == MK_PANIC || kind == MK_STOP || len < 1) return 2;
  const int st = b[0], ty = st & 0xF0;
  if (ty == 0x80) return 2;
  if (ty == 0x90) return len >= 3 && b[2] > 0 ? 1 : 2;
  if (ty == 0xB0 && len >= 2 && (b[1] == 120 || b[1] == 123)) return 2;
  if (st == 0xFC) return 2;
  if (st == 0xFA || st == 0xFB || st == 0xF2) return 1;
  return 0;
}
bool MidiSched::push(int port, const uint8_t* b, int len, int64_t frame, uint8_t kind, uint8_t flags, int cable) {
  MidiMsg m; std::memset(&m, 0, sizeof m);
  m.frame = frame; m.port = (uint8_t)port; m.len = (uint8_t)len; m.flags = flags; m.kind = kind; m.cable = (uint8_t)(cable == -1000 ? cable_[port] : cable);   // pad = the cable the port had when the message was made
  for (int i = 0; i < len && i < 3; i++) m.b[i] = b[i];
  const int pr = prioOf(b, len, kind, flags); const uint32_t sp = ring_.space();
  if (sp == 0 || (pr == 0 && sp < (uint32_t)kRingReserveLow) || (pr == 1 && sp < (uint32_t)kRingReserveNote) || !ring_.push(m)) {
    st_.ringFull++;
    const int st = len >= 1 ? b[0] : 0, ty = st & 0xF0;
    if (pr == 2) { st_.mayHang = 1; if ((flags & MIDI_F_CLEAR) || st == 0xFC || (ty == 0xB0 && len >= 2 && (b[1] == 120 || b[1] == 123))) st_.dropClear++; else st_.dropOff++; }
    else if (pr == 1) { if (ty == 0x90) st_.dropNoteOn++; else st_.dropStart++; }
    else if (st == 0xF8) st_.dropTicks++; else st_.dropCtl++;
    return false;
  }
  st_.sent++; return true;
}
// A note message to the gear: remembered by pitch (wire_: what the gear was last told) and, while it is still ahead of `now`, in fut_ (F3).
bool MidiSched::pushNote(int port, const uint8_t* b, int64_t frame, uint8_t kind, int cable, int idx, bool on) {
  if (!push(port, b, 3, frame, kind, 0, cable)) return false;
  wire_[idx] = on ? 1 : 0;
  if (frame > frameOf(now_)) {
    if (nFut_ >= kMidiFutCap) { st_.futFull++; std::memmove(fut_, fut_ + 1, (kMidiFutCap - 1) * sizeof(Fut)); nFut_ = kMidiFutCap - 1; }
    fut_[nFut_++] = {frame, idx, (uint8_t)(on ? 1 : 0)};
  }
  return true;
}

// msend :16787. `hasT` false = "now" (t null): no FX latency, no Gear delay. A scheduled message is `early` (B3: the transport too).
void MidiSched::send(const uint8_t* b, int len, double t, bool hasT, int port, uint8_t kind, int track) {
  if (port < 0 || port >= kMidiPorts || !conn_[port]) return;              // :16788 no cable: nothing
  double at = hasT ? t + fxLat_ - latSec(port) : now_;
  if (!std::isfinite(at)) { st_.badArgs++; return; }
  if (at < now_ && kind != MK_CLOCK) at = now_;                              // :16791 Math.max(now, at). A tick keeps its own (past) time: the phase stays on the grid (B7)
  PendMsg m; m.at = at; m.fr = 0; m.ord = ord_++; m.port = (uint8_t)port; m.len = (uint8_t)len; m.kind = kind; m.track = (uint8_t)track; m.cable = (uint8_t)cable_[port];
  for (int i = 0; i < len && i < 3; i++) m.b[i] = b[i];
  pendInsert(m);   // F2/F6: EVERYTHING waits in the time-sorted outbox and is released by book() in (frame, creation) order: Start / Continue before tick 0, and the stream does not depend on when the host asks
}
void MidiSched::pendInsert(const PendMsg& m0) {
  PendMsg m = m0; m.fr = frameOf(m.at);
  if (nPend_ >= kMidiPendCap) { st_.pendFull++; emit(m); return; }
  int lo = 0, hi = nPend_;
  while (lo < hi) { const int mid = (lo + hi) >> 1; if (pend_[mid].fr <= m.fr) lo = mid + 1; else hi = mid; }   // by frame, then creation order
  std::memmove(pend_ + lo + 1, pend_ + lo, (size_t)(nPend_ - lo) * sizeof(PendMsg));
  pend_[lo] = m; nPend_++;
  if ((uint64_t)nPend_ > st_.maxPend) st_.maxPend = (uint64_t)nPend_;
}
void MidiSched::flush(double now) {
  int i = 0;
  const double lead = leadEff();
  while (i < nPend_ && pend_[i].at - now <= lead + 1e-12) {
    if (pend_[i].kind == MK_CLOCK && pend_[i].at < now - kClockLateSec) st_.lateTicks++;   // B7: a tick that waited past its time while the host stalled is dropped, not burst
    else emit(pend_[i]);
    i++;
  }
  if (i) { std::memmove(pend_, pend_ + i, (size_t)(nPend_ - i) * sizeof(PendMsg)); nPend_ -= i; }
  { const int64_t nf = frameOf(now); int w = 0; for (int k = 0; k < nFut_; k++) if (fut_[k].fr > nf) fut_[w++] = fut_[k]; nFut_ = w; }   // what is due can no longer be undone by a STOP
}
// A message leaves the scheduler (in time order). Notes are owned per port / channel / pitch (B4) and a note-off without a sounding note is not sent (B9).
void MidiSched::emit(const PendMsg& m) {
  const int64_t fr = frameOf(m.at);
  const int ty = m.b[0] & 0xF0;
  if (m.track != 0xFF && (ty == 0x90 || ty == 0x80) && m.len == 3) {
    const int ch = m.b[0] & 15, n = m.b[1] & 127, idx = (m.port * 16 + ch) * 128 + n;
    const bool on = ty == 0x90 && m.b[2] > 0;
    if (on) {
      if (cnt_[idx] > 0) {
        if (owner_[idx] != (int8_t)m.track) st_.collisions++; else st_.sameTrackRetrig++;
        const uint8_t o[3] = {(uint8_t)(0x80 | ch), (uint8_t)n, 0}; pushNote(m.port, o, fr, m.kind, m.cable, idx, false);   // off, then on
      }
      if (cnt_[idx] < 255) cnt_[idx]++;
      owner_[idx] = (int8_t)m.track;
      if (!pushNote(m.port, m.b, fr, m.kind, m.cable, idx, true) && cnt_[idx] > 0) cnt_[idx]--;   // F4: a note-on the ring dropped cancels its own later off
    } else {
      if (cnt_[idx] == 0) { st_.offsSuppressed++; return; }                  // B9: nothing sounding there
      if (cnt_[idx] > 0 && --cnt_[idx] > 0) { st_.offsSuppressed++; return; }   // B4: another track still holds it
      pushNote(m.port, m.b, fr, m.kind, m.cable, idx, false);
    }
    return;
  }
  if (m.kind == MK_CLOCK) { int& k = lastTickN_[m.port]; lastTickFrame_[m.port][k & 7] = fr; k++; }
  push(m.port, m.b, m.len, fr, m.kind, 0, m.cable);
}
int64_t MidiSched::drain(MidiMsg* out, int64_t cap) {
  int64_t n = 0; MidiMsg m;
  while (n < cap && ring_.pop(m)) out[n++] = m;
  return n;
}

// ---- held notes (mx.held) ----------------------------------------------------------------------------------------------------------
// offOne :16846
void MidiSched::offOne(Held& h, double at) {
  if (h.end <= at) return;
  const uint8_t b[3] = {(uint8_t)(0x80 | h.ch), h.n, 0};
  send(b, 3, std::max(at, h.t + 0.001), true, h.p, MK_EV, 0);
  h.end = at;
}
// moOff :16850: ends every note track t holds (owned per track: B4) at `at`.
void MidiSched::moOff(int t, double at) {
  pendAt(t, at);
  Held* H = held_ + (size_t)t * kMidiHeldCap; int& n = nHeld_[t];
  if (!n) return;
  for (int i = 0; i < n; i++) offOne(H[i], at);
  int w = 0; for (int i = 0; i < n; i++) if (H[i].end > at) H[w++] = H[i];
  n = w;
}
// moOffCols :16849: a poly column's OFF
void MidiSched::moOffCols(int t, uint32_t cols, double at) {
  pendAt(t, at);
  Held* H = held_ + (size_t)t * kMidiHeldCap; const int n = nHeld_[t];
  uint32_t ids[kMidiHeldCap]; int ni = 0;
  for (int i = 0; i < n; i++) if (H[i].col >= 0 && H[i].col < 32 && ((cols >> H[i].col) & 1) && H[i].end > at) ids[ni++] = H[i].nid;
  for (int k = 0; k < ni; k++) offNote(t, ids[k], at);
}
// offNote :16922: a whole note (every ARP step of it, and its waiting return)
void MidiSched::offNote(int t, uint32_t nid, double at) {
  Held* H = held_ + (size_t)t * kMidiHeldCap; const int n = nHeld_[t];
  for (int i = 0; i < n; i++) if (H[i].nid == nid) offOne(H[i], at);
  PendRet* P = pendRet_ + (size_t)t * 8; int& np = nPendRet_[t]; int w = 0;
  for (int i = 0; i < np; i++) if (P[i].nid != nid) P[w++] = P[i];
  np = w;
}
// moPlay :16908
uint32_t MidiSched::moPlay(int t, int p, int ch, int n, int vel, int rng, double fine, const Seg* segs, int nSegs, double end, uint32_t nid, bool hasNid, int32_t col) {
  if (!hasNid) nid = ++nidN_;
  Held* H = held_ + (size_t)t * kMidiHeldCap; int& nh = nHeld_[t];
  { int w = 0; for (int i = 0; i < nh; i++) if (H[i].end > segs[0].t) H[w++] = H[i]; nh = w; }   // (the page filters after; the room is made first)
  if (nh + nSegs > kMidiHeldCap) { st_.heldFull++; return nid; }
  for (int i = 0; i < nSegs; i++) {
    const int nn = clampi(n + segs[i].d, 0, 127); const double off = i < nSegs - 1 ? segs[i + 1].t : end;
    Held h; h.p = (uint8_t)p; h.ch = (uint8_t)ch; h.n = (uint8_t)nn; h.base = (uint8_t)n; h.vel = (uint8_t)vel; h.rng = (uint8_t)rng; h.fine = fine; h.t = segs[i].t; h.end = kInf; h.col = col; h.nid = nid;
    const uint8_t on[3] = {(uint8_t)(0x90 | ch), (uint8_t)nn, (uint8_t)vel};
    send(on, 3, segs[i].t, true, p, MK_EV, t);
    if (off < kInf) { h.end = std::max(segs[i].t + 0.001, off); const uint8_t of[3] = {(uint8_t)(0x80 | ch), (uint8_t)nn, 0}; send(of, 3, h.end, true, p, MK_EV, t); }
    H[nh++] = h;
  }
  used_ |= 1ull << (p * 16 + ch);
  return nid;
}
// ARP returns :16887-16906
void MidiSched::pendPlay(int t, const PendRet& x) {
  { Held* H = held_ + (size_t)t * kMidiHeldCap; for (int i = 0; i < nHeld_[t]; i++) if (H[i].nid == x.nid && H[i].end == kInf) offOne(H[i], x.t); }
  const Seg s0 = {x.t, 0};
  moPlay(t, x.p, x.ch, x.n, x.vel, x.rng, x.fine, &s0, 1, x.end, x.nid, true, x.col);
}
void MidiSched::pendAt(int t, double at) {
  int& np = nPendRet_[t]; if (!np) return;
  PendRet P[8]; const int n = np; for (int i = 0; i < n; i++) P[i] = pendRet_[(size_t)t * 8 + i];
  np = 0;
  if (at > P[0].t + 1e-4) for (int i = 0; i < n; i++) pendPlay(t, P[i]);
}
void MidiSched::pendFlush(int t, double at) {
  int& np = nPendRet_[t]; if (!np) return;
  PendRet P[8]; const int n = np; for (int i = 0; i < n; i++) P[i] = pendRet_[(size_t)t * 8 + i];
  int w = 0; for (int i = 0; i < n; i++) if (P[i].t > at + 1e-4) pendRet_[(size_t)t * 8 + w++] = P[i];
  np = w;
  for (int i = 0; i < n; i++) if (P[i].t <= at + 1e-4) pendPlay(t, P[i]);
}

// ---- pitch bend ----------------------------------------------------------------------------------------------------------------------
void MidiSched::bendTo(int p, int ch, double st, int rng, double t, bool hasT) {   // :16856
  const int k = p * 16 + ch; const int v = clampi(rnd(8192 + st / rng * 8192), 0, 16383);
  Bend& L = bend_[k]; const double tt = hasT ? t : kInf;
  if (L.has && L.v == v && tt >= L.t) return;
  if (!L.has && v == 8192) return;
  if (!L.has || tt >= L.t) L = {true, v, tt};
  used_ |= 1ull << k;
  const uint8_t b[3] = {(uint8_t)(0xE0 | ch), (uint8_t)(v & 127), (uint8_t)(v >> 7)};
  send(b, 3, t, hasT, p, MK_EV, 0xFE);
}
double MidiSched::bendNow(int p, int ch, int rng) const { const Bend& L = bend_[p * 16 + ch]; return ((L.has ? L.v : 8192) - 8192) / 8192.0 * rng; }
bool MidiSched::rampOnce(int p, int ch, double t0) {   // :16866 'p:ch:t0.toFixed(4)'
  const int64_t key = ((int64_t)(p * 16 + ch) << 50) ^ ((int64_t)std::llround(t0 * 1e4) & 0x3FFFFFFFFFFFFLL);
  for (int i = 0; i < nRampKeys_; i++) if (rampKeys_[i].k == key) return false;
  if (nRampKeys_ >= 256) { st_.rampKeyFull++; return true; }
  rampKeys_[nRampKeys_++].k = key; return true;
}
void MidiSched::bendRamp(int p, int ch, double a, double b, int rng, double t0, double t1) {   // :16868
  bendTo(p, ch, a, rng, t0, true);
  const int n = ceilClamp((t1 - t0) / 0.01, 1, 48);
  for (int i = 1; i <= n; i++) bendTo(p, ch, a + (b - a) * i / n, rng, t0 + (t1 - t0) * i / n, true);
}
void MidiSched::bendVib(int p, int ch, double c, double spd, double dep, int rng, double t0, double t1) {   // :16874
  const double d = std::max(0.02, t1 - t0), hz = 1 + spd * 1.2, st = dep / 15 * 1.5;
  const int N = ceilClamp(d * hz * 16, 8, 160);
  for (int i = 0; i < N; i++) bendTo(p, ch, c + st * js_sin(2 * 3.141592653589793 * hz * d * i / (N - 1)), rng, t0 + d * i / (N - 1), true);
  bendTo(p, ch, c, rng, t1, true);
}

// ---- notes -----------------------------------------------------------------------------------------------------------------------------
// moNote :16926. Returns the note id (live keys keep it). B1: a silent note ends the mono track's previous note (moOff first) and sends no note-on.
uint32_t MidiSched::moNote(const SeqEvent& e, const SongInst& I, const double* arp, double bpm, int cap) {
  const int t = e.track, p = clampi(I.mpt, 0, kMidiPorts - 1), ch = I.mch & 15, rng = clampi(I.pbr ? I.pbr : 2, 1, 48);
  if (!(e.vel > 0)) { st_.onsSilent++; if (!e.poly) moOff(t, e.t); return 0; }       // B1 (the page returned here before moOff: :16927)
  if (!std::isfinite(e.mn) || !std::isfinite(e.t)) { st_.badEvents++; return 0; }
  const int n = clampi(rnd(e.mn), 0, 127); const double fine = e.mn - rnd(e.mn);
  const double cut = (e.has & SH_CUT) ? e.cut : kInf, rel = (e.has & SH_REL) ? e.rel : kInf;
  const double endE = std::min(cut, rel);
  const double end = std::min(endE, (I.mlen > 0 && e.tick > 0) ? e.t + I.mlen * e.tick : kInf);   // NOTE LEN :16931
  const int vel = clampi(rnd(e.vel * 127), 1, 127);
  pendAt(t, e.t);
  if (e.ii >= 0 && I.lrg && I.lcc >= 0) lfoTrig((int)e.ii, e.t);
  if (!e.poly) moOff(t, e.t);
  else {
    Held* H = held_ + (size_t)t * kMidiHeldCap; int ids[kMidiHeldCap]; uint32_t idv[kMidiHeldCap]; int ni = 0; (void)ids;
    const int nh = nHeld_[t];
    if (e.col >= 0) { uint32_t cid[kMidiHeldCap]; int nc = 0; for (int i = 0; i < nh; i++) if (H[i].col == e.col && H[i].end > e.t) cid[nc++] = H[i].nid; for (int k = 0; k < nc; k++) offNote(t, cid[k], e.t); }
    { uint32_t sid[kMidiHeldCap]; int ns = 0; for (int i = 0; i < nHeld_[t]; i++) if (H[i].p == p && H[i].ch == ch && H[i].base == n && H[i].end > e.t) sid[ns++] = H[i].nid; for (int k = 0; k < ns; k++) offNote(t, sid[k], e.t); }
    for (int i = 0; i < nHeld_[t]; i++) if (H[i].end > e.t && H[i].t <= e.t + 1e-6) { bool dup = false; for (int k = 0; k < ni; k++) if (idv[k] == H[i].nid) dup = true; if (!dup) idv[ni++] = H[i].nid; }
    const int capN = cap > 0 ? cap : 4;
    int head = 0; while (ni - head >= capN) { offNote(t, idv[head], e.t); head++; }
  }
  const bool sl = (e.has & SH_SLIDE) && e.slideFrom > 0 && e.rate > 0;
  const double b0 = sl ? clampd(12 * js_log2(e.slideFrom / e.rate), -rng, rng) + fine : fine;
  bendTo(p, ch, b0, rng, e.t, true);
  // ARP steps (arpSegs :16880) and the return (arpBack :16887)
  Seg segs[kSeqArpMax + 2]; int nSegs = 0; double back = -1; bool hasBack = false;
  const int arpN = (e.has & SH_ARP) && arp ? std::min<int>(e.arpN, kSeqArpMax) : 0;
  if (arpN > 0) {
    for (int i = 0; i < arpN; i++) {
      const double tt = arp[2 * i]; const int d = rnd(12 * js_log2(arp[2 * i + 1]));
      if (i == 0 || tt < end - 1e-6) segs[nSegs++] = {tt, d};
    }
    back = arp[2 * (arpN - 1)] + (arpN > 1 ? arp[2] - arp[0] : 0.02); hasBack = true;
  }
  if (nSegs == 0) segs[nSegs++] = {e.t, 0};
  const bool pend = hasBack && back < end - 1e-6;
  const uint32_t nid = moPlay(t, p, ch, n, vel, rng, fine, segs, nSegs, pend ? kInf : end, 0, false, e.col);
  if (pend && nPendRet_[t] < 8) { PendRet& r = pendRet_[(size_t)t * 8 + nPendRet_[t]++]; r = {(uint8_t)p, (uint8_t)ch, (uint8_t)n, (uint8_t)n, (uint8_t)vel, (uint8_t)rng, fine, back, end, e.col, nid}; }
  // then it moves: PSL, PBN, PVB (one ramp per channel and row)
  double t0 = e.t;
  if (sl) { t0 = e.t + std::max(0.001, e.slideDur); if (rampOnce(p, ch, e.t)) bendRamp(p, ch, b0, fine, rng, e.t, t0); }
  if ((e.has & SH_BEND) && e.bendT1 > t0) { if (rampOnce(p, ch, t0)) bendRamp(p, ch, fine, fine + 12 * js_log2(e.bendMul), rng, t0, e.bendT1); }
  else if ((e.has & SH_VIB) && e.vibDep != 0 && e.vibT1 > t0) { if (rampOnce(p, ch, t0)) bendVib(p, ch, fine, e.vibSpd, e.vibDep, rng, t0, e.vibT1); }
  // ECHO :16964
  if (I.eco > 0 && t < kSongNT && !(e.vc < 0 && false)) {
    const double dt = (I.ecr > 0 ? I.ecr : 0.5) * 60 / bpm, len = std::max(0.005, std::min(dt * 0.9, end < kInf ? end - e.t : dt * 0.9)), fd = 1 - clampi(I.ecf, 0, 100) / 100.0;
    for (int k = 1; k <= std::min(16, I.eco); k++) {
      const int v2 = rnd(vel * js_pow(fd, k)); if (v2 < 1) break;
      if (nEcho_ >= 512) { st_.echoFull++; break; }
      echo_[nEcho_++] = {e.t + k * dt, 0, len, (uint8_t)p, (uint8_t)ch, (uint8_t)clampi(n + k * I.ecp, 0, 127), (uint8_t)v2, false};
    }
  }
  return nid;
}

// moMod :16973
void MidiSched::moMod(const SeqEvent& e, const double* arp) {
  const int t = e.track;
  const bool hasArp = (e.has & SH_ARP) && arp && e.arpN > 0, hasMul = (e.has & SH_MUL), hasBend = (e.has & SH_BEND), hasVib = (e.has & SH_VIB) && e.vibDep != 0;
  if (hasMul || hasArp || hasBend || hasVib) pendAt(t, e.t); else pendFlush(t, e.t);
  Held* H = held_ + (size_t)t * kMidiHeldCap;
  Held Hc[kMidiHeldCap]; int nc = 0;
  for (int i = 0; i < nHeld_[t]; i++) if (H[i].end > e.t && H[i].t <= e.t) Hc[nc++] = H[i];
  if (!nc) return;
  const int p = Hc[nc - 1].p, ch = Hc[nc - 1].ch, rng = Hc[nc - 1].rng; const double fine = Hc[nc - 1].fine;
  double cur = bendNow(p, ch, rng);
  if (hasMul) { cur = clampd(12 * js_log2(e.mul), -rng, rng) + fine; bendTo(p, ch, cur, rng, e.t, true); }
  const auto real = [&](const Held& c) -> Held* { Held* HH = held_ + (size_t)t * kMidiHeldCap; for (int i = 0; i < nHeld_[t]; i++) if (HH[i].nid == c.nid && HH[i].t == c.t && HH[i].n == c.n) return &HH[i]; return nullptr; };
  if (hasArp) {
    const int arpN = std::min<int>(e.arpN, kSeqArpMax);
    const double back = arp[2 * (arpN - 1)] + (arpN > 1 ? arp[2] - arp[0] : 0.02);
    for (int q = 0; q < nc; q++) {
      const Held& h = Hc[q]; Held* rh = real(h);
      Seg segs[kSeqArpMax + 2]; int ns = 0;
      for (int i = 0; i < arpN; i++) segs[ns++] = {arp[2 * i], rnd(12 * js_log2(arp[2 * i + 1]))};   // (end Infinity: no step is dropped)
      int first = 0; const bool keep = ns > 0 && segs[0].d == 0;
      if (keep) {
        if (ns < 2) continue;
        if (rh) { rh->end = std::max(rh->t + 0.001, segs[1].t); const uint8_t b[3] = {(uint8_t)(0x80 | rh->ch), rh->n, 0}; send(b, 3, rh->end, true, rh->p, MK_EV, 0); }
        first = 1;
      } else if (rh) offOne(*rh, e.t);
      const uint32_t nid = moPlay(t, h.p, h.ch, h.base, h.vel, h.rng, h.fine, segs + first, ns - first, kInf, 0, false, h.col);
      if (nPendRet_[t] < 8) { PendRet& r = pendRet_[(size_t)t * 8 + nPendRet_[t]++]; r = {h.p, h.ch, h.base, h.base, h.vel, h.rng, h.fine, back, kInf, h.col, nid}; }
    }
  } else if (hasBend) bendRamp(p, ch, cur, cur + 12 * js_log2(e.bendMul), rng, e.t, e.bendT1);
  else if (hasVib) bendVib(p, ch, cur, e.vibSpd, e.vibDep, rng, e.t, e.vibT1);
}

// moCmd :16995: CCA..CCD, PGM, MPB, MAT
void MidiSched::moCmd(const SeqEvent& e) {
  if (!(e.ii >= 0 && e.ii < kNI) || !S_) return;
  if (e.track < 0 || e.track > 32) { st_.badEvents++; return; }
  const int ii = (int)e.ii;
  const SongInst& I = S_->insts[ii];
  if (!I.mo || !(e.track >= 32 || audibleT(e.track))) return;
  const int p = clampi(I.mpt, 0, kMidiPorts - 1), ch = I.mch & 15;
  if (!std::isfinite(e.v) || !std::isfinite(e.t)) { st_.badEvents++; return; }
  const int vi = (int)clampd(e.v, -1e9, 1e9);
  if (e.type == SEV_MPG) { const uint8_t b[2] = {(uint8_t)(0xC0 | ch), (uint8_t)(vi & 127)}; send(b, 2, e.t - 0.001, true, p, MK_EV, 0xFF); }
  else if (e.type == SEV_MPB) { const int bv = clampi(rnd(e.v * 8192 / 128), 0, 16383); const uint8_t b[3] = {(uint8_t)(0xE0 | ch), (uint8_t)(bv & 127), (uint8_t)(bv >> 7)}; send(b, 3, e.t, true, p, MK_EV, 0xFF); }
  else if (e.type == SEV_MAT) { const uint8_t b[2] = {(uint8_t)(0xD0 | ch), (uint8_t)(vi & 127)}; send(b, 2, e.t, true, p, MK_EV, 0xFF); }
  else {
    static const int kCcDef[4] = {74, 71, 1, 7};
    const int k = clampi(e.k, 0, 3); const int cc = (I.cc[k] >= 0 && I.cc[k] <= 127) ? I.cc[k] : kCcDef[k];
    const int v = clampi(vi, 0, 127), key = (p * 16 + ch) * 128 + cc, was = ccv_[key];
    for (int i = 0; i < nGlide_; i++) if (glide_[i].key == key && glide_[i].t1 > e.t) { glide_[i].t1 = e.t; glide_[i].cut = true; }
    if (e.ramp > 0 && was >= 0 && was != v) {
      if (nGlide_ < 64) glide_[nGlide_++] = {key, (uint8_t)p, (uint8_t)ch, (uint8_t)cc, (double)was, (double)v, e.t, e.t + e.ramp, e.t, was, false};
      else st_.glideFull++;
    } else { const uint8_t b[3] = {(uint8_t)(0xB0 | ch), (uint8_t)cc, (uint8_t)v}; send(b, 3, e.t, true, p, MK_EV, 0xFF); }
    ccv_[key] = (int16_t)v;
  }
  used_ |= 1ull << (p * 16 + ch);
}

void MidiSched::moRamp(double now) {   // :17032 CSL glides, booked 80 ms ahead
  if (!nGlide_) return;
  const double upto = now + kClockAheadSec + block_; int w = 0;
  for (int i = 0; i < nGlide_; i++) {
    Glide r = glide_[i];
    for (int n = 0; r.at <= std::min(upto, r.t1) && n < 40; n++, r.at += kLfoDt) {
      const int v = rnd(r.a + (r.b - r.a) * clampd((r.at - r.t0) / std::max(1e-6, r.t1 - r.t0), 0, 1));
      if (v != r.last) { const uint8_t b[3] = {(uint8_t)(0xB0 | r.ch), r.cc, (uint8_t)clampi(v, 0, 127)}; send(b, 3, r.at, true, r.p, MK_RAMP, 0xFF); r.last = v; }
    }
    if (r.at > r.t1) { if (!r.cut && r.last != (int)r.b) { const uint8_t b[3] = {(uint8_t)(0xB0 | r.ch), r.cc, (uint8_t)clampi((int)r.b, 0, 127)}; send(b, 3, r.t1, true, r.p, MK_RAMP, 0xFF); } continue; }
    glide_[w++] = r;
  }
  nGlide_ = w;
}

void MidiSched::moEcho(double now) {   // :17047
  if (!nEcho_) return;
  const double upto = now + kEchoAheadSec + block_; int w = 0;
  for (int i = 0; i < nEcho_; i++) {
    EchoN x = echo_[i]; bool drop = false;
    const auto same = [&](const Held& h) { return h.p == x.p && h.ch == x.ch && h.n == x.n; };
    if (!x.on) {
      if (x.t > upto) { echo_[w++] = x; continue; }
      for (int t = 0; t < kMidiTracks && !drop; t++) for (int q = 0; q < nHeld_[t]; q++) { const Held& h = held_[(size_t)t * kMidiHeldCap + q]; if (same(h) && h.t < x.t + 1e-4 && h.end > x.t - 1e-4) { drop = true; break; } }
      if (drop) continue;
      const uint8_t b[3] = {(uint8_t)(0x90 | x.ch), x.n, x.vel}; send(b, 3, x.t, true, x.p, MK_ECHO, 0xFD); used_ |= 1ull << (x.p * 16 + x.ch); x.on = true; x.off = x.t + x.len;
    }
    for (int t = 0; t < kMidiTracks; t++) for (int q = 0; q < nHeld_[t]; q++) { const Held& h = held_[(size_t)t * kMidiHeldCap + q]; if (same(h) && h.t > x.t + 1e-4 && h.t < x.off + 1e-4) x.off = std::max(x.t + 0.001, h.t - 0.001); }
    if (x.off > upto) { echo_[w++] = x; continue; }
    const uint8_t b[3] = {(uint8_t)(0x80 | x.ch), x.n, 0}; send(b, 3, x.off, true, x.p, MK_ECHO, 0xFD);
  }
  nEcho_ = w;
}

// ---- LFO :17136-17175 -------------------------------------------------------------------------------------------------------------------
static double lfoWave(int sh, double ph, double seed) {
  switch (sh) {
    case 0: return js_sin(2 * 3.141592653589793 * ph);
    case 1: return ph < 0.25 ? 4 * ph : ph < 0.75 ? 2 - 4 * ph : 4 * ph - 4;
    case 2: return 2 * ph - 1;
    case 3: return 1 - 2 * ph;
    case 4: return ph < 0.5 ? 1 : -1;
    default: { uint32_t x = js_imul(js_to_uint32(seed), 374761393u) ^ 0x5bd1e995u; x = js_imul(x ^ (x >> 13), 1274126177u); x ^= x >> 16; return (double)x / 2147483648.0 - 1; }
  }
}
void MidiSched::lfoTrig(int ii, double t) {
  if (ii < 0 || ii >= kNI) return;
  Lfo& L = lfo_[ii];
  if (!L.nTrig || t > L.trig[L.nTrig - 1]) { if (L.nTrig >= 64) { std::memmove(L.trig, L.trig + 1, 63 * sizeof(double)); L.nTrig = 63; } L.trig[L.nTrig++] = t; }
}
void MidiSched::moLfo(double now, double seqTime, double bpm) {
  if (!nAnc_ || !haveT0_ || !S_) return;
  const double upto = std::min(seqTime, now + kClockAheadSec + block_);
  for (int ii = 0; ii < kNI; ii++) {
    const SongInst& I = S_->insts[ii]; const int cc = I.lcc >= 0 ? clampi(I.lcc, 0, 127) : -1;
    if (cc < 0 || !I.mo) continue;
    Lfo& L = lfo_[ii];
    const int p = clampi(I.mpt, 0, kMidiPorts - 1), ch = I.mch & 15, sh = clampi(I.lsh, 0, 5);
    int ri = 0; { const double bb = I.lrb > 0 ? I.lrb : 4; for (int i = 0; i < 14; i++) if (std::fabs(js_log(kLfoRates[i] / bb)) < std::fabs(js_log(kLfoRates[ri] / bb))) ri = i; }
    const double rate = kLfoRates[ri]; const int dep = clampi(I.ldp, 0, 127), mid = clampi(I.lct, 0, 127);
    double t = std::max(L.hasT ? L.t + kLfoDt : t0_, now);
    for (int n = 0; t <= upto && n < 40; n++, t += kLfoDt) {
      while (L.nTrig > 1 && L.trig[1] <= t) { std::memmove(L.trig, L.trig + 1, (size_t)(L.nTrig - 1) * sizeof(double)); L.nTrig--; }
      const bool hasTr = I.lrg && L.nTrig && L.trig[0] <= t; const double tr = hasTr ? L.trig[0] : 0;
      const double pos = (hasTr ? (t - tr) * bpm / 60 : beatAt(t, bpm)) / rate, cyc = std::floor(pos);
      const int v = clampi(rnd(mid + lfoWave(sh, pos - cyc, cyc * 7919 + ii * 104729 + (hasTr ? rnd(tr * 1000) : 0)) * dep * 64 / 127), 0, 127);
      if (v != L.v) { const uint8_t b[3] = {(uint8_t)(0xB0 | ch), (uint8_t)cc, (uint8_t)v}; send(b, 3, t, true, p, MK_LFO, 0xFF); L.v = v; used_ |= 1ull << (p * 16 + ch); }
      L.t = t; L.hasT = true;
    }
  }
}

// ---- the tempo map and the clock (B2, B3, B7, B8) --------------------------------------------------------------------------------------
void MidiSched::addAnchor(double t, double b, double bpm) {
  if (!std::isfinite(t) || !std::isfinite(b) || !(bpm > 0) || !std::isfinite(bpm)) { st_.badEvents++; return; }
  if (nAnc_ == kAnc) { std::memmove(anc_, anc_ + 1, (kAnc - 1) * sizeof(Anchor)); nAnc_ = kAnc - 1; }
  anc_[nAnc_++] = {t, b, bpm};
}
// the song position in beats at audio time t: forward from the newest anchor at or before t (the first one's if none)
double MidiSched::beatAt(double t, double bpm) const {
  if (!nAnc_) return 0;
  int i = nAnc_ - 1; while (i > 0 && anc_[i].t > t) i--;
  return anc_[i].b + (t - anc_[i].t) * (anc_[i].bpm > 0 ? anc_[i].bpm : bpm) / 60;
}
// the time of a song position: from the newest anchor whose beat is at or before it (forward integration: never from a later anchor)
double MidiSched::timeOfBeat(double beat, double seqTime, double bpm, bool& ok) const {
  (void)seqTime; (void)bpm; ok = false;
  int i = nAnc_ - 1; while (i >= 0 && anc_[i].b > beat + 1e-9) i--;
  if (i < 0) return 0;
  ok = true; return anc_[i].t + (beat - anc_[i].b) * 60 / anc_[i].bpm;
}
void MidiSched::clkPorts(int* ps, int& n) const {   // :16759: each cable once, ports with the clock lit and a cable
  n = 0;
  for (int p = 0; p < kMidiPorts; p++) {
    if (!clkOn_[p] || !conn_[p]) continue;
    bool seen = false; for (int i = 0; i < n; i++) if (cable_[ps[i]] == cable_[p]) seen = true;
    if (!seen) ps[n++] = p;
  }
}
void MidiSched::sendClk(const uint8_t* b, int len, double t, uint8_t kind) {
  int ps[kMidiPorts], n; clkPorts(ps, n);
  for (int i = 0; i < n; i++) send(b, len, t, true, ps[i], kind, 0xFF);
}
void MidiSched::moClock(double now, double seqTime, double bpm) {
  bool wanted = false; for (int p = 0; p < kMidiPorts; p++) wanted = wanted || clkOn_[p];
  if (!wanted || !clk_.on || !nAnc_) return;
  const double limit = std::min(seqTime, now + kClockAheadSec + block_);
  int ps[kMidiPorts], np; clkPorts(ps, np);
  { const int64_t first = (int64_t)std::ceil(anc_[0].b * 24 - 1e-9);   // ticks older than the oldest anchor we still hold are long gone (the host stalled): late, dropped, counted
    if (clk_.k < first && anc_[0].t < now) { st_.lateTicks += (uint64_t)(first - clk_.k); clk_.k = first; } }
  for (int n = 0; n < 4096; n++) {
    bool ok; const double tk = timeOfBeat((double)clk_.k / 24, seqTime, bpm, ok);
    if (!ok || !(tk < limit)) break;
    // B7: a tick this far behind the block is dropped, and the phase is kept
    double worst = -kInf; for (int i = 0; i < np; i++) worst = std::max(worst, tk + fxLat_ - latSec(ps[i]));
    if (np && worst < now - kClockLateSec) { st_.lateTicks++; clk_.k++; continue; }
    const uint8_t b = 0xF8; sendClk(&b, 1, tk, MK_CLOCK);
    clk_.last = tk; clk_.hasLast = true; st_.ticks++; clk_.k++;
  }
}

// ---- events ---------------------------------------------------------------------------------------------------------------------------
void MidiSched::onEvent(const SeqEvent& e, const double* arp, double bpm, bool late, double now) {
  now_ = now;
  if (e.type == SEV_BEAT) {   // moEv :17013
    if (!std::isfinite(e.t)) { st_.badEvents++; return; }
    if (!haveT0_) { t0_ = e.t; haveT0_ = true; }
    bool wanted = false; for (int p = 0; p < kMidiPorts; p++) wanted = wanted || clkOn_[p];
    if (wanted && !clk_.on && e.v == 0) {
      if (spp_ > 0) { const uint8_t s[3] = {0xF2, (uint8_t)(spp_ & 127), (uint8_t)(spp_ >> 7)}; sendClk(s, 3, e.t - 0.05, MK_START); const uint8_t c = 0xFB; sendClk(&c, 1, e.t, MK_START); }
      else { const uint8_t c = 0xFA; sendClk(&c, 1, e.t, MK_START); }
      st_.starts++; clk_.on = true; clk_.k = 0; clk_.hasLast = false;
    }
    return;
  }
  if (e.type == SEV_OFF) {
    if (e.track < 0 || e.track >= 32) return;
    if (e.has & SH_COLS) moOffCols(e.track, e.cols, e.t); else moOff(e.track, e.t);
    return;
  }
  if (e.type == SEV_MOD) {
    if (e.track < 0 || e.track >= 32) return;
    if (!audibleT(e.track)) { moOff(e.track, e.t); return; }
    moMod(e, arp);
    const double end = std::min((e.has & SH_CUT) ? e.cut : kInf, (e.has & SH_REL) ? e.rel : kInf);
    if (end < kInf) moOff(e.track, end);
    return;
  }
  if (e.type == SEV_MPG || e.type == SEV_MPB || e.type == SEV_MAT || e.type == SEV_MCC) { moCmd(e); return; }
  if (e.type != SEV_NOTE) return;
  if (!(e.ii >= 0) || !S_ || e.ii >= kNI || e.track < 0 || e.track >= 32) return;
  const SongInst& I = S_->insts[(int)e.ii];
  if (!I.mo) { moOff(e.track, e.t); return; }                                  // the column switched to a pad instrument
  if (!audibleT(e.track)) { moOff(e.track, e.t); return; }                      // a muted track lets its gear go quiet
  // B7 / F8: the pad's lateness verdict is MIDI's too, but only for an instrument that HAS a pad voice (OUTPUT 2, MIDI + PAD). A MIDI-only instrument (OUTPUT 1)
  // has no pad to drop: the page never drops its note for lateness, it goes out at once, late (send() clamps it to `now`).
  if (late && I.mo != 1) { st_.lateNotes++; if (!e.poly) moOff(e.track, e.t); return; }
  moNote(e, I, arp, bpm, 4);
}

void MidiSched::onStep(const SeqEvent* evs, int n, const SeqStep& step, const Seq& seq, double now) {
  now_ = now;
  const double bpm = seq.bpm();
  // F5 / B12: the tempo IN FORCE WHEN THIS STEP STARTED. The sequencer times the step's length with it and only then applies the step's own TPO, so it is the tempo of the
  // interval from this step to the next (the web integrated the tempo after the step over that interval: the clock was bent for good after every TPO).
  double bpmStart = bpm; bool hasTempo = false;
  for (int i = 0; i < n; i++) if (evs[i].type == SEV_TEMPO) hasTempo = true, bpmStart = bpmPrev_ > 0 ? bpmPrev_ : (S_ && S_->bpm > 0 ? (double)S_->bpm : bpm);
  // N1: bpmPrev_ is the tempo after the PREVIOUS STEP, which is not the tempo this step started at when a live tempo change (the host's setBpm) landed in between. The sequencer
  // counts its beats (SEV_BEAT's v) with the tempo in force when the step starts, over the interval since the previous step, so that tempo is
  // (v - previous v) * 60 / (t - previous t): read it from there. When it equals bpmPrev_ (no live change: always, but for rounding) bpmPrev_ itself is kept, bit for bit.
  for (int i = 0; i < n; i++) if (evs[i].type == SEV_BEAT) {
    if (hasBeat_ && hasTempo) {
      const double dt = evs[i].t - beatT_, db = evs[i].v - beatV_, d = dt > 1e-9 ? db * 60 / dt : 0;
      if (std::isfinite(d) && d > 0 && d < 1e6 && !(bpmPrev_ > 0 && std::fabs(d - bpmPrev_) <= 1e-9 * bpmPrev_)) bpmStart = d;
    }
    hasBeat_ = true; beatT_ = evs[i].t; beatV_ = evs[i].v;
  }
  bpmPrev_ = bpm;
  for (int i = 0; i < n; i++) {
    const SeqEvent& e = evs[i];
    onEvent(e, (e.has & SH_ARP) ? seq.arp(e) : nullptr, bpm, e.type == SEV_NOTE && seq_note_late(e.t, now), now);
  }
  for (int t = 0; t < 32; t++) if (nPendRet_[t]) pendFlush(t, step.t);          // ARP returns due by this row, on tracks the row left alone
  for (int i = 0; i < n; i++) if (evs[i].type == SEV_BEAT) {   // the tempo map: this step's time, its beat, and the tempo that measures the interval after it
    const double b = nAnc_ ? anc_[nAnc_ - 1].b + (evs[i].t - anc_[nAnc_ - 1].t) * anc_[nAnc_ - 1].bpm / 60 : evs[i].v;
    addAnchor(evs[i].t, b, bpmStart);
  }
}

void MidiSched::book(double now, double seqTime, double bpm, bool seqAlive) {
  now_ = now; (void)seqAlive;
  moClock(now, seqTime, bpm); moLfo(now, seqTime, bpm); moRamp(now); moEcho(now);   // all into the outbox, in (frame, creation) order (F2)
  flush(now);
}

// ---- transport ---------------------------------------------------------------------------------------------------------------------------
void MidiSched::start(int spp, double now) {   // moStart :17071
  now_ = now; nAnc_ = 0; haveT0_ = false; t0_ = 0; playing_ = true; bpmPrev_ = -1; hasBeat_ = false;
  for (auto& L : lfo_) { L.v = -1; L.hasT = false; L.nTrig = 0; }
  clk_ = Clk(); bool wanted = false; for (int p = 0; p < kMidiPorts; p++) wanted = wanted || clkOn_[p];
  spp_ = (wanted && spp > 0) ? clampi(spp, 0, 16383) : 0;
  std::memset(lastTickN_, 0, sizeof lastTickN_);
}

// moRelease :17176. `playing` = tp.on: a cable / OUTPUT / CH edit while the song runs does not clear the outbox.
void MidiSched::releaseAll(double now, bool playing, bool stopping) {
  now_ = now; const int64_t nf = frameOf(now);
  if (!playing) {
    nPend_ = 0;                                                                 // mobDrop
    for (int p = 0; p < kMidiPorts; p++) if (conn_[p]) push(p, nullptr, 0, nf, MK_RELEASE, MIDI_F_CLEAR);   // o.clear()
  }
  // every note that SOUNDS at the stop time ends now (B9: one whose on was never sent gets no off; B4: one off per pitch, whoever held it).
  // F3: what the gear has been told is judged BY TIME. A STOP's CLEAR discards every message queued after `nf`, an off that was already pushed for a later
  // frame included, so those are undone first (newest first) and the pitch left sounding gets its one off here, whatever was in the ring.
  if (!playing) for (int i = nFut_ - 1; i >= 0; i--) if (fut_[i].fr > nf) wire_[fut_[i].idx] = fut_[i].on ? 0 : 1;
  // the offs go in the web's order: the held notes track by track as they were played (moRelease walks mx.held), then whatever else sounds, by pitch
  for (int t = 0; t < kMidiTracks; t++) for (int i = 0; i < nHeld_[t]; i++) {
    const Held& h = held_[(size_t)t * kMidiHeldCap + i]; const int idx = (h.p * 16 + h.ch) * 128 + h.n;
    if (wire_[idx]) { if (conn_[h.p]) { const uint8_t b[3] = {(uint8_t)(0x80 | h.ch), h.n, 0}; push(h.p, b, 3, nf, MK_RELEASE, 0); } wire_[idx] = 0; }
  }
  for (int p = 0; p < kMidiPorts; p++) for (int ch = 0; ch < 16; ch++) for (int n = 0; n < 128; n++) {
    const int idx = (p * 16 + ch) * 128 + n;
    if (wire_[idx]) { if (conn_[p]) { const uint8_t b[3] = {(uint8_t)(0x80 | ch), (uint8_t)n, 0}; push(p, b, 3, nf, MK_RELEASE, 0); } }
    else if (cnt_[idx] > 0 && conn_[p]) st_.offsSuppressed++;
  }
  std::memset(wire_, 0, kMidiPorts * 16 * 128);
  if (!playing) nFut_ = 0; else for (int k = 0; k < nFut_; k++) if (fut_[k].fr > nf) wire_[fut_[k].idx] = fut_[k].on;   // the queued messages still play: the gear's state after them
  std::memset(cnt_, 0, kMidiPorts * 16 * 128); std::memset(owner_, -1, kMidiPorts * 16 * 128);
  for (int k = 0; k < kMidiPorts * 16; k++) if ((used_ >> k) & 1) {
    const int p = k >> 4, ch = k & 15; if (!conn_[p]) continue;
    const uint8_t a[3] = {(uint8_t)(0xB0 | ch), 123, 0}; push(p, a, 3, nf, MK_RELEASE, 0);
    if (bend_[k].has && bend_[k].v != 8192) { const uint8_t c[3] = {(uint8_t)(0xE0 | ch), 0, 64}; push(p, c, 3, nf, MK_RELEASE, 0); }
  }
  for (int t = 0; t < kMidiTracks; t++) { nHeld_[t] = 0; nPendRet_[t] = 0; }
  for (auto& b : bend_) b.has = false;
  nRampKeys_ = 0; nGlide_ = 0; nEcho_ = 0;
  if (stopping) { for (int i = 0; i < kMidiPorts * 16 * 128; i++) ccv_[i] = -1; used_ = 0; }   // B5, B6: only a STOP forgets (after the All Notes Off above)
  if (sus_.on) { sus_.on = false; if (conn_[sus_.p]) { const uint8_t b[3] = {(uint8_t)(0xB0 | sus_.ch), 64, 0}; push(sus_.p, b, 3, nf, MK_RELEASE, 0); } }
  for (auto& k : keys_) k.used = false;
}
void MidiSched::release(double now, bool playing) { releaseAll(now, playing); }

// moStop :17191 (B8: the Stop is stamped at the stop time, after the last tick actually sent; B3: with the port's Gear delay)
void MidiSched::stop(double now) {
  now_ = now; const bool was = clk_.on; const int64_t nf = frameOf(now);
  releaseAll(now, false, true);
  if (was) {
    int ps[kMidiPorts], n; clkPorts(ps, n);
    if (!n) for (int p = 0; p < kMidiPorts; p++) if (conn_[p]) ps[n++] = p;
    for (int i = 0; i < n; i++) {
      const int p = ps[i]; int64_t f = frameOf(now + fxLat_ - latSec(p));
      const int cnt = std::min(lastTickN_[p], 8);
      for (int q = 0; q < cnt; q++) { const int64_t tf = lastTickFrame_[p][(lastTickN_[p] - 1 - q) & 7]; if (tf <= nf) { if (tf > f) f = tf; break; } }
      const uint8_t b = 0xFC; push(p, &b, 1, f, MK_STOP, 0);
    }
    st_.stops++;
  }
  clk_ = Clk(); nAnc_ = 0; haveT0_ = false; playing_ = false;
}

void MidiSched::panic(double now) {   // :23849
  st_.mayHang = 0;                                              // F4: the panic is the answer to the flag (set again below if even this cannot be queued)
  releaseAll(now, false); now_ = now; const int64_t nf = frameOf(now);
  for (int p = 0; p < kMidiPorts; p++) {
    if (!conn_[p]) continue;
    for (int ch = 0; ch < 16; ch++) for (int cc : {64, 120, 123}) { const uint8_t b[3] = {(uint8_t)(0xB0 | ch), (uint8_t)cc, 0}; push(p, b, 3, nf, MK_PANIC, 0); }
  }
}

void MidiSched::joinPort(int p, bool all, double now) {   // clkJoin :17677 (B3: with the Gear delay)
  if (!nAnc_ || !conn_[p]) return;
  const double at = now + 0.05, sppB = (double)spp_ / 4 + beatAt(at, 120);
  const int pos = clampi(rnd(sppB * 4), 0, 16383);
  int ps[kMidiPorts], n = 0; if (all) clkPorts(ps, n); else ps[n++] = p;
  for (int i = 0; i < n; i++) {
    const uint8_t s[3] = {0xF2, (uint8_t)(pos & 127), (uint8_t)(pos >> 7)}; send(s, 3, at - 0.03, true, ps[i], MK_JOIN, 0xFF);
    const uint8_t c = 0xFB; send(&c, 1, at, true, ps[i], MK_JOIN, 0xFF);
  }
}
void MidiSched::portToggled(int p, bool on, double now) {   // clkPortToggled :17672
  if (p < 0 || p >= kMidiPorts) { st_.badArgs++; return; }
  now_ = now; clkOn_[p] = on;
  if (!playing_) return;
  if (!on) {
    if (clk_.on) { const double l = clk_.last; const uint8_t b = 0xFC; if (clk_.hasLast && l > now) send(&b, 1, l + 0.001, true, p, MK_STOP, 0xFF); else send(&b, 1, now, true, p, MK_STOP, 0xFF); }
    bool wanted = false; for (int q = 0; q < kMidiPorts; q++) wanted = wanted || clkOn_[q];
    if (!wanted) clk_.on = false;
    return;
  }
  if (clk_.on) joinPort(p, false, now);
  else if (nAnc_) { clk_.on = true; clk_.k = (int64_t)std::ceil(beatAt(now + 0.05, 120) * 24); clk_.hasLast = false; joinPort(p, true, now); }
}
void MidiSched::cableChanged(int p, bool connected, int cable, double now) {
  if (p < 0 || p >= kMidiPorts) { st_.badArgs++; return; }
  now_ = now; const bool was = conn_[p];
  releaseAll(now, playing_);
  if (was && clk_.on && clkOn_[p]) { const uint8_t b = 0xFC; push(p, &b, 1, frameOf(now), MK_STOP, 0); }   // the old cable: Stop at once
  conn_[p] = connected; cable_[p] = cable;
  if (clk_.on && clkOn_[p]) joinPort(p, false, now);
}

// ---- live ----------------------------------------------------------------------------------------------------------------------------------
int MidiSched::liveNoteOn(int ii, int note, int vol, int cap, double now) {
  now_ = now;
  if (ii < 0 || ii >= kNI || !S_ || note < -200 || note > 300) { st_.badArgs++; return -1; }
  const SongInst& I = S_->insts[ii]; if (!I.mo) return -1;
  int slot = -1; for (int i = 0; i < 32; i++) if (!keys_[i].used) { slot = i; break; }
  if (slot < 0) { st_.badArgs++; return -1; }
  SeqEvent e; e.type = SEV_NOTE; e.track = kMidiKeysTrack; e.poly = true; e.col = keyCol_++; e.ii = ii; e.mn = note + I.tsp + 12; e.vel = vol >= 0 ? clampd(vol, 0, 255) / 255.0 : 1;
  e.t = now + latSec(clampi(I.mpt, 0, kMidiPorts - 1)) + 0.001;
  const uint32_t nid = moNote(e, I, nullptr, 120, cap > 0 ? cap : 16);
  keys_[slot] = {true, ii, note, nid, clampi(I.mpt, 0, kMidiPorts - 1)};
  return slot;
}
void MidiSched::liveNoteOff(int handle, double now) {
  now_ = now;
  if (handle < 0 || handle >= 32 || !keys_[handle].used) { st_.badArgs++; return; }
  const KeyH k = keys_[handle]; keys_[handle].used = false;
  offNote(kMidiKeysTrack, k.nid, now + latSec(k.port) + 0.001);
}
void MidiSched::liveRaw(int port, const uint8_t* b, int len, double extraSec, double now) {
  now_ = now;
  if (port < 0 || port >= kMidiPorts || !b || len < 1 || len > 3 || !(b[0] >= 0x80) || b[0] == 0xF0 || b[0] == 0xF7 || !std::isfinite(extraSec) || extraSec < 0 || extraSec > 10) { st_.badArgs++; return; }
  const int st = b[0], need = st < 0xC0 ? 3 : st < 0xE0 ? 2 : st < 0xF0 ? 3 : st == 0xF1 || st == 0xF3 ? 2 : st == 0xF2 ? 3 : 1;
  if (len != need) { st_.badArgs++; return; }
  for (int i = 1; i < len; i++) if (b[i] >= 0x80) { st_.badArgs++; return; }
  if (!conn_[port]) return;
  const int ty = st & 0xF0, ch = st & 15;
  if (ty == 0xB0) { ccv_[((port * 16 + ch) * 128) + b[1]] = b[2]; used_ |= 1ull << (port * 16 + ch); if (b[1] == 64 && b[2] >= 64) sus_ = {true, (uint8_t)port, (uint8_t)ch}; }
  else if (ty == 0xE0) { const int k = port * 16 + ch; bend_[k] = {true, b[1] | (b[2] << 7), now}; used_ |= 1ull << k; }
  else if (ty == 0xC0 || ty == 0xD0 || ty == 0xA0) used_ |= 1ull << (port * 16 + ch);
  PendMsg m; m.at = now + extraSec; m.ord = ord_++; m.port = (uint8_t)port; m.len = (uint8_t)len; m.kind = MK_LIVE; m.track = 0xFF; m.cable = (uint8_t)cable_[port];
  for (int i = 0; i < len; i++) m.b[i] = b[i];
  emit(m);
}

}  // namespace thunder
