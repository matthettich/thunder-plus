// N2 H2: the Android shell's MIDI on the LIVE host (no Android in it): MidiHost::engineStartExternal over thunder_live_midi(), audioBlockLive after each thunder_live_process, the control runner (every MIDI control call
// goes through the live control thread), the transport flag, shutdown order (stop threads, engineStop, destroy), and MIDI in (MidiInParser, the midi.in event, the sink into the engine's event queue).
// Against a FAKE PortIo and the real core. The audio thread is this test's, at real-time pace.
#include <time.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "live_engine.h"
#include "midi_in.h"
#include "midi_out.h"

static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { g_fail++; std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

namespace {
struct FakeIo : tpa::PortIo {
  std::mutex mu; std::vector<std::vector<uint8_t>> r;
  int send(const uint8_t* b, int len, int64_t) override { std::lock_guard<std::mutex> g(mu); r.emplace_back(b, b + len); return 0; }
  int count(uint8_t st, int d1 = -1) { std::lock_guard<std::mutex> g(mu); int n = 0; for (auto& x : r) if (x[0] == st && (d1 < 0 || x[1] == d1)) n++; return n; }
};

std::string songText() {   // the U36 debug song: two bars, four notes a bar, to a MIDI instrument on channel 1
  std::string s = "{\"app\":\"thunder-song\",\"v\":6,\"bpm\":120,\"lpb\":4,\"ticks\":6,\"swing\":50,\"dlen\":16,\"plen\":[16],\"song\":[";
  for (int r = 0; r < 2; r++) { s += r ? ",[" : "["; s += "0"; for (int t = 1; t < 33; t++) s += ",-1"; s += "]"; }
  s += "],\"chains\":[[[0,0]";
  for (int k = 1; k < 16; k++) s += ",[-1,0]";
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

struct Msgs { std::vector<tpa::MidiInMsg> v; };
void collect(void* u, const tpa::MidiInMsg& m) { static_cast<Msgs*>(u)->v.push_back(m); }
std::vector<tpa::MidiInMsg> parse(tpa::MidiInParser& p, std::vector<uint8_t> bytes) { Msgs m; p.feed(bytes.data(), (int)bytes.size(), &collect, &m); return m.v; }

void testParser() {
  tpa::MidiInParser p;
  auto a = parse(p, {0x90, 60, 100, 0x80, 60, 0});
  CHECK(a.size() == 2 && a[0].len == 3 && a[0].b[0] == 0x90 && a[1].b[0] == 0x80, "two whole messages: %zu", a.size());
  a = parse(p, {0x90, 62, 90, 64, 80, 65, 70});   // running status
  CHECK(a.size() == 3 && a[1].b[0] == 0x90 && a[1].b[1] == 64 && a[2].b[1] == 65, "running status: %zu", a.size());
  a = parse(p, {0xB0, 1, 0xF8, 127});   // a clock byte inside a message: both come out, the message intact
  CHECK(a.size() == 2 && a[0].b[0] == 0xF8 && a[1].b[0] == 0xB0 && a[1].b[1] == 1 && a[1].b[2] == 127, "realtime inside a message: %zu", a.size());
  a = parse(p, {0xC0, 5, 0xD0, 33});   // one data byte
  CHECK(a.size() == 2 && a[0].len == 2 && a[1].len == 2 && a[1].b[1] == 33, "program change and channel pressure");
  a = parse(p, {0xF0, 1, 2, 3, 0xF7, 0x90, 1, 2});   // SysEx dropped, the note after it kept
  CHECK(a.size() == 1 && a[0].b[0] == 0x90 && p.sysexSkipped >= 1, "sysex skipped: %zu", a.size());
  a = parse(p, {0xFE, 0xFF, 0xFA, 0xFC, 0xFB});   // active sensing and reset dropped; Start, Stop, Continue kept
  CHECK(a.size() == 3 && a[0].b[0] == 0xFA && a[1].b[0] == 0xFC && a[2].b[0] == 0xFB, "transport bytes: %zu", a.size());
  a = parse(p, {0xF2, 0x10, 0x02});   // Song Position Pointer
  CHECK(a.size() == 1 && a[0].b[0] == 0xF2 && a[0].len == 3 && a[0].b[1] == 0x10 && a[0].b[2] == 2, "SPP");
  a = parse(p, {0x05, 0x06});   // data with no status after SPP: the stray bytes are dropped (SPP leaves no running status)
  CHECK(a.empty() && p.strayData >= 2, "stray data dropped");
  a = parse(p, {0xF1, 0x20, 0x90, 70, 71});   // MTC quarter frame skipped with its data
  CHECK(a.size() == 1 && a[0].b[1] == 70, "MTC skipped: %zu", a.size());
  a = parse(p, {0x90, 60});   // a split message
  CHECK(a.empty(), "half a message");
  a = parse(p, {100});
  CHECK(a.size() == 1 && a[0].b[2] == 100, "split message completed");
  tpa::MidiInMsg m; m.b[0] = 0x90; m.b[1] = 60; m.b[2] = 100; m.len = 3;
  CHECK(tpa::midiInJson(1234, 99, 7, m) == "{\"t\":\"midi.in\",\"f\":1234,\"wallNs\":99,\"port\":7,\"bytes\":[144,60,100]}", "json: %s", tpa::midiInJson(1234, 99, 7, m).c_str());
  m.b[0] = 0xF8; m.len = 1;
  CHECK(tpa::midiInJson(0, 5, 1, m) == "{\"t\":\"midi.in\",\"f\":0,\"wallNs\":5,\"port\":1,\"bytes\":[248]}", "json realtime");
  CHECK(tpa::midiInIsNote(a[0]) , "a note-on is a note");
}
}  // namespace

int main() {
  testParser();

  tpa::LiveEngine E;
  tpa::LiveEngine::Config c; c.sampleRate = 48000; c.maxBlock = 4096;
  std::string err;
  if (!E.create(c, &err)) { std::printf("FAIL create: %s\n", err.c_str()); return 1; }
  thunder_midi* cm = thunder_live_midi(E.handle());
  CHECK(cm != nullptr, "the live host has a thunder_midi (F_MIDI)");
  tpa::MidiHost& H = tpa::host();
  auto io = std::make_shared<FakeIo>();
  // before the engine: saved (the Kotlin side lists devices at app start)
  tpa::DevInfo d; d.id = "n:Fake"; d.name = "Fake"; d.token = 7; d.ts = false;
  CHECK(H.setDevices({d}) == 0, "devices saved before the engine");
  CHECK(H.assign(0, "n:Fake") == 0, "port A saved");
  CHECK(H.setClock(0, true) == 0 && H.setGear(0, 0) == 0, "clock box and gear saved");
  H.attach(7, io, false);
  CHECK(H.engineStartExternal(cm, c.sampleRate, nullptr, nullptr), "engineStartExternal");
  CHECK(H.running(), "running");
  std::atomic<int> ended{0};
  E.setStateHook([&](int st) { if (st == THUNDER_LIVE_STATE_ENDED) { ended++; H.setPlaying(false); } });
  E.startThreads();
  std::atomic<int> runnerCalls{0};
  { std::shared_ptr<int> keepAlive; auto* Ep = &E; H.setControlRunner([Ep, &runnerCalls](std::function<int()> fn) { runnerCalls++; return Ep->call([fn](thunder_live*) { return fn(); }, 1500); }); }

  std::atomic<bool> stop{false}; std::atomic<int64_t> fedBlocks{0};
  std::thread audio([&] {
    std::vector<float> o(2 * 480);
    int64_t wall = tpa::monoNs();
    while (!stop.load()) {
      const int64_t F = E.hostFrameNow();
      E.render(o.data(), 480, wall);
      H.audioBlockLive(480, F, E.engineOffset(), wall);
      fedBlocks++;
      wall += 10000000LL;
      timespec t; t.tv_sec = (time_t)(wall / 1000000000LL); t.tv_nsec = (long)(wall % 1000000000LL); clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &t, nullptr);
    }
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  const std::string S = songText();
  char serr[256] = {0};
  int rc = E.call([&](thunder_live* L) { return thunder_live_song_publish(L, (const uint8_t*)S.data(), S.size(), nullptr, 0, 1, serr, sizeof serr); });
  CHECK(rc == 0, "song_publish: %d %s", rc, serr);
  // a control call from ANOTHER thread while the engine runs goes through the control thread (the runner)
  { const int before = runnerCalls.load(); std::thread t([&] { CHECK(H.setGear(0, 0) == 0, "setGear through the runner"); }); t.join(); CHECK(runnerCalls.load() == before + 1, "the call went through the control thread"); }
  rc = E.call([&](thunder_live* L) {
    thunder_live_play_opts po; std::memset(&po, 0, sizeof po);
    po.size = (uint32_t)sizeof po; po.t0 = THUNDER_LIVE_NOW; po.mode = THUNDER_LIVE_MODE_SONG; po.row = 0; po.pdc_mode = 1; po.random_seed = 5; po.spp = -1;
    H.setPlayingDirect(true);
    return thunder_live_play(L, &po);
  });
  CHECK(rc == 0, "play: %d", rc);
  // the live sequencer loops the two-bar song: run until its 8 notes have gone out, then stop it from the shell the way nativeTransportStop does
  for (int i = 0; i < 200 && io->count(0x90) < 8; i++) std::this_thread::sleep_for(std::chrono::milliseconds(50));
  CHECK(io->count(0x90) >= 8, "the song's notes reach the port: %d", io->count(0x90));
  rc = E.call([&](thunder_live* L) { const int r = thunder_live_stop(L, THUNDER_LIVE_NOW); if (r == 0) H.setPlayingDirect(false); return r; });
  CHECK(rc == 0, "stop: %d", rc);
  std::this_thread::sleep_for(std::chrono::milliseconds(1000));   // the Stop and the note-offs go through the pump
  const int nOn = io->count(0x90), nOff = io->count(0x80), nClk = io->count(0xF8);
  std::printf("midi out: start %d stop %d note-on %d note-off %d clock %d\n", io->count(0xFA), io->count(0xFC), nOn, nOff, nClk);
  CHECK(io->count(0xFA) == 1, "one Start, got %d", io->count(0xFA));
  CHECK(io->count(0xFC) == 1, "one Stop, got %d", io->count(0xFC));
  CHECK(nOff >= nOn, "every note-on has its note-off (%d on, %d off)", nOn, nOff);
  CHECK(nClk >= 90, "the MIDI clock runs while playing: %d ticks", nClk);
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  CHECK(io->count(0xF8) == nClk && io->count(0x90) == nOn, "nothing is sent after the Stop (clock %d -> %d)", nClk, io->count(0xF8));
  const std::string pj = H.portsJson();
  std::printf("ports %s\n", pj.c_str());
  CHECK(pj.find("\"state\":1,\"id\":\"n:Fake\"") != std::string::npos && pj.find("\"clock\":1") != std::string::npos, "port A is listed with its clock box on: %s", pj.c_str());

  // MIDI in: the sink pushes the engine's bridge events
  static tpa::LiveEngine* s_eng = &E;
  tpa::setMidiInSink([](uint64_t token, int64_t ns, const tpa::MidiInMsg& m) { s_eng->pushEvent(tpa::midiInJson(s_eng->hostFrameAt(ns), ns, token, m), tpa::midiInIsNote(m)); });
  { tpa::MidiInParser p; Msgs got; const uint8_t bytes[] = {0x90, 60, 100, 0xB0, 7, 99};
    p.feed(bytes, 6, [](void* u, const tpa::MidiInMsg& m) { Msgs* g = static_cast<Msgs*>(u); g->v.push_back(m); if (auto s = tpa::midiInSink()) s(5, tpa::monoNs(), m); }, &got); }
  const std::string ev = E.drainEvents();
  CHECK(ev.find("\"t\":\"midi.in\"") != std::string::npos && ev.find("\"port\":5,\"bytes\":[144,60,100]") != std::string::npos && ev.find("\"bytes\":[176,7,99]") != std::string::npos, "midi.in events in the engine's queue");
  tpa::setMidiInSink(nullptr);
  CHECK(tpa::midiInSink() == nullptr, "sink cleared");

  // shutdown order: the audio thread, the control and poller threads, the MIDI host, destroy
  stop = true; audio.join();
  E.stopThreads();
  H.engineStop();
  CHECK(!H.running(), "engineStop");
  // a control call after the engine is gone runs here (no runner, no thread): saved for the next start
  CHECK(H.setGear(1, 5) == 0, "a control call with no engine is saved");
  E.destroy();
  CHECK(!E.created(), "destroyed");
  if (g_fail) { std::printf("%d FAILED\n", g_fail); return 1; }
  std::printf("OK\n");
  return 0;
}
