// H7: the MIDI manager (midi_manager.h) with a fake backend and a REAL live host: the port list and `midi.ports`, hot-plug, a song with a MIDI instrument through the core's scheduler, pump and sink to a fake
// port (what arrived, in order, how far from its time), MIDI in -> `midi.in` events, the engine teardown order, the ring's drop counter. The audio callback is paced in real time (256 frames per 5.33 ms).
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>
#include "check.h"
#include "host_clock.h"
#include "live_host.h"
#include "midi_manager.h"
#include "pad_filler.h"
#include "rt/json.h"
#include "test_audio.h"

using namespace tpd;
static std::string slurp(const std::string& p) { std::ifstream f(p, std::ios::binary); std::stringstream s; s << f.rdbuf(); return s.str(); }
static void ms(int n) { std::this_thread::sleep_for(std::chrono::milliseconds(n)); }

struct Got { std::vector<uint8_t> b; int64_t at, sentNs; };
struct FakePort : tpa::PortIo {
  std::mutex m; std::vector<Got> got; std::atomic<int> rc{0};
  int send(const uint8_t* b, int len, int64_t at) override { std::lock_guard<std::mutex> g(m); got.push_back({std::vector<uint8_t>(b, b + len), at, hostNowNs()}); return rc.load(); }
  std::vector<Got> copy() { std::lock_guard<std::mutex> g(m); return got; }
};
struct FakeBackend : MidiBackend {
  std::mutex m; std::vector<MidiPortScan> outs, ins; std::map<int64_t, std::shared_ptr<FakePort>> ports; std::map<int64_t, std::shared_ptr<MidiRxRing>> rings; std::function<void()> onChange;
  std::vector<std::string> log;
  std::vector<MidiPortScan> listOutputs() override { std::lock_guard<std::mutex> g(m); return outs; }
  std::vector<MidiPortScan> listInputs() override { std::lock_guard<std::mutex> g(m); return ins; }
  std::shared_ptr<tpa::PortIo> openOutput(int64_t k) override { std::lock_guard<std::mutex> g(m); log.push_back("openOut"); auto p = std::make_shared<FakePort>(); ports[k] = p; return p; }
  bool openInput(int64_t k, std::shared_ptr<MidiRxRing> r) override { std::lock_guard<std::mutex> g(m); log.push_back("openIn"); rings[k] = r; return true; }
  void closeInput(int64_t k) override { std::lock_guard<std::mutex> g(m); log.push_back("closeIn"); rings.erase(k); }
  void setChangeHandler(std::function<void()> f) override { std::lock_guard<std::mutex> g(m); onChange = f; }
  std::shared_ptr<FakePort> port(int64_t k) { std::lock_guard<std::mutex> g(m); return ports[k]; }
  std::shared_ptr<MidiRxRing> ring(int64_t k) { std::lock_guard<std::mutex> g(m); return rings.count(k) ? rings[k] : nullptr; }
};

// two bars of a C major arpeggio on a MIDI instrument (output 1, channel 1), 120 bpm, 4 lines a beat (the Android debug song's text)
static std::string songText() {
  std::string s = "{\"app\":\"thunder-song\",\"v\":6,\"bpm\":120,\"lpb\":4,\"ticks\":6,\"swing\":50,\"dlen\":16,\"plen\":[16],\"song\":[";
  for (int r = 0; r < 2; r++) { s += r ? ",[" : "["; s += "0"; for (int t = 1; t < 33; t++) s += ",-1"; s += "]"; }
  s += "],\"chains\":[[[0,0]"; for (int k = 1; k < 16; k++) s += ",[-1,0]"; s += "]],\"phrases\":[[";
  const int notes[4] = {48, 52, 55, 60};
  for (int row = 0; row < 16; row++) { if (row) s += ","; char b[96];
    if (row % 4 == 0) std::snprintf(b, sizeof b, "[%d,-1,-1,-1,255,0,-1,-1,-1,-1,-1,-1]", notes[row / 4]);
    else if (row % 4 == 2) std::snprintf(b, sizeof b, "[200,-1,-1,-1,-1,0,-1,-1,-1,-1,-1,-1]");
    else std::snprintf(b, sizeof b, "[-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1]"); s += b; }
  s += "]],\"insts\":[{\"mo\":1,\"mch\":0,\"mpt\":0,\"pbr\":2}]}";
  return s;
}

int main() {
  auto be = std::make_shared<FakeBackend>();
  be->outs = {{11, "Synth A"}, {12, "Synth B"}}; be->ins = {{21, "Keys"}};
  std::mutex em; std::vector<std::string> ev; auto emit = [&](const std::string& j) { std::lock_guard<std::mutex> g(em); ev.push_back(j); };
  auto lastPorts = [&] { std::lock_guard<std::mutex> g(em); for (size_t i = ev.size(); i-- > 0;) if (ev[i].find("\"t\":\"midi.ports\"") != std::string::npos) return ev[i]; return std::string(); };
  MidiManager mm(be, emit);
  ManualOut* mo = nullptr;
  LiveHost host([&] { auto o = std::unique_ptr<ManualOut>(new ManualOut(48000)); mo = o.get(); return std::unique_ptr<AudioOut>(std::move(o)); });
  host.setMidi(&mm);
  std::string err; CHECK(host.start(true, &err) == 48000, "start with MIDI attached: %s", err.c_str());
  CHECK(mm.running(), "the MIDI host runs on the engine");
  std::atomic<bool> run{true};
  std::thread pace([&] { int64_t next = hostNowNs(); while (run) { mo->tick(256); next += (int64_t)(256.0 * 1e9 / 48000.0); const int64_t d = next - hostNowNs(); if (d > 0) std::this_thread::sleep_for(std::chrono::nanoseconds(d)); else if (d < -200000000) next = hostNowNs(); /* a shared CI runner may stall: catch up, so engine time stays wall time */ } });

  // ports: the scan thread lists, opens and tells the core
  for (int i = 0; i < 100 && lastPorts().find("Synth A") == std::string::npos; i++) ms(20);
  { thunder::JsonDoc d; CHECK(d.parse(lastPorts()), "midi.ports parses");
    auto devs = d.root().arr("devs"); CHECK(devs.size() == 2 && d.root().arr("ins").size() == 1, "devs lists the outputs, ins the inputs: %s", lastPorts().c_str()); }
  CHECK(lastPorts().find("n:Synth A") != std::string::npos && lastPorts().find("n:Keys") != std::string::npos, "ids are n:<name>");
  CHECK(be->port(11) && be->port(12) && be->ring(21), "both outputs and the input are open");

  // the four letters: assign A to Synth A and run the song through the core, pump and sink
  const bool clock[4] = {true, false, false, false}; const double gear[4] = {0, 0, 0, 0};
  CHECK(mm.applyCfg({"n:Synth A", "", "", ""}, clock, gear).empty(), "midi.cfg applied");
  const std::string file = slurp(std::string(TPD_REPO) + "/sounds/ELECTRO.thunder");
  const std::string_view K = tpa::extractMember(file, "kit");
  auto r = host.songLoad("{\"t\":\"song.load\",\"title\":\"x\",\"S\":" + songText() + ",\"vars\":null,\"rev\":3,\"id\":1}"); CHECK(r.ok, "song.load: rc %d %s", r.rc, r.err.c_str());
  auto k = host.kitLoad("{\"t\":\"kit.load\",\"kit\":" + std::string(K) + ",\"rev\":1}"); CHECK(k.ok, "kit.load");
  ms(200);   // the clock map needs a few callbacks
  const int64_t tPlay = hostNowNs();
  CHECK(host.play(0, 0, 0, -1, -1, 0, 0, 0, -1, 0, 0, 1, 0, 0) == 0, "play");
  for (int i = 0; i < 450; i++) { ms(20); int n = 0; for (auto& g : be->port(11)->copy()) if (g.b.size() > 2 && (g.b[0] & 0xF0) == 0x90 && g.b[2] > 0) n++; if (n >= 8) break; }
  host.stop(0); ms(300);
  auto got = be->port(11)->copy();
  int on = 0, off = 0, clk = 0, start = 0, stop = 0; std::vector<double> noteAt;
  for (auto& g : got) { if (g.b.empty()) continue; const uint8_t s = g.b[0]; if ((s & 0xF0) == 0x90 && g.b.size() > 2 && g.b[2] > 0) { on++; noteAt.push_back((double)(g.sentNs - tPlay) / 1e6); } else if ((s & 0xF0) == 0x80) off++; else if (s == 0xF8) clk++; else if (s == 0xFA) start++; else if (s == 0xFC) stop++; }
  CHECK(on >= 8 && off >= 8 && start == 1 && stop >= 1 && clk > 40, "the song's notes and the clock arrived: on %d off %d clock %d start %d stop %d", on, off, clk, start, stop);
  { std::vector<int> pitches; for (auto& g : got) if (!g.b.empty() && (g.b[0] & 0xF0) == 0x90 && g.b.size() > 2 && g.b[2] > 0) pitches.push_back(g.b[1]);
    CHECK(pitches.size() >= 8 && pitches[0] == 60 && pitches[1] == 64 && pitches[2] == 67 && pitches[3] == 72 && pitches[4] == 60 && pitches[7] == 72, "notes in order: C E G C, C E G C (the song loops)"); }
  { std::vector<double> gaps; for (size_t i = 1; i < noteAt.size(); i++) gaps.push_back(noteAt[i] - noteAt[i - 1]);
    if (gaps.size() >= 2) { double mn = *std::min_element(gaps.begin(), gaps.end()), mx = *std::max_element(gaps.begin(), gaps.end()); std::printf("note spacing (ms): min %.2f max %.2f (a beat at 120 bpm = 500)\n", mn, mx); CHECK(mn > 440 && mx < 560, "notes arrive a beat apart (the bound is loose for a shared CI runner; the figures are printed above)"); } }
  { std::vector<double> spacing; int64_t prev = 0; for (auto& g : got) if (!g.b.empty() && g.b[0] == 0xF8) { if (prev) spacing.push_back((double)(g.sentNs - prev) / 1e6); prev = g.sentNs; }
    if (spacing.size() > 10) { double mean = 0; for (double x : spacing) mean += x; mean /= spacing.size(); double sd = 0; for (double x : spacing) sd += (x - mean) * (x - mean); sd = std::sqrt(sd / spacing.size()); std::printf("clock tick spacing: mean %.3f ms (20.833 expected) sd %.3f ms\n", mean, sd); CHECK(std::fabs(mean - 20.833) < 2.0, "the clock runs at 24 ppqn at 120 bpm"); } }
  { thunder::JsonDoc d; d.parse(mm.portsEvent()); CHECK(d.root().arr("ports").size() == 4 && d.root().arr("ports").at(0).num("state") == 1 && d.root().arr("ports").at(0).str("name") == "Synth A" && d.root().arr("ports").at(0).num("clock") == 1, "letter A is Synth A, ok, clock on"); }

  // MIDI in: the OS callback pushes to the ring, the reader makes `midi.in` events for the page
  { const uint8_t on1[3] = {0x90, 64, 100}, cc[3] = {0xB0, 1, 77}; auto ring = be->ring(21); const int64_t t = hostNowNs(); ring->push(t, on1, 3); ring->push(t + 1000, cc, 3);
    std::string evs; for (int i = 0; i < 50 && evs.find("\"v\":77") == std::string::npos && evs.find("77") == std::string::npos; i++) { ms(20); evs += host.drainEvents(); }
    CHECK(evs.find("\"t\":\"midi.in\"") != std::string::npos, "midi.in events reach the page: %s", evs.substr(0, 200).c_str());
    CHECK(mm.inMessages() >= 2 && mm.inDropped() == 0, "two messages in, none dropped"); }
  // the ring never blocks the OS thread: a flood is dropped and counted
  { auto ring = be->ring(21); const uint8_t m[3] = {0xB0, 2, 1}; for (int i = 0; i < 200000; i++) ring->push(hostNowNs(), m, 3); ms(200); host.drainEvents(); CHECK(mm.inDropped() == ring->dropped(), "drops are counted (%llu)", (unsigned long long)mm.inDropped()); }

  // hot-plug: a new output appears, an old one goes
  { std::lock_guard<std::mutex> g(be->m); be->outs = {{11, "Synth A"}, {13, "Pad"}}; }
  if (be->onChange) be->onChange();
  for (int i = 0; i < 100 && lastPorts().find("\"Pad\"") == std::string::npos && lastPorts().find("n:Pad") == std::string::npos; i++) ms(20);
  CHECK(lastPorts().find("n:Pad") != std::string::npos && lastPorts().find("n:Synth B") == std::string::npos, "hot-plug: Pad is listed, Synth B is gone: %s", lastPorts().c_str());
  CHECK(be->port(13) != nullptr, "the new output was opened");
  // letter A's device unplugged: the letter keeps its id, the state says it is missing
  { std::lock_guard<std::mutex> g(be->m); be->outs = {{13, "Pad"}}; }
  if (be->onChange) be->onChange();
  for (int i = 0; i < 100 && lastPorts().find("\"state\":2") == std::string::npos; i++) ms(20);
  { thunder::JsonDoc d; d.parse(lastPorts()); CHECK(d.root().arr("ports").at(0).str("id") == "n:Synth A" && d.root().arr("ports").at(0).num("state") == 2, "letter A remembers its device, which is not here now: %s", lastPorts().c_str()); }

  // teardown order: the audio callback stops first, then MIDI, then the engine is freed; nothing is sent after shutdown returns
  run = false; pace.join();
  host.shutdown();
  CHECK(!mm.running(), "MIDI is down with the engine");
  { auto n = be->port(11)->copy().size(); ms(100); CHECK(be->port(11)->copy().size() == n, "nothing is sent after the engine is gone"); }
  // and the engine comes back up with the same manager
  mo = nullptr; CHECK(host.start(true, &err) == 48000 && mm.running(), "a second start: MIDI runs again");
  host.shutdown();
  DONE("midi_manager");
}
