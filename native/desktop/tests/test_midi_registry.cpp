// H7: the MIDI id / token / diff logic (midi_registry.h), mirroring the Android MidiModelTest, and the lock-free hand-over ring (midi_rx.h) under a producer thread.
#include <atomic>
#include <thread>
#include "check.h"
#include "midi_registry.h"
#include "midi_rx.h"
#include "rt/json.h"

using namespace tpd;
int main() {
  auto rows = midiBuildIds({{10, "Synth"}, {11, "Synth"}, {12, "Synth"}, {5, "Cirklon"}});
  CHECK(rows.size() == 4 && rows[0].key == 5 && rows[0].id == "n:Cirklon", "sorted by key, ids n:Name");
  CHECK(rows[1].id == "n:Synth" && rows[2].id == "n:Synth #2" && rows[3].id == "n:Synth #3", "same names get #2, #3: %s %s", rows[2].id.c_str(), rows[3].id.c_str());
  rows = midiBuildIds({{1, ""}, {2, "  \t"}, {3, "A\nB"}});
  CHECK(rows[0].id == "n:MIDI device" && rows[1].id == "n:MIDI device #2" && rows[2].id == "n:A B", "empty names, control characters: %s | %s | %s", rows[0].id.c_str(), rows[1].id.c_str(), rows[2].id.c_str());
  { const std::string longName(200, 'x'); rows = midiBuildIds({{1, longName}, {2, longName}});
    CHECK(rows[0].id.size() <= (size_t)kMidiMaxIdBytes && rows[1].id.size() <= (size_t)kMidiMaxIdBytes && rows[0].id != rows[1].id && rows[1].id.find(" #2") != std::string::npos, "long names fit 95 bytes, the suffix kept: %zu %zu", rows[0].id.size(), rows[1].id.size()); }
  { std::string u; for (int i = 0; i < 60; i++) u += "\xC3\xA9"; rows = midiBuildIds({{1, u}});   // 2-byte characters: never cut inside one
    const std::string body = rows[0].id.substr(2); CHECK(rows[0].id.size() <= (size_t)kMidiMaxIdBytes && body.size() % 2 == 0, "UTF-8 cut on a character: %zu", rows[0].id.size()); }
  CHECK(midiClampUtf8("abc", 2) == "ab" && midiClampUtf8("a\xC3\xA9", 2) == "a" && midiClampUtf8("abc", 0).empty(), "clamp");

  MidiRegistry reg;
  auto d = reg.update({{1, "A"}, {2, "B"}});
  CHECK(d.added.size() == 2 && d.removed.empty() && reg.entries()[0].token == 1 && reg.entries()[1].token == 2, "first scan: tokens 1, 2");
  d = reg.update({{1, "A"}, {2, "B"}});
  CHECK(d.empty() && d.kept.size() == 2, "same scan: nothing changed");
  d = reg.update({{2, "B"}});
  CHECK(d.removed.size() == 1 && d.removed[0].token == 1 && d.kept.size() == 1 && d.kept[0].token == 2, "A leaves: B keeps its token");
  d = reg.update({{1, "A"}, {2, "B"}});
  CHECK(d.added.size() == 1 && d.added[0].token == 3, "A comes back: a NEW token (never reused): %llu", (unsigned long long)d.added[0].token);
  d = reg.update({{1, "A2"}, {2, "B"}});
  CHECK(d.changed.size() == 1 && d.changed[0].token == 3 && d.changed[0].name == "A2" && d.kept.size() == 1, "a renamed port keeps its token and is 'changed'");
  d = reg.update({{7, "Synth"}, {8, "Synth"}});   // two same-name ports
  CHECK(reg.entries().size() == 2 && reg.entries()[1].id == "n:Synth #2", "same-name ports");
  d = reg.update({{8, "Synth"}});   // the first leaves: the second's id shifts to n:Synth
  CHECK(d.removed.size() == 1 && d.changed.size() == 1 && d.changed[0].id == "n:Synth", "an id shifts when one of two same-name ports leaves");

  MidiPortState ps[4]; ps[0].state = 1; ps[0].id = "n:A"; ps[0].name = "A"; ps[0].gear = 12; ps[0].clock = 1; ps[1].state = 2; ps[1].id = "n:Gone\"x";
  const std::string ev = midiPortsEventJson({{3, 1, "n:A", "A"}}, {{4, 2, "n:K", "K"}}, ps, 5, 9);
  thunder::JsonDoc j; CHECK(j.parse(ev), "the event is JSON: %s", ev.c_str());
  auto r = j.root();
  CHECK(r.str("t") == "midi.ports" && r.arr("devs").size() == 1 && r.arr("devs").at(0).str("id") == "n:A" && r.arr("ins").at(0).num("port") == 4 && r.arr("ports").size() == 4 && r.arr("ports").at(0).num("gear") == 12 && r.arr("ports").at(1).str("id") == "n:Gone\"x" && r.num("late") == 5 && r.num("sent") == 9, "event fields");

  // the ring: records in order, all-or-nothing, wrap-around, a drop counted; then a producer thread against a consumer
  MidiRxRing ring; ring.init(256);
  uint8_t b[8] = {0x90, 60, 100}; int64_t ts = 0; uint8_t out[300];
  CHECK(ring.push(111, b, 3) && ring.pop(&ts, out, sizeof out) == 3 && ts == 111 && out[0] == 0x90 && out[2] == 100 && ring.pop(&ts, out, sizeof out) == -1, "one record in, one out, then empty");
  int pushed = 0; while (ring.push(pushed, b, 3)) pushed++;
  CHECK(pushed == 17 && ring.dropped() == 1, "a full ring refuses whole records and counts them: %d %llu", pushed, (unsigned long long)ring.dropped());
  int popped = 0; bool ordered = true; while (ring.pop(&ts, out, sizeof out) >= 0) { if (ts != popped) ordered = false; popped++; }
  CHECK(popped == pushed && ordered, "everything pushed comes out in order across the wrap");
  { uint8_t big[600]; for (int i = 0; i < 600; i++) big[i] = (uint8_t)i; MidiRxRing r2; r2.init(4096); r2.push(1, big, 600); int n = r2.pop(&ts, out, sizeof out);
    CHECK(n == (int)MidiRxRing::kMaxRecord && out[255] == 255, "a long packet is cut at kMaxRecord (SysEx is dropped by the parser anyway)"); }
  MidiRxRing r3; r3.init(1 << 12);
  std::atomic<bool> stop{false}; std::atomic<int> sent{0};
  std::thread prod([&] { uint8_t m[3] = {0x90, 1, 2}; int64_t i = 0; while (!stop) { m[1] = (uint8_t)(i & 0x7f); if (r3.push(i, m, 3)) { i++; sent = (int)i; } else std::this_thread::yield(); if (i >= 200000) break; } });
  int64_t expect = 0; bool ok = true; uint64_t guard = 0;
  while (expect < 200000 && guard < 400000000ull) { int n = r3.pop(&ts, out, sizeof out); guard++; if (n < 0) continue; if (ts != expect || n != 3 || out[1] != (uint8_t)(expect & 0x7f)) { ok = false; break; } expect++; }
  stop = true; prod.join();
  CHECK(ok && expect == 200000, "200000 packets across two threads, in order, intact (%lld)", (long long)expect);
  DONE("midi_registry");
}
