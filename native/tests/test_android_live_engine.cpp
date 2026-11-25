// N2 H1a: the Android shell's side of the live host with no Android in it (native/android/app/src/main/cpp/live_engine.{h,cpp}, pad_filler.{h,cpp}).
//   1. extractMember: the raw text of a top-level member, string-aware (braces and escaped quotes inside strings, the last of equal keys, absent, malformed).
//   2. renderPads on the bundled sounds/ELECTRO.thunder: the core's own renderers fill the pads without the page; the dry pads (spring, crush) are counted.
//   3. LiveEngine: create, threads, song_publish + pad_take + pads_publish + play through the control thread (E_BUSY retried while no callback runs), a fake audio callback calling render()
//      in blocks of 480 frames, the poller sees PLAYING, the audio is finite and not silent, nothing dropped, stop, destroy.
#include <atomic>
#include <cctype>
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
#include "live_events.h"
#include "pad_filler.h"
#include "songfit.h"
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
  // padFxApplies against the PAGE (verify-n2 F14): the table is kept by hand, so read index.html and the pump engine file and fail on drift
  {
    const std::string page = readFile(std::string(THUNDER_ROOT) + "/index.html"), pump = readFile(std::string(THUNDER_ROOT) + "/pump/synth/pump-synth-engine.js");
    CHECK(page.find("const engFx = V => isBlipV(V) || (isXEngV(V) && !(XENG[V.engine] && XENG[V.engine].live));") != std::string::npos, "the page's engFx rule changed: re-read it and update padFxApplies (pad_filler.cpp)");
    CHECK(page.find("if (isBassV(V)) return renderBass(V, sr, hold);\n  if (isVoxV(V)) return renderVox(V, sr);") != std::string::npos, "renderVoice no longer returns for Bass and Voice before padFx: padFxApplies(bass / vox) must be re-checked");
    CHECK(!padFxApplies("bass") && !padFxApplies("vox") && padFxApplies("layers") && padFxApplies("blip"), "bass / vox dry, layers / blip wet");
    int n = 0, bad = 0;
    const size_t a = page.find("const XENG = {"), b = page.find("const isXEngV", a);
    CHECK(a != std::string::npos && b != std::string::npos, "XENG table not found");
    size_t at = a;
    while ((at = page.find("\n  ", at)) != std::string::npos && at < b) {   // lines `  name:{live:true|false, ...`
      at += 3; size_t e = at; while (e < b && (std::isalnum((unsigned char)page[e]) || page[e] == '_')) e++;
      if (e > at && page.compare(e, 7, ":{live:") == 0) {
        const bool live = page.compare(e + 7, 4, "true") == 0; const std::string name = page.substr(at, e - at);
        n++; if (padFxApplies(name) == live) { bad++; std::printf("  padFxApplies(%s) = %d but the page has live:%d\n", name.c_str(), (int)padFxApplies(name), (int)live); }
      }
    }
    CHECK(n >= 24 && bad == 0, "XENG entries read: %d, disagreeing: %d", n, bad);
    int tw = 0, twBad = 0;
    const size_t pa = pump.find("const LIVE = {"), pb = pump.find("\n};", pa);
    CHECK(pa != std::string::npos && pb != std::string::npos, "pump LIVE table not found");
    for (size_t q = pa; (q = pump.find("\n  ", q)) != std::string::npos && q < pb;) {
      q += 3; size_t e = q; while (e < pb && std::isalnum((unsigned char)pump[e])) e++;
      if (e > q && pump.compare(e, 7, ":{twin:") == 0) { tw++; if (padFxApplies(pump.substr(q, e - q))) { twBad++; std::printf("  pump live twin %s takes pad effects\n", pump.substr(q, e - q).c_str()); } }
    }
    CHECK(tw >= 13 && twBad == 0, "pump live twins read: %d, wet: %d", tw, twBad);
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
  // ELECTRO's voice 10 (mach + spring) is DRY on the page too (engFx is false for a live engine): never a note or a reason. Voice 17 (layers + crush) is dry here until the pad-effects unit (#314) is in the core, then 0.
  CHECK(ps.dry <= 1, "at most one pad (crush on a layers voice) lacks its pad effect: %d", ps.dry);
  for (auto& n : ps.notes) CHECK(n.find("pad 10 ") == std::string::npos, "pad 10 (mach + spring) must not be reported: %s", n.c_str());
  { size_t loud = 0; for (auto& p : ps.pads) { float pk = 0; for (int64_t i = 0; i < p.frames; i++) pk = std::fmax(pk, std::fabs(p.block[i])); if (pk > 0.001f) loud++; } CHECK(loud >= 30, "pads with sound: %zu", loud); }

  // 2b. SongFit (songfit.h). ELECTRO: two voices have a pad effect the core does not host, so by the lead's ruling the SONG is not natively playable.
  {
    const uint32_t full = THUNDER_LIVE_CAP_EFFECTS | 0x200u;
    FitResult f = songFit(songT, kitT, full, 0, expectSkipped == 0);
    int pe = 0, other = 0; for (auto& r : f.reasons) { if (r.code == "pad-effect") pe++; else other++; }
    CHECK(pe <= 1 && other == 0 && f.ok == (pe == 0), "ELECTRO fit: ok %d, %d pad-effect, %d other (one until #314: crush on pad 17; none after)", (int)f.ok, pe, other);
    for (auto& r : f.reasons) CHECK(r.text.find("pad 10 ") == std::string::npos, "voice 10 (mach + spring) is dry on the page: no reason: %s", r.text.c_str());
    CHECK(f.warns.size() == 1 && f.warns[0].find("4 pads play its rendered twin") != std::string::npos, "ELECTRO warn: %zu entries, first '%s'", f.warns.size(), f.warns.empty() ? "" : f.warns[0].c_str());
    { const std::string fj = f.json(3); CHECK(fj.find("\"reasonTexts\":[\"pad ") != std::string::npos && fj.find("\"warn\":[\"4 pads") != std::string::npos, "fit json lists: %s", fj.c_str()); }
    FitResult g = songFit(songT, kitT, THUNDER_LIVE_CAP_EFFECTS, 0, expectSkipped == 0);   // no chains in the host: ELECTRO has chainOn
    bool nc = false; for (auto& r : g.reasons) if (r.code == "no-chains") nc = true;
    CHECK(nc, "ELECTRO without CAP_CHAINS must say no-chains");
    FitResult n = songFit(songT, kitT, 0, 0, true);
    bool ne = false; for (auto& r : n.reasons) if (r.code == "no-effects") ne = true; CHECK(ne, "no-effects");
    const std::string synthSong = R"({"insts":[{"sync":0},{"sync":2}],"chainOn":[true],"chainPatch":{"modules":[{"type":"trackIn"},{"type":"scin"}]}})";
    const std::string synthKit = R"({"voices":[{"engine":"sf"},{"name":"lx","engine":"lxsnare"},{"layers":[{"type":"sine","on":true,"level":1}]}]})";
    FitResult h = songFit(synthSong, synthKit, THUNDER_LIVE_CAP_EFFECTS | 0x200u, 0, false);
    std::string codes; for (auto& r : h.reasons) codes += r.code + " ";
    CHECK(codes.find("stretch") != std::string::npos && codes.find("sidechain") != std::string::npos && codes.find("font") != std::string::npos && codes.find("no-lxr") != std::string::npos && codes.find("no-chains") == std::string::npos, "synthetic fit codes: %s", codes.c_str());
    const std::string emptyKit = R"({"voices":[]})";
    CHECK(songFit(R"({"insts":[],"chainOn":[]})", emptyKit, THUNDER_LIVE_CAP_EFFECTS, 0, true).ok, "an empty song fits");
    CHECK(!songFit("not json", emptyKit, THUNDER_LIVE_CAP_EFFECTS, 0, true).ok, "garbage does not fit");
    CHECK(!songFit(R"({"insts":[]})", emptyKit, THUNDER_LIVE_CAP_EFFECTS, 10, true).ok, "too big");
    // verify-n2 F6: NO kit, NO verdict (pending, never ok); an unreadable kit is a reason
    { FitResult np = songFit(R"({"insts":[],"chainOn":[]})", "", THUNDER_LIVE_CAP_EFFECTS, 0, true);
      CHECK(!np.ok && np.pending && np.reasons.size() == 1 && np.reasons[0].code == "no-kit", "no kit: pending, not ok");
      CHECK(np.json(1).find("\"pending\":true") != std::string::npos, "pending in the json: %s", np.json(1).c_str());
      CHECK(!songFit(R"({"insts":[]})", "{broken", THUNDER_LIVE_CAP_EFFECTS, 0, true).ok, "an unreadable kit does not fit"); }
    // the song-level reasons the verifier found missing: Resample, a pump module in a chain, an unknown module, lxp with key follow, MIDI clock follow
    { const uint32_t caps = THUNDER_LIVE_CAP_EFFECTS | 0x200u;
      auto codesOf = [&](const std::string& song, FitContext ctx = FitContext()) { std::string c; for (auto& r : songFit(song, emptyKit, caps, 0, true, ctx).reasons) c += r.code + " "; return c; };
      CHECK(codesOf(R"({"insts":[],"chainOn":[true],"chainPatch":{"modules":[{"type":"trackIn"},{"type":"resample"}]}})").find("resample") != std::string::npos, "resample");
      CHECK(codesOf(R"({"insts":[],"chainOn":[true],"chainPatch":{"modules":[{"type":"pecho"}]}})").find("pump-in-chain") != std::string::npos, "pump module in a chain");
      CHECK(codesOf(R"({"insts":[],"chainOn":[true],"chainPatch":{"modules":[{"type":"nosuchmodule"}]}})").find("module") != std::string::npos, "unknown module");
      CHECK(codesOf(R"({"insts":[],"chainOn":[true],"chainPatch":{"modules":[{"type":"trackIn"},{"type":"vcf"},{"type":"chainOut"}]}})").empty(), "known modules fit: '%s'", codesOf(R"({"insts":[],"chainOn":[true],"chainPatch":{"modules":[{"type":"trackIn"},{"type":"vcf"},{"type":"chainOut"}]}})").c_str());
      FitContext lx; lx.lxpActive = true; FitContext ck; ck.clockIn = true;
      CHECK(codesOf(R"({"insts":[{"kf":true}]})", lx).find("lxp") != std::string::npos, "lxp with a key-follow instrument");
      CHECK(codesOf(R"({"insts":[{"kf":false}]})", lx).empty() && codesOf(R"({"insts":[{"kf":true}]})").empty(), "lxp alone, or kf alone, is no reason");
      CHECK(codesOf(R"({"insts":[]})", ck).find("clock-follow") != std::string::npos, "clock follow"); }
    const std::string js = h.json(7);
    CHECK(js.find("\"t\":\"fit\"") != std::string::npos && js.find("\"rev\":7") != std::string::npos && js.find("\"ok\":false") != std::string::npos, "fit json: %s", js.c_str());
  }
  // 2c. the event formatter on synthetic records
  {
    std::string o;
    thunder_live_ev_pos p; std::memset(&p, 0, sizeof p); p.h.type = THUNDER_LIVE_EV_POS; p.h.bytes = sizeof p; p.h.f = 12345; p.row = 7; p.step = 3; p.cr = 2; p.bpm10 = 1310;
    CHECK(eventToJson((const unsigned char*)&p, sizeof p, &o) && o == "{\"t\":\"pos\",\"f\":12345,\"row\":7,\"step\":3,\"cr\":2,\"bpm\":131.0}", "pos json: %s", o.c_str());
    unsigned char rb[sizeof(thunder_live_ev_rows) + 16]; std::memset(rb, 0, sizeof rb);
    thunder_live_ev_rows w; std::memset(&w, 0, sizeof w); w.h.type = THUNDER_LIVE_EV_ROWS; w.h.bytes = sizeof rb; w.h.f = 9; w.valid = (1ull << 1) | (1ull << 32);
    std::memcpy(rb, &w, sizeof w); thunder_live_ev_rowent e1 = {4, 5, 6, 7}, e2 = {-1, 0, 0, -1}; std::memcpy(rb + sizeof w, &e1, 8); std::memcpy(rb + sizeof w + 8, &e2, 8);
    CHECK(eventToJson(rb, sizeof rb, &o) && o.find("{\"tr\":1,\"row\":4,\"cr\":5,\"step\":6,\"phrase\":7}") != std::string::npos && o.find("\"tr\":32") != std::string::npos, "rows json: %s", o.c_str());
    thunder_live_ev_state st; std::memset(&st, 0, sizeof st); st.h.type = THUNDER_LIVE_EV_STATE; st.h.bytes = sizeof st; st.state = THUNDER_LIVE_ST_STOPPED; st.arg = 1;
    CHECK(eventToJson((const unsigned char*)&st, sizeof st, &o) && o.find("\"state\":\"stopped\"") != std::string::npos, "state json");
    thunder_live_ev_head bad; std::memset(&bad, 0, sizeof bad); bad.type = 99; bad.bytes = sizeof bad;
    CHECK(!eventToJson((const unsigned char*)&bad, sizeof bad, &o), "an unknown type is not forwarded");
    CHECK(!eventToJson((const unsigned char*)&p, 8, &o), "a short record is refused");
    EventQueue q(3);
    q.push("1", false); q.push("2", true); q.push("3", false); q.push("4", true);   // full: the oldest non-critical (1) goes
    CHECK(q.drain() == "2\n3\n4" && q.dropped() == 1 && q.drain().empty(), "queue drop rule");
    EventQueue q2(2); q2.push("a", true); q2.push("b", true); q2.push("c", true);
    CHECK(q2.drain() == "a\nb" && q2.dropped() == 1, "a queue of critical records refuses the newest");
  }
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
  std::atomic<bool> stop{false}; std::atomic<int> nonfinite{0}; std::atomic<int64_t> frames{0}; std::atomic<float> peak{0}; std::atomic<double> sq{0};
  std::thread audio([&] {   // the fake device callback: 480-frame blocks, a little faster than real time
    std::vector<float> o(2 * 480); int64_t wall = 1000000000LL;
    while (!stop.load()) {
      E.render(o.data(), 480, wall); wall += 10000000LL;
      float pk = 0; for (float v : o) { if (!std::isfinite(v)) nonfinite++; else pk = std::fmax(pk, std::fabs(v)); }
      float cur = peak.load(); if (pk > cur) peak.store(pk);
      { double s = 0; for (float v : o) if (std::isfinite(v)) s += (double)v * v; sq.store(sq.load() + s); }
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
  { const std::string ev = E.drainEvents(); std::printf("events: %zu bytes, dropped %zu\n", ev.size(), E.eventsDropped());
    for (const char* t : {"clock", "pos", "meters", "state", "song"}) CHECK(ev.find(std::string("\"t\":\"") + t + "\"") != std::string::npos, "no %s event in the drained queue", t);
    CHECK(ev.size() > 2 && ev[0] == '{' && ev.back() == '}' && ev.find("}\n{") != std::string::npos, "events are JSON objects, one per line"); }
  { // H1c: the shell's ducking gain (audio focus): energy over two seconds at unity, then at 0.25 (after its 150 ms ramp), then back
    auto window = [&] { const double a = sq.load(); const int64_t f0 = frames.load(); while (frames.load() < f0 + 96000) std::this_thread::sleep_for(std::chrono::milliseconds(20)); return std::sqrt((sq.load() - a) / (double)(frames.load() - f0)); };
    const double unity = window();
    E.setDuck(0.25f); std::this_thread::sleep_for(std::chrono::milliseconds(400));
    const double ducked = window();
    E.setDuck(1.f); std::this_thread::sleep_for(std::chrono::milliseconds(400));
    const double back = window();
    std::printf("duck: rms unity %.4f, ducked %.4f (x%.3f), back %.4f\n", unity, ducked, unity > 0 ? ducked / unity : 0.0, back);
    CHECK(unity > 1e-4 && ducked < unity * 0.5 && ducked > unity * 0.08 && back > unity * 0.5, "ducking gain: %.4f -> %.4f -> %.4f", unity, ducked, back);
    CHECK(E.duck() == 1.f, "duck target back at unity"); }
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
