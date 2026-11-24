// U35: the thunder_midi_* C API (include/thunder/midi.h). A handle = one MidiSched + a command ring (control -> audio) + a retire ring (audio -> control: the
// songs the audio thread no longer uses) + the published stats. The audio thread only pops / pushes the rings and calls MidiSched: no allocation, no lock.
#include <cassert>
#include "thunder/midi.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>

#include "midi/midi_host.h"
#include "midi/midisched.h"
#include "rt/spsc.h"
#include "song/song.h"

using namespace thunder;

namespace {
enum Op : uint8_t { OP_SONG = 1, OP_PORT, OP_LEAD, OP_FXLAT, OP_AUDIBLE, OP_START, OP_STOP, OP_RELEASE, OP_PANIC, OP_PCLOCK, OP_CABLE, OP_KEYON, OP_KEYOFF, OP_RAW };
struct Cmd { uint8_t op; uint8_t raw[3]; int32_t a, b, c, d; double x; uint64_t m; void* p; };
constexpr int kStatN = 28;
constexpr uint64_t kNoNowBits = 0xBFF0000000000000ull;   // the double -1.0: no block yet
}  // namespace

struct thunder_midi {
  MidiSched sched;
  Spsc<Cmd, kMidiCmdCap> cmds;
  Spsc<Song*, 8> retire;
  std::atomic<uint64_t> pub[kStatN];
  std::atomic<uint64_t> lastNow;   // the bits of the last midi_end's `now` (a double; U36: the clock map's floor reads it from another thread)
  std::atomic<int> ctlIn{0};    // debug builds: how many CONTROL calls are inside right now (the control calls are single-producer: > 1 is a caller bug)
  Song* cur = nullptr;          // audio thread's song (freed by the control thread once retired / at free)
  int outstanding = 0;          // songs posted and not yet freed (control thread)
  int keySlot[128];             // audio thread: key -> sched live handle
  explicit thunder_midi(double sr) : sched(sr), lastNow(kNoNowBits) { for (auto& a : pub) a.store(0, std::memory_order_relaxed); for (int& k : keySlot) k = -1; }
};

#ifndef NDEBUG
struct CtlGuard {   // the control calls post on a SINGLE-PRODUCER ring (and set_song / the song retire count are plain state): two threads inside them at once is a bug in the caller
  std::atomic<int>* a; explicit CtlGuard(thunder_midi* h) : a(h ? &h->ctlIn : nullptr) { if (a) { const int was = a->fetch_add(1); assert(was == 0 && "thunder_midi: two threads in the CONTROL calls (single producer)"); (void)was; } }
  ~CtlGuard() { if (a) a->fetch_sub(1); }
};
#define CTL_GUARD(h) CtlGuard ctlGuard_(h)
#else
#define CTL_GUARD(h) ((void)0)
#endif

namespace {
void setErr(char* err, size_t cap, const char* m) { if (err && cap) std::snprintf(err, cap, "%s", m); }
void reap(thunder_midi* h) { Song* s; while (h->retire.pop(s)) { delete s; h->outstanding--; } }
int post(thunder_midi* h, const Cmd& c) { return h->cmds.push(c) ? THUNDER_MIDI_OK : THUNDER_MIDI_E_BUSY; }
bool fin(double v) { return std::isfinite(v); }

void publish(thunder_midi* h) {
  const MidiStats& s = h->sched.stats();
  const uint64_t v[kStatN] = {s.sent, s.ringFull, s.pendFull, s.heldFull, s.echoFull, s.glideFull, s.rampKeyFull, s.lateTicks, s.lateNotes, s.collisions, s.sameTrackRetrig, s.offsSuppressed,
                              s.onsSilent, s.ticks, s.starts, s.stops, s.badEvents, s.badArgs, s.cmdFull, s.maxPend, s.dropTicks, s.dropCtl, s.dropNoteOn, s.dropStart, s.dropOff, s.dropClear, s.futFull, s.mayHang};
  for (int i = 0; i < kStatN; i++) h->pub[i].store(v[i], std::memory_order_relaxed);
}

void apply(thunder_midi* h, const Cmd& c, double now, bool hostFigures = false) {
  MidiSched& s = h->sched;
  switch (c.op) {
    case OP_SONG: { Song* old = h->cur; h->cur = static_cast<Song*>(c.p); s.setSong(h->cur); if (old) h->retire.push(old); break; }   // (control keeps <= 6 posted: the ring never fills)
    case OP_PORT: s.setPort(c.a, c.b != 0, c.c != 0, c.x, c.d); break;
    case OP_LEAD: s.setLead(c.x); break;
    case OP_FXLAT: if (!hostFigures) s.setFxLatency(c.x); break;      // a host that feeds the figures every block (midi_begin) owns them: a posted one must not override them
    case OP_AUDIBLE: if (!hostFigures) s.setAudible(c.m); break;
    case OP_START: s.start(c.a, now); break;
    case OP_STOP: s.stop(now); break;
    case OP_RELEASE: s.release(now, c.a != 0); break;
    case OP_PANIC: s.panic(now); break;
    case OP_PCLOCK: s.portToggled(c.a, c.b != 0, now); break;
    case OP_CABLE: s.cableChanged(c.a, c.b != 0, c.c, now); break;
    case OP_KEYON: { if (h->keySlot[c.a] >= 0) { s.liveNoteOff(h->keySlot[c.a], now); h->keySlot[c.a] = -1; } h->keySlot[c.a] = s.liveNoteOn(c.b, c.c, c.d, 16, now); break; }
    case OP_KEYOFF: { if (h->keySlot[c.a] >= 0) { s.liveNoteOff(h->keySlot[c.a], now); h->keySlot[c.a] = -1; } break; }
    case OP_RAW: s.liveRaw(c.a, c.raw, c.b, c.x, now); break;
    default: break;
  }
}
// U36 T2: the two halves of a block (thunder_midi_process = begin + end; the C++ host's midi_begin / midi_end add the mixer's figures and the step loop between them).
int blockBegin(thunder_midi* h, double now, double blockSec, bool hostFigures = false, double fxLatSec = 0, uint64_t audible = 0) {
  if (hostFigures) { h->sched.setFxLatency(fxLatSec); h->sched.setAudible(audible); }   // BEFORE the commands: a key-on posted before the first block is timed with this block's figures
  int n = 0; Cmd c;
  while (n < kMidiCmdCap && h->cmds.pop(c)) { apply(h, c, now, hostFigures); n++; }
  h->sched.setBlock(blockSec);
  return n;
}
void blockEnd(thunder_midi* h, double now, double seqTime, double bpm, bool alive) {
  h->sched.book(now, seqTime, bpm, alive);
  publish(h);
  uint64_t b; std::memcpy(&b, &now, sizeof b); h->lastNow.store(b, std::memory_order_relaxed);
}
}  // namespace

extern "C" {

thunder_midi* thunder_midi_create(double sample_rate, char* err, size_t errcap) {
  if (!fin(sample_rate) || sample_rate < 8000 || sample_rate > 384000) { setErr(err, errcap, "sample rate must be 8000 .. 384000"); return nullptr; }
  try { return new thunder_midi(sample_rate); } catch (...) { setErr(err, errcap, "out of memory"); return nullptr; }
}

void thunder_midi_free(thunder_midi* h) {
  if (!h) return;
  reap(h);
  Cmd c; while (h->cmds.pop(c)) if (c.op == OP_SONG) delete static_cast<Song*>(c.p);
  delete h->cur;
  delete h;
}

int thunder_midi_set_song(thunder_midi* h, const uint8_t* json, size_t bytes) { CTL_GUARD(h);
  if (!h || !json || bytes == 0 || bytes > THUNDER_MIDI_MAX_SONG_BYTES) return THUNDER_MIDI_E_ARG;
  reap(h);
  if (h->outstanding >= 6) return THUNDER_MIDI_E_BUSY;
  Song* s = new (std::nothrow) Song; if (!s) return THUNDER_MIDI_E_MEMORY;
  JsonError je; bool ok = false;
  try { ok = s->load(reinterpret_cast<const char*>(json), bytes, &je); } catch (...) { delete s; return THUNDER_MIDI_E_MEMORY; }
  if (!ok) { delete s; return THUNDER_MIDI_E_SONG; }
  Cmd c; std::memset(&c, 0, sizeof c); c.op = OP_SONG; c.p = s;
  const int r = post(h, c);
  if (r != THUNDER_MIDI_OK) { delete s; return r; }
  h->outstanding++;
  return THUNDER_MIDI_OK;
}

int thunder_midi_set_port(thunder_midi* h, int port, int connected, int clock_on, double gear_ms, int cable) { CTL_GUARD(h);
  if (!h || port < 0 || port >= THUNDER_MIDI_PORTS || !fin(gear_ms) || cable < 0 || cable > 250) return THUNDER_MIDI_E_ARG;
  Cmd c; std::memset(&c, 0, sizeof c); c.op = OP_PORT; c.a = port; c.b = connected != 0; c.c = clock_on != 0; c.d = cable; c.x = gear_ms; return post(h, c);
}
int thunder_midi_set_lead(thunder_midi* h, double s) { CTL_GUARD(h); if (!h || !fin(s) || s < 0.005 || s > kMaxLeadSec) return THUNDER_MIDI_E_ARG; Cmd c; std::memset(&c, 0, sizeof c); c.op = OP_LEAD; c.x = s; return post(h, c); }
int thunder_midi_set_fx_latency(thunder_midi* h, double s) { CTL_GUARD(h); if (!h || !fin(s) || s < 0 || s > 2) return THUNDER_MIDI_E_ARG; Cmd c; std::memset(&c, 0, sizeof c); c.op = OP_FXLAT; c.x = s; return post(h, c); }
int thunder_midi_set_audible(thunder_midi* h, uint64_t mask) { CTL_GUARD(h); if (!h) return THUNDER_MIDI_E_ARG; Cmd c; std::memset(&c, 0, sizeof c); c.op = OP_AUDIBLE; c.m = mask; return post(h, c); }
int thunder_midi_start(thunder_midi* h, int spp) { CTL_GUARD(h); if (!h || spp < 0 || spp > 16383) return THUNDER_MIDI_E_ARG; Cmd c; std::memset(&c, 0, sizeof c); c.op = OP_START; c.a = spp; return post(h, c); }
int thunder_midi_stop(thunder_midi* h) { CTL_GUARD(h); if (!h) return THUNDER_MIDI_E_ARG; Cmd c; std::memset(&c, 0, sizeof c); c.op = OP_STOP; return post(h, c); }
int thunder_midi_release(thunder_midi* h, int playing) { CTL_GUARD(h); if (!h) return THUNDER_MIDI_E_ARG; Cmd c; std::memset(&c, 0, sizeof c); c.op = OP_RELEASE; c.a = playing != 0; return post(h, c); }
int thunder_midi_panic(thunder_midi* h) { CTL_GUARD(h); if (!h) return THUNDER_MIDI_E_ARG; Cmd c; std::memset(&c, 0, sizeof c); c.op = OP_PANIC; return post(h, c); }
int thunder_midi_port_clock(thunder_midi* h, int port, int on) { CTL_GUARD(h);
  if (!h || port < 0 || port >= THUNDER_MIDI_PORTS) return THUNDER_MIDI_E_ARG;
  Cmd c; std::memset(&c, 0, sizeof c); c.op = OP_PCLOCK; c.a = port; c.b = on != 0; return post(h, c);
}
int thunder_midi_set_cable(thunder_midi* h, int port, int connected, int cable) { CTL_GUARD(h);
  if (!h || port < 0 || port >= THUNDER_MIDI_PORTS || cable < 0 || cable > 250) return THUNDER_MIDI_E_ARG;
  Cmd c; std::memset(&c, 0, sizeof c); c.op = OP_CABLE; c.a = port; c.b = connected != 0; c.c = cable; return post(h, c);
}
int thunder_midi_key_on(thunder_midi* h, int key, int inst, int note, int vol) { CTL_GUARD(h);
  if (!h || key < 0 || key > 127 || inst < 0 || inst >= kNI || note < -200 || note > 300 || vol < -1 || vol > 255) return THUNDER_MIDI_E_ARG;
  Cmd c; std::memset(&c, 0, sizeof c); c.op = OP_KEYON; c.a = key; c.b = inst; c.c = note; c.d = vol; return post(h, c);
}
int thunder_midi_key_off(thunder_midi* h, int key) { CTL_GUARD(h); if (!h || key < 0 || key > 127) return THUNDER_MIDI_E_ARG; Cmd c; std::memset(&c, 0, sizeof c); c.op = OP_KEYOFF; c.a = key; return post(h, c); }
int thunder_midi_raw(thunder_midi* h, int port, const uint8_t* bytes, int len, double extra_seconds) { CTL_GUARD(h);
  if (!h || !bytes || port < 0 || port >= THUNDER_MIDI_PORTS || len < 1 || len > 3 || !fin(extra_seconds) || extra_seconds < 0 || extra_seconds > 10) return THUNDER_MIDI_E_ARG;
  const int st = bytes[0];
  if (st < 0x80 || st == 0xF0 || st == 0xF7) return THUNDER_MIDI_E_ARG;
  const int need = st < 0xC0 ? 3 : st < 0xE0 ? 2 : st < 0xF0 ? 3 : (st == 0xF1 || st == 0xF3) ? 2 : st == 0xF2 ? 3 : 1;
  if (len != need) return THUNDER_MIDI_E_ARG;
  for (int i = 1; i < len; i++) if (bytes[i] >= 0x80) return THUNDER_MIDI_E_ARG;
  Cmd c; std::memset(&c, 0, sizeof c); c.op = OP_RAW; c.a = port; c.b = len; c.x = extra_seconds; for (int i = 0; i < len; i++) c.raw[i] = bytes[i];
  return post(h, c);
}

}  // extern "C"

namespace thunder {
int midi_begin(thunder_midi* h, double now, double blockSec, double fxLatSec, uint64_t audible) {
  if (!h || !fin(now) || now < 0 || !fin(blockSec) || blockSec < 0 || blockSec > 1 || !fin(fxLatSec) || fxLatSec < 0 || fxLatSec > 2) return THUNDER_MIDI_E_ARG;
  return blockBegin(h, now, blockSec, true, fxLatSec, audible);
}
MidiSched& midi_sched(thunder_midi* h) { return h->sched; }
bool midi_end(thunder_midi* h, double now, double seqTime, double bpm, bool alive) {
  if (!h || !fin(now) || now < 0 || !fin(seqTime) || !(bpm > 0) || !fin(bpm)) return false;
  blockEnd(h, now, seqTime, bpm, alive);
  return true;
}
double midi_last_now(const thunder_midi* h) { const uint64_t b = h->lastNow.load(std::memory_order_relaxed); double d; std::memcpy(&d, &b, sizeof d); return d; }
}  // namespace thunder

extern "C" {
int thunder_midi_process(thunder_midi* h, double now_sec, double block_sec, double seq_time, double bpm, int seq_alive) {
  if (!h || !fin(now_sec) || now_sec < 0 || !fin(block_sec) || block_sec < 0 || block_sec > 1 || !fin(seq_time) || !(bpm > 0) || !fin(bpm)) return THUNDER_MIDI_E_ARG;
  const int n = blockBegin(h, now_sec, block_sec);
  blockEnd(h, now_sec, seq_time, bpm, seq_alive != 0);
  return n;
}

int64_t thunder_midi_drain(thunder_midi* h, thunder_midi_msg* out, int64_t cap) {
  if (!h || (!out && cap > 0) || cap < 0) return THUNDER_MIDI_E_ARG;
  static_assert(sizeof(thunder_midi_msg) == sizeof(MidiMsg), "the C record is the scheduler's record");
  return h->sched.drain(reinterpret_cast<MidiMsg*>(out), cap);
}

int thunder_midi_stats_get(const thunder_midi* h, thunder_midi_stats* out) {
  if (!h || !out || out->size < 16) return THUNDER_MIDI_E_ARG;
  thunder_midi_stats t; std::memset(&t, 0, sizeof t); t.size = out->size;
  uint64_t* f = &t.sent; for (int i = 0; i < 28; i++) f[i] = h->pub[i].load(std::memory_order_relaxed);
  const size_t n = out->size < sizeof t ? out->size : sizeof t; std::memcpy(out, &t, n);
  return THUNDER_MIDI_OK;
}

}  // extern "C"
