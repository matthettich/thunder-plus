// U36 T9: the Android shell's MIDI out, the parts with no Android in them (native/android/app/src/main/cpp/midi_out.{h,cpp}) against a FAKE PortIo, on the real scheduler,
// pump and port table. A fake "audio callback" (this thread) calls MidiHost::audioBlock at real-time pace. Checks: the token table (unknown token, replace, remove while a
// send holds a copy), the debug song's whole chain (Start, 4 song notes and the raw ping, ~190 clock ticks, Stop) for a timestamp-capable port and for one that is not (held
// until its time), the clock map pair of spec 3.4 with an exact fake AAudio timestamp, hot-plug (the device leaves mid-song, returns under a new token), shutdown order.
#include <time.h>

#include <atomic>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "midi_out.h"
#include "midi/midi_host.h"
#include "thunder/midi.h"
#include "thunder/midiport.h"

static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { g_fail++; std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

namespace {
struct Rec { uint8_t b[3]; int len; int64_t at, got; };
struct FakeIo : tpa::PortIo {
  std::mutex mu; std::vector<Rec> r; std::atomic<int> failNext{0}; std::atomic<bool> closed{false}; std::atomic<int> delayUs{0};
  std::function<void()> onDtor;
  ~FakeIo() override { if (onDtor) onDtor(); }
  int send(const uint8_t* b, int len, int64_t at) override {
    if (closed.load()) return -1;
    if (failNext.load() > 0) { failNext--; return -1; }
    { const int d = delayUs.exchange(0); if (d > 0) { timespec t{0, (long)d * 1000}; nanosleep(&t, nullptr); } }
    Rec x; std::memset(&x, 0, sizeof x); std::memcpy(x.b, b, (size_t)len); x.len = len; x.at = at; x.got = tpa::monoNs();
    std::lock_guard<std::mutex> g(mu); r.push_back(x); return 0;
  }
  int count(uint8_t status, int d1 = -1) { std::lock_guard<std::mutex> g(mu); int n = 0; for (auto& x : r) if (x.b[0] == status && (d1 < 0 || x.b[1] == d1)) n++; return n; }
  size_t total() { std::lock_guard<std::mutex> g(mu); return r.size(); }
};

std::atomic<int64_t> g_fed{0};
bool exactTs(void*, tpa::TsPair* p) {   // an AAudio that is perfect: the stream position equals the frames written, presented right now
  p->pos = g_fed.load(); p->written = g_fed.load(); p->ns = tpa::monoNs(); return true;
}

void sleepUntil(int64_t ns) { timespec t; t.tv_sec = (time_t)(ns / 1000000000LL); t.tv_nsec = (long)(ns % 1000000000LL); clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &t, nullptr); }

tpa::DevInfo dev(const char* id, uint64_t tok, bool ts) { tpa::DevInfo d; d.id = id; d.name = id + 2; d.token = tok; d.ts = ts; return d; }

// A port is destroyed OUTSIDE the table's lock: its destructor calls back into the table (from another thread, so a held lock shows as a timeout, not a hang of the test).
bool tableUsableFromDtor(tpa::PortTable& t) {
  std::atomic<bool> done{false};
  std::thread th([&] { (void)t.size(); done = true; });
  for (int i = 0; i < 300 && !done.load(); i++) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  const bool ok = done.load();
  th.join();
  return ok;
}

void testTable() {
  tpa::PortTable t;
  auto a = std::make_shared<FakeIo>(), b = std::make_shared<FakeIo>();
  CHECK(!t.find(5).io, "unknown token");
  t.put(5, a, true); CHECK(t.find(5).io == a && t.find(5).ts, "found");
  CHECK(a.use_count() == 2, "a and the table: %ld", (long)a.use_count());
  auto held = t.find(5).io;               // the sender's copy
  CHECK(a.use_count() == 3, "a, the table and the copy: %ld", (long)a.use_count());
  t.put(5, b, false); CHECK(t.find(5).io == b && !t.find(5).ts, "replaced");
  CHECK(a.use_count() == 2, "the table let go of the old port, the copy keeps it alive: %ld", (long)a.use_count());
  t.remove(5); CHECK(!t.find(5).io && t.size() == 0, "removed");
  CHECK(b.use_count() == 1, "removed port released: %ld", (long)b.use_count());
  t.put(1, a, true); t.put(2, b, true); t.clear(); CHECK(t.size() == 0, "cleared");
  CHECK(a.use_count() == 2 && b.use_count() == 1, "clear released the ports (a still has the copy)");
  // the last reference dies outside the lock: put (replace), remove and clear
  int hit = 0; bool usable = true;
  auto mk = [&]() { auto p = std::make_shared<FakeIo>(); p->onDtor = [&] { hit++; usable = usable && tableUsableFromDtor(t); }; return p; };
  t.put(1, mk(), true); t.put(1, mk(), true);
  CHECK(hit == 1, "replace destroyed the old port: %d", hit);
  t.remove(1); CHECK(hit == 2, "remove destroyed the port: %d", hit);
  t.put(2, mk(), true); t.put(3, mk(), true); t.clear(); CHECK(hit == 4, "clear destroyed the ports: %d", hit);
  CHECK(usable, "a port was destroyed while the table's lock was held");
}

// One run of the debug song. exact: the fake AAudio timestamp; ts: the port declares timestamps; hotplug: the device leaves at 2 s and returns under a new token at 3 s.
void scenario(bool exact, bool ts, bool hotplug) {
  std::printf("scenario exact=%d ts=%d hotplug=%d\n", exact, ts, hotplug);
  const double sr = 48000; const int blk = 128;
  tpa::MidiHost h;
  auto io1 = std::make_shared<FakeIo>(), io2 = std::make_shared<FakeIo>();
  h.attach(7, io1, ts);
  CHECK(h.setDevices({dev("n:Fake", 7, ts)}) == 0, "set_devices before the engine (saved)");
  CHECK(h.assign(0, "n:Fake") == 0, "assign before the engine (saved)");
  g_fed = 0;
  CHECK(h.engineStart(sr, exact ? &exactTs : nullptr, nullptr), "engineStart");
  CHECK(h.debugTest() == -5, "before the first clock map the test is refused");
  { int64_t d0 = tpa::monoNs(); for (int i = 0; i < 4; i++) { d0 += (int64_t)(blk / sr * 1e9); sleepUntil(d0); h.audioBlock(blk); g_fed += blk; } }
  CHECK(h.debugTest() == 0, "debugTest starts (port A is listed)");
  CHECK(h.debugTest() == -3, "a second test is refused while one runs");
  const int64_t t0 = tpa::monoNs(); int64_t due = t0; bool gone = false, back = false, snap = false, sawMissing = false; size_t n1 = 0;
  bool running = true;
  while (running && tpa::monoNs() - t0 < 9000000000LL) {
    due += (int64_t)(blk / sr * 1e9); sleepUntil(due);
    h.audioBlock(blk); g_fed += blk;
    const double el = (tpa::monoNs() - t0) / 1e9;
    if (hotplug && !gone && el > 2.0) { h.setDevices({}); h.detach(7); gone = true; }
    if (hotplug && gone && !snap && el > 2.2) { snap = true; n1 = io1->total(); }   // 200 ms: the held messages of the old port are out
    if (hotplug && gone && !back && !sawMissing && el > 2.5) { sawMissing = h.status().find("A=(not connected)") != std::string::npos; }
    if (hotplug && gone && !back && el > 3.0) { h.attach(9, io2, ts); h.setDevices({dev("n:Fake", 9, ts)}); back = true; }
    running = h.debugPoll();
  }
  CHECK(!running, "the test song finished by itself");
  std::this_thread::sleep_for(std::chrono::milliseconds(150));   // the sender drains the tail
  std::printf("%s\n", h.status().c_str());
  const int nOn = io1->count(0x90) + io2->count(0x90), nTick = io1->count(0xF8) + io2->count(0xF8);
  if (!hotplug) {
    CHECK(io1->count(0xFA) == 1, "one Start, got %d", io1->count(0xFA));
    CHECK(io1->count(0xFC) == 1, "one Stop, got %d", io1->count(0xFC));
    CHECK(nOn == 8 + 1, "8 song notes (2 bars x 4) and the ping, got %d", nOn);
    CHECK(io1->count(0x90, 72) >= 1 && io1->count(0x80, 72) >= 1, "the raw ping went on and off");
    CHECK(nTick >= 185 && nTick <= 193, "about 192 clock ticks, got %d", nTick);
    CHECK(io1->count(0x80) >= 8, "the notes end");
  } else {
    CHECK(io1->total() > 0 && io2->total() > 0, "both the old and the new token got messages (%zu, %zu)", io1->total(), io2->total());
    CHECK(sawMissing, "while the device was away port A showed (not connected)");
    CHECK(snap && io1->total() == n1, "nothing reached the old port after it left (%zu then, %zu at the end)", n1, io1->total());
    CHECK(io2->count(0x90) > 0 || io2->count(0xF8) > 0, "the new token got the rest of the song");
  }
  // a stamped port is given its time; an unstamped one gets 0 (sent now, after being held until due)
  { std::lock_guard<std::mutex> g(io1->mu); int stamped = 0; for (auto& x : io1->r) if (x.at > 0) stamped++;
    if (ts) CHECK(stamped > 0, "a timestamp-capable port is sent stamped"); else CHECK(stamped == 0, "a port without timestamps is never given one"); }
  if (!ts && !hotplug) {   // held messages arrive at their time: late by at most a few ms (the pump's own 'late' counts are in the status line above)
    CHECK(h.deferred.load() > 0, "the unstamped port's messages were held until due");
  }
  h.engineStop();
  CHECK(!h.running(), "stopped");
  h.engineStop();   // twice is fine
}

// The engine is stopped while the song plays: the last block (Stop: notes off, clock Stop) is delivered before the sender ends.
void testStopMidSong() {
  const double sr = 48000; const int blk = 128;
  tpa::MidiHost h; auto io = std::make_shared<FakeIo>();
  h.attach(3, io, true); h.setDevices({dev("n:Fake", 3, true)}); h.assign(0, "n:Fake");
  g_fed = 0; CHECK(h.engineStart(sr, nullptr, nullptr), "start");
  int64_t due = tpa::monoNs();
  for (int i = 0; i < 4; i++) { due += (int64_t)(blk / sr * 1e9); sleepUntil(due); h.audioBlock(blk); }
  CHECK(h.debugTest() == 0, "test");
  const int64_t t0 = tpa::monoNs();
  while (tpa::monoNs() - t0 < 1500000000LL) { due += (int64_t)(blk / sr * 1e9); sleepUntil(due); h.audioBlock(blk); }   // 1.5 s in: the first arpeggio note is sounding or about to
  h.engineStop();
  CHECK(io->count(0xFC) == 1, "Stop reached the port before the sender ended, got %d", io->count(0xFC));
  CHECK(io->count(0x80) >= io->count(0x90), "no note left on: %d on, %d off", io->count(0x90), io->count(0x80));
}

void testNoDevice() {
  tpa::MidiHost h;
  CHECK(h.debugTest() == -1, "no engine");
  CHECK(h.engineStart(48000, nullptr, nullptr), "start");
  CHECK(h.debugTest() == -5, "no clock map yet");
  for (int i = 0; i < 4; i++) h.audioBlock(128);
  CHECK(h.debugTest() == -2, "port A not assigned");
  for (int i = 0; i < 4; i++) h.audioBlock(128);
  CHECK(h.assign(0, "n:Nobody") == 0 && h.debugTest() == -2, "assigned but not listed: MISSING");
  CHECK(h.assign(4, "x") != 0, "bad port letter");
  tpa::DevInfo bad = dev("n:A", 1, false), dup = dev("n:B", 1, false);
  CHECK(h.setDevices({bad, dup}) != 0, "two devices with one token are refused");
  h.engineStop();
}

// ---- the rig for the focused tests: one host, one fake port on letter A (token 7), a fake audio callback at real-time pace -------------------------------------
const double kSr = 48000; const int kBlk = 128; const int64_t kBlkNs = (int64_t)(kBlk / kSr * 1e9);
struct Rig {
  tpa::MidiHost h; std::shared_ptr<FakeIo> io = std::make_shared<FakeIo>(); int64_t due = 0;
  bool start(bool ts, tpa::TsFn fn, int64_t latNs = 0) {
    h.attach(7, io, ts);
    h.setDevices({dev("n:Fake", 7, ts)}); h.assign(0, "n:Fake");
    if (latNs) h.setOutLatencyNs(latNs);
    g_fed = 0;
    if (!h.engineStart(kSr, fn, nullptr)) return false;
    due = tpa::monoNs(); blocks(4);
    return true;
  }
  void block() { due += kBlkNs; sleepUntil(due); h.audioBlock(kBlk); g_fed += kBlk; }
  void blocks(int n) { for (int i = 0; i < n; i++) block(); }
  void runFor(double sec) { const int64_t end = tpa::monoNs() + (int64_t)(sec * 1e9); while (tpa::monoNs() < end) block(); }
  // posts one raw message (port letter `port`), then runs the block that picks it up; returns the clock at the start of that block (the message sounds at this + delay + latency)
  int64_t rawBlock(int port, uint8_t st, uint8_t d1, uint8_t d2, double delay) {
    due += kBlkNs; sleepUntil(due);
    const int64_t tb = tpa::monoNs();
    const uint8_t b[3] = {st, d1, d2}; const int len = (st >= 0xC0 && st < 0xE0) ? 2 : 3;
    const int r = thunder_midi_raw(h.coreMidi(), port, b, len, delay);
    CHECK(r == 0, "thunder_midi_raw returned %d", r);
    h.audioBlock(kBlk); g_fed += kBlk;
    return tb;
  }
};
// By stamp (a port without stamps: arrival), the last event for the note on this channel is a note-off (0x80, or 0x90 with velocity 0), and the note was played.
bool lastEventIsOff(FakeIo& io, int ch, int note) {
  std::lock_guard<std::mutex> g(io.mu); int64_t bestAt = -1; bool off = false, any = false; size_t bi = 0;
  for (size_t i = 0; i < io.r.size(); i++) { const Rec& x = io.r[i]; const int ty = x.b[0] & 0xF0;
    if ((ty != 0x80 && ty != 0x90) || (x.b[0] & 0x0F) != ch || x.b[1] != note) continue;
    if (!any || x.at > bestAt || (x.at == bestAt && i > bi)) { bestAt = x.at; bi = i; off = ty == 0x80 || x.b[2] == 0; any = true; } }
  return any && off;
}
thunder_midiout_stats outStats(tpa::MidiHost& h) { thunder_midiout_stats st; std::memset(&st, 0, sizeof st); st.size = sizeof st; thunder_midiout_stats_get(h.coreOut(), &st); return st; }
double ms(int64_t ns) { return (double)ns / 1e6; }
std::atomic<int> g_tsMode{0};
// A fake AAudio whose pair is NOT trivial: the stream position trails the frames fed by 480 frames, `written` is 256 ahead of them (so off = written - F = 256, in neither pos nor fed).
bool offTs(void*, tpa::TsPair* p) {
  const int64_t mono = tpa::monoNs();
  p->pos = g_fed.load() - 480; p->written = g_fed.load() + 256;
  p->ns = g_tsMode == 1 ? mono - 5000000000LL : g_tsMode == 2 ? mono + 1000000000LL : mono;   // 1: five seconds old, 2: a second ahead: both outside the sanity window
  return true;
}

// Spec 3.4: frame0 = pos - (written - F), host0 = ns. With pos = fed - 480 and written = fed + 256: frame (fed - 736) is audible at the callback time, so a message 0.2 s on is
// stamped at callback + 0.2 s + 736 frames. A wrong sign, a dropped `off` or a wrong term moves it by 5 ms or more.
void testMapPair() {
  g_tsMode = 0;
  Rig r; CHECK(r.start(true, &offTs), "start");
  const int64_t t0 = tpa::monoNs();
  r.runFor(0.3);
  const int64_t tb = r.rawBlock(0, 0x90, 70, 100, 0.2);
  r.runFor(0.5);
  const double dur = ms(tpa::monoNs() - t0) / 1000.0;
  int64_t at = 0; { std::lock_guard<std::mutex> g(r.io->mu); for (auto& x : r.io->r) if (x.b[0] == 0x90 && x.b[1] == 70) at = x.at; }
  const double want = ms(tb) + 200.0 + 736.0 / kSr * 1000.0;
  CHECK(at > 0 && std::fabs(ms(at) - want) < 3.0, "stamp %.2f ms, wanted %.2f ms (+- 3)", ms(at), want);
  CHECK(r.h.mapExact.load() > 10 && r.h.mapOdd.load() == 0 && r.h.mapFallback.load() == 0, "exact %llu odd %llu fallback %llu", (unsigned long long)r.h.mapExact.load(), (unsigned long long)r.h.mapOdd.load(), (unsigned long long)r.h.mapFallback.load());
  CHECK(r.h.lastOffsetFrames.load() == 256, "offset reported %lld", (long long)r.h.lastOffsetFrames.load());
  const double maps = (double)(r.h.mapExact.load() + r.h.mapFallback.load());
  CHECK(maps <= dur / 0.020 + 3 && maps >= dur / 0.020 * 0.5, "the map is published about every 20 ms: %.0f maps in %.2f s", maps, dur);
  r.h.engineStop();
}

// A timestamp outside the window (older than 2 s, or more than 200 ms ahead) is refused: counted in mapOdd and the fallback map is used.
void testMapWindow() {
  for (int mode = 1; mode <= 2; mode++) {
    g_tsMode = mode;
    Rig r; CHECK(r.start(true, &offTs), "start");
    r.runFor(0.3);
    CHECK(r.h.mapExact.load() == 0 && r.h.mapOdd.load() > 5 && r.h.mapFallback.load() > 5, "mode %d: exact %llu odd %llu fallback %llu", mode, (unsigned long long)r.h.mapExact.load(), (unsigned long long)r.h.mapOdd.load(), (unsigned long long)r.h.mapFallback.load());
    r.h.engineStop();
  }
  g_tsMode = 0;
}

// A port without timestamps: the message is HELD until its time, and the fallback map has the output latency in it (30 ms): it must not come before callback + 0.2 s + 30 ms.
void testHeldUntilDue() {
  Rig r; CHECK(r.start(false, nullptr, 30000000), "start");
  r.runFor(0.1);
  const int64_t tb = r.rawBlock(0, 0x90, 71, 100, 0.2);
  r.runFor(0.4);
  int64_t got = 0, at = -1; { std::lock_guard<std::mutex> g(r.io->mu); for (auto& x : r.io->r) if (x.b[0] == 0x90 && x.b[1] == 71) { got = x.got; at = x.at; } }
  const double want = ms(tb) + 230.0;
  CHECK(got > 0 && at == 0, "delivered once, unstamped (got %lld, at %lld)", (long long)got, (long long)at);
  CHECK(ms(got) >= want - 2.0, "not before its time: %.2f ms after the callback, wanted %.2f", ms(got) - ms(tb), want - ms(tb));
  CHECK(ms(got) <= want + 25.0, "not far late: %.2f ms after the callback, wanted %.2f", ms(got) - ms(tb), want - ms(tb));
  CHECK(r.h.deferred.load() >= 1, "it was held");
  r.h.engineStop();
}

// F7: a held message that is due goes before a later message that is sent at once, even when the sender was blocked (a slow device) in between.
void testOrderHeldVsImmediate() {
  const double sr = kSr; (void)sr;
  tpa::MidiHost h; auto io1 = std::make_shared<FakeIo>(), slow = std::make_shared<FakeIo>();
  h.attach(1, io1, false); h.attach(2, slow, true);
  h.setDevices({dev("n:One", 1, false), dev("n:Slow", 2, true)}); h.assign(0, "n:One"); h.assign(1, "n:Slow");
  g_fed = 0; CHECK(h.engineStart(kSr, nullptr, nullptr), "start");
  int64_t due = tpa::monoNs();
  auto blk = [&]() { due += kBlkNs; sleepUntil(due); h.audioBlock(kBlk); g_fed += kBlk; };
  for (int i = 0; i < 4; i++) blk();
  slow->delayUs = 90000;   // the slow device blocks the sender for 90 ms on its first message
  auto raw = [&](int port, uint8_t n, double delay) { const uint8_t b[3] = {0x90, n, 100}; CHECK(thunder_midi_raw(h.coreMidi(), port, b, 3, delay) == 0, "raw"); };
  raw(0, 60, 0.040);   // A: held from 20 ms before its time (port without stamps)
  raw(1, 61, 0.045);   // the slow port's message: stamped, handed over 20 ms ahead, the send blocks the sender from about +25 ms to +115 ms
  raw(0, 62, 0.060);   // C: due when the sender comes back: sent at once
  for (int i = 0; i < 100; i++) blk();   // 0.27 s
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  int ia = -1, ic = -1, k = 0; { std::lock_guard<std::mutex> g(io1->mu); for (auto& x : io1->r) { if (x.b[0] == 0x90 && x.b[1] == 60) ia = k; if (x.b[0] == 0x90 && x.b[1] == 62) ic = k; k++; } }
  CHECK(ia >= 0 && ic >= 0, "both delivered (%d, %d)", ia, ic);
  CHECK(ia >= 0 && ic >= 0 && ia < ic, "the earlier message went first (A at %d, C at %d)", ia, ic);
  CHECK(slow->count(0x90, 61) == 1, "the slow port got its message");
  h.engineStop();
}

// F2: stop mid-song on a port WITHOUT timestamps: the Stop and the note-offs that are still held (the output latency makes them due 50 ms on) are sent before the sender ends.
void testStopMidSongNoTs() {
  Rig r; CHECK(r.start(false, nullptr, 40000000), "start");
  CHECK(r.h.debugTest() == 0, "test");
  r.runFor(1.5);
  r.h.engineStop();
  CHECK(r.io->count(0xFA) == 1, "one Start, got %d", r.io->count(0xFA));
  CHECK(r.io->count(0xFC) == 1, "exactly one Stop reached the port, got %d", r.io->count(0xFC));
  CHECK(r.io->count(0x80) >= r.io->count(0x90), "no note left on: %d on, %d off", r.io->count(0x90), r.io->count(0x80));
  CHECK(r.h.deferred.load() > 0, "the messages were held");
}

// F3: a CLEAR (the Stop) drops the held messages that come after its marker and keeps the ones before it (the core replays those: delivered once, not early); a stamped port is flushed.
void testClearDropsHeld() {
  Rig r; CHECK(r.start(false, nullptr), "start");
  r.runFor(0.05);
  const int kStopBlocks = 37; const double tStop = kStopBlocks * kBlk / kSr;   // 98.7 ms after the block that picks the raw messages up
  const int64_t tb = r.rawBlock(0, 0x92, 62, 100, tStop + 0.007 - kBlk / kSr);   // R2: before the marker (stop at tStop, marker 10 ms later)
  { const uint8_t b[3] = {0x91, 61, 100}; CHECK(thunder_midi_raw(r.h.coreMidi(), 0, b, 3, tStop + 0.016 - kBlk / kSr) == 0, "raw R1"); }   // R1: after the marker
  r.blocks(kStopBlocks - 1);
  { const double now = (double)g_fed.load() / kSr + 0.01;   // what engineStop does for the last block
    thunder::midi_begin(r.h.coreMidi(), now, 0.01, 0.0, ~(uint64_t)0); thunder::midi_sched(r.h.coreMidi()).stop(now); thunder::midi_end(r.h.coreMidi(), now, now, 120, false); }
  r.runFor(0.3);
  int64_t got2 = 0; { std::lock_guard<std::mutex> g(r.io->mu); for (auto& x : r.io->r) if (x.b[0] == 0x92) got2 = x.got; }
  CHECK(r.io->count(0x91, 61) == 0, "the note after the marker was recalled, got %d", r.io->count(0x91, 61));
  CHECK(r.io->count(0x92, 62) == 1, "the note before the marker arrived exactly once, got %d", r.io->count(0x92, 62));
  CHECK(got2 == 0 || ms(got2) >= ms(tb) + tStop * 1000.0 - 100.0 + 7.0 - 3.0, "and not before its time");
  CHECK(r.h.heldCleared.load() >= 1, "a held message was dropped by the CLEAR (%llu)", (unsigned long long)r.h.heldCleared.load());
  r.h.engineStop();
  // a STAMPED port: a CLEAR recalls nothing (no flush) and the core's replay must not send anything twice: every note-on and note-off arrives exactly once
  Rig s; CHECK(s.start(true, nullptr), "start ts");
  s.runFor(0.05);
  const double sStop = kStopBlocks * kBlk / kSr;
  s.rawBlock(0, 0x91, 61, 100, sStop + 0.008 - kBlk / kSr);   // on and off both before the marker (stop + 10 ms): handed over stamped, still held by the OS at the CLEAR, replayed by the core
  { const uint8_t b[3] = {0x81, 61, 0}; CHECK(thunder_midi_raw(s.h.coreMidi(), 0, b, 3, sStop + 0.009 - kBlk / kSr) == 0, "raw off"); }
  { const uint8_t b[3] = {0x91, 62, 100}; CHECK(thunder_midi_raw(s.h.coreMidi(), 0, b, 3, sStop + 0.016 - kBlk / kSr) == 0, "raw after"); }   // after the marker, handed over stamped: this is what makes the core call clear + replay
  s.blocks(kStopBlocks - 1);
  { const double now = (double)g_fed.load() / kSr + 0.01;
    thunder::midi_begin(s.h.coreMidi(), now, 0.01, 0.0, ~(uint64_t)0); thunder::midi_sched(s.h.coreMidi()).stop(now); thunder::midi_end(s.h.coreMidi(), now, now, 120, false); }
  s.runFor(0.3);
  CHECK(s.io->count(0x91, 61) == 1 && s.io->count(0x81, 61) == 1, "stamped port: note 61 on %d, off %d (each once)", s.io->count(0x91, 61), s.io->count(0x81, 61));
  // nothing is recalled from a stamped port, so what it holds plays: the note after the marker (62) plays once, and the core releases it (a note-off stamped at least 1 ms after it)
  CHECK(s.io->count(0x91, 62) == 1 && s.io->count(0x81, 62) == 1, "note 62 after the marker: on %d, off %d", s.io->count(0x91, 62), s.io->count(0x81, 62));
  CHECK(lastEventIsOff(*s.io, 0x01, 61) && lastEventIsOff(*s.io, 0x01, 62), "by time, the last event of every note is a note-off");
  CHECK(outStats(s.h).released_after_stop == 1, "one release counted, got %llu", (unsigned long long)outStats(s.h).released_after_stop);
  s.h.engineStop();
}

// The device is gone (a send returns < 0): counted by the core as an error, and the port's messages are dropped, not retried.
void testDeviceGone() {
  Rig r; CHECK(r.start(true, nullptr), "start");
  r.io->failNext = 1;
  r.rawBlock(0, 0x90, 64, 100, 0.0);
  r.runFor(0.15);
  const thunder_midiout_stats st = outStats(r.h);
  CHECK(st.send_errors == 1, "one send error counted, got %llu", (unsigned long long)st.send_errors);
  CHECK(st.dropped_gone >= 1, "and the message counted as gone, got %llu", (unsigned long long)st.dropped_gone);
  CHECK(r.io->count(0x90, 64) == 0, "the failed message was not delivered");
  r.h.engineStop();
  Rig c; CHECK(c.start(false, nullptr), "start closed");
  c.io->closed = true;
  c.rawBlock(0, 0x90, 65, 100, 0.0);
  c.runFor(0.15);
  CHECK(outStats(c.h).send_errors >= 1, "a closed port (no timestamps) is an error too");
  c.h.engineStop();
}

// A burst of 356 messages for a port without timestamps (more than the 256 the held array holds): all of them arrive.
void testHeldCap() {
  Rig r; CHECK(r.start(false, nullptr), "start");
  r.runFor(0.05);
  int posted = 0;
  r.due += kBlkNs; sleepUntil(r.due);
  // the command ring takes 256 at a time: 256 now, and 100 more one block later, due before the first lot has been sent (all of them are handed over within 20 ms of their time)
  for (int i = 0; i < 256; i++) { const uint8_t b[3] = {0x90, (uint8_t)(i % 100), 100}; if (thunder_midi_raw(r.h.coreMidi(), 0, b, 3, 0.05) == 0) posted++; else break; }
  r.h.audioBlock(kBlk); g_fed += kBlk;
  r.due += kBlkNs; sleepUntil(r.due);
  for (int i = 0; i < 100; i++) { const uint8_t b[3] = {0x91, (uint8_t)(i % 100), 100}; if (thunder_midi_raw(r.h.coreMidi(), 0, b, 3, 0.045) == 0) posted++; else break; }
  r.h.audioBlock(kBlk); g_fed += kBlk;
  r.runFor(0.3);
  const thunder_midiout_stats st = outStats(r.h);
  std::printf("held cap: posted %d, sent %llu, queue_full %llu, deferred %llu, deferFull %llu\n", posted, (unsigned long long)st.sent, (unsigned long long)st.queue_full, (unsigned long long)r.h.deferred.load(), (unsigned long long)r.h.deferFull.load());
  CHECK(posted == 356, "the burst was posted (%d)", posted);
  CHECK(r.h.deferFull.load() == 0, "held messages never exceeded the array (%llu)", (unsigned long long)r.h.deferFull.load());   // the core hands a message over early only while its 256-slot history has room, so at most 256 are ever held: the cap is a guard that cannot be reached through the core
  CHECK(st.sent == 356 && r.io->total() == 356 && st.queue_full == 0, "everything was delivered: %zu at the port, %llu sent, %llu dropped for room", r.io->total(), (unsigned long long)st.sent, (unsigned long long)st.queue_full);
  r.h.engineStop();
}

void testJniUtf8() {
  CHECK(tpa::toJniUtf8("n:Synth #2") == "n:Synth #2", "ascii unchanged");
  CHECK(tpa::toJniUtf8("n:caf\xC3\xA9 \xE2\x82\xAC") == "n:caf\xC3\xA9 \xE2\x82\xAC", "2 and 3 byte sequences unchanged");
  CHECK(tpa::toJniUtf8("a\xF0\x9F\x8E\xB9z") == "a\xED\xA0\xBC\xED\xBE\xB9z", "U+1F3B9 becomes a surrogate pair of two 3-byte sequences");
  CHECK(tpa::toJniUtf8(std::string("a\0b", 3)) == "a\xC0\x80" "b", "NUL becomes C0 80");
  CHECK(tpa::toJniUtf8("a\xFF" "b\xC3") == "a?b?", "invalid bytes and a cut sequence become ?");
  CHECK(tpa::toJniUtf8("\xED\xA0\x80") == "???", "an encoded surrogate in UTF-8 is invalid");
}
}  // namespace

int main() {
  testTable();
  testNoDevice();
  testStopMidSong();
  testJniUtf8();
  testMapPair();
  testMapWindow();
  testHeldUntilDue();
  testOrderHeldVsImmediate();
  testStopMidSongNoTs();
  testClearDropsHeld();
  testDeviceGone();
  testHeldCap();
  scenario(false, true, false);
  scenario(true, false, false);
  scenario(true, true, true);
  if (g_fail) { std::printf("%d FAILED\n", g_fail); return 1; }
  std::printf("android_midi_sink: ok\n");
  return 0;
}
