// U25: the Plaits ("plaits", Macro osc) and Elements ("elements", Resonator) layer types (core/src/engine/layer_pe.cpp, the strict native build of the Mutable Instruments libraries: core/dsp/pe_host.cc)
// against the real ENGINE block of index.html running the web's fast-math thunder-dsp.wasm (tools/engine-u25-dump.mjs: samples of every case, a fresh wasm instance per voice).
// THE GATE (the lead's rule for units whose reference is a fast-math wasm, TODAY.md "Evening, 16 Nov" 1 and 7):
//  (a) the native library is strict (-fno-fast-math -ffp-contract=off): the same on every platform;
//  (b) every case (the 56 factory-style voices of the layer picker and the direct cases) is within -100 dB of the reference's peak (max |difference| / peak; samples missing at the end count as zeros), the worst printed;
//      a case that is bit-exact with the wasm today is listed (golden/engine_u25_native.inc, kPeExact) and must stay exact;
//  (c) the SHA-256 of the native render of every case (made on Linux x86-64) is committed in the same file and checked on every platform: a platform whose libm differs shows up here, as a finding, never papered over;
//  (d) a case that cannot be within the line is left out of (b) BY NAME in kTolSkip below with its numbers printed; it stays in (c). No wildcard.
//  Also: the refusals (an Elements model outside 0..2; a pre-render length the JS cannot allocate), and the allocation guard in rt_harness.
//   test_engine_u25 <refDir> [name-substring] [--write-native FILE]
// Without the library (Windows / MSVC builds no DSP libraries) the two layer types are not supported: the test says so and checks only that.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "engine/engine.h"
#include "engine/engine_json.h"
#include "engine/layer_pe.h"
#include "rt/json.h"
#include "sha256.h"
#include "golden/engine_u25_native.inc"

using namespace thunder;

static bool readFile(const std::string& path, std::string* out) { std::ifstream f(path, std::ios::binary); if (!f) return false; std::stringstream ss; ss << f.rdbuf(); *out = ss.str(); return true; }
static int fails = 0;
static const double kGateDb = -100.0;

// (d) cases left out of the tolerance gate, by name, with the reason (printed with the figures). Kept in the native-hash check.
struct TolSkip { const char* name; const char* why; };
static const TolSkip kTolSkip[] = {
  {"", ""}};   // (none yet)
static const TolSkip* skipOf(const char* nm) { for (const TolSkip& k : kTolSkip) if (k.name[0] && !std::strcmp(k.name, nm)) return &k; return nullptr; }
static bool inExact(const char* nm) { for (int i = 0; i < kPeExactN; i++) if (!std::strcmp(kPeExact[i], nm)) return true; return false; }
static const char* nativeSha(const char* nm) { for (int i = 0; i < kPeNativeN; i++) if (!std::strcmp(kPeNative[i].name, nm)) return kPeNative[i].sha256; return nullptr; }

struct Cmp { bool exact = true; double db = -INFINITY, err = 0, peak = 0; long nDiff = 0, first = -1; double got = 0, want = 0; size_t nGot = 0, nRef = 0; bool nan = false; };
static Cmp compareF(const float* a, size_t na, const float* r, size_t nr) {
  Cmp c; c.nGot = na; c.nRef = nr; const size_t n = std::max(na, nr);
  for (size_t i = 0; i < n; i++) {
    const float x = i < na ? a[i] : 0.0f, y = i < nr ? r[i] : 0.0f;
    if (!std::isnan(y)) c.peak = std::max(c.peak, (double)std::fabs(y));
    if (std::memcmp(&x, &y, 4) == 0 || (std::isnan(x) && std::isnan(y))) continue;
    c.exact = false; c.nDiff++; if (c.first < 0) { c.first = (long)i; c.got = x; c.want = y; }
    if (std::isnan(x) != std::isnan(y)) { c.nan = true; c.err = INFINITY; } else c.err = std::max(c.err, std::fabs((double)x - (double)y));
  }
  c.db = c.exact ? -INFINITY : (c.nan ? INFINITY : 20 * std::log10(c.err / std::max(c.peak, 1e-30)));
  return c;
}
struct Row { std::string name, sha; bool exact; };
static std::vector<Row> gRows; static bool gWrite = false;
static int gOk = 0, gExact = 0, gSkipped = 0, gVoices = 0, gVoicesOk = 0; static double gWorst = -INFINITY; static std::string gWorstName;

int main(int argc, char** argv) {
  if (!pe_available()) {
    std::printf("engine U25: NOT BUILT on this platform (no Plaits / Elements library: the DSP libraries are not built here): the two layer types are not supported; checking only that\n");
    int bad = 0; if (eng_layer_type("plaits", 6) != LType::Other || eng_layer_type("elements", 8) != LType::Other) { std::printf("FAIL: the types must be unknown without the library\n"); bad++; }
    EngVoice V; V.layers[0].on = true; V.layers[0].type = LType::Plaits; EngRenderOpts o; size_t n = 0; float tmp[64];
    if (eng_render_voice(V, o, tmp, 64, &n) != kEngUnsupported) { std::printf("FAIL: a Plaits layer must be refused without the library\n"); bad++; }
    if (!bad) std::printf("PASS test_engine_u25 (library not built)\n");
    return bad ? 1 : 0;
  }
  if (argc < 2) { std::printf("FAIL: usage: test_engine_u25 <refDir> [name-substring] [--write-native FILE]\n"); return 1; }
  const std::string refDir = argv[1]; std::string filt; const char* writeNative = nullptr;
  for (int i = 2; i < argc; i++) { if (!std::strcmp(argv[i], "--write-native") && i + 1 < argc) { writeNative = argv[++i]; gWrite = true; } else filt = argv[i]; }
  std::string cj, rj; if (!readFile(refDir + "/cases.json", &cj) || !readFile(refDir + "/ref.json", &rj)) { std::printf("FAIL: the reference (cases.json, ref.json) is missing: a fixture that cannot run is a failure\n"); return 1; }
  JsonDoc cdoc, rdoc; if (!cdoc.parse(cj) || !rdoc.parse(rj)) { std::printf("FAIL: a reference file does not parse\n"); return 1; }
  eng_init();
  Json C = cdoc.root().arr("cases"), R = rdoc.root().arr("cases");
  if (!(C.size() == R.size() && C.size() > 0)) { std::printf("FAIL: %zu cases, %zu reference rows\n", C.size(), R.size()); return 1; }
  std::printf("reference wasm: %s\n", std::string(rdoc.root().str("wasm")).c_str());
  int ran = 0;
  for (size_t ci = 0; ci < C.size(); ci++) {
    Json c = C.at(ci); const std::string name(c.str("name")); if (!filt.empty() && name.find(filt) == std::string::npos) continue;
    const bool voice = c.str("kind") == "voice"; ran++; if (voice) gVoices++;
    EngVoice V; if (!eng_voice_from_json(V, c.obj("v"), c.arr("L"), c.arr("fx"))) { std::printf("%s: loader refused  FAIL\n", name.c_str()); fails++; continue; }
    EngRenderOpts o; o.sr = c.num("sr"); o.hold = c.num("hold"); o.bpm = c.num("bpm", 120);
    const size_t bound = eng_render_bound(V, o); std::vector<float> buf(bound + 8, 12345.0f); size_t n = 0;
    const int st = eng_render_voice(V, o, buf.data(), bound, &n);
    if (st != kEngOk) { std::printf("%-34s status %d  FAIL\n", name.c_str(), st); fails++; continue; }
    if (buf[bound] != 12345.0f || buf[bound + 7] != 12345.0f) { std::printf("%s: wrote past the bound  FAIL\n", name.c_str()); fails++; }
    std::string fb; std::vector<float> ref;
    if (!readFile(refDir + "/" + name + ".f32", &fb) || fb.size() % 4) { std::printf("%-34s reference samples missing  FAIL\n", name.c_str()); fails++; continue; }
    ref.resize(fb.size() / 4); std::memcpy(ref.data(), fb.data(), fb.size());
    Json rrow = R.at(ci); if (std::string(rrow.str("name")) != name || (size_t)rrow.num("len") != ref.size()) { std::printf("%s: ref.json out of step with the samples  FAIL\n", name.c_str()); fails++; continue; }
    const Cmp k = compareF(buf.data(), n, ref.data(), ref.size());
    Sha256 h; h.add((const char*)buf.data(), n * 4); const std::string sha = h.hex();
    gRows.push_back({name, sha, k.exact && n == ref.size()});
    const TolSkip* sk = skipOf(name.c_str());
    bool ok = true; const char* why = "";
    if (!sk && !(k.exact || k.db <= kGateDb)) { ok = false; why = "  beyond the line"; }
    if (!gWrite) {
      if (inExact(name.c_str()) && !(k.exact && n == ref.size())) { ok = false; why = "  was bit-exact: the exact list is enforced"; }
      const char* w = nativeSha(name.c_str()); if (!w || sha != w) { ok = false; why = w ? "  NATIVE HASH DIFFERS from the committed one (golden/engine_u25_native.inc)" : "  no committed native hash"; }
    }
    if (sk) { gSkipped++; std::printf("%-34s LEFT OUT of the line (%s): %.1f dB, %ld of %zu samples differ, first at %ld: got %.9g want %.9g; native %zu samples, reference %zu%s\n", name.c_str(), sk->why, k.db, k.nDiff, std::max(k.nGot, k.nRef), k.first, k.got, k.want, k.nGot, k.nRef, ok ? "" : why); }
    else if (ok) { gOk++; if (k.exact && n == ref.size()) gExact++; else if (k.db > gWorst) { gWorst = k.db; gWorstName = name; } if (voice) gVoicesOk++; }
    if (!sk && (!k.exact || !ok)) std::printf("%-34s %.1f dB (%ld of %zu samples differ; lengths %zu / %zu)%s%s\n", name.c_str(), k.db, k.nDiff, std::max(k.nGot, k.nRef), k.nGot, k.nRef, why, ok ? "" : "  FAIL");
    if (!ok) { fails++; std::printf("    first differing sample %ld: got %.9g want %.9g; largest difference %.3g against a peak of %.3g\n", k.first, k.got, k.want, k.err, k.peak); }
  }
  // the refusals
  if (filt.empty()) {
    EngRenderOpts o; size_t n = 0; float tmp[1 << 14];
    EngVoice V; V.layers[0].on = true; V.layers[0].type = LType::Elements; V.layers[0].pe.eModel = 3; V.len = 0.05;
    if (eng_render_voice(V, o, tmp, 1 << 14, &n) != kEngUnsupported) { std::printf("FAIL: an Elements model outside 0..2 must be refused\n"); fails++; }
    EngVoice W; W.layers[0].on = true; W.layers[0].type = LType::Plaits; W.layers[0].delay = 0; W.layers[0].atk = -5000; W.layers[0].dec = 1; W.layers[0].hold = -4000; W.len = 0.05;
    const int st = eng_render_voice(W, o, tmp, 1 << 14, &n); (void)st;   // (a negative layer length: whatever the JS does with it is refused or silent, never a crash)
    for (const char* nm : {"plaits", "elements"}) if (!eng_type_supported(eng_layer_type(nm, std::strlen(nm)))) { std::printf("FAIL: %s is not a supported layer type\n", nm); fails++; }
  }
  if (gRows.empty()) { std::printf("FAIL: no case ran\n"); fails++; }
  std::printf("engine U25 (native strict against the fast-math wasm): %d cases (%d factory-style voices), %d inside the %.0f dB line (%d of them bit-exact; %d factory-style voices), %d left out by name; worst of the rest %s (%s); %d failed\n",
    ran, gVoices, gOk, kGateDb, gExact, gVoicesOk, gSkipped, std::isinf(gWorst) ? "none" : (std::to_string(gWorst) + " dB").c_str(), gWorstName.c_str(), fails);
  if (writeNative) {
    std::ofstream o(writeNative); int ne = 0; for (auto& r : gRows) if (r.exact) ne++;
    o << "// GENERATED by test_engine_u25 --write-native: the Plaits / Elements layer types, the native STRICT build against the fast-math thunder-dsp.wasm (docs/port/U25-NOTES.md). kPeExact: the cases that are bit-exact with the wasm today\n// (a case that leaves the list fails); kPeNative: the SHA-256 of the native render of every case (made on Linux x86-64, checked on every platform). Never hand-edited.\n";
    o << "static const char* const kPeExact[] = {\n"; for (auto& r : gRows) if (r.exact) o << "  \"" << r.name << "\",\n"; o << "  \"\"};\nstatic const int kPeExactN = " << ne << ";\n";
    o << "struct PeNative { const char* name; const char* sha256; };\nstatic const PeNative kPeNative[] = {\n"; for (auto& r : gRows) o << "  {\"" << r.name << "\", \"" << r.sha << "\"},\n"; o << "  {\"\", \"\"}};\nstatic const int kPeNativeN = " << gRows.size() << ";\n";
    std::printf("wrote %s: %d exact, %zu hashes\n", writeNative, ne, gRows.size());
  }
  if (!fails) std::printf("PASS test_engine_u25\n");
  return fails ? 1 : 0;
}
