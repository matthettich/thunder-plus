// N2 H1a: the Android shell's side of the live host with no Android in it (native/android/app/src/main/cpp/live_engine.{h,cpp}, pad_filler.{h,cpp}).
//   1. extractMember: the raw text of a top-level member, string-aware (braces and escaped quotes inside strings, the last of equal keys, absent, malformed).
//   2. renderPads on the bundled sounds/ELECTRO.thunder: the core's own renderers fill the pads without the page; the dry pads (spring, crush) are counted.
//   3. LiveEngine: create, threads, song_publish + pad_take + pads_publish + play through the control thread (E_BUSY retried while no callback runs), a fake audio callback calling render()
//      in blocks of 480 frames, the poller sees PLAYING, the audio is finite and not silent, nothing dropped, stop, destroy.
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "live_engine.h"
#include "pad_filler.h"
#include "rt/json.h"
#ifdef THUNDER_HAVE_LXR
#include "thunder/lxr_module.h"
#endif

static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { g_fail++; std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static std::string readFile(const std::string& p) { std::ifstream f(p, std::ios::binary); std::stringstream ss; ss << f.rdbuf(); return ss.str(); }

int main() {
  using namespace tpa;
  // 1. extractMember
  {
    const std::string j = R"( {"a": 1, "song": {"x": "}{\"", "y": [1, {"z": "]"}]}, "kit": [1,2], "n": -1.5e3, "song2": 3, "s": "a\\" , "song": {"last": true} } )";
    CHECK(extractMember(j, "kit") == "[1,2]", "kit: '%.*s'", (int)extractMember(j, "kit").size(), extractMember(j, "kit").data());
    CHECK(extractMember(j, "n") == "-1.5e3", "n");
    CHECK(extractMember(j, "song") == R"({"last": true})", "the last of equal keys");
    CHECK(extractMember(j, "song2") == "3", "song2");
    CHECK(extractMember(j, "s") == R"("a\\")", "s: '%.*s'", (int)extractMember(j, "s").size(), extractMember(j, "s").data());
    CHECK(extractMember(j, "nope").empty(), "absent");
    CHECK(extractMember("[1,2]", "a").empty(), "not an object");
    CHECK(extractMember(R"({"a": {"b": )", "a").empty(), "malformed");
    CHECK(extractMember("", "a").empty(), "empty");
  }
  // relOf
  {
    thunder::JsonDoc d; d.parse(std::string(R"({"engine":"bass","bass":{"rel":250}})"));
    CHECK(std::fabs(relOfVoice(d.root()) - 0.25) < 1e-12, "bass rel");
    thunder::JsonDoc e; e.parse(std::string(R"({"layers":[{"on":true,"level":1,"mode":"ADSR","rel":400},{"on":false,"level":1,"mode":"ADSR","rel":900}]})"));
    CHECK(std::fabs(relOfVoice(e.root()) - 0.4) < 1e-12, "layers rel");
    thunder::JsonDoc f; f.parse(std::string(R"({"layers":[]})"));
    CHECK(std::fabs(relOfVoice(f.root()) - 0.03) < 1e-12, "default 30 ms");
  }
#ifdef THUNDER_HAVE_LXR
  thunder_lxr_module_register(); const int expectSkipped = 0;
#else
  const int expectSkipped = 2;
#endif
  // 2. the pads of ELECTRO
  const std::string root = THUNDER_ROOT;
  const std::string proj = readFile(root + "/sounds/ELECTRO.thunder");
  CHECK(!proj.empty(), "sounds/ELECTRO.thunder not found");
  const std::string_view songT = extractMember(proj, "song"), kitT = extractMember(proj, "kit");
  CHECK(songT.size() > 100000 && kitT.size() > 10000, "song %zu bytes, kit %zu bytes", songT.size(), kitT.size());
  double bpm = 131, lpb = 4;
  { thunder::JsonDoc sd; CHECK(sd.parse(songT), "song parses"); bpm = sd.root().num("bpm", 120); lpb = sd.root().num("lpb", 4); }
  PadSet ps;
  CHECK(renderPads(kitT, bpm, lpb, 48000, &ps), "renderPads");
  std::printf("pads: %zu rendered, %d skipped, %d dry, %.0f ms\n", ps.pads.size(), ps.skipped, ps.dry, ps.ms);
  for (auto& n : ps.notes) std::printf("  %s\n", n.c_str());
  CHECK(ps.pads.size() >= 36, "ELECTRO has 40 voices, all of engines the core has: %zu rendered", ps.pads.size());
  CHECK(ps.skipped == expectSkipped, "the two lxsnare voices render with the optional drum module and are left empty without it (thunder_lxr_api() is null): %d skipped, expected %d", ps.skipped, expectSkipped);
  CHECK(ps.dry == 2, "ELECTRO has two voices with a pad effect the core does not host (spring, crush): %d", ps.dry);
  { size_t loud = 0; for (auto& p : ps.pads) { float pk = 0; for (int64_t i = 0; i < p.frames; i++) pk = std::fmax(pk, std::fabs(p.block[i])); if (pk > 0.001f) loud++; } CHECK(loud >= 30, "pads with sound: %zu", loud); }

  // 3. the engine
  LiveEngine E; LiveEngine::Config cfg; cfg.sampleRate = 48000; cfg.maxBlock = 1024;
  std::string err;
  CHECK(E.create(cfg, &err), "create: %s", err.c_str());
  E.startThreads();
  // the engine's first publishes with NO audio callback running: the song ring holds 16, the scheduler's six; a seventh publish is E_BUSY and the control thread retries it once the callback runs
  std::atomic<int> rcs{0}, bad{0};
  std::thread caller([&] {
    for (int k = 0; k < 9; k++) {
      char e[200] = {0};
      const int rc = E.call([&](thunder_live* L) { return thunder_live_song_publish(L, (const uint8_t*)songT.data(), songT.size(), nullptr, 0, 100 + k, e, sizeof e); }, 20000);
      if (rc != 0) bad++;
      rcs++;
    }
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  std::atomic<bool> stop{false}; std::atomic<int> nonfinite{0}; std::atomic<int64_t> frames{0}; std::atomic<float> peak{0};
  std::thread audio([&] {   // the fake device callback: 480-frame blocks, a little faster than real time
    std::vector<float> o(2 * 480); int64_t wall = 1000000000LL;
    while (!stop.load()) {
      E.render(o.data(), 480, wall); wall += 10000000LL;
      float pk = 0; for (float v : o) { if (!std::isfinite(v)) nonfinite++; else pk = std::fmax(pk, std::fabs(v)); }
      float cur = peak.load(); if (pk > cur) peak.store(pk);
      frames += 480;
      std::this_thread::sleep_for(std::chrono::milliseconds(3));
    }
  });
  caller.join();
  CHECK(rcs.load() == 9 && bad.load() == 0, "9 publishes: %d done, %d failed", rcs.load(), bad.load());
  int rc = E.call([&](thunder_live* L) {
    for (auto& p : ps.pads) {
      int r = thunder_live_pad_take(L, p.slot, p.block, 0, p.frames, 48000.0); if (r) return r; p.block = nullptr;
      r = thunder_live_pad_kit(L, p.slot, p.choke, p.syn ? 1 : 0, p.rel); if (r) return r;
    }
    return thunder_live_pads_publish(L);
  });
  CHECK(rc == 0, "pads: %d", rc);
  rc = E.call([&](thunder_live* L) { thunder_live_play_opts po; std::memset(&po, 0, sizeof po); po.size = sizeof po; po.t0 = THUNDER_LIVE_NOW; po.pdc_mode = 1; po.random_seed = 0x2545F491u; return thunder_live_play(L, &po); });
  CHECK(rc == 0, "play: %d", rc);
  E.call([&](thunder_live* L) { return thunder_live_meters_want(L, 10.0); });
  bool playing = false;
  for (int i = 0; i < 400 && !playing; i++) { std::this_thread::sleep_for(std::chrono::milliseconds(20)); playing = E.statusJson().find("\"state\":1") != std::string::npos; }
  CHECK(playing, "the poller never saw PLAYING: %s", E.statusJson().c_str());
  while (frames.load() < 48000 * 6) std::this_thread::sleep_for(std::chrono::milliseconds(50));
  const std::string st = E.statusJson(), ss = E.statsJson();
  std::printf("status %s\nstats  %s\npeak %.3f\n", st.c_str(), ss.c_str(), (double)peak.load());
  CHECK(nonfinite.load() == 0, "non-finite samples: %d", nonfinite.load());
  CHECK(peak.load() > 0.02f, "the song is silent: peak %.4f", (double)peak.load());
  CHECK(ss.find("\"seqDropped\":0") != std::string::npos, "sequencer dropped events");
  CHECK(st.find("\"fatal\":0") != std::string::npos, "a fatal error record");
  rc = E.call([&](thunder_live* L) { return thunder_live_stop(L, THUNDER_LIVE_NOW); });
  CHECK(rc == 0, "stop: %d", rc);
  { bool idle = false;   // verify-n2 F3: a user Stop shows as IDLE (0), not as "ended" (2)
    for (int i = 0; i < 200 && !idle; i++) { std::this_thread::sleep_for(std::chrono::milliseconds(10)); idle = E.statusJson().find("\"state\":0") != std::string::npos; }
    CHECK(idle, "after Stop the state must read 0 (IDLE): %s", E.statusJson().c_str()); }
  CHECK(stateOfRecord(THUNDER_LIVE_ST_PLAYING) == 1 && stateOfRecord(THUNDER_LIVE_ST_STOPPED) == 0 && stateOfRecord(THUNDER_LIVE_ST_ENDED) == 2, "stateOfRecord");
  stop = true; audio.join();
  E.destroy();
  CHECK(!E.created(), "destroyed");
  // a destroyed engine renders silence and refuses calls
  { std::vector<float> o(960, 1.f); E.render(o.data(), 480, 0); float s = 0; for (float v : o) s += std::fabs(v); CHECK(s == 0.f, "silence after destroy"); CHECK(E.call([](thunder_live*) { return 0; }) == -1000, "call after destroy"); }
  std::printf(g_fail ? "FAILED: %d\n" : "OK\n", g_fail);
  return g_fail ? 1 : 0;
}
