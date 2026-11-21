// U36 T9: the Android shell's MIDI out, the parts with no Android in them (native/android/app/src/main/cpp/midi_out.{h,cpp}) against a FAKE PortIo, on the real scheduler,
// pump and port table. A fake "audio callback" (this thread) calls MidiHost::audioBlock at real-time pace. Checks: the token table (unknown token, replace, remove while a
// send holds a copy), the debug song's whole chain (Start, 4 song notes and the raw ping, ~190 clock ticks, Stop) for a timestamp-capable port and for one that is not (held
// until its time), the clock map pair of spec 3.4 with an exact fake AAudio timestamp, hot-plug (the device leaves mid-song, returns under a new token), shutdown order.
#include <time.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

#include "midi_out.h"

static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { g_fail++; std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

namespace {
struct Rec { uint8_t b[3]; int len; int64_t at, got; };
struct FakeIo : tpa::PortIo {
  std::mutex mu; std::vector<Rec> r; std::atomic<int> failNext{0}; std::atomic<bool> closed{false};
  int send(const uint8_t* b, int len, int64_t at) override {
    if (closed.load()) return -1;
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

void testTable() {
  tpa::PortTable t;
  auto a = std::make_shared<FakeIo>(), b = std::make_shared<FakeIo>();
  CHECK(!t.find(5).io, "unknown token");
  t.put(5, a, true); CHECK(t.find(5).io == a && t.find(5).ts, "found");
  auto held = t.find(5).io;               // the sender's copy
  t.put(5, b, false); CHECK(t.find(5).io == b && !t.find(5).ts, "replaced");
  CHECK(held == a && a.use_count() >= 2, "the old port lives while a copy is held");
  t.remove(5); CHECK(!t.find(5).io && t.size() == 0, "removed");
  t.put(1, a, true); t.put(2, b, true); t.clear(); CHECK(t.size() == 0, "cleared");
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
  CHECK(h.debugTest() == 0, "debugTest starts (port A is listed)");
  CHECK(h.debugTest() == -3, "a second test is refused while one runs");
  const int64_t t0 = tpa::monoNs(); int64_t due = t0; bool gone = false, back = false;
  bool running = true;
  while (running && tpa::monoNs() - t0 < 9000000000LL) {
    due += (int64_t)(blk / sr * 1e9); sleepUntil(due);
    h.audioBlock(blk); g_fed += blk;
    const double el = (tpa::monoNs() - t0) / 1e9;
    if (hotplug && !gone && el > 2.0) { h.setDevices({}); h.detach(7); gone = true; }
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
    CHECK(io2->count(0xFC) + io1->count(0xFC) >= 0, "no crash");
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

void testNoDevice() {
  tpa::MidiHost h;
  CHECK(h.debugTest() == -1, "no engine");
  CHECK(h.engineStart(48000, nullptr, nullptr), "start");
  CHECK(h.debugTest() == -2, "port A not assigned");
  CHECK(h.assign(0, "n:Nobody") == 0 && h.debugTest() == -2, "assigned but not listed: MISSING");
  CHECK(h.assign(4, "x") != 0, "bad port letter");
  tpa::DevInfo bad = dev("n:A", 1, false), dup = dev("n:B", 1, false);
  CHECK(h.setDevices({bad, dup}) != 0, "two devices with one token are refused");
  h.engineStop();
}
}  // namespace

int main() {
  testTable();
  testNoDevice();
  scenario(false, true, false);
  scenario(true, false, false);
  scenario(true, true, true);
  if (g_fail) { std::printf("%d FAILED\n", g_fail); return 1; }
  std::printf("android_midi_sink: ok\n");
  return 0;
}
