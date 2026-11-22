// A CLEAR (the Stop) meets stamped messages the pump has already handed to the sink (up to 20 ms ahead). A sink that cannot recall them (clear == NULL, or it returns INT64_MAX) leaves them
// to play, and the core must not replay them and must end every note it started: the last message of every note, by stamp, is a note-off. A recalling sink (returns 0) is the old behaviour:
// nothing is released. The song is the debug song of the Android shell (two bars of C major, 120 bpm) through the real scheduler, pump and clock map on a FAKE clock: the audio clock
// runs 1:1 with the host clock plus the output latency; the Stop is issued at every offset before / after a note-on; latency 0, 5, 19, 20, 26 ms; Gear delay 0, +30, -20 ms.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "midi/midi_host.h"
#include "seq/runner.h"
#include "seq/seq.h"
#include "song/song.h"
#include "thunder/midi.h"
#include "thunder/midiport.h"

static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { g_fail++; std::printf("FAIL %d: ", __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

namespace {
using namespace thunder;
struct Rec { int64_t at, sentAt; int order, dev; uint8_t b[3]; int len; bool recalled; };
// The fake OS. mode: 0 clear == NULL, 1 clear returns INT64_MAX (recalls nothing), 2 recalls everything not yet due and returns 0, 3 the Android shell's port without stamps (recalls the stamps
// after now + 50 us, returns now + 50 us), 4 recalls the stamps after now + 5 ms and returns that, 5 recalls the stamps after rCut (set by the test) and returns rCut.
struct Os {
  int64_t now = 0; int mode = 0; int64_t rCut = INT64_MAX;
  std::vector<Rec> r; int clears = 0; std::map<int, int> clearsBy; bool fail[16] = {};
  static int64_t nowFn(void* u) { return static_cast<Os*>(u)->now; }
  static int sendFn(void* u, uint64_t dev, const uint8_t* b, int len, int64_t at) {
    Os* o = static_cast<Os*>(u); if (dev != 7 && dev != 8 && dev != 9) return -1; if (o->fail[dev]) return -1;
    Rec x; std::memset(&x, 0, sizeof x); std::memcpy(x.b, b, (size_t)len); x.len = len; x.at = at; x.sentAt = o->now; x.dev = (int)dev; x.order = (int)o->r.size(); o->r.push_back(x); return 0;
  }
  static int64_t clearFn(void* u, uint64_t dev) {
    Os* o = static_cast<Os*>(u); o->clears++; o->clearsBy[(int)dev]++;
    int64_t R = o->mode == 1 ? INT64_MAX : o->mode == 2 ? 0 : o->mode == 3 ? o->now + 50000 : o->mode == 4 ? o->now + 5000000 : o->rCut;
    if (R == INT64_MAX) return R;
    for (Rec& x : o->r) if (x.dev == (int)dev && x.at > (R < o->now ? o->now : R)) x.recalled = true;
    return R;
  }
};

// The device's view: per (device, channel, note) the last message by stamp (then an off before an on at the same stamp, then arrival order) must be a note-off.
int stuckNotes(const Os& os) {
  std::map<int, const Rec*> last; int stuck = 0;
  auto isOn = [](const Rec* x) { return ((x->b[0] & 0xF0) == 0x90 && x->b[2] > 0) ? 1 : 0; };
  for (const Rec& x : os.r) { if (x.recalled) continue; const int ty = x.b[0] & 0xF0; if (ty != 0x80 && ty != 0x90) continue;
    const int key = (x.dev * 16 + (x.b[0] & 15)) * 128 + x.b[1]; const Rec*& l = last[key];
    if (!l || x.at > l->at || (x.at == l->at && x.order > l->order)) l = &x; }   // equal stamps: in the order they arrived (the gap itself is checked on its own)
  for (auto& kv : last) if (isOn(kv.second)) stuck++;
  return stuck;
}
// A note-on sent twice with the same stamp to one device (a replay), and a note-off for a note that was never on at that device (a release sent to the wrong device).
int dupOns(const Os& os) {
  int d = 0;
  for (size_t i = 0; i < os.r.size(); i++) for (size_t j = i + 1; j < os.r.size(); j++) { const Rec &a = os.r[i], &b = os.r[j];
    if (!a.recalled && !b.recalled && a.dev == b.dev && (a.b[0] & 0xF0) == 0x90 && a.b[2] > 0 && a.len == b.len && a.at == b.at && std::memcmp(a.b, b.b, (size_t)a.len) == 0) d++; }
  return d;
}
int strayOffs(const Os& os) {
  int s = 0;
  for (const Rec& x : os.r) if ((x.b[0] & 0xF0) == 0x80) { bool on = false; for (const Rec& y : os.r) on = on || (y.dev == x.dev && (y.b[0] & 0xF0) == 0x90 && y.b[2] > 0 && (y.b[0] & 15) == (x.b[0] & 15) && y.b[1] == x.b[1]); if (!on) s++; }
  return s;
}
int countOf(const Os& os, int dev, int ty, int ch, int note) { int n = 0; for (const Rec& x : os.r) if (!x.recalled && x.dev == dev && (x.b[0] & 0xF0) == ty && (x.b[0] & 15) == ch && x.b[1] == note && (ty != 0x90 || x.b[2] > 0)) n++; return n; }

std::string songText() {   // the debug song of midi_out.cpp
  std::string s = "{\"app\":\"thunder-song\",\"v\":6,\"bpm\":120,\"lpb\":4,\"ticks\":6,\"swing\":50,\"dlen\":16,\"plen\":[16],\"song\":[";
  for (int r = 0; r < 2; r++) { s += r ? ",[" : "["; s += "0"; for (int t = 1; t < 33; t++) s += ",-1"; s += "]"; }
  s += "],\"chains\":[[[0,0]"; for (int k = 1; k < 16; k++) s += ",[-1,0]";
  s += "]],\"phrases\":[[";
  const int notes[4] = {48, 52, 55, 60};
  for (int row = 0; row < 16; row++) {
    if (row) s += ",";
    char b[96];
    if (row % 4 == 0) std::snprintf(b, sizeof b, "[%d,-1,-1,-1,255,0,-1,-1,-1,-1,-1,-1]", notes[row / 4]);
    else if (row % 4 == 2) std::snprintf(b, sizeof b, "[200,-1,-1,-1,-1,0,-1,-1,-1,-1,-1,-1]");
    else std::snprintf(b, sizeof b, "[-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1]");
    s += b;
  }
  s += "]],\"insts\":[{\"mo\":1,\"mch\":0,\"mpt\":0,\"pbr\":2}]}";
  return s;
}

struct Result { bool ok = true; uint64_t released = 0, cleared = 0, sent = 0; int clears = 0; int stuck = 0, dup = 0; };

// One run. leadNs: the Stop is issued this long BEFORE the second note-on's time (negative: after it).
Result run(int mode, int64_t latNs, double gearMs, int64_t leadNs, int64_t pumpDelayNs = 0) {   // pumpDelayNs: the pump does not run for this long after the Stop is posted (the CLEAR is drained late)
  const double sr = 48000; const int blk = 128; const int64_t H0 = 1000000000LL;
  Os os; os.mode = mode; os.now = H0;
  thunder_midiout_sink sk; std::memset(&sk, 0, sizeof sk); sk.user = &os; sk.now_ns = Os::nowFn; sk.send = Os::sendFn; sk.clear = mode == 0 ? nullptr : Os::clearFn; sk.caps = THUNDER_MIDIOUT_CAP_TS;
  thunder_midi* h = thunder_midi_create(sr, nullptr, 0); thunder_midiout* o = thunder_midiout_create(h, sr, &sk);
  thunder_midiout_dev d; std::memset(&d, 0, sizeof d); std::strcpy(d.id, "n:Fake"); std::strcpy(d.name, "Fake"); d.token = 7; d.flags = THUNDER_MIDIOUT_DEV_TS;
  thunder_midiout_set_devices(o, &d, 1); thunder_midiout_assign(o, 0, "n:Fake"); thunder_midiout_set_out_latency(o, latNs);
  const std::string text = songText();
  Song song; SeqVars vars; JsonError je; song.load(text.data(), text.size(), &je);
  SeqOpts so; so.live = true; so.once = true; so.row = 0;
  std::unique_ptr<Seq> seq(new Seq(song, vars, SeqMode::Song, so)); SeqRunner runner; std::vector<SeqEvent> evs(256); SeqStep st;
  thunder_midiout_set_clock(o, 0, 1); thunder_midiout_set_gear(o, 0, gearMs); thunder_midiout_set_playing(o, 1);
  thunder_midi_set_song(h, reinterpret_cast<const uint8_t*>(text.data()), text.size()); thunder_midi_start(h, 0);
  const int64_t stepNs = 250000, blkNs = (int64_t)(blk / sr * 1e9);
  const int64_t T1 = 500000000LL + 500000000LL;   // the second note-on (song time 0.5 s after the start + 0.5 s): audio time
  const int64_t stopAt = T1 - leadNs;
  int64_t nextBlk = 0, frames = 0; bool stopped = false; int64_t tStop = 0;
  const int64_t tEnd = stopAt + 600000000LL;
  for (int64_t t = 0; t <= tEnd; t += stepNs) {
    os.now = H0 + t;
    if (!stopped && t >= stopAt) {   // the Stop: what engineStop does for the last block, at this audio time
      const double now = (double)t / 1e9;
      midi_begin(h, now, 0.01, 0.0, ~(uint64_t)0); midi_sched(h).stop(now); midi_end(h, now, now, 120, false); thunder_midiout_set_playing(o, 0);
      stopped = true; tStop = t;
    }
    while (!stopped && t >= nextBlk) {   // an audio block (the callback runs at the audio time: the block is heard one output latency later)
      const double now = (double)frames / sr, bs = (double)blk / sr;
      midi_begin(h, now, bs, 0.0, ~(uint64_t)0);
      if (frames == 0) { seq->setTime(now + 0.5); runner.start(seq.get()); }
      runner.pump(now, 0.25, evs.data(), (int)evs.size(), &st, [&](const SeqEvent* e, int n, const SeqStep& step) { midi_sched(h).onStep(e, n, step, *seq, now); });
      midi_end(h, now, seq->time(), seq->bpm(), seq->alive());
      thunder_midiout_clock_map(o, frames, H0 + (int64_t)(now * 1e9) + latNs);
      frames += blk; nextBlk += blkNs;
    }
    if (!stopped || t >= tStop + pumpDelayNs) thunder_midiout_pump(o, nullptr);
  }
  Result R; thunder_midiout_stats ss; std::memset(&ss, 0, sizeof ss); ss.size = sizeof ss; thunder_midiout_stats_get(o, &ss);
  R.released = ss.released_after_stop; R.cleared = ss.dropped_cleared; R.sent = ss.sent; R.clears = os.clears;
  R.stuck = stuckNotes(os); R.dup = dupOns(os); R.ok = R.stuck == 0 && R.dup == 0 && strayOffs(os) == 0;
  CHECK(R.sent == os.r.size(), "stats.sent %llu equals what the sink got %zu", (unsigned long long)R.sent, os.r.size());
  if (mode == 0) CHECK(os.clears == 0, "no clear function, no clear calls");
  thunder_midiout_free(o); thunder_midi_free(h);
  return R;
}

// ---- raw scenarios: one or two devices, raw notes through the real scheduler / pump / clock map, the Stop at a chosen time ---------------------------------------------------------------
const int64_t kH0 = 1000000000LL, kStep = 250000, kBlkNs = 2666667;
struct Sim {
  Os os; thunder_midi* h; thunder_midiout* o; int64_t t = 0, nextBlk = 0, frames = 0, pumpFrom = 0, lat; bool stopped = false;
  Sim(int mode, int ndev, int64_t latNs, bool samedev = false) : lat(latNs) {
    os.mode = mode; os.now = kH0;
    thunder_midiout_sink sk; std::memset(&sk, 0, sizeof sk); sk.user = &os; sk.now_ns = Os::nowFn; sk.send = Os::sendFn; sk.clear = mode == 0 ? nullptr : Os::clearFn; sk.caps = THUNDER_MIDIOUT_CAP_TS;
    h = thunder_midi_create(48000, nullptr, 0); o = thunder_midiout_create(h, 48000, &sk);
    thunder_midiout_dev d[2]; std::memset(d, 0, sizeof d); std::strcpy(d[0].id, "n:A"); std::strcpy(d[0].name, "A"); d[0].token = 7; std::strcpy(d[1].id, "n:B"); std::strcpy(d[1].name, "B"); d[1].token = 8;
    thunder_midiout_set_devices(o, d, ndev); thunder_midiout_assign(o, 0, "n:A"); if (ndev > 1) thunder_midiout_assign(o, 1, "n:B"); else if (samedev) thunder_midiout_assign(o, 1, "n:A");
    thunder_midiout_set_out_latency(o, latNs);
    advance(30000000);
  }
  ~Sim() { thunder_midiout_free(o); thunder_midi_free(h); }
  void block() { const double now = (double)frames / 48000; midi_begin(h, now, 128.0 / 48000, 0.0, ~(uint64_t)0); midi_end(h, now, now, 120, false); thunder_midiout_clock_map(o, frames, kH0 + (int64_t)(now * 1e9) + lat); frames += 128; nextBlk += kBlkNs; }
  void advance(int64_t ns) { const int64_t end = t + ns; while (t < end) { t += kStep; os.now = kH0 + t; while (t >= nextBlk) block(); if (t >= pumpFrom) thunder_midiout_pump(o, nullptr); } }
  int64_t post(int port, uint8_t b0, uint8_t b1, uint8_t b2, int64_t atNs) {   // the message sounds at host-clock time atNs (ns since kH0) + latency: delay from the block that picks it up
    const uint8_t b[3] = {b0, b1, b2}; const int len = (b0 >= 0xC0 && b0 < 0xE0) ? 2 : 3;
    const double delay = (double)(atNs - nextBlk) / 1e9; if (thunder_midi_raw(h, port, b, len, delay < 0 ? 0 : delay) != 0) CHECK(false, "raw refused");
    return atNs;
  }
  void stop() { const double now = (double)t / 1e9; midi_begin(h, now, 0.01, 0.0, ~(uint64_t)0); midi_sched(h).stop(now); midi_end(h, now, now, 120, false); stopped = true; }
  thunder_midiout_stats stats() { thunder_midiout_stats s; std::memset(&s, 0, sizeof s); s.size = sizeof s; thunder_midiout_stats_get(o, &s); return s; }
  void finish(int64_t ns = 300000000) { advance(ns); }
};
struct Ev { int port; uint8_t b0, b1, b2; int64_t rel; };
// Posts the events at T0 + rel (T0 = 60 ms after the next block), runs to T0 - leadNs, stops, finishes. Blocks keep running after the Stop (nothing else plays).
int64_t playAndStop(Sim& s, const std::vector<Ev>& evs, int64_t leadNs, int64_t holdNs = 0) {
  const int64_t T0 = s.nextBlk + 60000000;
  for (const Ev& e : evs) s.post(e.port, e.b0, e.b1, e.b2, T0 + e.rel);
  s.advance(T0 - leadNs - s.t);
  s.pumpFrom = s.t + holdNs; s.stop();
  s.finish();
  return T0;
}
void expectClean(const Sim& s, const char* what, bool stray = true) {
  CHECK(stuckNotes(s.os) == 0, "%s: a note is left on", what); CHECK(dupOns(s.os) == 0, "%s: a note-on was sent twice", what); if (stray) CHECK(strayOffs(s.os) == 0, "%s: a note-off for a note that never played there", what);
}
void rawScenarios() {
  const int64_t ms = 1000000;
  for (int mode = 0; mode <= 5; mode++) {
    const bool exact = mode <= 1;   // nothing recalled: the counts are exact
    // A: the same pitch on two channels: ch 0 on, then an off on ch 1 (it must not hide the release of the ch 0 note); B: another pitch's off does not either
    { Sim s(mode, 1, 0); const int64_t T0 = playAndStop(s, {{0, 0x90, 60, 100, 0}, {0, 0x81, 60, 0, 1 * ms}}, 5 * ms); (void)T0;
      expectClean(s, "A channel", false); if (exact) { CHECK(s.stats().released_after_stop == 1, "A: one release, got %llu", (unsigned long long)s.stats().released_after_stop); CHECK(countOf(s.os, 7, 0x80, 0, 60) == 1, "A: the ch 0 note was released"); CHECK(s.stats().dropped_cleared == 0, "A: nothing counted as cleared"); }
      CHECK(s.stats().sent == s.os.r.size(), "A: sent == what the sink got"); }
    { Sim s(mode, 1, 0); playAndStop(s, {{0, 0x90, 60, 100, 0}, {0, 0x80, 61, 0, 1 * ms}}, 5 * ms);
      expectClean(s, "B pitch", false); if (exact) CHECK(s.stats().released_after_stop == 1 && countOf(s.os, 7, 0x80, 0, 60) == 1, "B: one release for pitch 60 (%llu)", (unsigned long long)s.stats().released_after_stop); }
    // C: a velocity-0 note-on is an off: no release; D: two note-ons of one note get one release; the gap and the order of the release
    { Sim s(mode, 1, 0); playAndStop(s, {{0, 0x90, 60, 100, 0}, {0, 0x90, 60, 0, 1 * ms}}, 5 * ms);
      expectClean(s, "C vel 0"); if (exact) CHECK(s.stats().released_after_stop == 0, "C: no release (the vel-0 on is the off), got %llu", (unsigned long long)s.stats().released_after_stop); }
    { Sim s(mode, 1, 0); playAndStop(s, {{0, 0x90, 60, 100, 0}, {0, 0x90, 60, 90, 2 * ms}}, 5 * ms);
      expectClean(s, "D twice"); if (exact) CHECK(s.stats().released_after_stop == 1, "D: one release for two note-ons, got %llu", (unsigned long long)s.stats().released_after_stop); }
    if (exact) {   // the release is stamped at least RELEASE_GAP_NS after the note-on it ends
      Sim s(mode, 1, 0); playAndStop(s, {{0, 0x90, 64, 100, 0}}, 5 * ms);
      int64_t on = -1, off = -1; for (const Rec& x : s.os.r) { if ((x.b[0] & 0xF0) == 0x90) on = x.at; if ((x.b[0] & 0xF0) == 0x80) off = x.at; }
      CHECK(on > 0 && off - on >= THUNDER_MIDIOUT_RELEASE_GAP_NS, "the release is %lld us after the note-on (>= %lld)", (long long)((off - on) / 1000), (long long)(THUNDER_MIDIOUT_RELEASE_GAP_NS / 1000)); }
    // E: a second Stop does not release the same note again
    { Sim s(mode, 1, 0); const int64_t T0 = s.nextBlk + 60 * ms; s.post(0, 0x90, 60, 100, T0); s.advance(T0 - 5 * ms - s.t); s.stop(); s.advance(2 * ms); s.stop(); s.finish();
      expectClean(s, "E second stop"); if (exact) CHECK(s.stats().released_after_stop == 1, "E: released once over two Stops, got %llu", (unsigned long long)s.stats().released_after_stop); }
    // F: two devices (two cables), a note each: each is released on its own device only
    { Sim s(mode, 2, 0); playAndStop(s, {{0, 0x90, 60, 100, 0}, {1, 0x90, 62, 100, 1 * ms}}, 5 * ms);
      expectClean(s, "F two devices"); if (exact) { CHECK(s.stats().released_after_stop == 2, "F: two releases, got %llu", (unsigned long long)s.stats().released_after_stop); CHECK(countOf(s.os, 7, 0x80, 0, 60) == 1 && countOf(s.os, 8, 0x80, 0, 62) == 1, "F: each release went to its own device"); } }
    // F2: two ports on one device (one cable): both notes released
    { Sim s(mode, 1, 0, true); playAndStop(s, {{0, 0x90, 60, 100, 0}, {1, 0x92, 61, 100, 1 * ms}}, 5 * ms);
      expectClean(s, "F2 two ports, one device"); if (exact) CHECK(s.stats().released_after_stop == 2, "F2: two releases, got %llu", (unsigned long long)s.stats().released_after_stop); }
  }
  // L: device A is opened again with a new token (9) while a note-on for its old token (7) is still handed over: one cable, two tokens. The CLEAR releases each note on the token it went to
  //    (the device filter of the release set; a note-off for a token that never got the note-on would be a stray, and the new token's note would hang)
  {
    Sim s(1, 1, 0); const int64_t T0 = s.nextBlk + 60 * ms;
    s.post(0, 0x90, 60, 100, T0); s.advance(T0 - 16 * ms - s.t);   // handed to token 7
    thunder_midiout_dev d; std::memset(&d, 0, sizeof d); std::strcpy(d.id, "n:A"); std::strcpy(d.name, "A"); d.token = 9;
    thunder_midiout_set_devices(s.o, &d, 1);
    s.post(0, 0x90, 62, 100, T0 + 3 * ms); s.advance(T0 - 5 * ms - s.t); s.stop(); s.finish();
    expectClean(s, "L reopened device");
    CHECK(countOf(s.os, 7, 0x80, 0, 60) == 1 && countOf(s.os, 9, 0x80, 0, 62) == 1 && countOf(s.os, 7, 0x80, 0, 62) == 0 && countOf(s.os, 9, 0x80, 0, 60) == 0, "L: each release went to the token of its note-on"); }
  // G (nothing recallable): the CLEAR is drained 3 ms late; device B's note-on is already due, device A's is not: clear() is asked for A only, B's note is released all the same
  for (int mode = 1; mode <= 5; mode++) {
    Sim s(mode, 2, 0); const int64_t T0 = s.nextBlk + 60 * ms;
    s.post(1, 0x90, 62, 100, T0 + 1 * ms); s.post(0, 0x90, 60, 100, T0 + 12 * ms);
    s.advance(T0 - s.t); s.pumpFrom = s.t + 3 * ms; s.stop(); s.finish();
    expectClean(s, "G late drain");
    if (mode == 1) { CHECK(s.os.clearsBy[8] == 0, "G: clear() not called for the device that holds nothing recallable (%d)", s.os.clearsBy[8]); CHECK(s.os.clearsBy[7] == 1, "G: clear() called once for the other (%d)", s.os.clearsBy[7]);
      CHECK(s.stats().released_after_stop == 2, "G: both released, got %llu", (unsigned long long)s.stats().released_after_stop);
      for (const Rec& x : s.os.r) if ((x.b[0] & 0xF0) == 0x80) CHECK(x.at >= x.sentAt, "G: a release is never stamped in the past (%lld us)", (long long)((x.at - x.sentAt) / 1000)); }
  }
  // H: the device fails when the releases are sent: one error, the others counted as gone, may_hang set, the cable muted (a later message is dropped, not sent)
  { Sim s(1, 1, 0); const int64_t T0 = s.nextBlk + 60 * ms;
    s.post(0, 0x90, 60, 100, T0); s.post(0, 0x90, 62, 100, T0 + 1 * ms); s.post(0, 0x90, 64, 100, T0 + 2 * ms);
    s.advance(T0 - 5 * ms - s.t); s.os.fail[7] = true; s.stop(); s.advance(20 * ms); s.post(0, 0xB0, 7, 99, s.nextBlk + 40 * ms); s.finish();
    const thunder_midiout_stats st = s.stats();
    CHECK(st.send_errors == 1, "H: one send error, got %llu", (unsigned long long)st.send_errors);
    CHECK(st.dropped_gone == 4, "H: the failed release, the two after it and the later CC counted as gone, got %llu", (unsigned long long)st.dropped_gone);
    CHECK(st.may_hang == 1 && st.released_after_stop == 0, "H: may_hang set, nothing released (%llu, %llu)", (unsigned long long)st.may_hang, (unsigned long long)st.released_after_stop); }
  // I: the hand-over ring wraps between two Stops (the first Stop's handled flag must not survive on a reused slot): 245..262 CCs, then a note and a second Stop
  for (int ncc = 245; ncc <= 262; ncc++) {
    Sim s(1, 1, 0); const int64_t T0 = s.nextBlk + 60 * ms;
    s.post(0, 0x90, 60, 100, T0); s.advance(T0 - 5 * ms - s.t); s.stop(); s.advance(60 * ms);
    for (int k = 0; k < ncc; k++) { s.post(0, 0xB0, 7, (uint8_t)(k & 127), s.nextBlk + 15 * ms); s.advance(kBlkNs); }
    const int64_t T1 = s.nextBlk + 40 * ms; s.post(0, 0x90, 61, 100, T1); s.advance(T1 - 5 * ms - s.t); s.stop(); s.finish();
    expectClean(s, "I wrapped ring"); CHECK(s.stats().released_after_stop == 2, "I (%d CCs): both notes released, got %llu", ncc, (unsigned long long)s.stats().released_after_stop);
  }
  // K: a release that is still to play is in the ring: a LATER CLEAR whose sink recalls it makes the core replay it
  { Sim s(5, 1, 0); const int64_t T0 = s.nextBlk + 60 * ms;
    s.post(0, 0x90, 60, 100, T0); s.advance(T0 - 5 * ms - s.t); s.os.rCut = INT64_MAX; s.stop();   // Stop 1: nothing recalled, the note-on plays, the release is stamped T0 + 1 ms
    s.advance(500000); s.post(0, 0x90, 61, 100, T0 + 8 * ms); s.advance(3 * ms);
    s.os.rCut = kH0 + T0 + ms / 2; s.stop(); s.finish();   // Stop 2: the sink recalls every stamp after T0 + 0.5 ms: the release of note 60 and the new note 61
    expectClean(s, "K second flush"); CHECK(countOf(s.os, 7, 0x80, 0, 60) >= 1 && s.stats().released_after_stop == 1, "K: the release of 60 survived a later flush (%llu)", (unsigned long long)s.stats().released_after_stop); }
  // J: a caller with the old 96-byte stats struct gets its 96 bytes and nothing is written past them
  { Sim s(1, 1, 0); playAndStop(s, {{0, 0x90, 60, 100, 0}}, 5 * ms);
    struct { uint8_t b[96]; uint64_t guard; } old; std::memset(&old, 0, sizeof old); old.guard = 0x1122334455667788ULL; uint32_t sz = 96; std::memcpy(old.b, &sz, 4);
    CHECK(thunder_midiout_stats_get(s.o, reinterpret_cast<thunder_midiout_stats*>(&old)) == THUNDER_MIDIOUT_OK, "stats_get with the 96-byte struct");
    thunder_midiout_stats full = s.stats(); uint64_t sentOld; std::memcpy(&sentOld, old.b + 8, 8);
    CHECK(old.guard == 0x1122334455667788ULL && sentOld == full.sent && full.sent > 0, "the old struct is filled up to its size only"); }
}
}  // namespace

int main() {
  const int64_t lats[] = {0, 5000000, 19000000, 20000000, 26000000};
  const double gears[] = {0, 30, -20};
  uint64_t releasedTotal[5] = {0, 0, 0, 0, 0}; int runs = 0, bad = 0;
  for (int mode = 0; mode <= 4; mode++) for (int64_t lat : lats) for (double gear : gears) for (int lead = -5; lead <= 25; lead++) {
    const Result R = run(mode, lat, gear, (int64_t)lead * 1000000LL);
    runs++; releasedTotal[mode] += R.released;
    if (!R.ok) { bad++; if (bad <= 10) std::printf("FAIL mode %d lat %lld ms gear %.0f lead %d ms: %d notes left on, %d replayed note-ons\n", mode, (long long)(lat / 1000000), gear, lead, R.stuck, R.dup); g_fail++; }
    if (mode == 2) CHECK(R.released == 0, "a recalling sink never gets a release (lat %lld gear %.0f lead %d: %llu)", (long long)(lat / 1000000), gear, lead, (unsigned long long)R.released);
  }
  // the CLEAR is drained late (the pump stalls after the Stop): a note-on that was handed over, and is already due when the CLEAR is drained, must be released too, with every kind of sink
  uint64_t lateReleased[5] = {0, 0, 0, 0, 0};
  for (int mode = 0; mode <= 4; mode++) for (int64_t lat : {(int64_t)0, (int64_t)5000000}) for (double gear : gears) for (int64_t dly : {500000LL, 2000000LL, 10000000LL}) for (int lead = -5; lead <= 25; lead++) {
    const Result R = run(mode, lat, gear, (int64_t)lead * 1000000LL, dly);
    runs++; lateReleased[mode] += R.released;
    if (!R.ok) { bad++; if (bad <= 10) std::printf("FAIL late drain %lld us mode %d lat %lld ms gear %.0f lead %d ms: %d notes left on, %d replayed note-ons\n", (long long)(dly / 1000), mode, (long long)(lat / 1000000), gear, lead, R.stuck, R.dup); g_fail++; }
  }
  std::printf("late drain: releases sent: no clear %llu, INT64_MAX %llu, recalling %llu\n", (unsigned long long)lateReleased[0], (unsigned long long)lateReleased[1], (unsigned long long)lateReleased[2]);
  CHECK(lateReleased[2] > 0, "a recalling sink releases a note-on that was already due when the CLEAR was drained (%llu)", (unsigned long long)lateReleased[2]);
  rawScenarios();
  std::printf("%d runs, %d with a stuck or replayed note; releases sent: no clear %llu, clear returns INT64_MAX %llu, recalling %llu\n", runs, bad, (unsigned long long)releasedTotal[0], (unsigned long long)releasedTotal[1], (unsigned long long)releasedTotal[2]);
  CHECK(releasedTotal[0] > 20 && releasedTotal[0] == releasedTotal[1], "the release path was exercised and is the same for a NULL clear and INT64_MAX (%llu, %llu)", (unsigned long long)releasedTotal[0], (unsigned long long)releasedTotal[1]);
  if (g_fail) { std::printf("%d FAILED\n", g_fail); return 1; }
  std::printf("midiout_norecall: ok\n");
  return 0;
}
