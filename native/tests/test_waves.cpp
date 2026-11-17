// U30: the …waves engine (core/src/waves) against waves/waves-worklet.js. Grows with the unit; see docs/port/U30-NOTES.md for what each section gates.
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
static void runGolden(const std::string& golden, const std::string& nativeFile, const std::string& refDir, const std::string& filter, const std::string& dumpDir, const std::string& writeNative) {
  std::string txt; if (!readFile(golden, &txt)) { CHECK(false, "cannot read %s", golden.c_str()); return; }
  JsonDoc d; if (!d.parse(txt)) { CHECK(false, "golden parse"); return; }
  JsonDoc nd; std::string ntxt; Json pins; if (!nativeFile.empty() && readFile(nativeFile, &ntxt) && nd.parse(ntxt)) pins = nd.root();
  if (writeNative.empty() && filter.empty() && !pins.isObj()) CHECK(false, "the pinned native hashes (waves_native.json) are missing");
  Json all = d.root(); int ran = 0, bad = 0, tol = 0; double worst = -999; std::string worstName; std::string outPins = "{";
  for (size_t i = 0; i < all.size(); i++) {
    Json c = all.at(i); const std::string name(c.str("name")); if (!filter.empty() && name.find(filter) == std::string::npos) continue;
    CaseResult r = runCase(c); ran++;
    if (!r.ok) { CHECK(false, "%s: patch refused", name.c_str()); bad++; continue; }
    std::string fn = name; for (char& ch : fn) if (ch == '/' || ch == '@') ch = '_';
    if (!dumpDir.empty()) { std::ofstream f(dumpDir + "/" + fn + ".f32", std::ios::binary); f.write((const char*)r.L.data(), (std::streamsize)(r.L.size() * 4)); f.write((const char*)r.R.data(), (std::streamsize)(r.R.size() * 4)); }
    const std::string h = hex(fnv({&r.L, &r.R})), want(c.str("hash"));
    std::string ref; bool haveRef = !refDir.empty() && readFile(refDir + "/" + fn + ".f32", &ref); bool okdb = false; const double db = haveRef ? dbOf(r.L, r.R, ref, &okdb) : 0;
    Json pin = pins.get(name);
    if (h == want) { if (pin.isObj()) { bad++; CHECK(false, "%s: listed as inexact but now bit-exact (stale entry)", name.c_str()); } continue; }
    tol++;
    if (!writeNative.empty()) { char b[256]; std::snprintf(b, sizeof b, "%s\"%s\":{\"hash\":\"%s\",\"db\":%.1f}", outPins.size() > 1 ? "," : "", name.c_str(), h.c_str(), okdb ? db : 0.0); outPins += b; continue; }
    if (!pin.isObj()) { bad++; CHECK(false, "%s: not bit-exact (%s != %s)%s and not listed", name.c_str(), h.c_str(), want.c_str(), okdb ? (" " + std::to_string(db) + " dB").c_str() : ""); continue; }
    if (std::string(pin.str("hash")) != h) { bad++; CHECK(false, "%s: native hash %s != pinned %s", name.c_str(), h.c_str(), std::string(pin.str("hash")).c_str()); }
    if (okdb) { if (db > worst) { worst = db; worstName = name; } if (db > pin.num("db") + 1) { bad++; CHECK(false, "%s: %.1f dB is worse than the recorded %.1f dB", name.c_str(), db, pin.num("db")); }
      const bool isMods = name.rfind("mod/marbles/", 0) == 0 || name.rfind("mod/tides/", 0) == 0;
      if (isMods && db > -100) { bad++; CHECK(false, "%s: %.1f dB is above the -100 dB line for Marbles / Tides", name.c_str(), db); } }
  }
  if (!writeNative.empty()) { outPins += "}\n"; std::ofstream f(writeNative, std::ios::binary); f << outPins; std::printf("wrote %s (%d inexact cases)\n", writeNative.c_str(), tol); }
  std::printf("golden cases: %d run, %d bit-exact, %d listed as library-build differences (worst %.1f dB %s), %d wrong\n", ran, ran - tol, tol, worst, worstName.c_str(), bad);
}
int main(int argc, char** argv) {
  std::string golden, nativeFile, refDir, filter, dumpDir, writeNative;
  for (int i = 1; i < argc; i++) { const std::string a = argv[i]; if (a == "--ref" && i + 1 < argc) refDir = argv[++i]; else if (a == "--native" && i + 1 < argc) nativeFile = argv[++i]; else if (a == "--write-native" && i + 1 < argc) writeNative = argv[++i]; else if (a == "--dump" && i + 1 < argc) dumpDir = argv[++i]; else if (a == "--only" && i + 1 < argc) filter = argv[++i]; else golden = a; }
  if (golden.empty()) { CHECK(false, "no golden file given"); }
  else runGolden(golden, nativeFile, refDir, filter, dumpDir, writeNative);

  const std::string q = driveProcessor(.05), h = driveProcessor(1);
  std::printf("processor quiet %s hot %s\n", q.c_str(), h.c_str());
  CHECK(q == "9f094f39", "quiet row %s", q.c_str());
  CHECK(h == "5d99c475", "hot row %s", h.c_str());
  std::printf(fails ? "test_waves: %d FAILED\n" : "test_waves: ok\n", fails);
  return fails ? 1 : 0;
}
