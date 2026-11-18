// U31: the live runner of the …chains node (ChainsLive, core/src/chains/clive.cpp): latency, sample-exact events, and the live output equal to the offline render bit for bit at every host block size.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "chains/chains.h"
#include "fx/pumpfx.h"
#include "rt/jsmath.h"
#include "rt/json.h"

using namespace thunder;
using namespace thunder::chains;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)
struct Doc { JsonDoc d; Json r; explicit Doc(const std::string& t) { if (!d.parse(t)) std::printf("bad test json: %s\n", t.c_str()); r = d.root(); } };

static std::vector<float> noise(int n, uint32_t seed, float amp) { std::vector<float> v((size_t)n); uint32_t s = seed; for (float& x : v) { s = s * 1664525u + 1013904223u; x = ((float)(s >> 8) / 16777216.f - .5f) * 2 * amp; } return v; }
static std::vector<float> tone(int n, double f, float amp) { std::vector<float> v((size_t)n); for (int i = 0; i < n; i++) v[(size_t)i] = (float)(amp * js_sin(2 * 3.141592653589793 * f * i / 48000)); return v; }

// a node patch with every kind of input: Track In 1 and 2 (feed), VCF, LFO, VCA, a Sidechain Comp keyed by a track's tap, Sidechain In / Out, two Chain Outs
static const char* kPatch =
  "{\"modules\":[{\"id\":\"a\",\"type\":\"trackIn\",\"params\":{\"track\":1}},{\"id\":\"b\",\"type\":\"trackIn\",\"params\":{\"track\":2}},{\"id\":\"f\",\"type\":\"vcf\",\"params\":{\"cutoff\":0.5,\"res\":0.4}},"
  "{\"id\":\"l\",\"type\":\"lfo\",\"params\":{\"rate\":3,\"depth\":0.8}},{\"id\":\"g\",\"type\":\"vca\",\"params\":{\"gain\":0.7}},{\"id\":\"k\",\"type\":\"sccomp\",\"params\":{\"sck\":10,\"thresh\":0.3,\"ratio\":4}},"
  "{\"id\":\"i\",\"type\":\"scin\",\"params\":{\"sck\":2}},{\"id\":\"o\",\"type\":\"scout\",\"params\":{\"snd\":2}},{\"id\":\"c\",\"type\":\"chainOut\",\"params\":{\"track\":1,\"level\":1}},{\"id\":\"d\",\"type\":\"chainOut\",\"params\":{\"track\":1,\"level\":0.3}}],"
  "\"cables\":[{\"from\":[\"a\",\"out\"],\"to\":[\"f\",\"in\"]},{\"from\":[\"i\",\"out\"],\"to\":[\"f\",\"p:cutoff\"]},{\"from\":[\"f\",\"out\"],\"to\":[\"g\",\"in\"]},{\"from\":[\"l\",\"out\"],\"to\":[\"g\",\"cv\"]},{\"from\":[\"g\",\"out\"],\"to\":[\"k\",\"in\"]},"
  "{\"from\":[\"k\",\"out\"],\"to\":[\"c\",\"in\"]},{\"from\":[\"k\",\"outR\"],\"to\":[\"c\",\"inR\"]},{\"from\":[\"b\",\"out\"],\"to\":[\"d\",\"in\"]},{\"from\":[\"a\",\"out\"],\"to\":[\"o\",\"in\"]}]}";
static const char* kPatch2 =
  "{\"modules\":[{\"id\":\"a\",\"type\":\"trackIn\",\"params\":{\"track\":1}},{\"id\":\"g\",\"type\":\"vca\",\"params\":{\"gain\":0.4}},{\"id\":\"m\",\"type\":\"mixer\"},{\"id\":\"c\",\"type\":\"chainOut\",\"params\":{\"track\":1,\"level\":0.9}}],"
  "\"cables\":[{\"from\":[\"a\",\"out\"],\"to\":[\"g\",\"in\"]},{\"from\":[\"g\",\"out\"],\"to\":[\"c\",\"in\"]},{\"from\":[\"g\",\"outR\"],\"to\":[\"c\",\"inR\"]}],\"transport\":{\"tempo\":97,\"playing\":true}}";

static void ev(std::vector<ChainsEvent>& v, ChainsEvent::Kind k, int64_t at, double a, double b = 0, const char* id = "", const char* name = "") {
  ChainsEvent e; e.kind = k; e.at = at; e.a = a; e.b = b; std::strcpy(e.id, id); std::strcpy(e.name, name); v.push_back(e);
}
static std::vector<ChainsEvent> events() {
  std::vector<ChainsEvent> v;
  ev(v, ChainsEvent::Param, 333, 0.9, 0, "g", "gain"); ev(v, ChainsEvent::Param, 128, 0.3, 0, "f", "cutoff"); ev(v, ChainsEvent::Param, 129, 0.35, 0, "f", "cutoff"); ev(v, ChainsEvent::Param, 1000, 0.5, 0, "c", "level");
  ev(v, ChainsEvent::Playing, 700, 1); ev(v, ChainsEvent::Tempo, 2000, 100); ev(v, ChainsEvent::Bypass, 1500, 1, 1); ev(v, ChainsEvent::Bypass, 2400, 1, -1); ev(v, ChainsEvent::Bypass, 2401, 2, 2);
  ev(v, ChainsEvent::Param, 2500, 1, 0, "c", "track"); ev(v, ChainsEvent::Param, 2600, 1, 0, "nope", "x"); ev(v, ChainsEvent::Param, 2700, 1, 0, "g", "nope");
  ev(v, ChainsEvent::Patch, 3001, 0); ev(v, ChainsEvent::Param, 3500, 0.2, 0, "g", "gain"); ev(v, ChainsEvent::Param, 3500, 0.6, 0, "g", "gain");
  return v;
}
static const ChainsLiveConfig kCfg = [] { ChainsLiveConfig c; c.taps = 2; c.sc = true; c.sends = true; c.feeds = 1; return c; }();

struct Inputs { std::vector<std::vector<float>> ch; };
static Inputs makeInputs(int frames) {
  Inputs in; in.ch.push_back(noise(frames, 1, .4f)); in.ch.push_back(tone(frames, 330, .5f)); in.ch.push_back(noise(frames, 3, .3f)); in.ch.push_back(noise(frames, 4, .3f));   // Track In L, R, sc L, R
  for (int t = 0; t < 2; t++) in.ch.push_back(tone(frames, 200 + 77 * t, .6f));                                                                                       // taps
  for (int k = 0; k < 8; k++) in.ch.push_back(noise(frames, 10 + (uint32_t)k, .25f));                                                                                // sends
  in.ch.push_back(tone(frames, 440, .3f)); in.ch.push_back(noise(frames, 21, .2f));                                                                                    // feed 0: L, R
  return in;
}
// the live runner fed with host blocks of the given sizes; the output of each channel (L, R, sends 0..7) as the host heard it
static bool runLive(const Inputs& in, int hostFrames, const std::vector<int>& sizes, std::vector<std::vector<float>>& out, int64_t* enginePos, ChainsLiveStats* st) {
  Host host; host.setRandom(Host::Rnd::Xorshift, 0x9e3779b9u);
  ChainsLive live(48000, 0, kCfg, &host); if (!live.valid()) return false;
  Doc p1(kPatch), p2(kPatch2); const std::vector<int> bm = {1}, tm = {0, 1}; live.node().engine().setBusMap(bm.data(), 1); live.node().engine().setTapMap(tm.data(), 2);
  if (!live.node().engine().loadPatch(p1.r)) return false;
  if (!live.preparePatch(p2.r, 3001)) return false;   // (the patch event of events() is the one that swaps; this prepares the slot it names)
  for (const ChainsEvent& e : events()) if (e.kind != ChainsEvent::Patch) live.post(e);
  out.assign(10, std::vector<float>((size_t)hostFrames, 0.f));
  size_t si = 0; int done = 0;
  while (done < hostFrames) {
    const int n = std::min(sizes[si++ % sizes.size()], hostFrames - done);
    std::vector<const float*> taps = {&in.ch[4][(size_t)done], &in.ch[5][(size_t)done]}, sends, feeds = {&in.ch[14][(size_t)done], &in.ch[15][(size_t)done]};
    for (int k = 0; k < 8; k++) sends.push_back(&in.ch[(size_t)(6 + k)][(size_t)done]);
    NodeIn ni; ni.inL = &in.ch[0][(size_t)done]; ni.inR = &in.ch[1][(size_t)done]; ni.scL = &in.ch[2][(size_t)done]; ni.scR = &in.ch[3][(size_t)done]; ni.taps = taps.data(); ni.nTaps = 2; ni.sends = sends.data(); ni.nSends = 8; ni.feeds = feeds.data(); ni.nFeeds = 1;
    std::vector<float*> so; for (int k = 0; k < 8; k++) so.push_back(&out[(size_t)(2 + k)][(size_t)done]);
    NodeOut no; no.L = &out[0][(size_t)done]; no.R = &out[1][(size_t)done]; no.sends = so.data(); no.nSends = 8;
    live.process(ni, no, n); live.service(); done += n;
  }
  *enginePos = live.enginePos(); *st = live.stats(); return true;
}
static bool runOffline(const Inputs& in, int64_t frames, std::vector<std::vector<float>>& out) {
  Host host; host.setRandom(Host::Rnd::Xorshift, 0x9e3779b9u); Doc p1(kPatch), p2(kPatch2); const std::vector<int> bm = {1}, tm = {0, 1};
  return ChainsLive::renderOffline(48000, 0, kCfg, &host, events(), {p2.r}, in.ch, frames, out, p1.r, &bm, &tm);
}
static void testEqualsOffline() {
  const int H = 6000; const Inputs in = makeInputs(H);
  std::vector<std::vector<float>> off;
  const std::vector<std::pair<const char*, std::vector<int>>> sched = {{"128", {128}}, {"64", {64}}, {"17", {17}}, {"1", {1}}, {"uneven", {100, 29, 128, 3, 255, 61, 1, 7, 4096 % 311}}};
  for (auto& s : sched) {
    std::vector<std::vector<float>> out; int64_t pos = 0; ChainsLiveStats st;
    CHECK(runLive(in, H, s.second, out, &pos, &st), "live (%s)", s.first);
    if (off.empty()) CHECK(runOffline(in, H, off), "offline");
    // engine sample e reaches the host at e + latency; compared where the live runner has rendered (a segment the host has not filled yet is not)
    const int64_t upto = std::min<int64_t>(pos, H - kChainsLatency); size_t bad = 0, cmp = 0; double energy = 0;
    for (int c = 0; c < 10; c++) for (int64_t e = 0; e < upto; e++) { const float a = out[(size_t)c][(size_t)(e + kChainsLatency)], b = off[(size_t)c][(size_t)e]; cmp++; energy += (double)b * b; if (std::memcmp(&a, &b, 4) != 0) bad++; }
    CHECK(bad == 0, "block size %s: %zu of %zu samples differ from the offline render", s.first, bad, cmp);
    CHECK(energy > 1, "block size %s: the render is not silent (energy %g)", s.first, energy);
    CHECK(st.lateEvents == 0 && st.droppedEvents == 0 && st.refusedPatches == 0 && st.failedBlocks == 0, "block size %s: late %d dropped %d refused %d failed %d", s.first, (int)st.lateEvents, (int)st.droppedEvents, (int)st.refusedPatches, (int)st.failedBlocks);
    CHECK(st.cutBlocks > 5, "block size %s: events cut blocks (%d)", s.first, (int)st.cutBlocks);
    std::printf("live == offline at host block size %-7s: %zu samples bit-identical, %d cut blocks of %d\n", s.first, cmp, (int)st.cutBlocks, (int)st.blocks);
  }
}
static void testLatencyAndExactEvents() {
  const int H = 1500; const Doc p("{\"modules\":[{\"id\":\"a\",\"type\":\"trackIn\",\"params\":{\"track\":1}},{\"id\":\"c\",\"type\":\"chainOut\",\"params\":{\"track\":1,\"level\":1}}],\"cables\":[{\"from\":[\"a\",\"out\"],\"to\":[\"c\",\"in\"]},{\"from\":[\"a\",\"outR\"],\"to\":[\"c\",\"inR\"]}]}");
  std::vector<float> one((size_t)H, 1.f), imp((size_t)H, 0.f); imp[10] = 1.f;
  for (int bs : {128, 17, 1, 300}) {
    Host host; ChainsLive live(48000, 0, ChainsLiveConfig(), &host); CHECK(live.node().engine().loadPatch(p.r), "patch");
    CHECK(live.postParam("c", "level", 0.0, 300), "post");
    std::vector<float> L((size_t)H), R((size_t)H); int done = 0;
    while (done < H) { const int n = std::min(bs, H - done); NodeIn in; in.inL = &one[(size_t)done]; in.inR = &one[(size_t)done]; NodeOut out; out.L = &L[(size_t)done]; out.R = &R[(size_t)done]; live.process(in, out, n); done += n; }
    bool ok = true; for (int j = 0; j < kChainsLatency; j++) if (L[(size_t)j] != 0) ok = false;
    CHECK(ok, "bs %d: silence until the latency has passed", bs);
    CHECK(L[(size_t)(kChainsLatency + 299)] == 1.f && L[(size_t)(kChainsLatency + 300)] == 0.f && R[(size_t)(kChainsLatency + 299)] == 1.f && R[(size_t)(kChainsLatency + 300)] == 0.f, "bs %d: the level change lands on its own sample (300): %g then %g", bs, L[(size_t)(kChainsLatency + 299)], L[(size_t)(kChainsLatency + 300)]);
    CHECK(L[(size_t)(kChainsLatency + 0)] == 1.f, "bs %d: the first sample arrives at the latency", bs);
  }
  { Host host; ChainsLive live(48000, 0, ChainsLiveConfig(), &host); CHECK(live.node().engine().loadPatch(p.r), "patch"); std::vector<float> L((size_t)H), R((size_t)H); NodeIn in; in.inL = imp.data(); in.inR = imp.data(); NodeOut out; out.L = L.data(); out.R = R.data(); live.process(in, out, H);
    CHECK(L[10 + kChainsLatency] == 1.f && L[9 + kChainsLatency] == 0.f && L[11 + kChainsLatency] == 0.f, "an impulse at sample 10 comes out at %d", 10 + kChainsLatency); }
}
static void testRefusals() {
  Host host; ChainsLive live(48000, 0, ChainsLiveConfig(), &host); Doc bad("{\"modules\":\"x\"}");
  CHECK(!live.preparePatch(bad.r, 0), "a refused patch is not posted"); CHECK(live.stats().refusedPatches == 1, "counted");
  ChainsEvent e; e.kind = ChainsEvent::Param; std::memset(e.id, 'x', sizeof e.id); CHECK(!live.post(e), "an id with no end is refused");
  CHECK(!live.postParam("a_module_id_that_is_much_too_long_to_fit_in_forty_chars", "p", 1, 0), "a long id is refused"); CHECK(live.stats().droppedEvents == 2, "counted: %d", (int)live.stats().droppedEvents);
  std::vector<float> a(64, 0.f), b(64, 0.f); NodeIn in; NodeOut out; out.L = a.data(); out.R = b.data(); live.process(in, out, 64);   // no inputs, no patch: silence, no crash
  for (float v : a) CHECK(v == 0, "silence");
}

// the pump modules' spare instances (decision (e)): a block whose output goes bad (a value outside +-8) is silent, the dead instance goes to the control thread and a spare, made there in advance, takes over at the next block
static void testPumpSpares() {
  if (!pump::available()) { std::printf("pump spares: no pump library in this build (skipped)\n"); return; }
  uint64_t bad0 = chains_pump_bad(), sw0 = chains_pump_swaps(), dry0 = chains_pump_dry(); int types = 0, bad = 0;
  for (int i = 0; i < 13; i++) {
    static const char* const T[13] = {"ptube", "psnap", "pgate", "pecho", "pspring", "pverb", "pgrain", "pvox", "ptune", "pcrush", "pswirl", "pstutter", "ptwang"};
    const WInfo* wi = cinfo(T[i]); if (!wi) continue;
    std::string ps; for (int k = 0; k < wi->np; k++) { char b[200]; std::snprintf(b, sizeof b, "%s\"%s\":%.17g", k ? "," : "", wi->params[k].name, wi->params[k].mx); ps += b; }
    Doc d(std::string("{\"modules\":[{\"id\":\"a\",\"type\":\"trackIn\",\"params\":{\"track\":1}},{\"id\":\"p\",\"type\":\"") + T[i] + "\",\"params\":{" + ps + "}},{\"id\":\"c\",\"type\":\"chainOut\",\"params\":{\"track\":1,\"level\":1}}],"
      "\"cables\":[{\"from\":[\"a\",\"out\"],\"to\":[\"p\",\"in\"]},{\"from\":[\"a\",\"outR\"],\"to\":[\"p\",\"inR\"]},{\"from\":[\"p\",\"out\"],\"to\":[\"c\",\"in\"]},{\"from\":[\"p\",\"outR\"],\"to\":[\"c\",\"inR\"]}]}");
    Host host; ChainsEngine e(48000, EngineOptions(), &host); if (!e.loadPatch(d.r)) { CHECK(false, "%s refused", T[i]); continue; }
    e.setInlineService(false); types++;
    const std::vector<float> s = noise(128 * 200, 9u + (uint32_t)i, 7.9f); uint64_t b0 = chains_pump_bad();
    for (int b = 0; b < 200; b++) { e.setTrackInputs(&s[(size_t)b * 128], &s[(size_t)b * 128], 1); e.process(128); e.serviceSpares(); }
    if (chains_pump_bad() > b0) { bad++; std::printf("  pump module that went bad: %s\n", T[i]); }
  }
  std::printf("pump spares: %d pump modules run on a +-7.9 input with every parameter at its maximum: %d went bad at least once; %llu bad blocks, %llu spares swapped in, %llu bad blocks with no spare ready\n", types, bad,
    (unsigned long long)(chains_pump_bad() - bad0), (unsigned long long)(chains_pump_swaps() - sw0), (unsigned long long)(chains_pump_dry() - dry0));
  CHECK(chains_pump_swaps() - sw0 >= chains_pump_bad() - bad0 - (uint64_t)types, "a bad block is followed by a spare taking over");
}

int main() {
  testPumpSpares();
  testEqualsOffline(); testLatencyAndExactEvents(); testRefusals();
  std::printf(fails ? "test_chains_live: %d FAILED\n" : "test_chains_live: ok\n", fails);
  return fails ? 1 : 0;
}
