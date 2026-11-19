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
  Host host; ChainsLive live(48000, 0, ChainsLiveConfig(), &host); Doc bad("{\"modules\":[null]}");   /* (the JS throws on a null module) */
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
    for (int b = 0; b < 200; b++) { e.setTrackInputs(&s[(size_t)b * 128], &s[(size_t)b * 128], 1); e.process(128); e.serviceSpares();
      const float* po = e.outBuf("p", "out"); for (int k = 0; po && k < 128; k++) if (!(po[k] > -8 && po[k] < 8)) { CHECK(false, "%s: an output outside +-8 reached the bus in block %d", T[i], b); break; } }
    if (chains_pump_bad() > b0) { bad++; std::printf("  pump module that went bad: %s\n", T[i]); }
  }
  std::printf("pump spares: %d pump modules run on a +-7.9 input with every parameter at its maximum: %d went bad at least once; %llu bad blocks, %llu spares swapped in, %llu bad blocks with no spare ready\n", types, bad,
    (unsigned long long)(chains_pump_bad() - bad0), (unsigned long long)(chains_pump_swaps() - sw0), (unsigned long long)(chains_pump_dry() - dry0));
  CHECK(bad > 0, "some pump module went bad on the hot input, so the spare path was exercised");
  CHECK(chains_pump_swaps() - sw0 >= chains_pump_bad() - bad0 - (uint64_t)types, "a bad block is followed by a spare taking over");
}


// Chain Out's track is held to 1..128 for the run list (focus): 500 is track 128, -5 and 0 are track 1; the focus decides who runs it
static void testChainOutTrackClamp() {
  const int tr[3] = {500, -5, 0}, foc[3] = {127, 0, 0}, other[3] = {0, 1, 1};
  for (int i = 0; i < 3; i++) {
    Doc d(std::string("{\"modules\":[{\"id\":\"a\",\"type\":\"trackIn\"},{\"id\":\"c\",\"type\":\"chainOut\",\"params\":{\"track\":") + std::to_string(tr[i]) + "}}],\"cables\":[{\"from\":[\"a\",\"out\"],\"to\":[\"c\",\"in\"]}]}");
    EngineOptions o; o.focus = foc[i]; Host h; ChainsEngine e(48000, o, &h); CHECK(e.loadPatch(d.r), "clamp patch"); CHECK(e.isActive("c"), "Chain Out track %d is run by the node of track %d", tr[i], foc[i] + 1);
    EngineOptions o2; o2.focus = other[i] ? 1 : 5; Host h2; ChainsEngine e2(48000, o2, &h2); CHECK(e2.loadPatch(d.r), "clamp patch 2"); CHECK(!e2.isActive("c"), "Chain Out track %d is not run by another node", tr[i]);
  }
}

// Resample: one take of one bar at the first bar line, the bars / slices tables, the copy of the input; no second take until rec is toggled
static void testResampleTake() {
  Doc d("{\"modules\":[{\"id\":\"a\",\"type\":\"trackIn\"},{\"id\":\"s\",\"type\":\"resample\",\"params\":{\"rec\":1,\"bars\":0,\"pad\":5,\"slices\":4}},{\"id\":\"c\",\"type\":\"chainOut\"}],"
        "\"cables\":[{\"from\":[\"a\",\"out\"],\"to\":[\"s\",\"in\"]},{\"from\":[\"s\",\"out\"],\"to\":[\"c\",\"in\"]}]}");
  EngineOptions o; o.transport.playing = true; o.transport.tempo = 120; Host h; ChainsEngine e(48000, o, &h); CHECK(e.loadPatch(d.r), "resample patch");
  const int N = 128 * 900; const std::vector<float> in = noise(N, 77, .5f); int takes = 0; ResampleTake first; std::memset(&first, 0, sizeof first); std::vector<float> keep;
  for (int b = 0; b < 900; b++) {
    e.setTrackInputs(&in[(size_t)b * 128], &in[(size_t)b * 128], 1); e.process(128);
    ResampleTake tk; while (e.popTake(&tk)) { if (!takes++) { first = tk; keep.assign(tk.L, tk.L + tk.n); } e.releaseTake(tk); }
  }
  CHECK(takes == 1, "one take (got %d)", takes);
  if (takes) {
    CHECK(first.n == 96000 && first.bars == 1 && first.slices == 32 && first.pad == 5 && first.tempo == 120 && first.sr == 48000 && std::strcmp(first.id, "s") == 0, "take: n %d bars %d slices %d pad %d tempo %g id %s", first.n, first.bars, first.slices, first.pad, first.tempo, first.id);
    bool same = (int)keep.size() == 96000; for (int i = 0; same && i < 96000; i++) if (keep[(size_t)i] != in[(size_t)i]) same = false; CHECK(same, "the take is the input from the bar line");
  }
}
// the live input's right side falls back to the left when the host gives none: the same output as the host giving the left twice
static void testMonoFallback() {
  Doc d(kPatch2); const int H = 4000; const std::vector<float> x = noise(H, 5, .5f); std::vector<float> L1((size_t)H), R1((size_t)H), L2((size_t)H), R2((size_t)H);
  for (int k = 0; k < 2; k++) {
    Host h; ChainsLive live(48000, 0, ChainsLiveConfig(), &h); CHECK(live.node().engine().loadPatch(d.r), "patch");
    NodeIn in; in.inL = x.data(); in.inR = k ? x.data() : nullptr; NodeOut out; out.L = k ? L2.data() : L1.data(); out.R = k ? R2.data() : R1.data(); live.process(in, out, H);
  }
  CHECK(L1 == L2 && R1 == R2, "no right input equals the left twice"); double s = 0; for (int i = 0; i < H; i++) s += std::fabs(R1[(size_t)i]); CHECK(s > 1, "the right side is not silent");
}
// the sidechain compressor at ratio 1 changes nothing (the ratio's floor is 1)
static void testCompRatioOne() {
  Doc d("{\"modules\":[{\"id\":\"a\",\"type\":\"trackIn\"},{\"id\":\"q\",\"type\":\"sccomp\",\"params\":{\"threshold\":0,\"ratio\":1}},{\"id\":\"c\",\"type\":\"chainOut\"}],\"cables\":[{\"from\":[\"a\",\"out\"],\"to\":[\"q\",\"in\"]},{\"from\":[\"q\",\"out\"],\"to\":[\"c\",\"in\"]}]}");
  Host h; ChainsEngine e(48000, EngineOptions(), &h); CHECK(e.loadPatch(d.r), "comp patch"); const std::vector<float> x = noise(128 * 20, 3, .5f); double worst = 0;
  for (int b = 0; b < 20; b++) { e.setTrackInputs(&x[(size_t)b * 128], &x[(size_t)b * 128], 1); e.process(128); const float* o = e.outBuf("q", "out"); for (int i = 0; i < 128; i++) worst = std::max(worst, (double)std::fabs(o[i] - x[(size_t)b * 128 + (size_t)i])); }
  CHECK(worst == 0, "ratio 1 leaves the sound alone (worst %g)", worst);
}

// ---- U31 verifier follow-up: the live feed's mono fallback, setSidechain, the tempo clamp, the Track In bound, Chain Out's level, the queue rules and the mailbox ----
// the other tracks' feed: a right side the host does not give is the left, as a host giving the left twice (the same output bit for bit)
static void testFeedMonoFallback() {
  const int H = 3000; const Inputs in = makeInputs(H); std::vector<std::vector<float>> o[2];
  for (int k = 0; k < 2; k++) {
    Host host; ChainsLive live(48000, 0, kCfg, &host); Doc p(kPatch); const std::vector<int> bm = {1}, tm = {0, 1}; live.node().engine().setBusMap(bm.data(), 1); live.node().engine().setTapMap(tm.data(), 2);
    CHECK(live.node().engine().loadPatch(p.r), "patch"); o[k].assign(10, std::vector<float>((size_t)H, 0.f));
    std::vector<const float*> taps = {in.ch[4].data(), in.ch[5].data()}, sends, feeds = {in.ch[14].data(), k ? in.ch[14].data() : nullptr}; for (int s = 0; s < 8; s++) sends.push_back(in.ch[(size_t)(6 + s)].data());
    NodeIn ni; ni.inL = in.ch[0].data(); ni.inR = in.ch[1].data(); ni.scL = in.ch[2].data(); ni.scR = in.ch[3].data(); ni.taps = taps.data(); ni.nTaps = 2; ni.sends = sends.data(); ni.nSends = 8; ni.feeds = feeds.data(); ni.nFeeds = 1;
    std::vector<float*> so; for (int s = 0; s < 8; s++) so.push_back(o[k][(size_t)(2 + s)].data()); NodeOut no; no.L = o[k][0].data(); no.R = o[k][1].data(); no.sends = so.data(); no.nSends = 8; live.process(ni, no, H);
  }
  CHECK(o[0] == o[1], "a feed with no right side equals the left twice"); double e = 0; for (float v : o[0][1]) e += std::fabs(v); CHECK(e > 1, "the right output carries the feed (%g)", e);
}
// setSidechain: no right side is the left; both given are kept; none is silence and off
static void testSetSidechain() {
  Host h; ChainsEngine e(48000, EngineOptions(), &h); const std::vector<float> a = noise(128, 1, .5f), b = noise(128, 2, .5f);
  e.setSidechain(a.data(), nullptr, 128); CHECK(std::memcmp(e.sidechainBuffer(0), a.data(), 512) == 0 && std::memcmp(e.sidechainBuffer(1), a.data(), 512) == 0, "no right side: the left on both");
  e.setSidechain(a.data(), b.data(), 128); CHECK(std::memcmp(e.sidechainBuffer(0), a.data(), 512) == 0 && std::memcmp(e.sidechainBuffer(1), b.data(), 512) == 0, "both given: kept");
  e.setSidechain(nullptr, nullptr, 128); bool z = true; for (int i = 0; i < 128; i++) if (e.sidechainBuffer(0)[i] != 0 || e.sidechainBuffer(1)[i] != 0) z = false; CHECK(z, "none: silence");
  e.setSidechain(a.data(), nullptr, 40); bool t = std::memcmp(e.sidechainBuffer(1), a.data(), 160) == 0; for (int i = 40; i < 128; i++) if (e.sidechainBuffer(1)[i] != 0) t = false; CHECK(t, "a short block: the rest silent");
}
// a hostile tempo is held to +-1e5 BPM: Resample's bar (4 * 60 / tempo seconds, tempo floored at 20 there) and the tempo it records show the value the modules were given
static void testTempoClamp() {
  Doc d("{\"modules\":[{\"id\":\"a\",\"type\":\"trackIn\"},{\"id\":\"s\",\"type\":\"resample\",\"params\":{\"rec\":1,\"bars\":0,\"pad\":0,\"slices\":0}},{\"id\":\"c\",\"type\":\"chainOut\"}],\"cables\":[{\"from\":[\"a\",\"out\"],\"to\":[\"s\",\"in\"]},{\"from\":[\"s\",\"out\"],\"to\":[\"c\",\"in\"]}]}");
  const double tempi[4] = {1e30, 1e5, 2e5, -1e30}, want[4] = {1e5, 1e5, 1e5, -1e5};
  for (int k = 0; k < 4; k++) {
    EngineOptions o; o.transport.playing = true; o.transport.tempo = tempi[k]; Host h; ChainsEngine e(48000, o, &h); CHECK(e.loadPatch(d.r), "resample patch"); const std::vector<float> x = noise(128 * 20, 4, .5f); int got = 0; ResampleTake first; std::memset(&first, 0, sizeof first); const int NB = k == 3 ? 4700 : 20;   // (a bar at tempo 20 is 576000 samples)
    for (int b = 0; b < NB; b++) { const size_t o = (size_t)(b % 20) * 128; e.setTrackInputs(&x[o], &x[o], 1); e.process(128); ResampleTake tk; while (e.popTake(&tk)) { if (!got++) first = tk; e.releaseTake(tk); } }
    CHECK(got >= 1, "tempo %g: a take", tempi[k]);
    if (got) { const int bar = (int)std::lround(4.0 * 60 / std::max(20.0, want[k]) * 48000); CHECK(first.tempo == want[k] && first.n == bar, "tempo %g: recorded tempo %g (want %g), take of %d samples (want %d)", tempi[k], first.tempo, want[k], first.n, bar); }
  }
}
// Track In asks for a track the host's buffer does not hold: silence (the buffer's next block is not read)
static void testTrackInBound() {
  Doc d("{\"modules\":[{\"id\":\"a\",\"type\":\"trackIn\",\"params\":{\"track\":2}},{\"id\":\"c\",\"type\":\"chainOut\"}],\"cables\":[{\"from\":[\"a\",\"out\"],\"to\":[\"c\",\"in\"]},{\"from\":[\"a\",\"outR\"],\"to\":[\"c\",\"inR\"]}]}");
  const std::vector<float> x = noise(128 * 3, 6, .5f); Host h; ChainsEngine e(48000, EngineOptions(), &h); CHECK(e.loadPatch(d.r), "patch");
  e.setTrackInputs(x.data(), x.data(), 1); e.process(128); const float* o = e.outBuf("a", "out"); bool z = true; for (int i = 0; i < 128; i++) if (o[i] != 0) z = false; CHECK(z, "track 2 of a 1-track buffer is silent");
  e.setTrackInputs(x.data(), x.data(), 2); e.process(128); o = e.outBuf("a", "out"); bool same = true; for (int i = 0; i < 128; i++) if (o[i] != x[(size_t)(128 + i)]) same = false; CHECK(same, "track 2 of a 2-track buffer is its second block");
}
// Chain Out's level is a plain gain, never held to 0..1 (the JS adds in * level): 3, -1, 0.25 and 1e30 on a constant
static void testChainOutLevel() {
  const double lv[4] = {3, -1, 0.25, 1e30};
  for (int k = 0; k < 4; k++) {
    Doc d(std::string("{\"modules\":[{\"id\":\"a\",\"type\":\"trackIn\"},{\"id\":\"c\",\"type\":\"chainOut\",\"params\":{\"level\":") + std::to_string(lv[k]) + "}}],\"cables\":[{\"from\":[\"a\",\"out\"],\"to\":[\"c\",\"in\"]},{\"from\":[\"a\",\"outR\"],\"to\":[\"c\",\"inR\"]}]}");
    const std::vector<float> x(128 * 2, 0.5f); Host h; ChainsEngine e(48000, EngineOptions(), &h); CHECK(e.loadPatch(d.r), "patch"); e.setTrackInputs(x.data(), x.data(), 1); e.process(128);
    const float* L = e.bus(1); const float* R = e.busR(1); const float want = (float)((double)0.5f * lv[k]);
    CHECK(L && R && L[5] == want && R[5] == want, "Chain Out level %g: bus %g (want %g)", lv[k], L ? L[5] : 0.f, want);
  }
}

// the host's blocks of 128 frames with no input, to let the queue drain and the events land
static void idle(ChainsLive& live, int blocks) { std::vector<float> a(128), b(128); NodeIn in; NodeOut out; out.L = a.data(); out.R = b.data(); for (int i = 0; i < blocks; i++) live.process(in, out, 128); }
static void flood(ChainsLive& live, ChainsEvent::Kind k, int n, int64_t at, double a = 0, bool names = false) {   // n events of one kind, in rounds the queue can hold
  for (int i = 0; i < n;) { int m = 0; for (; i < n && m < 700; i++, m++) { ChainsEvent e; e.kind = k; e.at = at; e.a = a; if (names) { std::snprintf(e.id, sizeof e.id, "x"); std::snprintf(e.name, sizeof e.name, "n%d", i); } live.post(e); } idle(live, 1); }
}
static Doc* sPatch2() { static Doc d(kPatch2); return &d; }
static void testQueueRules() {
  const int64_t FAR = 1000000000;
  { // (a) the pending array full of parameter events: a later value of the same parameter replaces the earlier one; a new parameter is refused first; Playing / Bypass / Tempo are never lost
    Host h; ChainsLive live(48000, 0, ChainsLiveConfig(), &h); Doc p(kPatch2); CHECK(live.node().engine().loadPatch(p.r), "patch");
    ChainsEngine& E = live.node().engine(); Transport t = E.transport(); t.playing = true; E.setTransport(t);
    flood(live, ChainsEvent::Param, kChainsPending - kChainsReserve, FAR, 0.5, true); CHECK(live.stats().droppedEvents == 0 && live.stats().coalescedEvents == 0, "a full array: nothing lost yet");
    CHECK(live.postParam("x", "n5", 0.7, FAR), "post the same parameter"); idle(live, 1); CHECK(live.stats().coalescedEvents == 1 && live.stats().droppedEvents == 0, "same (module, parameter): coalesced (%d), nothing dropped (%d)", (int)live.stats().coalescedEvents, (int)live.stats().droppedEvents);
    CHECK(live.postParam("x", "brand_new", 0.7, FAR), "post a new parameter"); idle(live, 1); CHECK(live.stats().droppedEvents == 1, "a new parameter in a full array is the one refused (%d)", (int)live.stats().droppedEvents);
    const int64_t now = live.enginePos(); ChainsEvent e; e.kind = ChainsEvent::Bypass; e.at = now + 200; e.a = 3; e.b = 1; CHECK(live.post(e), "bypass on"); e.at = now + 400; e.b = -1; CHECK(live.post(e), "bypass release");
    e = ChainsEvent(); e.kind = ChainsEvent::Playing; e.at = now + 600; e.a = 0; CHECK(live.post(e), "playing off");
    idle(live, 8); CHECK(live.bypassSwitch(3) == -1, "the Bypass release landed (%g)", live.bypassSwitch(3)); CHECK(!E.transport().playing, "Playing=0 landed");
  }
  { // the array full of events that must be kept (Tempo): Playing=0 and the Bypass release make room by replacing a Tempo a later Tempo replaces anyway
    Host h; ChainsLive live(48000, 0, ChainsLiveConfig(), &h); Doc p(kPatch2); CHECK(live.node().engine().loadPatch(p.r), "patch"); ChainsEngine& E = live.node().engine(); Transport t = E.transport(); t.playing = true; E.setTransport(t);
    flood(live, ChainsEvent::Tempo, 4200, FAR, 100); const int64_t now = live.enginePos();
    ChainsEvent e; e.kind = ChainsEvent::Bypass; e.at = now + 200; e.a = 4; e.b = 1; live.post(e); e.at = now + 400; e.b = -1; live.post(e); e = ChainsEvent(); e.kind = ChainsEvent::Playing; e.at = now + 600; e.a = 0; live.post(e);
    idle(live, 8); CHECK(live.bypassSwitch(4) == -1, "(Tempo flood) the Bypass release landed (%g)", live.bypassSwitch(4)); CHECK(!E.transport().playing, "(Tempo flood) Playing=0 landed"); CHECK(live.stats().coalescedEvents > 0, "tempo events gave way (%d)", (int)live.stats().coalescedEvents);
  }
  { // (b) the array full of Playing events (the last one posted wins: an earlier one gives way): a Patch event is NOT dropped, and every delivered patch lands and is collected
    Host h; ChainsLive live(48000, 0, ChainsLiveConfig(), &h); Doc p(kPatch2); CHECK(live.node().engine().loadPatch(p.r), "patch");
    flood(live, ChainsEvent::Playing, 4200, FAR, 1); CHECK(live.stats().droppedEvents == 0, "a Playing flood loses nothing (%d): the last state wins", (int)live.stats().droppedEvents); CHECK(live.stats().coalescedEvents > 0, "earlier Playing events gave way (%d)", (int)live.stats().coalescedEvents);
    int ok = 0; for (int i = 0; i < 30; i++) { if (live.preparePatch(sPatch2()->r, live.enginePos() + 10)) ok++; idle(live, 1); if (i % 5 == 4) live.service(); }
    CHECK(live.patchesInFlight() <= kChainsSlots - 2, "in flight %d", live.patchesInFlight()); CHECK(ok >= 20, "after every service() a patch can be prepared again (%d of 30 delivered)", ok);
    CHECK(live.stats().droppedEvents == 0, "no patch event was dropped (%d)", (int)live.stats().droppedEvents);
    live.service(); CHECK(live.patchesInFlight() == 0, "service() freed every patch the audio thread gave back (%d left)", live.patchesInFlight());
  }
  { // F: a Playing flood, then Playing = 0: the stop lands, the engine ends stopped (the last state posted wins)
    Host h; ChainsLive live(48000, 0, ChainsLiveConfig(), &h); Doc p(kPatch2); CHECK(live.node().engine().loadPatch(p.r), "patch"); ChainsEngine& E = live.node().engine();
    flood(live, ChainsEvent::Playing, 4200, FAR, 1); ChainsEvent e; e.kind = ChainsEvent::Playing; e.at = live.enginePos() + 300; e.a = 0; CHECK(live.post(e), "post the stop"); idle(live, 6);
    CHECK(!E.transport().playing, "Playing = 0 landed after a Playing flood"); CHECK(live.stats().droppedEvents == 0, "nothing dropped (%d)", (int)live.stats().droppedEvents);
  }
  { // E: a full array of parameters: a Playing event removes the oldest parameter event of ANOTHER parameter: that is a loss (droppedParams), not a coalesce
    Host h; ChainsLive live(48000, 0, ChainsLiveConfig(), &h); Doc p(kPatch2); CHECK(live.node().engine().loadPatch(p.r), "patch");
    for (int round = 0; round < 3; round++) flood(live, ChainsEvent::Param, 700, FAR, 0.5, true);   // fills pending to kChainsPending - kChainsReserve; the rest are refused
    const uint64_t c0 = live.stats().coalescedEvents; CHECK(live.stats().droppedParams == 0, "nothing removed yet");
    flood(live, ChainsEvent::Tempo, 400, FAR, 100); idle(live, 1);   // 256 + more events that must be kept: the array is filled, then parameters give way
    CHECK(live.stats().droppedParams > 0, "parameters removed to make room are counted as dropped (%d)", (int)live.stats().droppedParams); (void)c0;
  }
  { const int kMaxFlight = kChainsSlots - 2; // (c) service() never called: no more than kChainsSlots - 2 patches are in flight, the rest are refused (false), and one service() gives all the places back; each swap lands
    Host h; ChainsLive live(48000, 0, ChainsLiveConfig(), &h); Doc p(kPatch2); CHECK(live.node().engine().loadPatch(p.r), "patch"); int ok = 0, no = 0;
    for (int i = 0; i < 14; i++) { if (live.preparePatch(sPatch2()->r, live.enginePos() + 10)) ok++; else no++; idle(live, 2); }
    CHECK(ok == kMaxFlight && no == 14 - kMaxFlight, "14 prepares with no service(): %d delivered, %d refused (want %d / %d)", ok, no, kMaxFlight, 14 - kMaxFlight);
    CHECK(live.patchesInFlight() == kMaxFlight, "in flight %d", live.patchesInFlight()); CHECK(live.stats().refusedPatches == (uint64_t)no, "refusals counted (%d)", (int)live.stats().refusedPatches);
    live.service(); CHECK(live.patchesInFlight() == 0, "one service() collects the %d swapped-out patches (%d left)", kMaxFlight, live.patchesInFlight());
    CHECK(live.preparePatch(sPatch2()->r, live.enginePos() + 10), "after service() a patch is delivered again"); idle(live, 2); live.service(); CHECK(live.patchesInFlight() == 0, "and collected");
  }
  { // the queue filling up: a Param is refused with kChainsReserve places left, and those places take Playing / Bypass / Tempo / Patch events
    Host h; ChainsLive live(48000, 0, ChainsLiveConfig(), &h); ChainsEvent e; e.kind = ChainsEvent::Param; e.at = FAR; std::snprintf(e.id, sizeof e.id, "x"); std::snprintf(e.name, sizeof e.name, "p"); int np = 0; while (live.post(e)) np++;
    CHECK(np == kChainsQueue - kChainsReserve, "Param events fill the queue to %d (got %d)", kChainsQueue - kChainsReserve, np); CHECK(live.stats().droppedEvents == 1, "the refused Param is counted (%d)", (int)live.stats().droppedEvents);
    ChainsEvent k; k.kind = ChainsEvent::Bypass; k.at = FAR; k.a = 2; k.b = -1; int nk = 0; while (live.post(k)) nk++; CHECK(nk == kChainsReserve, "the reserve takes %d more events that must not be lost (got %d)", kChainsReserve, nk); }
  { // preparePatch false when the swap cannot be queued (the queue full): the patch is freed, its slot is free
    Host h; ChainsLive live(48000, 0, ChainsLiveConfig(), &h); Doc p(kPatch2); CHECK(live.node().engine().loadPatch(p.r), "patch");
    ChainsEvent e; e.kind = ChainsEvent::Playing; e.at = FAR; e.a = 1; int n = 0; while (live.post(e)) n++; CHECK(n == kChainsQueue, "the queue holds %d", n);
    CHECK(!live.preparePatch(sPatch2()->r, 0), "queue full: false"); CHECK(live.patchesInFlight() == 0, "no slot kept (%d)", live.patchesInFlight());
    idle(live, 1); CHECK(live.preparePatch(sPatch2()->r, live.enginePos() + 5), "room again: true"); idle(live, 2); live.service(); CHECK(live.patchesInFlight() == 0, "collected");
  }
}
// a Bypass event for switch 0, 16, -1 or 100 is ignored: the output and the position are those of a run without it (an out-of-range write would hit the neighbouring members)
static void testBypassRange() {
  const int H = 2000; const std::vector<float> x = noise(H, 8, .5f); std::vector<float> o[2]; int64_t pos[2];
  for (int k = 0; k < 2; k++) {
    Host h; ChainsLive live(48000, 0, ChainsLiveConfig(), &h); Doc d(kPatch2); CHECK(live.node().engine().loadPatch(d.r), "patch");
    if (k) { const double sw[5] = {0, 16, -1, 100, -1e30}; for (double s : sw) { ChainsEvent e; e.kind = ChainsEvent::Bypass; e.at = 0; e.a = s; e.b = 0.1; live.post(e); }   /* (0.1: bits whose low part is not zero, so a stray write over a neighbouring 64-bit member shows in the output) */ }
    std::vector<float> L((size_t)H), R((size_t)H); NodeIn in; in.inL = x.data(); in.inR = x.data(); NodeOut out; out.L = L.data(); out.R = R.data(); live.process(in, out, H); o[k] = L; pos[k] = live.enginePos();
    for (int s = 1; s <= 15; s++) CHECK(live.bypassSwitch(s) == -1, "switch %d unchanged", s);
  }
  CHECK(o[0] == o[1] && pos[0] == pos[1], "out-of-range Bypass events change nothing (pos %lld / %lld)", (long long)pos[0], (long long)pos[1]);
}

// H: a Bypass crossfade is a 128-sample ramp whatever the cuts: an event that lands inside the ramp (a parameter set to the value it already has, on another module) cuts the block, and the output is bit-equal to the run
// without it (the engine used to finish the fade in the short block and jump to fully bypassed in the next: a click). Both directions (into bypass and out of it), and two cuts inside one ramp.
static void testBypassCut() {
  const int H = 1800; const std::vector<float> x = noise(H, 12, .5f);
  struct V { int64_t cutAt[2]; int ncut; };
  for (int dir = 0; dir < 2; dir++) {   // dir 0: into bypass (switch 1 -> 1), dir 1: bypass first, then out of it
    std::vector<float> o[4]; const V vs[4] = {{{0, 0}, 0}, {{10, 0}, 1}, {{10, 77}, 2}, {{127, 0}, 1}};
    for (int k = 0; k < 4; k++) {
      Host h; ChainsLive live(48000, 0, ChainsLiveConfig(), &h); Doc d(kPatch2); CHECK(live.node().engine().loadPatch(d.r), "patch");
      const int64_t S = 520; std::vector<ChainsEvent> v;
      if (dir == 0) ev(v, ChainsEvent::Bypass, S, 1, 1); else { ev(v, ChainsEvent::Bypass, 20, 1, 1); ev(v, ChainsEvent::Bypass, S, 1, 0); }
      for (int c = 0; c < vs[k].ncut; c++) ev(v, ChainsEvent::Param, S + vs[k].cutAt[c], 0.9, 0, "c", "level");   // c.level is 0.9 already
      for (const ChainsEvent& e : v) CHECK(live.post(e), "post");
      std::vector<float> L((size_t)H), R((size_t)H); NodeIn in; in.inL = x.data(); in.inR = x.data(); NodeOut out; out.L = L.data(); out.R = R.data(); live.process(in, out, H); o[k] = L;
      if (k) CHECK(live.stats().cutBlocks > 0, "the block was cut (%d)", (int)live.stats().cutBlocks);
    }
    for (int k = 1; k < 4; k++) CHECK(o[k] == o[0], "Bypass %s: an event inside the ramp (variant %d) changes the output", dir ? "release" : "on", k);
    double mx = 0; for (size_t i = 1; i < o[0].size(); i++) mx = std::max(mx, (double)std::fabs(o[0][i] - o[0][i - 1])); CHECK(mx < 1.0, "no jump in the output (%g)", mx);
  }
}

// the pump instances' dead handles: when service() is never called (the wiring condition broken) a module still never leaks or overflows: at most two handles die (the one that went bad and the spare that took over), then the
// module is dry (silent; counted) until service() makes a spare. The JS has no dry state (it remakes the instance on the audio thread); see docs/port/U31-NOTES.md "Pump instances"
static void testPumpNoService() {
  if (!pump::available()) { std::printf("pump without service(): no pump library in this build (skipped)\n"); return; }
  const WInfo* wi = cinfo("ptube"); std::string ps; for (int k = 0; k < wi->np; k++) { char b[200]; std::snprintf(b, sizeof b, "%s\"%s\":%.17g", k ? "," : "", wi->params[k].name, wi->params[k].mx); ps += b; }
  Doc d(std::string("{\"modules\":[{\"id\":\"a\",\"type\":\"trackIn\",\"params\":{\"track\":1}},{\"id\":\"p\",\"type\":\"ptube\",\"params\":{") + ps + "}},{\"id\":\"c\",\"type\":\"chainOut\",\"params\":{\"track\":1,\"level\":1}}],"
    "\"cables\":[{\"from\":[\"a\",\"out\"],\"to\":[\"p\",\"in\"]},{\"from\":[\"a\",\"outR\"],\"to\":[\"p\",\"inR\"]},{\"from\":[\"p\",\"out\"],\"to\":[\"c\",\"in\"]},{\"from\":[\"p\",\"outR\"],\"to\":[\"c\",\"inR\"]}]}");
  Host host; ChainsEngine e(48000, EngineOptions(), &host); CHECK(e.loadPatch(d.r), "patch"); e.setInlineService(false);
  const uint64_t o0 = chains_pump_overflow(), b0 = chains_pump_bad(), dry0 = chains_pump_dry(), s0 = chains_pump_swaps(); const std::vector<float> s = noise(128 * 300, 31u, 7.9f); bool range = true;
  for (int b = 0; b < 300; b++) { e.setTrackInputs(&s[(size_t)b * 128], &s[(size_t)b * 128], 1); e.process(128); const float* po = e.outBuf("p", "out"); for (int k = 0; po && k < 128; k++) if (!(po[k] > -8 && po[k] < 8)) range = false; }
  CHECK(range, "the output stays inside +-8 with no service()"); CHECK(chains_pump_overflow() == o0, "no dead handle overflowed (%d)", (int)(chains_pump_overflow() - o0));
  CHECK(chains_pump_bad() - b0 >= 2, "blocks went bad (%d)", (int)(chains_pump_bad() - b0)); CHECK(chains_pump_swaps() - s0 <= 1, "one spare took over (%d)", (int)(chains_pump_swaps() - s0)); CHECK(chains_pump_dry() - dry0 > 100, "then the module is dry, counted (%d)", (int)(chains_pump_dry() - dry0));
  const float* po = e.outBuf("p", "out"); bool zero = true; for (int k = 0; k < 128; k++) if (po[k] != 0) zero = false; CHECK(zero, "a dry module is silent");
  e.serviceSpares(); e.setTrackInputs(&s[0], &s[0], 1); e.process(128); e.serviceSpares(); e.process(128); CHECK(chains_pump_swaps() - s0 >= 2 || chains_pump_dry() - dry0 > 100, "service() makes a spare and the module runs again");
  std::printf("pump without service(): %llu bad blocks, %llu spare swaps, %llu dry blocks, overflow %llu\n", (unsigned long long)(chains_pump_bad() - b0), (unsigned long long)(chains_pump_swaps() - s0), (unsigned long long)(chains_pump_dry() - dry0), (unsigned long long)(chains_pump_overflow() - o0));
}

// post(): the longest id (39 characters) and name (23) fit with their end; one more is refused (a copy would run over the neighbouring field); a Param in the past is pulled up to the engine's position and counted
static void testPostLimits() {
  Host h; ChainsLive live(48000, 0, ChainsLiveConfig(), &h); const std::string id39(39, 'i'), id40(40, 'i'), nm23(23, 'n'), nm24(24, 'n');
  CHECK(live.postParam(id39.c_str(), nm23.c_str(), 1, 0), "39 / 23 characters fit"); CHECK(!live.postParam(id40.c_str(), "p", 1, 0), "a 40-character id is refused"); CHECK(!live.postParam("m", nm24.c_str(), 1, 0), "a 24-character name is refused");
  ChainsEvent e; e.kind = ChainsEvent::Param; std::memset(e.id, 'x', 39); e.id[39] = 0; std::memset(e.name, 'y', 23); e.name[23] = 0; CHECK(live.post(e), "post(): the longest id and name"); std::memset(e.name, 'y', 24); CHECK(!live.post(e), "post(): a name with no end is refused");
  idle(live, 4); const uint64_t late0 = live.stats().lateEvents; ChainsEvent p; p.kind = ChainsEvent::Playing; p.at = 10; p.a = 1; CHECK(live.post(p), "a Playing event in the past"); idle(live, 1);
  CHECK(live.stats().lateEvents == late0 + 1, "an event meant for sample 10 at position %lld is late (%d)", (long long)live.enginePos(), (int)(live.stats().lateEvents - late0)); CHECK(live.node().engine().transport().playing, "and still lands");
}
// a pump module's spare: made again by service() after a take-over even when no block has gone bad since (the take-over asks for the next spare)
static void testPumpSpareAfterTakeover() {
  if (!pump::available()) { std::printf("pump spare after a take-over: no pump library in this build (skipped)\n"); return; }
  const WInfo* wi = cinfo("ptube"); std::string ps; for (int k = 0; k < wi->np; k++) { char b[200]; std::snprintf(b, sizeof b, "%s\"%s\":%.17g", k ? "," : "", wi->params[k].name, wi->params[k].mx); ps += b; }
  Doc d(std::string("{\"modules\":[{\"id\":\"a\",\"type\":\"trackIn\",\"params\":{\"track\":1}},{\"id\":\"p\",\"type\":\"ptube\",\"params\":{") + ps + "}},{\"id\":\"c\",\"type\":\"chainOut\",\"params\":{\"track\":1,\"level\":1}}],"
    "\"cables\":[{\"from\":[\"a\",\"out\"],\"to\":[\"p\",\"in\"]},{\"from\":[\"a\",\"outR\"],\"to\":[\"p\",\"inR\"]},{\"from\":[\"p\",\"out\"],\"to\":[\"c\",\"in\"]},{\"from\":[\"p\",\"outR\"],\"to\":[\"c\",\"inR\"]}]}");
  Host host; ChainsEngine e(48000, EngineOptions(), &host); CHECK(e.loadPatch(d.r), "patch"); e.setInlineService(false);
  const std::vector<float> hot = noise(128, 31u, 7.9f), quiet = noise(128, 32u, 0.01f); auto run = [&](const std::vector<float>& x) { e.setTrackInputs(x.data(), x.data(), 1); e.process(128); };
  const uint64_t b0 = chains_pump_bad(), s0 = chains_pump_swaps(), d0 = chains_pump_dry();
  auto hotUntilBad = [&]() { const uint64_t nb = chains_pump_bad(); for (int i = 0; i < 400 && chains_pump_bad() == nb; i++) run(hot); return chains_pump_bad() > nb; };
  // the module starts with a spare. bad 1 -> swap 1 (the spare), no service -> bad 2 -> service (the next spare) -> swap 2 -> service: this one makes the third spare only because the take-over asked for it (no block went
  // bad since) -> bad 3, no service -> swap 3 finds that spare
  CHECK(hotUntilBad(), "a hot input drives the module bad"); run(quiet); CHECK(chains_pump_swaps() == s0 + 1, "a quiet block: the spare takes over");
  CHECK(hotUntilBad(), "the swapped-in instance goes bad again"); e.serviceSpares(); run(quiet); CHECK(chains_pump_swaps() == s0 + 2, "the spare made by service() takes over");
  e.serviceSpares(); CHECK(hotUntilBad(), "and goes bad"); CHECK(chains_pump_bad() >= b0 + 3, "three bad blocks");   // no service() now
  run(quiet); CHECK(chains_pump_swaps() == s0 + 3 && chains_pump_dry() == d0, "the next block finds a spare: swaps %d dry %d", (int)(chains_pump_swaps() - s0), (int)(chains_pump_dry() - d0));
}

int main() {
  testResampleTake(); testMonoFallback(); testCompRatioOne();
  testChainOutTrackClamp();
  testPumpSpares(); testPumpNoService(); testPumpSpareAfterTakeover();
  testFeedMonoFallback(); testSetSidechain(); testTempoClamp(); testTrackInBound(); testChainOutLevel(); testBypassRange(); testPostLimits(); testQueueRules(); testBypassCut();
  testEqualsOffline(); testLatencyAndExactEvents(); testRefusals();
  std::printf(fails ? "test_chains_live: %d FAILED\n" : "test_chains_live: ok\n", fails);
  return fails ? 1 : 0;
}
