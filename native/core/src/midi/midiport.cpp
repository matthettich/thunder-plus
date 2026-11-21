// U36 T5 + T6: the thunder_midiout_* C API (include/thunder/midiport.h): the port table, the clock map and the time-ordered send queue with its pump. See the header for
// the thread roles. Everything the pump touches is a fixed array inside the object (made by create, on the control thread): the pump allocates nothing, locks nothing.
//
// DECISIONS (docs/port/U36-NOTES.md): the cable id of a device is a STICKY slot (0..199) taken when its id is first listed, not its index in the list, so a re-plug that renumbers
// the list cannot re-route a queued record; a record's destination is its cable's device in the CURRENT list (gone: dropped, counted). The clock map's floor reads the
// scheduler's last block start (midi_last_now) as the web's mapUpd reads ctx.currentTime.
#include "thunder/midiport.h"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <new>

#include "midi/midi_host.h"

using namespace thunder;

namespace {
constexpr int kPorts = 4, kDevs = THUNDER_MIDIOUT_MAX_DEVS, kIdBytes = THUNDER_MIDIOUT_ID_BYTES, kCabs = 200, kQ = THUNDER_MIDIOUT_MAX_QUEUE;
constexpr double kKeep = 0.98, kRefNs = 25e6, kJumpNs = 50e6, kImpMinNs = -500e6;   // the web's mapUpd: 2 % per call at the web's call rate (below), a jump over 50 ms is taken at once, the floor applies while imp > -0.5 s
constexpr int64_t kNoNext = INT64_MAX;
constexpr int kHand = 256;
constexpr int kSpinCap = 1 << 20;   // a stuck clock must not hang the pump

bool idOk(const char* s) { return s && std::memchr(s, 0, kIdBytes) != nullptr; }   // NUL-terminated within the buffer
inline bool fin(double v) { return std::isfinite(v); }
void copyId(char* dst, const char* src) { const size_t n = std::strlen(src); std::memcpy(dst, src, n + 1); }   // src is NUL-terminated within kIdBytes (checked by the callers)

// F4's ring priorities, the same classes (midisched.cpp prioOf): 0 clock ticks, CCs, bends, pressure, program; 1 ... see classOf
int classOf(const thunder_midi_msg& m) {
  if ((m.flags & THUNDER_MIDI_F_CLEAR) || m.kind == THUNDER_MIDI_K_RELEASE || m.kind == THUNDER_MIDI_K_PANIC || m.kind == THUNDER_MIDI_K_STOP || m.len < 1) return 3;
  const int st = m.b[0], ty = st & 0xF0;
  if (ty == 0x80) return 3;
  if (ty == 0x90) return m.len >= 3 && m.b[2] > 0 ? 2 : 3;
  if (ty == 0xB0 && m.len >= 2 && (m.b[1] == 120 || m.b[1] == 123)) return 3;
  if (st == 0xFC) return 3;
  if (st == 0xFA || st == 0xFB || st == 0xF2) return 2;
  if (st == 0xF8) return 0;
  return 1;
}
}  // namespace

struct thunder_midiout {
  thunder_midi* core; double sr; thunder_midiout_sink sink;

  // ---- CONTROL thread ----
  struct Ctl {
    thunder_midiout_dev devs[kDevs]; int ndevs;
    char portId[kPorts][kIdBytes]; int portClock[kPorts]; double portGear[kPorts];
    int playing;
    struct Applied { bool known; int conn, clock, cable; double gear; } app[kPorts];   // what the scheduler has been told
    struct Cab { char id[kIdBytes]; bool used; uint32_t seq; } cab[kCabs]; uint32_t cabSeq;
  } c;

  // ---- control -> pump ----
  // THE LIST AS THE PUMP SEES IT, per cable slot (L1 / L2 of the lead's read of #268): `cabLive` = the cable's device is in the current list, `cabTok` = the token the shell gave it. set_devices
  // writes a slot only when its value CHANGES (a device that stays listed is never seen as gone, whatever the pump is doing), the token before the live flag when a device appears, and
  // the pump reads a slot once per message and sends with the token it read: the sink looks the token up itself, so a list that is renumbered while the pump runs cannot send to
  // the wrong device (an unknown token is a failed send: "gone").
  std::atomic<uint8_t> cabLive[kCabs];
  std::atomic<uint64_t> cabTok[kCabs];
  std::atomic<uint32_t> epoch;             // bumped by every set_devices
  std::atomic<uint8_t> cabPurge[kCabs];    // the slot was reused for another device id: whatever is queued for the cable belongs to the old one (the pump drops it at its next epoch sweep)
  std::atomic<uint8_t> cabGone[kCabs];     // the sink said so for the cable's device: until the next set_devices (the mark; only the pump sets it, set_devices clears it)
  std::atomic<uint64_t> cabGoneTok[kCabs]; // ...and WHICH token failed (PR B, CodeRabbit on #268): a cable is gone iff the mark is set AND this equals its current token, so control replacing the token between the
                                           // pump's failed send and its mark can never mute the replacement (the pump stores the token, then the mark with release; resolve reads the mark with acquire, then the token)
  std::atomic<int64_t> outLat;             // the clock map's floor, ns

  // ---- the clock map: a seqlock, one writer at a time (busy), the pump reads ----
  std::atomic_flag busy = ATOMIC_FLAG_INIT;
  std::atomic<uint32_t> mseq;
  std::atomic<uint64_t> mOffBits;          // the smoothed offset: host ns - frame * 1e9 / sr (a double)
  std::atomic<int64_t> mHost;              // the last raw host_ns0
  std::atomic<uint32_t> mValid;

  // ---- the pump (SENDER thread only) ----
  thunder_midi_msg q[kQ]; int head, tail;       // sorted by frame (stable), the live part is [head, tail)
  uint32_t seenEpoch;
  bool held;                               // PUMP thread: records were queued while no map existed (the first map's backlog rule applies once)
  struct Hand { thunder_midi_msg m; int64_t at; uint64_t dev; bool live; bool done; } hand[kHand]; int handN;   // the last stamped messages given to the OS (CAP_TS): what a CLEAR may have to recall (and what the OS's flush must not lose)
  struct { bool ok; double off; int64_t host; } lastSnap;   // the last map the pump read whole: kept when a writer is caught mid-update
  std::atomic<uint64_t> st[12];                  // the stats, in the order of thunder_midiout_stats from `sent`
};

namespace {
enum St { S_SENT, S_GONE, S_CLEARED, S_NOMAP, S_LATE, S_QFULL, S_MAXQ, S_ERR, S_LATETICK, S_HANG, S_STALE, S_RELEASED };
inline void bump(thunder_midiout* o, int i, uint64_t n = 1) { o->st[i].store(o->st[i].load(std::memory_order_relaxed) + n, std::memory_order_relaxed); }   // one writer (the pump)
inline void setSt(thunder_midiout* o, int i, uint64_t v) { o->st[i].store(v, std::memory_order_relaxed); }

double bitsToD(uint64_t b) { double d; std::memcpy(&d, &b, sizeof d); return d; }
uint64_t dToBits(double d) { uint64_t b; std::memcpy(&b, &d, sizeof b); return b; }

// ---- the table ------------------------------------------------------------------------------------------------------------------
int findDev(const thunder_midiout* o, const char* id) { for (int i = 0; i < o->c.ndevs; i++) if (std::strcmp(o->c.devs[i].id, id) == 0) return i; return -1; }

int cabOf(thunder_midiout* o, const char* id) {   // the sticky slot of a device id, taken at first sight; when all 200 are taken the one longest unlisted is reused
  auto& c = o->c; int free = -1, oldest = -1;
  for (int i = 0; i < kCabs; i++) {
    if (!c.cab[i].used) { if (free < 0) free = i; continue; }
    if (std::strcmp(c.cab[i].id, id) == 0) { c.cab[i].seq = ++c.cabSeq; return i; }
    if (o->cabLive[i].load(std::memory_order_relaxed) == 0 && (oldest < 0 || c.cab[i].seq < c.cab[oldest].seq)) oldest = i;
  }
  const int s = free >= 0 ? free : oldest >= 0 ? oldest : 0;
  if (c.cab[s].used) o->cabPurge[s].store(1, std::memory_order_release);   // a reuse
  c.cab[s].used = true; copyId(c.cab[s].id, id); c.cab[s].seq = ++c.cabSeq; return s;
}

struct Want { int conn, clock, cable; double gear; };
Want wantOf(thunder_midiout* o, int p) {
  const auto& c = o->c; Want w; w.clock = c.portClock[p]; w.gear = c.portGear[p]; w.conn = 0; w.cable = 200 + p;
  if (c.portId[p][0]) { const int d = findDev(o, c.portId[p]); if (d >= 0) { w.conn = 1; w.cable = cabOf(o, c.portId[p]); } }
  return w;
}

// Brings the scheduler up to date with the table. While stopped: one set_port per changed port. While playing: set_cable (stops the old cable's clock, joins the new one),
// port_clock, and set_port only for a Gear change. A full command ring stops it with E_BUSY; the table is already changed and the next control call tries again.
int sync(thunder_midiout* o) {
  auto& c = o->c;
  for (int p = 0; p < kPorts; p++) {
    const Want w = wantOf(o, p); auto& a = c.app[p]; int r = THUNDER_MIDI_OK;
    if (!a.known) {
      r = thunder_midi_set_port(o->core, p, w.conn, w.clock, w.gear, w.cable);
      if (r == THUNDER_MIDI_OK) { a.known = true; a.conn = w.conn; a.clock = w.clock; a.cable = w.cable; a.gear = w.gear; }
    } else if (!c.playing) {
      if (a.conn != w.conn || a.clock != w.clock || a.cable != w.cable || a.gear != w.gear) {
        r = thunder_midi_set_port(o->core, p, w.conn, w.clock, w.gear, w.cable);
        if (r == THUNDER_MIDI_OK) { a.conn = w.conn; a.clock = w.clock; a.cable = w.cable; a.gear = w.gear; }
      }
    } else {
      if (a.conn != w.conn || a.cable != w.cable) { r = thunder_midi_set_cable(o->core, p, w.conn, w.cable); if (r == THUNDER_MIDI_OK) { a.conn = w.conn; a.cable = w.cable; } }
      if (r == THUNDER_MIDI_OK && a.clock != w.clock) { r = thunder_midi_port_clock(o->core, p, w.clock); if (r == THUNDER_MIDI_OK) a.clock = w.clock; }
      if (r == THUNDER_MIDI_OK && a.gear != w.gear) { r = thunder_midi_set_port(o->core, p, a.conn, a.clock, w.gear, a.cable); if (r == THUNDER_MIDI_OK) a.gear = w.gear; }
    }
    if (r != THUNDER_MIDI_OK) return r;
  }
  return THUNDER_MIDIOUT_OK;
}

// ---- the queue (SENDER thread) -----------------------------------------------------------------------------------------------------
int64_t atNs(double off, double sr, int64_t frame) { return (int64_t)std::llround(off + (double)frame * 1e9 / sr); }

void qRemove(thunder_midiout* o, int i) { std::memmove(&o->q[i], &o->q[i + 1], (size_t)(o->tail - i - 1) * sizeof(thunder_midi_msg)); o->tail--; }

void enqueue(thunder_midiout* o, const thunder_midi_msg& m) {
  if (o->tail - o->head >= kQ) {   // full: F4's order. The victim is the lowest class below the new message's (the latest in time among equals); a note-off, Stop or CLEAR is never the victim
    const int cls = classOf(m); int victim = -1, vc = cls;
    for (int i = o->tail - 1; i >= o->head; i--) { const int k = classOf(o->q[i]); if (k < vc) { vc = k; victim = i; } }
    bump(o, S_QFULL);
    if (victim < 0) { if (cls == 3) setSt(o, S_HANG, 1); return; }   // nothing less important to give up: the new one is dropped
    qRemove(o, victim);
  }
  if (o->tail == kQ) { std::memmove(o->q, o->q + o->head, (size_t)(o->tail - o->head) * sizeof(thunder_midi_msg)); o->tail -= o->head; o->head = 0; }
  int pos = o->tail; while (pos > o->head && o->q[pos - 1].frame > m.frame) pos--;
  std::memmove(&o->q[pos + 1], &o->q[pos], (size_t)(o->tail - pos) * sizeof(thunder_midi_msg));
  o->q[pos] = m; o->tail++;
  const uint64_t n = (uint64_t)(o->tail - o->head); if (n > o->st[S_MAXQ].load(std::memory_order_relaxed)) setSt(o, S_MAXQ, n);
}

bool listed(const thunder_midiout* o, int cable) { return cable >= 0 && cable < kCabs && o->cabLive[cable].load(std::memory_order_acquire) != 0; }
bool resolve(const thunder_midiout* o, int cable, uint64_t& tok) {   // the cable's device token, false if it is not listed (or the sink said THIS token is gone)
  if (!listed(o, cable) || o->cabPurge[cable].load(std::memory_order_acquire)) return false;   // a purge flag: the slot was just reused, a record that is still queued belongs to the old device
  const bool marked = o->cabGone[cable].load(std::memory_order_acquire) != 0;
  const uint64_t t = o->cabTok[cable].load(std::memory_order_relaxed);
  if (marked && o->cabGoneTok[cable].load(std::memory_order_relaxed) == t) return false;   // the token the sink failed on is still the cable's
  tok = t; return true;
}
// The sink failed on `tok` for `cable` (PUMP thread): mark THAT token. No compare with the cable's current token first: there is no window for control to slip a replacement into.
void markGone(thunder_midiout* o, int cable, uint64_t tok) {
  if (o->cabGone[cable].load(std::memory_order_relaxed) && o->cabGoneTok[cable].load(std::memory_order_relaxed) == o->cabTok[cable].load(std::memory_order_relaxed)) return;   // the cable's current token is already marked: a late failure of an OLDER token (a replay of history) must not move the mark off it
  o->cabGoneTok[cable].store(tok, std::memory_order_relaxed);
  o->cabGone[cable].store(1, std::memory_order_release);
}

// A CLEAR marker (the cable's STOP): what is queued for the cable after the marker's frame goes. What the OS already holds, stamped for later (CAP_TS), is recalled with the sink's
// clear(), which drops EVERYTHING the device holds, the messages before the marker's frame too (CoreMIDI's MIDIFlushOutput): those are sent again with the same stamp.
// clear() returns R, the time up to which nothing was recalled (midiport.h): no clear, or R = INT64_MAX, recalls nothing. What is NOT recalled plays: it is not replayed, and a note-on after
// the marker that was handed over and not recalled (stamp at or before max(R, t): still to play, or already due and sent) gets its note-off here, stamped RELEASE_GAP_NS after it, or at t
// if later (MidiSched::releaseAll undid that note-on, so no off was made for it, and its off for the pitch that was already sounding is stamped at the marker, BEFORE it): the last
// message for every note of the device is a note-off. A recalling sink adds note-offs only for a note-on that was already due when the CLEAR was drained (a late pump); clear() is
// asked only for a device that holds something a recall can reach (stamp after t).
void applyClear(thunder_midiout* o, const thunder_midi_msg& m, int64_t now) {
  uint64_t n = 0;
  for (int i = o->tail - 1; i >= o->head; i--) if (o->q[i].cable == m.cable && o->q[i].frame > m.frame) { qRemove(o, i); n++; }
  if (!(o->sink.caps & THUNDER_MIDIOUT_CAP_TS)) { if (n) bump(o, S_CLEARED, n); return; }
  const int64_t t = o->sink.now_ns(o->sink.user); (void)now;
  uint64_t devs[kHand]; bool ask[kHand]; int nd = 0;   // every device with a handed-over message after the marker; ask = clear() is called (something a recall can reach: stamp after t)
  for (int i = 0; i < kHand; i++) { const auto& h = o->hand[i]; if (h.live && !h.done && h.m.cable == m.cable && h.m.frame > m.frame) {
    int k = 0; while (k < nd && devs[k] != h.dev) k++;
    if (k == nd) { devs[nd] = h.dev; ask[nd] = false; nd++; }
    if (h.at > t) ask[k] = true; } }
  for (int k = 0; k < nd; k++) {
    int64_t R = t;   // nothing to recall: what is already due was not recalled
    if (ask[k]) {
    R = o->sink.clear ? o->sink.clear(o->sink.user, devs[k]) : INT64_MAX;
    if (R < t) R = t;   // a recall cannot reach what is already due
    bool failed = false;
    for (int i = 0; i < kHand; i++) {   // the ring is in send order: the ones before the marker are sent again, in that order (only what was recalled: stamp after R)
      auto& h = o->hand[(o->handN + i) % kHand];
      if (!h.live || h.dev != devs[k] || h.at <= R) continue;
      if (h.m.cable == m.cable && h.m.frame > m.frame) { h.live = false; n++; continue; }
      if (failed) { h.live = false; bump(o, S_GONE); continue; }   // the device failed during this replay: what is left of its history is lost with it
      if (o->sink.send(o->sink.user, h.dev, h.m.b, h.m.len, h.at) < 0) {
        h.live = false; failed = true; bump(o, S_ERR); bump(o, S_GONE);
        markGone(o, h.m.cable, h.dev);   // the same rule as a normal send: the failed token
      }
    }
    }
    // what was not recalled and plays or has played: P = live, not yet handled, this device and cable, after the marker, stamp at or before R (R >= t)
    auto inP = [&](const auto& h) { return h.live && !h.done && h.dev == devs[k] && h.m.cable == m.cable && h.m.frame > m.frame && h.at <= R; };
    struct Rel { uint8_t ch, note; int64_t at; } rel[kHand]; int nr = 0;   // collected first: recording a release in hand[] moves the ring
    for (int i = 0; i < kHand; i++) {
      auto& e = o->hand[(o->handN + i) % kHand];
      if (!inP(e)) continue;
      const int ty = e.m.b[0] & 0xF0;
      if (ty != 0x90 || e.m.len < 3 || e.m.b[2] == 0) continue;   // only a note-on needs a release
      bool last = true;   // the last message for its note among P (an off after it, or another on that gets the release)
      for (int j = i + 1; j < kHand && last; j++) {
        const auto& f = o->hand[(o->handN + j) % kHand];
        const int fty = f.m.b[0] & 0xF0;
        if (inP(f) && (fty == 0x80 || fty == 0x90) && (f.m.b[0] & 0x0F) == (e.m.b[0] & 0x0F) && f.m.len >= 2 && f.m.b[1] == e.m.b[1]) last = false;
      }
      if (!last) continue;
      const int64_t at = e.at + THUNDER_MIDIOUT_RELEASE_GAP_NS;
      rel[nr].ch = (uint8_t)(e.m.b[0] & 0x0F); rel[nr].note = e.m.b[1]; rel[nr].at = at > t ? at : t; nr++;
    }
    for (int i = 0; i < kHand; i++) { auto& e = o->hand[i]; if (inP(e)) e.done = true; }
    bool relFailed = false;   // the first failed release ends the sending for this device (as the replay loop does): the rest are counted
    for (int i = 0; i < nr; i++) {
      if (relFailed) { bump(o, S_GONE); continue; }
      const uint8_t off[3] = {(uint8_t)(0x80 | rel[i].ch), rel[i].note, 0};
      if (o->sink.send(o->sink.user, devs[k], off, 3, rel[i].at) < 0) { relFailed = true; bump(o, S_ERR); bump(o, S_GONE); setSt(o, S_HANG, 1); markGone(o, m.cable, devs[k]); continue; }
      bump(o, S_SENT); bump(o, S_RELEASED);
      // A release that is still to play is a handed-over message like any other: a LATER CLEAR whose sink recalls it must replay it. It goes into the ring as handled (done: nothing
      // releases it) with frame 0 (before every later marker: the replay rule, never the dropped-after-the-marker one), only into a slot that is free (its message is due): a slot
      // that holds a message still to play is never overwritten (that message could no longer be recalled and restored).
      if (rel[i].at > t) {
        auto& slot = o->hand[o->handN];
        if (!(slot.live && slot.at > t)) { std::memset(&slot.m, 0, sizeof slot.m); slot.m.cable = m.cable; slot.m.len = 3; slot.m.kind = THUNDER_MIDI_K_RELEASE; std::memcpy(slot.m.b, off, 3); slot.m.frame = 0; slot.at = rel[i].at; slot.dev = devs[k]; slot.live = true; slot.done = true; o->handN = (o->handN + 1) % kHand; }
      }
    }
  }
  if (n) bump(o, S_CLEARED, n);
}
}  // namespace

extern "C" {

thunder_midiout* thunder_midiout_create(thunder_midi* core_midi, double sample_rate, const thunder_midiout_sink* sink) {
  if (!core_midi || !sink || !sink->now_ns || !sink->send || !fin(sample_rate) || sample_rate < 8000 || sample_rate > 384000) return nullptr;
  thunder_midiout* o = new (std::nothrow) thunder_midiout;
  if (!o) return nullptr;
  o->core = core_midi; o->sr = sample_rate; o->sink = *sink;
  std::memset(&o->c, 0, sizeof o->c);
  for (auto& d : o->cabLive) d.store(0, std::memory_order_relaxed);
  for (auto& d : o->cabTok) d.store(0, std::memory_order_relaxed);
  for (auto& g : o->cabGone) g.store(0, std::memory_order_relaxed);
  for (auto& g : o->cabGoneTok) g.store(0, std::memory_order_relaxed);
  for (auto& g : o->cabPurge) g.store(0, std::memory_order_relaxed);
  for (auto& s : o->st) s.store(0, std::memory_order_relaxed);
  o->epoch.store(0, std::memory_order_relaxed); o->outLat.store(0, std::memory_order_relaxed);
  o->busy.clear(); o->mseq.store(0, std::memory_order_relaxed); o->mOffBits.store(0, std::memory_order_relaxed); o->mHost.store(0, std::memory_order_relaxed); o->mValid.store(0, std::memory_order_relaxed);
  std::memset(o->q, 0, sizeof o->q); o->head = o->tail = 0; o->seenEpoch = 0; o->held = false; std::memset(o->hand, 0, sizeof o->hand); o->handN = 0; o->lastSnap.ok = false; o->lastSnap.off = 0; o->lastSnap.host = 0;
  return o;
}

void thunder_midiout_free(thunder_midiout* o) { delete o; }

int thunder_midiout_set_devices(thunder_midiout* o, const thunder_midiout_dev* devs, int n) {
  if (!o || n < 0 || n > kDevs || (n > 0 && !devs)) return THUNDER_MIDIOUT_E_ARG;
  for (int i = 0; i < n; i++) {
    if (!idOk(devs[i].id) || !devs[i].id[0] || !idOk(devs[i].name)) return THUNDER_MIDIOUT_E_ARG;
    for (int k = 0; k < i; k++) if (std::strcmp(devs[k].id, devs[i].id) == 0 || devs[k].token == devs[i].token) return THUNDER_MIDIOUT_E_ARG;   // an id or a token listed twice
  }
  auto& c = o->c;
  for (int i = 0; i < n; i++) c.devs[i] = devs[i];
  c.ndevs = n;
  // the new mapping is built on the side and each slot is written once, and only if it changed (a device that stays listed is never "unlisted" for a moment: the pump would drop its
  // queued note-offs as gone). `gone` marks are per cable and are cleared for the slots of the new list only: a device the sink failed on gets its chance again at every scan.
  uint8_t nl[kCabs]; uint64_t nt[kCabs]; std::memset(nl, 0, sizeof nl); std::memset(nt, 0, sizeof nt);
  for (int i = 0; i < n; i++) { const int sl = cabOf(o, c.devs[i].id); nl[sl] = 1; nt[sl] = c.devs[i].token; }
  for (int i = 0; i < kCabs; i++) {
    const bool was = o->cabLive[i].load(std::memory_order_relaxed) != 0;
    if (nl[i]) {
      if (!was || o->cabTok[i].load(std::memory_order_relaxed) != nt[i]) o->cabTok[i].store(nt[i], std::memory_order_relaxed);
      if (!was) o->cabLive[i].store(1, std::memory_order_release);
      if (o->cabGone[i].load(std::memory_order_relaxed)) o->cabGone[i].store(0, std::memory_order_relaxed);
    } else if (was) o->cabLive[i].store(0, std::memory_order_release);
  }
  o->epoch.fetch_add(1, std::memory_order_release);
  return sync(o);
}

int thunder_midiout_assign(thunder_midiout* o, int port, const char* dev_id) {
  if (!o || port < 0 || port >= kPorts || !idOk(dev_id)) return THUNDER_MIDIOUT_E_ARG;
  copyId(o->c.portId[port], dev_id);
  return sync(o);
}
int thunder_midiout_set_clock(thunder_midiout* o, int port, int on) {
  if (!o || port < 0 || port >= kPorts) return THUNDER_MIDIOUT_E_ARG;
  o->c.portClock[port] = on != 0; return sync(o);
}
int thunder_midiout_set_gear(thunder_midiout* o, int port, double ms) {
  if (!o || port < 0 || port >= kPorts || !fin(ms)) return THUNDER_MIDIOUT_E_ARG;
  o->c.portGear[port] = std::fmin(200.0, std::fmax(-50.0, std::round(ms))); return sync(o);
}
int thunder_midiout_set_playing(thunder_midiout* o, int playing) {
  if (!o) return THUNDER_MIDIOUT_E_ARG;
  o->c.playing = playing != 0; return sync(o);
}
int thunder_midiout_set_out_latency(thunder_midiout* o, int64_t ns) {
  if (!o || ns < 0 || ns > 2000000000LL) return THUNDER_MIDIOUT_E_ARG;
  o->outLat.store(ns, std::memory_order_relaxed); return THUNDER_MIDIOUT_OK;
}

int thunder_midiout_get_port(const thunder_midiout* o, int port, thunder_midiout_port* out) {
  if (!o || !out || port < 0 || port >= kPorts) return THUNDER_MIDIOUT_E_ARG;
  std::memset(out, 0, sizeof *out); out->dev = -1;
  const auto& c = o->c;
  copyId(out->id, c.portId[port]); out->clock = c.portClock[port]; out->gear_ms = c.portGear[port];
  if (!c.portId[port][0]) { out->state = THUNDER_MIDIOUT_PORT_NONE; return THUNDER_MIDIOUT_OK; }
  const int d = findDev(o, c.portId[port]);
  if (d < 0) { out->state = THUNDER_MIDIOUT_PORT_MISSING; return THUNDER_MIDIOUT_OK; }
  out->state = THUNDER_MIDIOUT_PORT_OK; out->dev = d; copyId(out->name, c.devs[d].name);
  return THUNDER_MIDIOUT_OK;
}

// ---- the clock map -------------------------------------------------------------------------------------------------------------
void thunder_midiout_clock_map(thunder_midiout* o, int64_t frame0, int64_t host_ns0) {
  if (!o || o->busy.test_and_set(std::memory_order_acquire)) return;   // a second writer at the same moment skips: the next call is 20 ms away
  const double heardAt = (double)frame0 * 1e9 / o->sr;                    // the audio time frame0 is heard, ns
  double off = (double)host_ns0 - heardAt;                               // the web's  o = pt - ct * 1000
  // NEVER SOONER THAN REPORTED (the web's imp < fl rule): imp = how far the engine is ahead of what is heard right now; below the reported base + output latency, move the heard moment later
  const double fl = (double)o->outLat.load(std::memory_order_relaxed), t0 = midi_last_now(o->core);
  if (fl > 0 && t0 >= 0) {
    const double now = (double)o->sink.now_ns(o->sink.user), imp = t0 * 1e9 - (heardAt + (now - (double)host_ns0));
    if (imp < fl && imp > kImpMinNs) off += fl - imp;
  }
  const uint32_t s = o->mseq.load(std::memory_order_relaxed); const int64_t prevHost = o->mHost.load(std::memory_order_relaxed);
  double cur = bitsToD(o->mOffBits.load(std::memory_order_relaxed));
  if (!o->mValid.load(std::memory_order_relaxed) || std::fabs(off - cur) > kJumpNs) cur = off; else cur += (off - cur) * (1.0 - std::pow(kKeep, std::fmin(std::fmax((double)(host_ns0 - prevHost), 0.0), 2e9) / kRefNs));   // a jump over 50 ms (a new device, a restart) at once
  o->mseq.store(s + 1, std::memory_order_release);                       // odd: being written
  o->mOffBits.store(dToBits(cur), std::memory_order_release); o->mHost.store(host_ns0, std::memory_order_release); o->mValid.store(1, std::memory_order_release);
  o->mseq.store(s + 2, std::memory_order_release);
  o->busy.clear(std::memory_order_release);
}

namespace {
struct Snap { bool valid; double off; int64_t host; };
bool readMap(const thunder_midiout* o, Snap& r) {   // false: no consistent snapshot this time (a writer was mid-update on every try)
  r = Snap{false, 0, 0};
  for (int tries = 0; tries < 16; tries++) {
    const uint32_t a = o->mseq.load(std::memory_order_acquire);
    if (a & 1) continue;
    r.valid = o->mValid.load(std::memory_order_acquire) != 0; r.off = bitsToD(o->mOffBits.load(std::memory_order_acquire)); r.host = o->mHost.load(std::memory_order_acquire);
    if (o->mseq.load(std::memory_order_acquire) == a) return true;
  }
  return false;
}
}  // namespace

// A message the backlog rule may drop: a note-on or a controller (CC, pitch bend, pressure, program), never All Notes Off / All Sound Off, note-offs, Start / Stop / SPP / Continue, ticks (own rule).
static bool staleClass(const thunder_midi_msg& m) {
  const int ty = m.b[0] & 0xF0;
  if (ty == 0x90) return m.len >= 3 && m.b[2] > 0;
  if (ty == 0xB0) return !(m.len >= 2 && (m.b[1] == 120 || m.b[1] == 123));
  return ty == 0xA0 || ty == 0xC0 || ty == 0xD0 || ty == 0xE0;
}

// ---- the pump ------------------------------------------------------------------------------------------------------------------
int thunder_midiout_pump(thunder_midiout* o, int64_t* next_ns) {
  if (!o) return THUNDER_MIDIOUT_E_ARG;
  if (next_ns) *next_ns = kNoNext;
  const bool ts = (o->sink.caps & THUNDER_MIDIOUT_CAP_TS) != 0;
  int64_t now = o->sink.now_ns(o->sink.user);
  Snap map; if (readMap(o, map)) { o->lastSnap.ok = map.valid; o->lastSnap.off = map.off; o->lastSnap.host = map.host; } else if (o->lastSnap.ok) { map.valid = true; map.off = o->lastSnap.off; map.host = o->lastSnap.host; }   // a writer caught mid-update: the last whole map
  else { map.valid = o->mValid.load(std::memory_order_acquire) != 0; map.off = bitsToD(o->mOffBits.load(std::memory_order_acquire)); map.host = o->mHost.load(std::memory_order_acquire); }   // never read whole yet: the offset is one 64-bit atomic, always a real value; "no map" only if none was ever written

  // 1. the scheduler's records into the queue: a CLEAR marker applies to what is queued at that point of the stream; before the first map records are queued, not sent (step 3)
  thunder_midi_msg buf[128];
  for (int rounds = 0; rounds < 128; rounds++) {
    const int64_t n = thunder_midi_drain(o->core, buf, 128);
    if (n <= 0) break;
    for (int64_t i = 0; i < n; i++) {
      if (buf[i].flags & THUNDER_MIDI_F_CLEAR) { applyClear(o, buf[i], now); continue; }
      if (!map.valid) o->held = true;
      // no map yet: the record WAITS in the queue (its frame needs no map; step 3 sends nothing until one exists). A lost Start is worse than a late burst; the fixed queue and F4's eviction order bound it.
      enqueue(o, buf[i]);
    }
  }
  // 2. a changed device list: what is queued for a device that is not listed any more is dropped now
  const uint32_t ep = o->epoch.load(std::memory_order_acquire);
  if (ep != o->seenEpoch) {
    o->seenEpoch = ep; uint64_t n = 0;
    uint8_t pg[kCabs]; for (int i = 0; i < kCabs; i++) pg[i] = o->cabPurge[i].exchange(0, std::memory_order_acq_rel) ? 1 : 0;   // take the flags first: a flag set after this is for the next epoch (and resolve() sees it meanwhile)
    for (int i = o->tail - 1; i >= o->head; i--) if (!listed(o, o->q[i].cable) || pg[o->q[i].cable]) { qRemove(o, i); n++; }
    if (n) bump(o, S_GONE, n);
  }
  if (!map.valid) return 0;
  { const int64_t t = o->sink.now_ns(o->sink.user); if (t > now) now = t; }   // the drain and the CLEARs took time
  const bool backlog = o->held; o->held = false;   // the first pump with a map after a hold: the backlog rule applies to what is due now
  const bool stale = now - map.host > THUNDER_MIDIOUT_STALE_NS;
  // 3. what is due
  int sent = 0;
  while (o->head < o->tail) {
    const thunder_midi_msg m = o->q[o->head];
    uint64_t dev;
    if (!resolve(o, m.cable, dev)) { o->head++; bump(o, S_GONE); continue; }
    int64_t at = atNs(map.off, o->sr, m.frame);
    // A message is handed to the OS early (CAP_TS) only if the CLEAR history has a free slot (a slot is free when its message's time has passed): the OS must never hold more than the
    // history can restore after a flush. Without a free slot the message stays queued and is sent like a non-TS one, in the spin window.
    bool early = ts; int64_t wake = 0;
    if (ts && at > now) { const auto& slot = o->hand[o->handN]; if (slot.live && slot.at > now) { early = false; wake = slot.at; } }
    const int64_t lead = early ? THUNDER_MIDIOUT_SEND_AHEAD_NS : THUNDER_MIDIOUT_SPIN_NS;
    if (at - now > lead) { if (next_ns) *next_ns = wake && wake < at - lead ? wake : at - lead; break; }
    if (!early && at > now) {                                   // no stamps: the pump itself waits out the last 0.5 ms
      int64_t t = now; for (int it = 0; it < kSpinCap && t < at; it++) t = o->sink.now_ns(o->sink.user);
      if (t > now) now = t;
      if (ts && at > now) { if (next_ns) *next_ns = now; break; }   // CAP_TS, history full and the time not reached: it stays queued, never sent with a future stamp the history cannot restore
    }
    o->head++;
    const int64_t late = now - at;
    if (backlog && late > THUNDER_MIDIOUT_BACKLOG_NS && staleClass(m)) { bump(o, S_STALE); continue; }   // the one lateness rule at the first map: a note-on or controller from the past is not replayed
    if (m.b[0] == 0xF8 && late > THUNDER_MIDIOUT_TICK_LATE_NS) { bump(o, S_LATE); bump(o, S_LATETICK); continue; }   // B7: a late tick is dropped, never sent in a burst
    if (late > THUNDER_MIDIOUT_LATE_NS || stale) bump(o, S_LATE);
    const int r = o->sink.send(o->sink.user, dev, m.b, m.len, at);
    { const int64_t t = o->sink.now_ns(o->sink.user); if (t > now) now = t; }   // a slow send (a failed one too) must count against the next message's lateness
    if (r < 0) {
      bump(o, S_ERR); bump(o, S_GONE);
      markGone(o, m.cable, dev);   // only the token that failed: a replacement for the same id is not marked (resolve compares the mark's token with the cable's current one)
      continue;
    }
    bump(o, S_SENT); sent++;
    if (early && at > now) { auto& h = o->hand[o->handN]; h.m = m; h.at = at; h.dev = dev; h.live = true; h.done = false; o->handN = (o->handN + 1) % kHand; }
  }
  if (o->head == o->tail) { o->head = o->tail = 0; }
  return sent;
}

int thunder_midiout_stats_get(const thunder_midiout* o, thunder_midiout_stats* out) {
  if (!o || !out || out->size < 16) return THUNDER_MIDIOUT_E_ARG;
  thunder_midiout_stats t; std::memset(&t, 0, sizeof t); t.size = out->size;
  t.sent = o->st[S_SENT].load(std::memory_order_relaxed); t.dropped_gone = o->st[S_GONE].load(std::memory_order_relaxed); t.dropped_cleared = o->st[S_CLEARED].load(std::memory_order_relaxed);
  t.dropped_nomap = o->st[S_NOMAP].load(std::memory_order_relaxed); t.late = o->st[S_LATE].load(std::memory_order_relaxed); t.queue_full = o->st[S_QFULL].load(std::memory_order_relaxed);
  t.max_queue = o->st[S_MAXQ].load(std::memory_order_relaxed); t.send_errors = o->st[S_ERR].load(std::memory_order_relaxed); t.dropped_late_ticks = o->st[S_LATETICK].load(std::memory_order_relaxed);
  t.may_hang = o->st[S_HANG].load(std::memory_order_relaxed); t.dropped_stale = o->st[S_STALE].load(std::memory_order_relaxed); t.released_after_stop = o->st[S_RELEASED].load(std::memory_order_relaxed);
  { thunder_midi_stats cs; std::memset(&cs, 0, sizeof cs); cs.size = sizeof cs; if (thunder_midi_stats_get(o->core, &cs) == THUNDER_MIDI_OK && cs.may_hang) t.may_hang = 1; }   // the scheduler's own ring lost a note-off / Stop too
  const size_t n = out->size < sizeof t ? out->size : sizeof t; std::memcpy(out, &t, n);
  return THUNDER_MIDIOUT_OK;
}

}  // extern "C"
