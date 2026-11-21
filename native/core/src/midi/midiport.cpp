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
#include <new>

#include "midi/midi_host.h"

using namespace thunder;

namespace {
constexpr int kPorts = 4, kDevs = THUNDER_MIDIOUT_MAX_DEVS, kIdBytes = THUNDER_MIDIOUT_ID_BYTES, kCabs = 200, kQ = THUNDER_MIDIOUT_MAX_QUEUE;
constexpr double kSmooth = 0.02, kJumpNs = 50e6, kImpMinNs = -500e6;   // the web's mapUpd: 2 % a call, a jump over 50 ms is taken at once, the floor applies while imp > -0.5 s
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
  std::atomic<int16_t> cabDev[kCabs];      // the cable's device in the current list, -1 none
  std::atomic<uint32_t> epoch;             // bumped by every set_devices
  std::atomic<uint8_t> gone[kDevs];        // the sink said so: until the next set_devices
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
  struct Hand { thunder_midi_msg m; int64_t at; int dev; bool live; } hand[kHand]; int handN;   // the last stamped messages given to the OS (CAP_TS): what a CLEAR may have to recall (and what the OS's flush must not lose)
  std::atomic<uint64_t> st[10];                  // the stats, in the order of thunder_midiout_stats from `sent`
};

namespace {
enum St { S_SENT, S_GONE, S_CLEARED, S_NOMAP, S_LATE, S_QFULL, S_MAXQ, S_ERR, S_LATETICK, S_HANG };
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
    if (o->cabDev[i].load(std::memory_order_relaxed) < 0 && (oldest < 0 || c.cab[i].seq < c.cab[oldest].seq)) oldest = i;
  }
  const int s = free >= 0 ? free : oldest >= 0 ? oldest : 0;
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

int devOfCable(const thunder_midiout* o, int cable) {
  if (cable < 0 || cable >= kCabs) return -1;
  const int d = o->cabDev[cable].load(std::memory_order_relaxed);
  return d >= 0 && d < kDevs && !o->gone[d].load(std::memory_order_relaxed) ? d : -1;
}

// A CLEAR marker (the cable's STOP): what is queued for the cable after the marker's frame goes. What the OS already holds, stamped for later (CAP_TS), is recalled with the sink's
// clear(), which drops EVERYTHING the device holds, the messages before the marker's frame too (CoreMIDI's MIDIFlushOutput): those are sent again with the same stamp.
void applyClear(thunder_midiout* o, const thunder_midi_msg& m, int64_t now) {
  uint64_t n = 0;
  for (int i = o->tail - 1; i >= o->head; i--) if (o->q[i].cable == m.cable && o->q[i].frame > m.frame) { qRemove(o, i); n++; }
  if (!(o->sink.caps & THUNDER_MIDIOUT_CAP_TS) || !o->sink.clear) { if (n) bump(o, S_CLEARED, n); return; }
  const int64_t t = o->sink.now_ns(o->sink.user); (void)now;
  int devs[kHand], nd = 0;
  for (int i = 0; i < kHand; i++) { const auto& h = o->hand[i]; if (h.live && h.m.cable == m.cable && h.at > t) { bool seen = false; for (int k = 0; k < nd; k++) seen = seen || devs[k] == h.dev; if (!seen) devs[nd++] = h.dev; } }
  for (int k = 0; k < nd; k++) {
    o->sink.clear(o->sink.user, devs[k]);
    for (int i = 0; i < kHand; i++) {   // the ring is in send order: the ones before the marker are sent again, in that order
      auto& h = o->hand[(o->handN + i) % kHand];
      if (!h.live || h.dev != devs[k] || h.at <= t) continue;
      if (h.m.cable == m.cable && h.m.frame > m.frame) { h.live = false; n++; continue; }
      if (o->sink.send(o->sink.user, h.dev, h.m.b, h.m.len, h.at) < 0) { h.live = false; bump(o, S_ERR); }
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
  for (auto& d : o->cabDev) d.store(-1, std::memory_order_relaxed);
  for (auto& g : o->gone) g.store(0, std::memory_order_relaxed);
  for (auto& s : o->st) s.store(0, std::memory_order_relaxed);
  o->epoch.store(0, std::memory_order_relaxed); o->outLat.store(0, std::memory_order_relaxed);
  o->busy.clear(); o->mseq.store(0, std::memory_order_relaxed); o->mOffBits.store(0, std::memory_order_relaxed); o->mHost.store(0, std::memory_order_relaxed); o->mValid.store(0, std::memory_order_relaxed);
  std::memset(o->q, 0, sizeof o->q); o->head = o->tail = 0; o->seenEpoch = 0; std::memset(o->hand, 0, sizeof o->hand); o->handN = 0;
  return o;
}

void thunder_midiout_free(thunder_midiout* o) { delete o; }

int thunder_midiout_set_devices(thunder_midiout* o, const thunder_midiout_dev* devs, int n) {
  if (!o || n < 0 || n > kDevs || (n > 0 && !devs)) return THUNDER_MIDIOUT_E_ARG;
  for (int i = 0; i < n; i++) {
    if (!idOk(devs[i].id) || !devs[i].id[0] || !idOk(devs[i].name)) return THUNDER_MIDIOUT_E_ARG;
    for (int k = 0; k < i; k++) if (std::strcmp(devs[k].id, devs[i].id) == 0) return THUNDER_MIDIOUT_E_ARG;
  }
  auto& c = o->c;
  for (int i = 0; i < n; i++) c.devs[i] = devs[i];
  c.ndevs = n;
  for (int i = 0; i < kCabs; i++) o->cabDev[i].store(-1, std::memory_order_relaxed);
  for (int i = 0; i < n; i++) o->cabDev[cabOf(o, c.devs[i].id)].store((int16_t)i, std::memory_order_relaxed);
  for (auto& g : o->gone) g.store(0, std::memory_order_relaxed);
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
  const uint32_t s = o->mseq.load(std::memory_order_relaxed);
  double cur = bitsToD(o->mOffBits.load(std::memory_order_relaxed));
  if (!o->mValid.load(std::memory_order_relaxed) || std::fabs(off - cur) > kJumpNs) cur = off; else cur += (off - cur) * kSmooth;   // a jump over 50 ms (a new device, a restart) at once
  o->mseq.store(s + 1, std::memory_order_release);                       // odd: being written
  o->mOffBits.store(dToBits(cur), std::memory_order_release); o->mHost.store(host_ns0, std::memory_order_release); o->mValid.store(1, std::memory_order_release);
  o->mseq.store(s + 2, std::memory_order_release);
  o->busy.clear(std::memory_order_release);
}

namespace {
struct Snap { bool valid; double off; int64_t host; };
Snap readMap(const thunder_midiout* o) {
  Snap r{false, 0, 0};
  for (int tries = 0; tries < 16; tries++) {
    const uint32_t a = o->mseq.load(std::memory_order_acquire);
    if (a & 1) continue;
    r.valid = o->mValid.load(std::memory_order_acquire) != 0; r.off = bitsToD(o->mOffBits.load(std::memory_order_acquire)); r.host = o->mHost.load(std::memory_order_acquire);
    if (o->mseq.load(std::memory_order_acquire) == a) return r;
  }
  return r;   // a writer that never finishes: the last read (a torn pair would still be a map from a few ms ago)
}
}  // namespace

// ---- the pump ------------------------------------------------------------------------------------------------------------------
int thunder_midiout_pump(thunder_midiout* o, int64_t* next_ns) {
  if (!o) return THUNDER_MIDIOUT_E_ARG;
  if (next_ns) *next_ns = kNoNext;
  const bool ts = (o->sink.caps & THUNDER_MIDIOUT_CAP_TS) != 0;
  int64_t now = o->sink.now_ns(o->sink.user);
  const Snap map = readMap(o);

  // 1. the scheduler's records into the queue: a CLEAR marker applies to what is queued at that point of the stream; before the first map nothing can be timed
  thunder_midi_msg buf[128];
  for (int rounds = 0; rounds < 128; rounds++) {
    const int64_t n = thunder_midi_drain(o->core, buf, 128);
    if (n <= 0) break;
    for (int64_t i = 0; i < n; i++) {
      if (buf[i].flags & THUNDER_MIDI_F_CLEAR) { applyClear(o, buf[i], now); continue; }
      if (!map.valid) { bump(o, S_NOMAP); continue; }
      enqueue(o, buf[i]);
    }
  }
  // 2. a changed device list: what is queued for a device that is not listed any more is dropped now
  const uint32_t ep = o->epoch.load(std::memory_order_acquire);
  if (ep != o->seenEpoch) {
    o->seenEpoch = ep; uint64_t n = 0;
    for (int i = o->tail - 1; i >= o->head; i--) if (devOfCable(o, o->q[i].cable) < 0) { qRemove(o, i); n++; }
    if (n) bump(o, S_GONE, n);
  }
  if (!map.valid) return 0;
  const bool stale = now - map.host > THUNDER_MIDIOUT_STALE_NS;
  // 3. what is due
  int sent = 0;
  while (o->head < o->tail) {
    const thunder_midi_msg m = o->q[o->head];
    const int dev = devOfCable(o, m.cable);
    if (dev < 0) { o->head++; bump(o, S_GONE); continue; }
    int64_t at = atNs(map.off, o->sr, m.frame);
    const int64_t lead = ts ? THUNDER_MIDIOUT_SEND_AHEAD_NS : THUNDER_MIDIOUT_SPIN_NS;
    if (at - now > lead) { if (next_ns) *next_ns = at - lead; break; }
    if (!ts && at > now) {                                   // no stamps: the pump itself waits out the last 0.5 ms
      int64_t t = now; for (int it = 0; it < kSpinCap && t < at; it++) t = o->sink.now_ns(o->sink.user);
      if (t > now) now = t;
    }
    o->head++;
    const int64_t late = now - at;
    if (m.b[0] == 0xF8 && late > THUNDER_MIDIOUT_TICK_LATE_NS) { bump(o, S_LATE); bump(o, S_LATETICK); continue; }   // B7: a late tick is dropped, never sent in a burst
    if (late > THUNDER_MIDIOUT_LATE_NS || stale) bump(o, S_LATE);
    const int r = o->sink.send(o->sink.user, dev, m.b, m.len, at);
    if (r < 0) { bump(o, S_ERR); bump(o, S_GONE); o->gone[dev].store(1, std::memory_order_relaxed); continue; }
    bump(o, S_SENT); sent++;
    if (ts && at > now) { auto& h = o->hand[o->handN]; h.m = m; h.at = at; h.dev = dev; h.live = true; o->handN = (o->handN + 1) % kHand; }
  }
  if (o->head == o->tail) { o->head = o->tail = 0; }
  return sent;
}

int thunder_midiout_stats_get(const thunder_midiout* o, thunder_midiout_stats* out) {
  if (!o || !out || out->size < 16) return THUNDER_MIDIOUT_E_ARG;
  thunder_midiout_stats t; std::memset(&t, 0, sizeof t); t.size = out->size;
  uint64_t* f = &t.sent; for (int i = 0; i < 10; i++) f[i] = o->st[i].load(std::memory_order_relaxed);
  const size_t n = out->size < sizeof t ? out->size : sizeof t; std::memcpy(out, &t, n);
  return THUNDER_MIDIOUT_OK;
}

}  // extern "C"
