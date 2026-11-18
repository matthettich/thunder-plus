// U30: the …waves engine (core/src/waves) against waves/waves-worklet.js. Grows with the unit; see docs/port/U30-NOTES.md for what each section gates.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <fstream>
#include <sstream>
#include "rt/json.h"
#include "waves/waves.h"

using namespace thunder;
using namespace thunder::waves;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static uint32_t fnv(const std::vector<const std::vector<float>*>& arrs) {
  uint32_t h = 0x811c9dc5u;
  for (auto* a : arrs) { const unsigned char* u = (const unsigned char*)a->data(); for (size_t i = 0; i < a->size() * 4; i++) { h ^= u[i]; h *= 0x01000193u; } }
  return h;
}
static std::string hex(uint32_t h) { char b[16]; std::snprintf(b, sizeof b, "%08x", h); return b; }

// ---- the processor rows of tests/golden/waves-processor.json ----
static std::string procPatch(double gain) {
  char g[64]; std::snprintf(g, sizeof g, "%.17g", gain);
  return std::string("{\"modules\":[{\"id\":\"h\",\"type\":\"hostIn\",\"params\":{\"transpose\":0}},{\"id\":\"v\",\"type\":\"vco\",\"params\":{\"wave\":1,\"tune\":0,\"fine\":0}},"
    "{\"id\":\"e\",\"type\":\"adsr\",\"params\":{\"a\":0.005,\"d\":0.2,\"s\":0.7,\"r\":0.3}},{\"id\":\"g\",\"type\":\"vca\",\"params\":{\"gain\":") + g +
    "}},{\"id\":\"o\",\"type\":\"out\",\"params\":{\"slot\":1,\"level\":1}}],\"cables\":[{\"from\":[\"h\",\"pitch\"],\"to\":[\"v\",\"pitch\"]},{\"from\":[\"h\",\"gate\"],\"to\":[\"e\",\"gate\"]},"
    "{\"from\":[\"v\",\"out\"],\"to\":[\"g\",\"in\"]},{\"from\":[\"e\",\"out\"],\"to\":[\"g\",\"cv\"]},{\"from\":[\"g\",\"out\"],\"to\":[\"o\",\"in\"]}]}";
}
static std::string driveProcessor(double gain) {
  EngineOptions o; o.maxVoices = 4; o.transport.tempo = 120; o.transport.playing = true;
  Host host; host.setRandom(Host::Rnd::Xorshift, 0x9e3779b9u);
  WavesEngine e(48000, o, &host);
  JsonDoc d; const std::string t = procPatch(gain); if (!d.parse(t)) { CHECK(false, "patch parse"); return ""; }
  CHECK(e.loadPatch(d.root()), "loadPatch");
  const int blocks = 160; std::vector<float> L((size_t)blocks * 128), R((size_t)blocks * 128);
  for (int b = 0; b < blocks; b++) {
    if (b == 0) { e.noteOn(60, 1); e.noteOn(64, 1); e.noteOn(67, 1); }
    CHECK(e.process(128), "process");
    waves_mixdown(e, &L[(size_t)b * 128], &R[(size_t)b * 128], 128);
  }
  return hex(fnv({&L, &R}));
}
// ---- the cases of tools/waves-dump.mjs (the committed golden/waves_cases.json), through the engine ----
static bool readFile(const std::string& path, std::string* out) { std::ifstream f(path, std::ios::binary); if (!f) return false; std::stringstream ss; ss << f.rdbuf(); *out = ss.str(); return true; }
struct CaseResult { std::vector<float> L, R; bool ok = false; };
static CaseResult runCase(Json c) {
  CaseResult r;
  EngineOptions o; o.maxVoices = (int)c.num("maxVoices", 8); o.transport.tempo = 120; o.transport.playing = true;
  Json sd = c.get("seed"); if (sd.isNum()) { o.hasSeed = true; o.seed = sd.asNum(); }
  Host host; host.setRandom(Host::Rnd::Xorshift, 0x9e3779b9u);
  WavesEngine e((double)c.num("sr", 48000), o, &host);
  if (!e.loadPatch(c.get("patch"))) return r;
  if (c.boolean("telemetry")) { e.setTelemetry(true); }
  const int blocks = (int)c.num("blocks"), bus = (int)c.num("bus", 1); Json ev = c.get("events");
  r.L.resize((size_t)blocks * 128); r.R.resize((size_t)blocks * 128);
  for (int b = 0; b < blocks; b++) {
    for (size_t k = 0; k < ev.size(); k++) {
      Json x = ev.at(k); if ((int)x.at(0).asNum() != b) continue;
      const std::string_view op = x.at(1).asStr();
      if (op == "noteOn") e.noteOn(x.at(2).asNum(), x.at(3).asNum());
      else if (op == "noteOff") e.noteOff(x.at(2).asNum());
      else if (op == "allOff") e.allNotesOff();
      else if (op == "param") e.setParam(x.at(2).asStr(), x.at(3).asStr(), x.at(4).asNum());
      else if (op == "connect") e.connect(x.at(2).asStr(), x.at(3).asStr(), x.at(4).asStr(), x.at(5).asStr(), x.at(6).isBool() && x.at(6).asBool());
      else if (op == "disconnect") e.disconnect(x.at(2).asStr(), x.at(3).asStr(), x.at(4).asStr(), x.at(5).asStr());
    }
    e.process(128);
    std::memcpy(&r.L[(size_t)b * 128], e.bus(bus), 128 * sizeof(float)); std::memcpy(&r.R[(size_t)b * 128], e.busR(bus), 128 * sizeof(float));
  }
  r.ok = true; return r;
}
static bool tolCase(const std::string& n) { return n.rfind("mod/marbles/", 0) == 0 || n.rfind("mod/tides/", 0) == 0; }
// Cases whose native output is not the wasm's bit for bit: the modules built from the strict upstream libraries (tfx effects, Plaits / Elements, the mods) against wasm built with fast-math (U14, U21, U25
// settled this: the library build is the only difference). They are listed BY NAME in golden/waves_native.json with the native hash (pinned, made on Linux x86-64) and the figure measured against the
// reference; a case outside the list must be bit-exact, a listed one must keep its pinned hash and (with the reference at hand) not get worse by more than 1 dB, and a listed one that became exact is stale.
static double dbOf(const std::vector<float>& L, const std::vector<float>& R, const std::string& ref, bool* ok) {
  *ok = false; if (ref.size() != L.size() * 8) return 0;
  const float* rl = (const float*)ref.data(); const float* rr = rl + L.size(); double mx = 0, pk = 0;
  for (size_t k = 0; k < L.size(); k++) { mx = std::fmax(mx, std::fmax(std::fabs((double)rl[k] - L[k]), std::fabs((double)rr[k] - R[k]))); pk = std::fmax(pk, std::fmax(std::fabs((double)rl[k]), std::fabs((double)rr[k]))); }
  *ok = true; return 20 * std::log10(mx / (pk > 0 ? pk : 1) + 1e-30);
}
// the pinned native hashes are the output of the strict effect libraries on one machine (GCC, x86-64 Linux, glibc): another compiler, another libm or another CPU rounds some effects
// differently (CI's own GCC runner differs from it on "ott"). So an effect-bearing case is held to "runs and is finite" unless THUNDER_WAVES_PINS=1 is set (the local run and the mutation
// script set it); every case without an effect is bit-exact on every platform, as are both processor rows.
static const bool kRefBuild = std::getenv("THUNDER_WAVES_PINS") != nullptr;
static bool patchUsesFx(Json patch) { Json m = patch.get("modules"); for (size_t i = 0; i < m.size(); i++) if (waves_type_is_fx(std::string(m.at(i).str("type")))) return true; return false; }
// a case whose patch has a module the build has no library for (effects, Marbles / Tides: the strict libraries; Plaits / Elements: the PE build) is skipped, as on the Windows build
static bool patchLacksLib(Json patch) {
  Json m = patch.get("modules");
  for (size_t i = 0; i < m.size(); i++) { const std::string t(m.at(i).str("type"));
    if ((waves_type_is_fx(t) || t == "marbles" || t == "tides") && !Host::haveEffects()) return true;
    if ((t == "plaits" || t == "elements") && !Host::havePE()) return true; }
  return false;
}
static bool finiteOut(const CaseResult& r) { for (float v : r.L) if (!std::isfinite(v)) return false; for (float v : r.R) if (!std::isfinite(v)) return false; return true; }
static void runGolden(const std::string& golden, const std::string& nativeFile, const std::string& refDir, const std::string& filter, const std::string& dumpDir, const std::string& writeNative) {
  std::string txt; if (!readFile(golden, &txt)) { CHECK(false, "cannot read %s", golden.c_str()); return; }
  JsonDoc d; if (!d.parse(txt)) { CHECK(false, "golden parse"); return; }
  JsonDoc nd; std::string ntxt; Json pins; if (!nativeFile.empty() && readFile(nativeFile, &ntxt) && nd.parse(ntxt)) pins = nd.root();
  if (writeNative.empty() && filter.empty() && !pins.isObj()) CHECK(false, "the pinned native hashes (waves_native.json) are missing");
  Json all = d.root(); int ran = 0, bad = 0, tol = 0, skipped = 0, loose = 0; double worst = -999; std::string worstName; std::string outPins = "{";
  for (size_t i = 0; i < all.size(); i++) {
    Json c = all.at(i); const std::string name(c.str("name")); if (!filter.empty() && name.find(filter) == std::string::npos) continue;
    const bool fx = patchUsesFx(c.get("patch"));
    if (patchLacksLib(c.get("patch")) && writeNative.empty()) { skipped++; continue; }
    CaseResult r = runCase(c); ran++;
    if (!r.ok) { CHECK(false, "%s: patch refused", name.c_str()); bad++; continue; }
    std::string fn = name; for (char& ch : fn) if (ch == '/' || ch == '@') ch = '_';
    if (!dumpDir.empty()) { std::ofstream f(dumpDir + "/" + fn + ".f32", std::ios::binary); f.write((const char*)r.L.data(), (std::streamsize)(r.L.size() * 4)); f.write((const char*)r.R.data(), (std::streamsize)(r.R.size() * 4)); }
    const std::string h = hex(fnv({&r.L, &r.R})), want(c.str("hash"));
    std::string ref; bool haveRef = !refDir.empty() && readFile(refDir + "/" + fn + ".f32", &ref); bool okdb = false; const double db = haveRef ? dbOf(r.L, r.R, ref, &okdb) : 0;
    Json pin = pins.get(name);
    if (h == want) { if (pin.isObj() && kRefBuild) { bad++; CHECK(false, "%s: listed as inexact but now bit-exact (stale entry)", name.c_str()); } continue; }
    if (fx && !kRefBuild && writeNative.empty()) { loose++; if (!finiteOut(r)) { bad++; CHECK(false, "%s: not finite", name.c_str()); } continue; }
    tol++;
    if (!writeNative.empty()) { char b[256]; std::snprintf(b, sizeof b, "%s\"%s\":{\"hash\":\"%s\",\"db\":%.1f}", outPins.size() > 1 ? "," : "", name.c_str(), h.c_str(), okdb ? db : 0.0); outPins += b; continue; }
    if (!pin.isObj()) { bad++; CHECK(false, "%s: not bit-exact (%s != %s)%s and not listed", name.c_str(), h.c_str(), want.c_str(), okdb ? (" " + std::to_string(db) + " dB").c_str() : ""); continue; }
    if (std::string(pin.str("hash")) != h) { bad++; CHECK(false, "%s: native hash %s != pinned %s", name.c_str(), h.c_str(), std::string(pin.str("hash")).c_str()); }
    if (okdb) { if (db > worst) { worst = db; worstName = name; } if (db > pin.num("db") + 1) { bad++; CHECK(false, "%s: %.1f dB is worse than the recorded %.1f dB", name.c_str(), db, pin.num("db")); }
      const bool isMods = name.rfind("mod/marbles/", 0) == 0 || name.rfind("mod/tides/", 0) == 0;
      if (isMods && db > -100) { bad++; CHECK(false, "%s: %.1f dB is above the -100 dB line for Marbles / Tides", name.c_str(), db); } }
  }
  if (!writeNative.empty()) { outPins += "}\n"; std::ofstream f(writeNative, std::ios::binary); f << outPins; std::printf("wrote %s (%d inexact cases)\n", writeNative.c_str(), tol); }
  std::printf("golden cases: %d run, %d skipped (a library is missing in this build), %d bit-exact, %d listed as library-build differences (worst %.1f dB %s), %d effect cases held to finite off the reference build, %d wrong\n", ran, skipped, ran - tol - loose, tol, worst, worstName.c_str(), loose, bad);
}

// ---- behaviours tests/waves.test.js and tests/feedback.test.js assert, as native cases ----
struct Doc { JsonDoc d; Json r; explicit Doc(const std::string& t) { if (!d.parse(t)) std::printf("bad test json: %s\n", t.c_str()); r = d.root(); } };
static std::string mod(const char* id, const char* type, const std::string& params = "{}") { return std::string("{\"id\":\"") + id + "\",\"type\":\"" + type + "\",\"params\":" + params + "}"; }
static std::string cab(const char* a, const char* ap, const char* b, const char* bp, bool fb = false) { return std::string("{\"from\":[\"") + a + "\",\"" + ap + "\"],\"to\":[\"" + b + "\",\"" + bp + "\"]" + (fb ? ",\"fb\":true" : "") + "}"; }
static std::string patch(std::initializer_list<std::string> m, std::initializer_list<std::string> c) {
  std::string s = "{\"modules\":["; bool f = true; for (auto& x : m) { s += (f ? "" : ",") + x; f = false; } s += "],\"cables\":["; f = true; for (auto& x : c) { s += (f ? "" : ",") + x; f = false; } return s + "]}";
}
static double energy(WavesEngine& e, int blocks, int bus = 1) { double en = 0; for (int b = 0; b < blocks; b++) { e.process(128); const float* l = e.bus(bus); for (int i = 0; i < 128; i++) en += (double)l[i] * l[i]; } return en; }
static EngineOptions opts(int voices) { EngineOptions o; o.maxVoices = voices; o.transport.tempo = 120; o.transport.playing = true; return o; }
static const std::string kBasic = patch({mod("h", "hostIn"), mod("v", "vco", "{\"wave\":1}"), mod("e", "adsr", "{\"a\":0.005,\"d\":0.2,\"s\":0.7,\"r\":0.3}"), mod("g", "vca"), mod("o", "out")},
  {cab("h", "pitch", "v", "pitch"), cab("h", "gate", "e", "gate"), cab("v", "out", "g", "in"), cab("e", "out", "g", "cv"), cab("g", "out", "o", "in")});

static void testGuard() {
  float src[128], dst[128], hot[128], o[128], neg[128], on[128];
  for (int i = 0; i < 128; i++) src[i] = (float)((i / 127.0) * 2 - 1);
  std::memset(dst, 0, sizeof dst); waves_fbadd(dst, src, 1); CHECK(!std::memcmp(dst, src, sizeof dst), "guard: bit for bit inside the range");
  for (int i = 0; i < 128; i++) dst[i] = .25f; waves_fbadd(dst, src, 1); for (int i = 0; i < 128; i++) CHECK(dst[i] == (float)(.25 + (double)src[i]), "guard adds");
  for (int i = 0; i < 128; i++) hot[i] = (float)(1 + i * .25); std::memset(o, 0, sizeof o); waves_fbadd(o, hot, 1);
  for (int i = 1; i < 128; i++) CHECK(o[i] >= o[i - 1] && o[i] <= 2, "guard rising and under 2");
  CHECK(std::fabs(o[1] - 1.2449) < 1e-3, "guard just over 1: %g", o[1]);
  for (int i = 0; i < 128; i++) neg[i] = -hot[i]; std::memset(on, 0, sizeof on); waves_fbadd(on, neg, 1); for (int i = 0; i < 128; i++) CHECK(on[i] == -o[i], "guard odd");
  float bad[128] = {NAN, INFINITY, -INFINITY, .5f}, ob[128] = {}; waves_fbadd(ob, bad, 1); CHECK(ob[0] == 0 && ob[1] == 0 && ob[2] == 0 && ob[3] == .5f, "guard: not a number becomes silence");
  float og[128] = {}; waves_fbadd(og, src, .5); for (int i = 0; i < 128; i++) CHECK(og[i] == (float)((double)src[i] * .5), "guard: the gain comes first");
  float edge[128] = {1.0f, -1.0f, 1.0000001f, 0.99999994f}; float oe[128] = {}; waves_fbadd(oe, edge, 1);
  CHECK(oe[0] == 1.0f + 0.0f && oe[1] == -1.0f, "guard: exactly 1 goes the bent way, which is 1 + tanh(0) = 1"); CHECK(oe[3] == 0.99999994f, "guard: just inside is untouched");
}
static std::string wavesLoop(std::initializer_list<const char*> order, double amount, bool fb = true) {
  char a[64]; std::snprintf(a, sizeof a, "{\"amount\":%.17g,\"offset\":0}", amount);
  auto one = [&](const std::string& k) { if (k == "h") return mod("h", "hostIn"); if (k == "v") return mod("v", "vco"); if (k == "x") return mod("x", "atten", "{\"amount\":1,\"offset\":0}"); if (k == "y") return mod("y", "atten", a); if (k == "z") return mod("z", "atten", a); return mod("o", "out"); };
  std::string s = "{\"modules\":["; bool f = true; for (const char* k : order) { s += (f ? "" : ",") + one(k); f = false; }
  s += "],\"cables\":[" + cab("h", "pitch", "v", "pitch") + "," + cab("v", "out", "x", "in") + "," + cab("x", "out", "o", "in") + "," + cab("x", "out", "y", "in") + "," + cab("x", "out", "z", "in") + "," + cab("y", "out", "x", "in", fb) + "," + cab("z", "out", "x", "in", fb) + "]}";
  return s;
}
static std::vector<float> renderLoop(const std::string& pj, int blocks) {
  Doc d(pj); Host host; host.setRandom(Host::Rnd::Xorshift, 0x9e3779b9u); EngineOptions o = opts(4); WavesEngine e(48000, o, &host); CHECK(e.loadPatch(d.r), "loop patch");
  e.noteOn(60, 1); std::vector<float> out((size_t)blocks * 128); for (int b = 0; b < blocks; b++) { e.process(128); std::memcpy(&out[(size_t)b * 128], e.bus(1), 512); } return out;
}
static void testFeedback() {
  testGuard();
  std::vector<float> first;
  const std::vector<std::vector<const char*>> orders = {{"h", "v", "x", "y", "z", "o"}, {"o", "z", "y", "x", "v", "h"}, {"y", "o", "x", "h", "z", "v"}, {"z", "x", "y", "v", "o", "h"}};
  for (auto& ord : orders) {
    std::string s = "{\"modules\":["; bool f = true; (void)f; (void)s;
    std::initializer_list<const char*> il = {ord[0], ord[1], ord[2], ord[3], ord[4], ord[5]};
    std::vector<float> out = renderLoop(wavesLoop(il, .4), 60); double en = 0; for (float v : out) { CHECK(std::isfinite(v), "loop finite"); en += (double)v * v; }
    CHECK(en > 0, "loop makes sound");
    if (first.empty()) first = out; else CHECK(out == first, "same audio whatever the storage order");
  }
  std::vector<float> big = renderLoop(wavesLoop({"h", "v", "x", "y", "z", "o"}, 1), 1500); double pk = 0; for (float v : big) { CHECK(std::isfinite(v), "gain-1 loop finite"); pk = std::fmax(pk, std::fabs(v)); }
  CHECK(pk > 1 && pk <= 5.0001, "built up and held: peak %g", pk);
  // a feedback cable does not make its target per voice
  std::string base = wavesLoop({"h", "v", "x", "y", "z", "o"}, .4);
  std::string with = base.substr(0, base.rfind("]}")) + "," + cab("x", "out", "m", "in", true) + "]}"; with.insert(with.find("],\"cables\""), "," + mod("m", "atten", "{\"amount\":1,\"offset\":0}"));
  Doc d(with); WavesEngine e(48000, opts(4)); CHECK(e.loadPatch(d.r), "fb patch");
  CHECK(!e.isPoly("m"), "not per voice through a feedback cable"); CHECK(e.isPoly("y"), "the loop itself is per voice");
  e.process(128); for (int v = 0; v < e.maxVoices(); v++) { float* x = e.outBuf("x", v, "out"); CHECK(x != nullptr, "x out"); if (x) for (int i = 0; i < 128; i++) x[i] = .9f; }
  e.gatherNow("m", -1); const float* in = e.inBuf("m", -1, "in"); CHECK(in && in[0] > 1 && in[0] <= 2 && e.maxVoices() >= 2, "summed and held under 2: %g", in ? in[0] : 0.0);
  std::string without = with; const size_t p = without.rfind(",\"fb\":true"); without.erase(p, 10);
  Doc d2(without); CHECK(e.loadPatch(d2.r), "unmarked"); CHECK(e.isPoly("m"), "the same cable unmarked makes it per voice");
}
static void testVoices() {
  { Doc d(kBasic); WavesEngine e(48000, opts(2)); CHECK(e.loadPatch(d.r), "basic"); CHECK(energy(e, 10) == 0, "silent before any note"); e.noteOn(60, 1); CHECK(energy(e, 40) > 0, "a held note sounds");
    e.noteOn(64, 1); e.noteOn(67, 1); e.noteOn(71, 1); CHECK(std::isfinite(energy(e, 20)), "finite with more notes than voices"); e.allNotesOff(); energy(e, 900); CHECK(energy(e, 10) < 1e-6, "silent after release"); }
  {   // a mono audio source into a voice-scope module is shared out: 1/N per voice (N = the voice count), so 4 voices make a quarter of the amplitude of 1
    const std::string mono = patch({mod("h", "hostIn"), mod("v", "vco", "{\"wave\":1}"), mod("e", "adsr", "{\"a\":0.005,\"d\":0.2,\"s\":0.7,\"r\":0.3}"), mod("g", "vca"), mod("o", "out")},
      {cab("h", "gate", "e", "gate"), cab("v", "out", "g", "in"), cab("e", "out", "g", "cv"), cab("g", "out", "o", "in")});
    double en[2]; for (int k = 0; k < 2; k++) { Doc d(mono); Host host; host.setRandom(Host::Rnd::Xorshift, 0x9e3779b9u); WavesEngine e(48000, opts(k ? 4 : 1), &host); CHECK(e.loadPatch(d.r), "mono source patch"); e.noteOn(60, 1); en[k] = energy(e, 60); }
    CHECK(en[0] > 0 && std::fabs(en[1] / en[0] - 1.0 / 16) < 1e-3, "mono source into a voice: energy ratio %g, wanted 1/16", en[0] > 0 ? en[1] / en[0] : -1);
  }
  {   // a parameter past its declared range is held to it (the JS would run on the raw value; a hostile value must not reach a module)
    std::vector<float> o2[2]; for (int k = 0; k < 2; k++) { const std::string pj = patch({mod("h", "hostIn"), mod("v", "vco", "{\"wave\":1}"), mod("e", "adsr", k ? "{\"a\":0.005,\"d\":0.2,\"s\":1,\"r\":0.3}" : "{\"a\":0.005,\"d\":0.2,\"s\":9,\"r\":0.3}"), mod("g", "vca"), mod("o", "out")},
      {cab("h", "pitch", "v", "pitch"), cab("h", "gate", "e", "gate"), cab("v", "out", "g", "in"), cab("e", "out", "g", "cv"), cab("g", "out", "o", "in")}); o2[k] = renderLoop(pj, 40); }
    CHECK(o2[0] == o2[1], "a sustain of 9 behaves as the declared maximum");
  }
  VoicePool p(3);
  int a = p.on(60, 1), b = p.on(64, 1), c = p.on(67, 1); CHECK(a == 0 && b == 1 && c == 2, "fills in order");
  int d = p.on(72, 1); CHECK(d == 0, "all busy: the oldest overall is stolen (row %d)", d);
  p.off(64); p.off(67); int e2 = p.on(70, 1); CHECK(e2 == 1, "an idle row: the oldest idle one (row %d)", e2);
  p.off(70); int f = p.on(70, .5); CHECK(f == 1, "the same pitch idle reuses its row (row %d)", f);
  p.off(67); p.off(72); p.off(70); int g = p.on(67, 1); CHECK(g == 2, "same-pitch idle row beats the oldest idle (row %d)", g);
  VoicePool z(0); CHECK(z.on(60, 1) == -1, "no rows: refused, not a crash");
  VoicePool q(2); q.on(60, 1); q.on(60, 1); CHECK(q.rows[0].age == 1 && q.rows[1].age == 2, "ages count up"); q.now = 500; q.off(60); CHECK(q.rows[0].offAt == 500 && q.rows[1].offAt == 500, "off stamps now");
  { Doc d3(kBasic); WavesEngine e3(48000, opts(4)); e3.loadPatch(d3.r); e3.noteOn(60, 1); e3.process(128); e3.allNotesOff(); for (int i = 0; i < 4000; i++) e3.process(128);
    const VoiceRow& r = e3.voices().rows[0]; CHECK(r.gate == 0, "released"); CHECK(!e3.voices().awake(0, 48000.0 * 12), "asleep after the tail has died"); }
}
static std::string twoBus(const std::string& extraM = "", const std::string& extraC = "") {
  std::string s = "{\"modules\":[" + mod("h", "hostIn") + "," + mod("a", "vco", "{\"wave\":1}") + "," + mod("ea", "adsr", "{\"a\":0.005,\"d\":0.2,\"s\":0.7,\"r\":0.05}") + "," + mod("ga", "vca") + "," + mod("o1", "out", "{\"slot\":1,\"level\":1}") + "," + mod("b", "vco", "{\"wave\":2}") + "," + mod("gb", "vca") + "," + mod("o2", "out", "{\"slot\":2,\"level\":1}") + extraM + "],\"cables\":[" +
    cab("h", "pitch", "a", "pitch") + "," + cab("h", "gate", "ea", "gate") + "," + cab("a", "out", "ga", "in") + "," + cab("ea", "out", "ga", "cv") + "," + cab("ga", "out", "o1", "in") + "," + cab("h", "pitch", "b", "pitch") + "," + cab("b", "out", "gb", "in") + "," + cab("ea", "out", "gb", "cv") + "," + cab("gb", "out", "o2", "in") + extraC + "]}";
  return s;
}
static std::vector<float> playBus(const std::string& pj, int bus, int only, std::vector<std::string>* ids, bool* hasOnly) {
  Doc d(pj); Host host; host.setFixed(.25); EngineOptions o = opts(4); o.hasSeed = true; o.seed = 1234; WavesEngine e(48000, o, &host); e.loadPatch(d.r); if (only) e.setOnly(only);
  const int k = e.voiceOn(-12, 1); std::vector<float> out;
  for (int b = 0; b < 300; b++) { if (b == 200) { e.voices().rows[(size_t)k].gate = 0; e.voices().rows[(size_t)k].offAt = e.voices().now; } e.process(128); for (int i = 0; i < 128; i++) out.push_back(e.bus(bus)[i]); for (int i = 0; i < 128; i++) out.push_back(e.busR(bus)[i]); }
  if (ids) *ids = e.onlyIds(); if (hasOnly) *hasOnly = e.hasOnly(); return out;
}
static void testSetOnly() {
  for (int bus = 1; bus <= 2; bus++) {
    bool ho = true; std::vector<float> full = playBus(twoBus(), bus, 0, nullptr, &ho), one = playBus(twoBus(), bus, bus, nullptr, nullptr);
    bool any = false; for (float v : full) if (std::fabs(v) > .01) any = true; CHECK(any, "it plays"); CHECK(full == one, "bus %d identical with setOnly", bus); CHECK(!ho, "without setOnly everything runs");
  }
  std::vector<std::string> ids; playBus(twoBus(), 1, 1, &ids, nullptr); CHECK((ids == std::vector<std::string>{"a", "ea", "ga", "h", "o1"}), "only for bus 1");
  playBus(twoBus(), 2, 2, &ids, nullptr); CHECK((ids == std::vector<std::string>{"b", "ea", "gb", "h", "o2"}), "only for bus 2");
  bool ho = false; playBus(twoBus("," + mod("s", "seq")), 1, 1, &ids, &ho); CHECK(ho && std::find(ids.begin(), ids.end(), "s") != ids.end(), "the Sequencer stays");
  playBus(twoBus("," + mod("l", "lfo"), "," + cab("l", "out", "o1", "p:slot")), 1, 1, nullptr, &ho); CHECK(!ho, "an Out whose slot is modulated: everything runs");
  { std::string s = twoBus(); const std::string rm = "," + cab("ga", "out", "o1", "in"); s.erase(s.find(rm), rm.size()); playBus(s, 1, 1, nullptr, &ho); CHECK(!ho, "no voice audio left on this bus: everything runs"); }
  Doc d(twoBus()); WavesEngine e(48000, opts(4)); e.loadPatch(d.r); e.setOnly(1); e.process(128); auto has = [&](const char* id) { auto v = e.onlyIds(); return std::find(v.begin(), v.end(), id) != v.end(); };
  CHECK(has("a"), "bus 1 keeps a"); e.setOnly(2); e.process(128); CHECK(!has("a") && has("b"), "changing the Bus follows");
  e.setParam("o1", "slot", 2); e.process(128); CHECK(has("a") && has("b"), "an Out moved onto this bus is picked up"); e.setOnly(0); e.process(128); CHECK(!e.hasOnly(), "off again");
}
static void testPatchLoading() {
  { Doc d("{}"); WavesEngine e(48000, opts(4)); CHECK(e.loadPatch(d.r) && e.patchModules() == 0 && e.order().empty(), "zero nodes"); e.process(128); CHECK(energy(e, 3) == 0, "silent"); }
  { Doc d("{\"modules\":[" + mod("a", "vca") + "," + mod("a", "atten") + "],\"cables\":[]}"); WavesEngine e(48000, opts(4)); e.loadPatch(d.r); CHECK(e.order().size() == 1 && e.param("a", "amount") == 1, "a duplicate id replaces the first (the later type wins)"); }
  { Doc d("{\"modules\":[" + mod("a", "nonsense") + "," + mod("b", "vca") + "],\"cables\":[" + cab("a", "out", "b", "in") + "," + cab("b", "out", "zzz", "in") + "," + cab("b", "nope", "b", "in") + "]}"); WavesEngine e(48000, opts(4)); CHECK(e.loadPatch(d.r), "unknown types and missing ends");
    CHECK(e.order().size() == 1 && e.cables().size() == 1, "an unknown type is skipped, a cable to nothing is dropped, a missing port is kept and does nothing (%zu cables)", e.cables().size()); e.process(128); }
  { Doc d1(kBasic); WavesEngine e(48000, opts(4)); CHECK(e.loadPatch(d1.r), "ok"); Doc bad("{\"modules\":[null]}"); CHECK(!e.loadPatch(bad.r), "a null module refuses the patch"); CHECK(e.order().size() == 5, "and the old patch stays");
    Doc bad2("{\"modules\":[{\"id\":\"a#1\",\"type\":\"vca\"}]}"); CHECK(!e.loadPatch(bad2.r), "'#' in an id is refused"); Doc bad3("{\"modules\":[{\"id\":7,\"type\":\"vca\"}]}"); CHECK(!e.loadPatch(bad3.r), "a numeric id is refused");
    Doc bad4("{\"modules\":\"abc\"}"); CHECK(!e.loadPatch(bad4.r), "modules that is not an array"); Doc bad5("{\"modules\":[],\"cables\":[{\"from\":1,\"to\":2}]}"); CHECK(!e.loadPatch(bad5.r), "cables that are not arrays");
    Doc bad6("[1,2]"); CHECK(!e.loadPatch(bad6.r), "a patch that is not an object"); CHECK(e.stats().refusedPatches >= 5, "counted"); e.process(128); }
  { std::string s = "{\"modules\":["; for (int i = 0; i < 1025; i++) s += (i ? "," : "") + mod(("m" + std::to_string(i)).c_str(), "atten"); s += "],\"cables\":[]}"; Doc d(s); WavesEngine e(48000, opts(4)); CHECK(!e.loadPatch(d.r), "1025 modules are refused"); }
  { std::string s = "{\"modules\":["; for (int i = 0; i < 1000; i++) s += (i ? "," : "") + mod(("m" + std::to_string(i)).c_str(), "atten"); s += "],\"cables\":[]}"; Doc d(s); WavesEngine e(48000, opts(8)); CHECK(e.loadPatch(d.r), "1000 attenuverters load"); e.noteOn(60, 1); e.process(128); }
  { Doc d("{\"modules\":[" + mod("a", "atten") + "," + mod("b", "atten") + "],\"cables\":[" + cab("a", "out", "b", "in") + "," + cab("b", "out", "a", "in") + "]}"); WavesEngine e(48000, opts(4)); CHECK(e.loadPatch(d.r), "a cycle without fb"); e.process(128);
    CHECK(e.order().size() == 2 && e.order()[0] == "a", "the cycle is appended in patch order"); }
  { Doc d("{\"modules\":[" + mod("a", "vco", "{\"wave\":1e999,\"tune\":-1e999}") + "," + mod("o", "out", "{\"level\":1e308}") + "],\"cables\":[" + cab("a", "out", "o", "in") + "]}"); WavesEngine e(48000, opts(4)); e.loadPatch(d.r); e.noteOn(60, 1); e.process(128); }
  { Doc d(kBasic); WavesEngine e(48000, opts(4)); e.loadPatch(d.r); e.noteOn(60, 1); e.process(128); Doc d2(twoBus()); e.loadPatch(d2.r); e.process(128); e.setParam("a", "wave", NAN); e.setParam("nope", "x", 1); e.removeModule("a"); e.connect("h", "pitch", "b", "pitch"); e.disconnect("h", "pitch", "b", "pitch"); e.process(128); CHECK(true, ""); }
  { WavesEngine e(48000, opts(4)); CHECK(!e.process(129) && !e.process(-1), "frames outside 0..128 are refused"); CHECK(e.process(0) && e.process(17), "0 and 17 frames run"); }
  { WavesEngine bad(5, opts(4)); CHECK(!bad.valid() && !bad.process(128), "an absurd sample rate is refused"); }
  { Doc d("{\"modules\":[" + mod("a", "vco") + "],\"cables\":[],\"transport\":{\"tempo\":90,\"lpb\":8}}"); WavesEngine e(48000, opts(4)); e.loadPatch(d.r); CHECK(e.transport().tempo == 90 && e.transport().lpb == 8 && e.transport().ticks == 6, "the doc's transport merges"); }
  { Doc d("{\"modules\":[" + mod("v", "vcf") + "," + mod("w", "vca") + "," + mod("x", "vco") + "],\"cables\":[" + cab("x", "out", "v", "in") + "," + cab("v", "out", "w", "in") + "]}"); WavesEngine e(48000, opts(4)); e.loadPatch(d.r);
    bool inR = false; for (auto& c : e.cables()) if (c.aPort == "outR") inR = true; CHECK(!inR, "stereoUpCables: only for the six modules with In R / Out R"); }
  { Doc d("{\"modules\":[" + mod("p", "phaser") + "," + mod("q", "phaser") + "],\"cables\":[" + cab("p", "out", "q", "in") + "]}"); WavesEngine e(48000, opts(4)); e.loadPatch(d.r); bool up = false; for (auto& c : e.cables()) if (c.aPort == "outR" && c.bPort == "inR") up = true; CHECK(up, "stereoUpCables adds Out R -> In R");
    Doc d2("{\"st2\":true,\"modules\":[" + mod("p", "phaser") + "," + mod("q", "phaser") + "],\"cables\":[" + cab("p", "out", "q", "in") + "]}"); e.loadPatch(d2.r); up = false; for (auto& c : e.cables()) if (c.aPort == "outR") up = true; CHECK(!up, "not when st2 is set"); }
}

// ---- the live runner: the output equals the offline render of the same events, bit for bit, at every host block size ----
static std::vector<LiveEvent> liveEvents(bool withSwap) {
  std::vector<LiveEvent> ev; auto E = [&](LiveEvent::Kind k, int64_t at, int lane, double a, double b = 0, bool leg = false) { LiveEvent e; e.kind = k; e.at = at; e.lane = (uint8_t)lane; e.a = a; e.b = b; e.legato = leg; ev.push_back(e); };
  E(LiveEvent::NoteOn, 0, 0, -12, .8); E(LiveEvent::NoteOn, 100, 1, -5, 1); E(LiveEvent::NoteOn, 129, 2, 0, .5); E(LiveEvent::Pitch, 300, 0, -11.5); E(LiveEvent::Pitch, 301, 0, -11);
  E(LiveEvent::NoteOn, 777, 0, -7, 1, true); E(LiveEvent::NoteOff, 900, 1, 0); E(LiveEvent::Tempo, 1000, 0, 140); E(LiveEvent::Level, 1001, 0, -6); E(LiveEvent::Drive, 1500, 0, .5);
  E(LiveEvent::NoteOn, 2000, 3, 4, .9); E(LiveEvent::NoteOn, 2000, 1, 7, .9); E(LiveEvent::NoteOff, 2500, 0, 0); E(LiveEvent::NoteOn, 2600, 0, -12, 1); E(LiveEvent::NoteOn, 2600, 0, -9, 1, true);
  E(LiveEvent::Bus, 3000, 0, 1); E(LiveEvent::NoteOff, 3500, 3, 0); E(LiveEvent::NoteOn, 3600, 2, -20, 1);
  if (withSwap) E(LiveEvent::Patch, 4000, 0, 0);
  E(LiveEvent::NoteOn, 4100, 1, -3, 1); E(LiveEvent::NoteOff, 5000, 1, 0); E(LiveEvent::NoteOff, 5001, 2, 0);
  return ev;
}
static void testLive() {
  const std::string patchB = patch({mod("h", "hostIn"), mod("v", "vco", "{\"wave\":0}"), mod("f", "vcf", "{\"cutoff\":0.4}"), mod("e", "adsr"), mod("g", "vca"), mod("d", "delay", "{\"time\":0.2,\"mix\":0.3}"), mod("o", "out")},
    {cab("h", "pitch", "v", "pitch"), cab("h", "gate", "e", "gate"), cab("v", "out", "f", "in"), cab("f", "out", "g", "in"), cab("e", "out", "g", "cv"), cab("g", "out", "d", "in"), cab("d", "out", "o", "in")});
  Doc d0(kBasic), d1(patchB); const std::vector<Json> patches = {d1.r};
  const std::vector<LiveEvent> ev = liveEvents(true); const int64_t N = 9000;
  std::vector<float> oL, oR; Host h0; h0.setFixed(.3);
  CHECK(WavesLive::renderOffline(48000, 8, &h0, ev, patches, N + 400, oL, oR), "offline render");
  double en = 0; for (int64_t i = 0; i < N; i++) en += (double)oL[(size_t)i] * oL[(size_t)i]; CHECK(en > 0, "the live render sounds");
  const std::vector<std::vector<int>> pats = {{128}, {64}, {17}, {1}, {1, 7, 128, 300, 5, 64, 2, 1000, 3, 129}, {256}, {4096}};
  // the first patch (kBasic) is loaded into the live node through the same mailbox before anything plays: Patch event at 0 with a second slot
  for (const auto& pat : pats) {
    Host h; h.setFixed(.3); WavesLive live(48000, 8, &h);
    // patch 0 = kBasic goes in at time 0 (slot 0), the swap to patchB is event 'Patch' at 4000 (slot 1)
    CHECK(live.preparePatch(d0.r, 0), "prepare basic");
    // the offline side has the same two patches in the same order: [kBasic, patchB], with a Patch(0) at 0 and Patch(1) at 4000
    for (const LiveEvent& e : ev) { LiveEvent x = e; if (x.kind == LiveEvent::Patch) continue; live.post(x); }
    CHECK(live.preparePatch(d1.r, 4000), "prepare B");
    std::vector<float> L((size_t)(N + 400 + 127)), R(L.size()); size_t at = 0, k = 0;
    while (at < L.size()) { const size_t n = std::min((size_t)pat[k++ % pat.size()], L.size() - at); live.process(&L[at], &R[at], (int)n); at += n; live.collectRetired(); }
    // offline with the same event list: kBasic at 0, patchB at 4000 (slots 0 and 1)
    std::vector<LiveEvent> ev2 = ev; for (LiveEvent& e : ev2) if (e.kind == LiveEvent::Patch) { e.a = 1; }
    LiveEvent first; first.kind = LiveEvent::Patch; first.at = 0; first.a = 0; ev2.insert(ev2.begin(), first);
    std::vector<Json> both = {d0.r, d1.r}; Host h2; h2.setFixed(.3); std::vector<float> oL2, oR2; CHECK(WavesLive::renderOffline(48000, 8, &h2, ev2, both, N + 400, oL2, oR2), "offline 2");
    size_t bad = 0, first_bad = 0; for (int64_t i = 0; i < N + 400; i++) { const size_t li = (size_t)i + 127; if (std::memcmp(&L[li], &oL2[(size_t)i], 4) || std::memcmp(&R[li], &oR2[(size_t)i], 4)) { if (!bad) first_bad = (size_t)i; bad++; } }
    CHECK(bad == 0, "live differs from the offline render at host blocks starting %d: %zu samples, first %zu", pat[0], bad, first_bad);
    for (int i = 0; i < 127; i++) CHECK(L[(size_t)i] == 0, "silent during the latency");
    CHECK(live.stats().lateEvents == 0, "no late events"); CHECK(live.stats().cutBlocks > 5, "blocks are cut at events");
  }
  (void)oL; (void)oR;
}
static void testSortOrder() {
  // Kahn's algorithm as the JS has it: a queue, started with the modules nothing feeds in patch order; a module joins the queue when its last feeder has run; a cycle is left over and appended in patch order
  { Doc d("{\"modules\":[" + mod("a", "atten") + "," + mod("b", "atten") + "," + mod("c", "atten") + "],\"cables\":[" + cab("b", "out", "a", "in") + "]}"); WavesEngine e(48000, opts(2)); e.loadPatch(d.r);
    CHECK((e.order() == std::vector<std::string>{"b", "c", "a"}), "tie-break: the queue starts in patch order, a joins after c"); }
  { Doc d("{\"modules\":[" + mod("a", "atten") + "," + mod("b", "atten") + "," + mod("c", "atten") + "," + mod("d", "atten") + "],\"cables\":[" + cab("a", "out", "b", "in") + "," + cab("b", "out", "a", "in") + "," + cab("d", "out", "c", "in") + "]}"); WavesEngine e(48000, opts(2)); e.loadPatch(d.r);
    CHECK((e.order() == std::vector<std::string>{"d", "c", "a", "b"}), "the cycle a<->b is left over and appended in patch order after d, c"); }
  { Doc d("{\"modules\":[" + mod("a", "atten") + "," + mod("b", "atten") + "],\"cables\":[" + cab("a", "out", "b", "in") + "," + cab("b", "out", "a", "in", true) + "]}"); WavesEngine e(48000, opts(2)); e.loadPatch(d.r);
    CHECK((e.order() == std::vector<std::string>{"a", "b"}), "a feedback cable does not decide who runs first"); }
  { Doc d("{\"modules\":[" + mod("a", "atten") + "," + mod("b", "atten") + "],\"cables\":[" + cab("b", "out", "a", "in", true) + "]}"); WavesEngine e(48000, opts(2)); e.loadPatch(d.r);
    CHECK((e.order() == std::vector<std::string>{"a", "b"}), "the module a feedback cable feeds runs first (patch order), not after its source"); }
}
static void testBehaviours() { testSortOrder(); testFeedback(); testVoices(); testSetOnly(); testPatchLoading(); testLive(); }
// ---- the page's own offline renders (golden/waves_render.json: index.html wavesPre / wavesLiveRender run in Node) ----
static void runRender(const std::string& file) {
  std::string txt; if (!readFile(file, &txt)) { CHECK(false, "cannot read %s", file.c_str()); return; }
  JsonDoc d; if (!d.parse(txt)) { CHECK(false, "render golden parse"); return; }
  Json all = d.root(); int ok = 0, skippedR = 0;
  for (size_t i = 0; i < all.size(); i++) {
    Json c = all.at(i), a = c.get("args"); const std::string name(c.str("name"), 0, 200); std::vector<float> out;
    if (patchLacksLib(c.get("patch"))) { skippedR++; continue; }
    if (c.str("kind") == "pre") { WavesPreArgs p; p.dur = a.num("dur"); p.pA = a.num("pA"); p.ptau = a.num("ptau"); p.tune = a.num("tune"); p.vtune = a.num("vtune"); p.fine = a.num("fine"); p.gate = a.num("gate"); p.bus = (int)a.num("bus"); p.tempo = a.num("tempo"); double rate = 0; out = waves_pre_render(c.get("patch"), p, &rate); CHECK(rate == 48000, "rate"); }
    else { WavesLiveRenderArgs p; p.bus = (int)a.num("bus"); p.tune = a.num("tune"); p.level = a.num("level"); p.drive = a.num("drive"); p.vel = a.num("vel"); p.hold = a.num("hold"); p.bpm = a.num("bpm"); p.sr = a.num("sr"); out = waves_live_render(c.get("patch"), p); }
    const std::string h = out.empty() ? "none" : hex(fnv({&out})); const bool same = h == c.str("hash") && (int)out.size() == (int)c.num("len");
    CHECK(same, "%s: %s (%zu samples) != %s (%d)", name.c_str(), h.c_str(), out.size(), std::string(c.str("hash")).c_str(), (int)c.num("len")); ok += same;
  }
  std::printf("page renders: %d of %zu equal the page's own (wavesPre / wavesLiveRender), %d skipped (a library is missing in this build)\n", ok, all.size(), skippedR);
}
int main(int argc, char** argv) {
  std::string renderFile, golden, nativeFile, refDir, filter, dumpDir, writeNative;
  for (int i = 1; i < argc; i++) { const std::string a = argv[i]; if (a == "--ref" && i + 1 < argc) refDir = argv[++i]; else if (a == "--render" && i + 1 < argc) renderFile = argv[++i]; else if (a == "--native" && i + 1 < argc) nativeFile = argv[++i]; else if (a == "--write-native" && i + 1 < argc) writeNative = argv[++i]; else if (a == "--dump" && i + 1 < argc) dumpDir = argv[++i]; else if (a == "--only" && i + 1 < argc) filter = argv[++i]; else golden = a; }
  testBehaviours();
  if (!renderFile.empty()) runRender(renderFile);
  if (golden.empty()) { CHECK(false, "no golden file given"); }
  else runGolden(golden, nativeFile, refDir, filter, dumpDir, writeNative);

  const std::string q = driveProcessor(.05), h = driveProcessor(1);
  std::printf("processor quiet %s hot %s\n", q.c_str(), h.c_str());
  CHECK(q == "9f094f39", "quiet row %s", q.c_str());
  CHECK(h == "5d99c475", "hot row %s", h.c_str());
  std::printf(fails ? "test_waves: %d FAILED\n" : "test_waves: ok\n", fails);
  return fails ? 1 : 0;
}
