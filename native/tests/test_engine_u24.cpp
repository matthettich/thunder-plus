// U24: the FM / additive layer types (core/src/engine/layer_*.cpp: fm2 fmfb fmmetal fmcross fm4 fm6 genesis sb arcade, addsaw addodd membrane bell bar chord, formant karplus vosim) against the real
// ENGINE block of index.html (tools/engine-u24-dump.mjs runs it in Node) and beta's tests/golden/engines.json. Same pattern as test_engine (U22).
//  1. THE GATE: each of the 110 factory voices built only from U22's, U23's and U24's layer types, with at least one of U24's: renderVoice(v, 48000, null, hold) in C++ gives the SAME SHA-256 of the float32
//     bytes as beta's golden (tests/golden/engine_u24_voices_golden.inc: beta's rows, copied, never edited), on every platform.
//  2. The direct cases (random voices and written-out edge cases through the real renderVoice): the same hash as the committed Linux hashes (engine_u24_golden.inc) on every platform.
//  3. The local Node's run of this repo's ENGINE block (ref.json) must give the same hashes on x86-64 Linux (macOS arm64's Math.sin / cos / exp / pow differ from V8 on x86-64 Linux: reported there).
//  4. The refusals: a layer of a type that is not here (U23's, U25's ...) and one the JS throws on give kEngUnsupported. A reference that is missing is a FAILURE, never a skip.
//   test_engine_u24 <refDir> <engine_u24_voices.json> [name-substring] [--first]
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
#include "rt/json.h"
#include "sha256.h"
#include "golden/engine_u24_voices_golden.inc"
#include "golden/engine_u24_golden.inc"

using namespace thunder;

static bool readFile(const std::string& path, std::string* out) { std::ifstream f(path, std::ios::binary); if (!f) return false; std::stringstream ss; ss << f.rdbuf(); *out = ss.str(); return true; }
static int fails = 0;
static uint32_t bits32(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }

struct Row { std::string sha; long len; };
// renders one voice / case and compares: the committed hash (gate), the local Node's (x86-64 Linux only). Returns true if it is bit-exact with the committed hash.
static bool runOne(const std::string& name, Json v, Json L, Json fx, double sr, double hold, double bpm, const Row* gold, const Row* local, bool localMustMatch, const std::string& f32path, int* localDiff) {
  EngVoice V; if (!eng_voice_from_json(V, v, L, fx)) { std::printf("%s: loader refused\n", name.c_str()); fails++; return false; }
  EngRenderOpts o; o.sr = sr; o.hold = hold; o.bpm = bpm;
  const size_t bound = eng_render_bound(V, o); std::vector<float> buf(bound + 8, 12345.0f);
  size_t n = 0; const int st = eng_render_voice(V, o, buf.data(), bound, &n);
  if (st != kEngOk || !gold || !local) { std::printf("%s: status %d, golden %s, reference %s  FAIL\n", name.c_str(), st, gold ? "there" : "missing", local ? "there" : "missing"); fails++; return false; }
  if (buf[bound] != 12345.0f || buf[bound + 7] != 12345.0f) { std::printf("%s: wrote past the bound  FAIL\n", name.c_str()); fails++; }
  Sha256 h; h.add((const char*)buf.data(), n * 4); const std::string hex = h.hex();
  const bool hashOk = hex == gold->sha && (long)n == gold->len, localOk = hex == local->sha && (long)n == local->len;
  if (!localOk) ++*localDiff;
  if (!(hashOk && (localOk || !localMustMatch))) {
    fails++; std::printf("%-34s %s%s  (len %zu, want %ld)\n", name.c_str(), hashOk ? "bit-exact (committed hash)" : "DIFFERS from the committed hash", localOk ? "" : "  [the local Node reference differs]", n, gold->len);
    std::string f; if (readFile(f32path, &f)) { const float* w = (const float*)f.data(); const size_t nw = f.size() / 4; size_t d = 0, first = (size_t)-1;
      for (size_t i = 0; i < (n < nw ? n : nw); i++) if (bits32(buf[i]) != bits32(w[i])) { if (first == (size_t)-1) first = i; d++; }
      if (first != (size_t)-1) std::printf("    %zu of %zu samples differ; first at %zu: got %.9g want %.9g\n", d, nw, first, (double)buf[first], (double)w[first]); }
  }
  return hashOk;
}

int main(int argc, char** argv) {
  if (argc < 3) { std::printf("FAIL: usage: test_engine_u24 <refDir> <engine_u24_voices.json> [name-substring] [--first]\n"); return 1; }
  std::string refDir = argv[1], fixPath = argv[2], filter; bool firstOnly = false;
  for (int i = 3; i < argc; i++) { if (!std::strcmp(argv[i], "--first")) firstOnly = true; else filter = argv[i]; }
  std::string rj, fj, cj;
  if (!readFile(refDir + "/ref.json", &rj) || !readFile(refDir + "/cases.json", &cj) || !readFile(fixPath, &fj)) { std::printf("FAIL: the reference (ref.json, cases.json) or the fixture is missing\n"); return 1; }
  JsonDoc rdoc, fdoc, cdoc; if (!rdoc.parse(rj) || !fdoc.parse(fj) || !cdoc.parse(cj)) { std::printf("FAIL: a reference file does not parse\n"); return 1; }
  eng_init();
#if defined(__linux__) && (defined(__x86_64__) || defined(_M_X64))
  const bool localMustMatch = true;
#else
  const bool localMustMatch = false;
#endif
  std::map<std::string, Row> gold, cgold, ref, cref;
  for (int i = 0; i < kEngineU24GoldenN; i++) gold[kEngineU24Golden[i].key] = {kEngineU24Golden[i].sha256, kEngineU24Golden[i].len};
  for (int i = 0; i < kEngineU24CaseGoldenN; i++) cgold[kEngineU24CaseGolden[i].name] = {kEngineU24CaseGolden[i].sha256, kEngineU24CaseGolden[i].len};
  Json R = rdoc.root().arr("voices"); for (size_t i = 0; i < R.size(); i++) ref[std::string(R.at(i).str("k"))] = {std::string(R.at(i).str("sha256")), (long)R.at(i).num("len")};
  Json RC = rdoc.root().arr("cases"); for (size_t i = 0; i < RC.size(); i++) cref[std::string(RC.at(i).str("name"))] = {std::string(RC.at(i).str("sha256")), (long)RC.at(i).num("len")};
  Json F = fdoc.root().arr("voices"), C = cdoc.root().arr("cases");
  if (!(F.size() == (size_t)kEngineU24GoldenN && F.size() == R.size() && F.size() > 0)) { std::printf("FAIL: fixture %zu voices, golden %d, reference %zu\n", F.size(), kEngineU24GoldenN, R.size()); fails++; }
  if (!(C.size() == (size_t)kEngineU24CaseGoldenN && C.size() == RC.size() && C.size() > 0)) { std::printf("FAIL: direct cases %zu, golden %d, reference %zu\n", C.size(), kEngineU24CaseGoldenN, RC.size()); fails++; }
  int ran = 0, exact = 0, localDiff = 0;
  for (size_t vi = 0; vi < F.size(); vi++) {
    Json fv = F.at(vi); const std::string key(fv.str("k")); if (!filter.empty() && key.find(filter) == std::string::npos) continue;
    const auto g = gold.find(key), r = ref.find(key); ran++;
    if (runOne(key, fv.obj("v"), fv.arr("L"), fv.arr("fx"), 48000, fv.num("hold"), 120, g == gold.end() ? nullptr : &g->second, r == ref.end() ? nullptr : &r->second, localMustMatch, refDir + "/" + std::to_string(vi) + ".f32", &localDiff)) exact++;
    if (firstOnly && fails) break;
  }
  int cran = 0, cexact = 0;
  for (size_t ci = 0; ci < C.size(); ci++) {
    Json c = C.at(ci); const std::string name(c.str("name")); if (!filter.empty() && name.find(filter) == std::string::npos) continue;
    const auto g = cgold.find(name), r = cref.find(name); cran++;
    if (runOne(name, c.obj("v"), c.arr("L"), c.arr("fx"), c.num("sr"), c.num("hold"), c.num("bpm", 120), g == cgold.end() ? nullptr : &g->second, r == cref.end() ? nullptr : &r->second, localMustMatch, refDir + "/case" + std::to_string(ci) + ".f32", &localDiff)) cexact++;
    if (firstOnly && fails) break;
  }
  // the refusals: a type of another unit, and the layers the JS throws on (a chord shape past the table; an FM algorithm that is NaN)
  { EngRenderOpts o; size_t n = 0; float tmp[64];
    EngVoice V; V.layers[0].on = true; V.layers[0].type = LType::Other;
    if (eng_render_voice(V, o, tmp, 64, &n) != kEngUnsupported) { std::printf("FAIL: a layer of a type of another unit must be refused\n"); fails++; }
    EngVoice W; W.layers[0].on = true; W.layers[0].type = LType::Chord; W.layers[0].a = 1.5;
    if (eng_render_voice(W, o, tmp, 64, &n) != kEngUnsupported) { std::printf("FAIL: a chord shape past the table (the JS throws) must be refused\n"); fails++; }
    EngVoice X; X.layers[0].on = true; X.layers[0].type = LType::Fm4; X.layers[0].a = NAN;
    if (eng_render_voice(X, o, tmp, 64, &n) != kEngUnsupported) { std::printf("FAIL: an FM algorithm that is NaN (the JS throws) must be refused\n"); fails++; }
    static const char* const names[] = {"fm2", "fmfb", "fmmetal", "fmcross", "fm4", "fm6", "genesis", "sb", "arcade", "addsaw", "addodd", "membrane", "bell", "bar", "chord", "formant", "karplus", "vosim"};
    for (const char* nm : names) if (!eng_type_supported(eng_layer_type(nm, std::strlen(nm)))) { std::printf("FAIL: %s is not a supported layer type\n", nm); fails++; }
    if (eng_type_supported(eng_layer_type("plaits", 6))) { std::printf("FAIL: plaits must not be supported\n"); fails++; } }
  std::printf("engine U24: %d of %d factory voices and %d of %d direct cases bit-exact against the committed hashes (beta's golden / the real JS on x86-64 Linux); local Node reference differs for %d; %d failed\n", exact, ran, cexact, cran, localDiff, fails);
  if (exact != ran || cexact != cran) fails++;
  if (!fails) std::printf("PASS test_engine_u24\n");
  return fails ? 1 : 0;
}
