// Independent verifier of #268 (U36 PR A). White-box: this file #includes midiport.cpp so it can reach the object's private state; the linker takes the thunder_midiout_* symbols
// from here, not from libthunder_core. Each case REPRODUCES a finding of docs/port/reviews/verify-268.md and prints "Fn: BUG PRESENT" or "Fn: fixed". Default: exit 0 whatever it
// finds (so ctest stays green on the branch under review); `--strict`: exit 1 if any bug is still present (use it once the findings are fixed).
#include "../core/src/midi/midiport.cpp"

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

static int present = 0, setup = 0;
#define BUG(id, cond, ...) do { if (cond) { present++; std::printf("%s: BUG PRESENT: ", id); std::printf(__VA_ARGS__); std::printf("\n"); } else { std::printf("%s: fixed / not reproduced\n", id); } } while (0)
#define SETUP(c, ...) do { if (!(c)) { setup++; std::printf("SETUP FAILED: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

struct Rec { uint64_t dev; uint8_t b[3]; int len; int64_t at; bool recalled; };
struct Fake {
  int64_t now = 0, step = 0; uint32_t caps = 0; std::vector<Rec> rec;
  static int64_t nowFn(void* u) { Fake* f = (Fake*)u; const int64_t v = f->now; f->now += f->step; return v; }
  static int sendFn(void* u, uint64_t dev, const uint8_t* b, int len, int64_t at) { Fake* f = (Fake*)u; Rec r{}; r.dev = dev; r.len = len; std::memcpy(r.b, b, (size_t)len); r.at = at; f->rec.push_back(r); return 0; }
  static void clearFn(void* u, uint64_t dev) { Fake* f = (Fake*)u; for (Rec& r : f->rec) if (r.dev == dev && r.at > f->now) r.recalled = true; }   // MIDIFlushOutput: the OS drops what it holds for the device
  thunder_midiout_sink sink() { thunder_midiout_sink s; s.user = this; s.now_ns = nowFn; s.send = sendFn; s.clear = clearFn; s.caps = caps; return s; }
};
struct Rig {
  Fake fk; thunder_midi* h; thunder_midiout* o; double now = 0; const double sr = 48000; const int64_t host0 = 1000000000;
  explicit Rig(uint32_t caps = 0) { fk.caps = caps; h = thunder_midi_create(sr, nullptr, 0); const auto s = fk.sink(); o = thunder_midiout_create(h, sr, &s); fk.now = host0; }
  ~Rig() { thunder_midiout_free(o); thunder_midi_free(h); }
  void proc() { thunder_midi_process(h, now, 128.0 / sr, now, 120, 0); now += 128.0 / sr; }
  void raw(int port, std::initializer_list<uint8_t> b, double extra) { std::vector<uint8_t> v(b); thunder_midi_raw(h, port, v.data(), (int)v.size(), extra); }
  void devs(const std::vector<std::pair<std::string, uint64_t>>& l) { std::vector<thunder_midiout_dev> v; for (auto& p : l) { thunder_midiout_dev d; std::memset(&d, 0, sizeof d); std::snprintf(d.id, sizeof d.id, "%s", p.first.c_str()); std::snprintf(d.name, sizeof d.name, "%s", p.first.c_str()); d.token = p.second; v.push_back(d); } thunder_midiout_set_devices(o, v.data(), (int)v.size()); }
};

// F1: more than 256 stamped messages outstanding (CAP_TS) and a CLEAR: the OS flush drops them all, the ring can give back 256: the others are lost (here: note-offs that were due BEFORE the marker)
static void f1() {
  Rig r(THUNDER_MIDIOUT_CAP_TS); r.devs({{"X", 5}}); thunder_midiout_assign(r.o, 0, "X"); r.proc(); thunder_midiout_clock_map(r.o, 0, r.host0);
  for (int i = 0; i < 150; i++) r.raw(0, {0x80, (uint8_t)(i % 128), 0}, 0.015 + i * 1e-5);
  r.proc();
  for (int i = 0; i < 150; i++) r.raw(0, {0x80, (uint8_t)(i % 128), 0}, 0.0125 + i * 1e-5);
  r.proc();
  r.fk.now = r.host0 + 4000000; r.fk.step = 0; const size_t handed = (thunder_midiout_pump(r.o, nullptr), r.fk.rec.size());   // all 300 are within 20 ms: stamped and handed to the OS
  SETUP(handed == 300, "F1: %zu of 300 handed over", handed);
  for (int i = 0; i < 8; i++) r.proc();                         // the audio side runs on: the STOP's marker frame (~29 ms) is after the 300 messages (~17..20 ms) while the host clock (4 ms) is not
  thunder_midi_stop(r.h); r.proc(); thunder_midiout_pump(r.o, nullptr);
  size_t alive = 0; for (const Rec& x : r.fk.rec) if (!x.recalled && x.b[0] == 0x80) alive++;
  BUG("F1", alive < 300, "300 note-offs were due before the CLEAR's frame and must still sound; %zu survive the flush (hand ring = %d)", alive, kHand);
}

// F2: the clock map's writer is between its odd and its even sequence store (preempted): readMap gives up after 16 tries and returns {valid=false}: the pump drops every drained record as `dropped_nomap`
static void f2() {
  Rig r(0); r.devs({{"X", 1}}); thunder_midiout_assign(r.o, 0, "X"); r.proc(); thunder_midiout_clock_map(r.o, 0, r.host0);   // a good map exists
  r.raw(0, {0x80, 60, 0}, 0.010); r.proc();
  r.o->mseq.store(r.o->mseq.load() + 1);                        // a writer that is part-way
  r.fk.now = r.host0 + 100000000; thunder_midiout_pump(r.o, nullptr);
  const uint64_t nm = r.o->st[S_NOMAP].load(); size_t sent = r.fk.rec.size();
  BUG("F2", nm == 1 && sent == 0, "a map was established earlier, yet the note-off was dropped as 'nomap' (dropped_nomap %llu, sent %zu, may_hang %llu)", (unsigned long long)nm, sent, (unsigned long long)r.o->st[S_HANG].load());
}

// F3: after a stall the core's ring (8192) lost note-offs (its own stats.may_hang = 1), but thunder_midiout_stats.may_hang stays 0 (the pump's queue coped): a shell that follows midiport.h reads only the latter
static void f3() {
  Rig r(0); r.devs({{"X", 1}}); thunder_midiout_assign(r.o, 0, "X"); r.proc(); thunder_midiout_clock_map(r.o, 0, r.host0);
  for (int blk = 0; blk < 100; blk++) { for (int i = 0; i < 100; i++) r.raw(0, {0xF8}, 0.001); r.proc(); }                       // 10000 clock ticks, no pump: the core's ring keeps 7680 (ticks stop below 512 free)
  for (int blk = 0; blk < 10; blk++) { for (int i = 0; i < 100; i++) r.raw(0, {0x80, (uint8_t)(i % 100), 0}, 0.001); r.proc(); }   // then 1000 note-offs: 512 fit, the rest are lost
  thunder_midi_stats cs; std::memset(&cs, 0, sizeof cs); cs.size = sizeof cs; thunder_midi_stats_get(r.h, &cs);
  r.fk.now = r.host0 + 10000000000LL; thunder_midiout_pump(r.o, nullptr); thunder_midiout_stats ms; std::memset(&ms, 0, sizeof ms); ms.size = sizeof ms; thunder_midiout_stats_get(r.o, &ms);
  SETUP(cs.may_hang == 1, "F3: the core's ring did not lose a note-off (may_hang %llu, ring_full %llu, pend_full %llu, cmd_full %llu, sent %llu)", (unsigned long long)cs.may_hang, (unsigned long long)cs.ring_full, (unsigned long long)cs.pend_full, (unsigned long long)cs.cmd_full, (unsigned long long)cs.sent);
  BUG("F3", cs.may_hang == 1 && ms.may_hang == 0, "the core ring dropped a note-off / CLEAR (core may_hang 1, ring_full %llu) and midiout may_hang is %llu", (unsigned long long)cs.ring_full, (unsigned long long)ms.may_hang);
}

// F4: >200 distinct device ids over a session: a slot is reused, and a record still queued for the OLD device is sent to the NEW device's token
static void f4() {
  Rig r(0); r.devs({{"old", 100}}); thunder_midiout_assign(r.o, 0, "old"); r.proc(); thunder_midiout_clock_map(r.o, 0, r.host0);
  r.raw(0, {0x80, 60, 0}, 0.050); r.proc();                      // a note-off for "old", queued in the core's ring
  r.devs({});                                                    // "old" is unplugged
  for (int i = 0; i < 200; i++) r.devs({{"d" + std::to_string(i), (uint64_t)(200 + i)}});   // 200 other ids: the slot of "old" (the longest unlisted) is reused for the 200th
  r.fk.now = r.host0 + 100000000; thunder_midiout_pump(r.o, nullptr);
  bool toNew = false, toOld = false; for (const Rec& x : r.fk.rec) { if (x.b[0] == 0x80 && x.dev >= 200) toNew = true; if (x.dev == 100) toOld = true; }
  BUG("F4", toNew && !toOld, "the note-off queued for the unplugged device 'old' went to a different device (token %llu)", (unsigned long long)(r.fk.rec.empty() ? 0 : r.fk.rec[0].dev));
}

// F5: the pump's `now` is read once: a slow send makes the lateness test of later messages wrong (a tick 26 ms late is sent, not dropped)
static void f5() {
  struct S { int64_t now; int sends = 0; int64_t firstAt = 0; std::vector<uint8_t> got; };
  static S s; s = S(); s.now = 1000000000;
  thunder_midiout_sink sk; sk.user = &s; sk.caps = THUNDER_MIDIOUT_CAP_TS;
  sk.now_ns = [](void*) -> int64_t { return s.now; };
  sk.send = [](void*, uint64_t, const uint8_t* b, int, int64_t) -> int { s.got.push_back(b[0]); s.now += 25000000; return 0; };   // the first send takes 25 ms (a busy USB stack)
  sk.clear = nullptr;
  thunder_midi* h = thunder_midi_create(48000, nullptr, 0); thunder_midiout* o = thunder_midiout_create(h, 48000, &sk);
  thunder_midiout_dev d; std::memset(&d, 0, sizeof d); std::strcpy(d.id, "X"); std::strcpy(d.name, "X"); d.token = 1; thunder_midiout_set_devices(o, &d, 1); thunder_midiout_assign(o, 0, "X");
  thunder_midi_process(h, 0, 128.0 / 48000, 0, 120, 0); thunder_midiout_clock_map(o, 0, 1000000000);
  const uint8_t cc[3] = {0xB0, 7, 1}, tk = 0xF8; thunder_midi_raw(h, 0, cc, 3, 0.010); thunder_midi_raw(h, 0, &tk, 1, 0.0105); thunder_midi_process(h, 128.0 / 48000, 128.0 / 48000, 0, 120, 0);
  s.now = 1000000000 + 14200000;                                 // the CC is 1.5 ms late, the tick 1 ms late
  thunder_midiout_pump(o, nullptr);
  bool tickSent = false; for (uint8_t b : s.got) tickSent = tickSent || b == 0xF8;
  BUG("F5", tickSent, "after a 25 ms send the next tick (now 26 ms late) was still sent: `now` is not refreshed inside the loop (late counted %llu)", (unsigned long long)o->st[S_LATE].load());
  thunder_midiout_free(o); thunder_midi_free(h);
}

// F6: precision and edge cases of the map (30 days of uptime, host time backwards, dt = 0, 44.1 / 96 kHz): all expected to hold
static void f6() {
  for (double sr : {44100.0, 48000.0, 96000.0}) {
    thunder_midi* h = thunder_midi_create(sr, nullptr, 0); Fake fk; const auto s = fk.sink(); thunder_midiout* o = thunder_midiout_create(h, sr, &s);
    const int64_t days30 = 30LL * 86400 * 1000000000LL, f0 = (int64_t)(30.0 * 86400 * sr);
    thunder_midiout_clock_map(o, f0, days30 + 5000000000LL);
    const int64_t F = f0 + 4410; const int64_t at = atNs(bitsToD(o->mOffBits.load()), sr, F); const double exact = (double)(days30 + 5000000000LL) + 4410.0 * 1e9 / sr;
    const bool ok1 = std::fabs((double)at - exact) <= 1.0;
    thunder_midiout_clock_map(o, f0 + 100, days30 + 5000000000LL);            // dt = 0: no move, no NaN
    const double a = bitsToD(o->mOffBits.load()); thunder_midiout_clock_map(o, f0 + 200, days30 + 5000000000LL - 1000);   // host time backwards 1 us: no NaN / no jump
    const double b = bitsToD(o->mOffBits.load());
    const bool ok2 = std::isfinite(a) && std::isfinite(b) && std::fabs(b - a) < 1e6;
    std::printf("F6 sr %.0f: 30-day precision %s (|err| <= 1 ns), dt = 0 / host backwards %s\n", sr, ok1 ? "ok" : "BAD", ok2 ? "ok" : "BAD");
    if (!ok1 || !ok2) present++;
    thunder_midiout_free(o); thunder_midi_free(h);
  }
}

// F7: a stuck clock in spin mode: the pump is bounded (2^20 clock reads per message) and sends
static void f7() {
  Rig r(0); r.devs({{"X", 1}}); thunder_midiout_assign(r.o, 0, "X"); r.proc(); thunder_midiout_clock_map(r.o, 0, r.host0);
  r.raw(0, {0xB0, 7, 1}, 0.010); r.proc(); r.fk.step = 0;
  r.fk.now = r.host0 + 12666667 - 300000;                         // 0.3 ms ahead: spin mode waits for a clock that does not move
  const auto t0 = std::chrono::steady_clock::now(); const int sent = thunder_midiout_pump(r.o, nullptr); const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  std::printf("F7 stuck clock: pump returned after %.2f ms, sent %d (bounded by 2^20 reads per message)\n", ms, sent);
  if (sent != 1 || ms > 2000) present++;
}

int main(int argc, char** argv) {
  const bool strict = argc > 1 && std::strcmp(argv[1], "--strict") == 0;
  f1(); f2(); f3(); f4(); f5(); f6(); f7();
  std::printf("verify268: %d finding(s) reproduced, %d setup problem(s)\n", present, setup);
  if (setup) return 2;
  return strict && present ? 1 : 0;
}
