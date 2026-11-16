// U23: the layer types click, wavetable, pd, chip, supersaw, pwm, sample (with its loop and the vintage modes) and grains (core/src/engine/layer_*.cpp) against the real ENGINE block of index.html
// (tools/engine-u23-dump.mjs runs it in Node) and against tests/golden/engines.json of `beta`.
// For each factory voice whose layers are of U22's or U23's types (at least one of U23's): renderVoice(v, 48000, null, hold) in C++ gives the SAME SHA-256 of the float32 bytes as beta's golden (THE GATE, on every
// platform: golden/engine_u23_voices_golden.inc holds beta's rows, copied, never edited). Then the direct cases (random voices and written-out edge cases through the real renderVoice, samples included): the
// SAME hash as the committed Linux golden (golden/engine_u23_golden.inc) on every platform. Both are also compared with the local Node's run of this repo's ENGINE block: that comparison must hold on x86-64
// Linux, elsewhere it is reported (Node on macOS arm64 rounds Math.sin / cos / exp / pow / tan differently). A reference that is missing is a FAILURE, never a skip.
//   test_engine_u23 <refDir> <engine_u23_voices.json> [voice-or-case-substring] [--first]    (refDir holds ref.json and cases.json, and u<n>.f32 / c<n>.f32 if made with --samples)
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "engine/engine.h"
#include "engine/engine_json.h"
#include "engine/layers.h"
#include "rt/json.h"
#include "sha256.h"
#include "golden/engine_u23_voices_golden.inc"
#include "golden/engine_u23_golden.inc"

using namespace thunder;

static bool readFile(const std::string& path, std::string* out) { std::ifstream f(path, std::ios::binary); if (!f) return false; std::stringstream ss; ss << f.rdbuf(); *out = ss.str(); return true; }
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; std::printf("FAIL: " __VA_ARGS__); std::printf("\n"); } } while (0)
static uint32_t bits32(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }

#if defined(__linux__) && (defined(__x86_64__) || defined(_M_X64))
static const bool localMustMatch = true;
#else
static const bool localMustMatch = false;
#endif

static void firstDiff(const std::string& path, const std::vector<float>& buf, size_t n) {
  std::string f32; if (!readFile(path, &f32)) return;
  const float* w = (const float*)f32.data(); const size_t nw = f32.size() / 4; size_t d = 0, first = (size_t)-1;
  for (size_t i = 0; i < (n < nw ? n : nw); i++) if (bits32(buf[i]) != bits32(w[i])) { if (first == (size_t)-1) first = i; d++; }
  if (first != (size_t)-1) std::printf("    %zu of %zu samples differ; first at %zu: got %.9g want %.9g\n", d, nw, first, (double)buf[first], (double)w[first]);
}

// one voice or case: fill, render, hash. Returns the status; *hex and *n set on success.
static int renderOne(Json c, EngSampleBank& bank, double sr, double hold, double bpm, std::vector<float>& buf, std::string* hex, size_t* n, int* nLayers) {
  EngVoice V; if (!eng_voice_from_json(V, c.obj("v"), c.arr("L"), c.arr("fx"))) return -100;
  Json L = c.arr("L"); *nLayers = (int)L.size();
  for (size_t i = 0; i < L.size(); i++) { Json s = L.at(i).get("sample"); if (s.isObj()) bank.attach(V.layers[i], s); }
  EngRenderOpts o; o.sr = sr; o.hold = hold; o.bpm = bpm;
  const size_t bound = eng_render_bound(V, o); buf.assign(bound + 8, 12345.0f);
  *n = 0; const int st = eng_render_voice(V, o, buf.data(), bound, n);
  if (st != kEngOk) return st;
  CHECK(buf[bound] == 12345.0f && buf[bound + 7] == 12345.0f, "wrote past the bound");
  Sha256 h; h.add((const char*)buf.data(), *n * 4); *hex = h.hex();
  return kEngOk;
}

int main(int argc, char** argv) {
  if (argc < 3) { std::printf("FAIL: usage: test_engine_u23 <refDir> <engine_u23_voices.json> [substring] [--first]\n"); return 1; }
  std::string refDir = argv[1], fixPath = argv[2], filter; bool firstOnly = false;
  for (int i = 3; i < argc; i++) { if (!std::strcmp(argv[i], "--first")) firstOnly = true; else filter = argv[i]; }
  std::string rj, fj, cj;
  if (!readFile(refDir + "/ref.json", &rj) || !readFile(refDir + "/cases.json", &cj) || !readFile(fixPath, &fj)) { std::printf("FAIL: the reference (ref.json, cases.json) or the fixture is missing\n"); return 1; }
  JsonDoc rdoc, fdoc, cdoc; if (!rdoc.parse(rj) || !fdoc.parse(fj) || !cdoc.parse(cj)) { std::printf("FAIL: a reference file does not parse\n"); return 1; }
  eng_init();
  std::vector<float> buf;
  // ---- the factory voices
  {
    std::map<std::string, const U23Golden*> gold; for (int i = 0; i < kU23GoldenN; i++) gold[kU23Golden[i].key] = &kU23Golden[i];
    std::map<std::string, std::pair<std::string, long>> ref; Json R = rdoc.root().arr("voices"); for (size_t i = 0; i < R.size(); i++) ref[std::string(R.at(i).str("k"))] = {std::string(R.at(i).str("sha256")), (long)R.at(i).num("len")};
    Json F = fdoc.root().arr("voices");
    CHECK(F.size() == (size_t)kU23GoldenN && F.size() == R.size() && F.size() > 0, "fixture %zu voices, golden %d, reference %zu", F.size(), kU23GoldenN, R.size());
    int ran = 0, exact = 0, localDiff = 0; std::set<std::string> types;
    for (size_t vi = 0; vi < F.size(); vi++) {
      Json fv = F.at(vi); const std::string key(fv.str("k"));
      if (!filter.empty() && key.find(filter) == std::string::npos) continue;
      for (size_t li = 0; li < fv.arr("L").size(); li++) types.insert(std::string(fv.arr("L").at(li).str("type")));
      EngSampleBank bank; std::string hex; size_t n = 0; int nl = 0;
      const int st = renderOne(fv, bank, 48000, fv.num("hold"), 120, buf, &hex, &n, &nl); ran++;
      const auto g = gold.find(key); const auto r = ref.find(key);
      if (st != kEngOk || g == gold.end() || r == ref.end()) { CHECK(false, "%s: status %d, golden %s, reference %s", key.c_str(), st, g == gold.end() ? "missing" : "there", r == ref.end() ? "missing" : "there"); if (firstOnly) break; continue; }
      const bool hashOk = hex == g->second->sha256 && (long)n == g->second->len, localOk = hex == r->second.first && (long)n == r->second.second;
      if (hashOk) exact++;
      if (!localOk) localDiff++;
      if (!(hashOk && (localOk || !localMustMatch))) {
        fails++; std::printf("%-44s %s%s  (len %zu, want %ld)\n", key.c_str(), hashOk ? "bit-exact (beta's golden)" : "DIFFERS from beta's golden", localOk ? "" : "  [the local Node reference differs]", n, g->second->len);
        firstDiff(refDir + "/u" + std::to_string(vi) + ".f32", buf, n);
        if (firstOnly) break;
      }
    }
    std::string tl; for (const auto& t : types) tl += (tl.empty() ? "" : " ") + t;
    std::printf("engine u23: %d of %d factory voices bit-exact against beta's golden (layer types: %s), local Node reference differs for %d\n", exact, ran, tl.c_str(), localDiff);
    if (exact != ran) fails++;
    if (filter.empty()) {   // the types this unit gates by factory voices: each of them must be there (the sample, grains, pwm and vint layers have no factory voice: the direct cases carry them)
      for (const char* t : {"click", "wavetable", "pd", "chip", "supersaw"}) CHECK(types.count(t), "no factory voice of type %s in the fixture", t);
    }
  }
  // ---- the direct cases
  {
    Json C = cdoc.root().arr("cases");
    std::map<std::string, const U23CaseGolden*> cg; for (int i = 0; i < kU23CaseGoldenN; i++) cg[kU23CaseGolden[i].name] = &kU23CaseGolden[i];
    std::map<std::string, std::pair<std::string, long>> cref; Json RR = rdoc.root().arr("cases"); for (size_t i = 0; i < RR.size(); i++) cref[std::string(RR.at(i).str("name"))] = {std::string(RR.at(i).str("sha256")), (long)RR.at(i).num("len")};
    CHECK(C.size() == (size_t)kU23CaseGoldenN && C.size() == cref.size() && C.size() > 0, "direct cases: %zu, golden %d, reference %zu", C.size(), kU23CaseGoldenN, cref.size());
    int cran = 0, cexact = 0, cdiff = 0; std::map<std::string, int> typeCount;
    for (size_t ci = 0; ci < C.size(); ci++) {
      Json c = C.at(ci); const std::string name(c.str("name"));
      if (!filter.empty() && name.find(filter) == std::string::npos) continue;
      for (size_t li = 0; li < c.arr("L").size(); li++) typeCount[std::string(c.arr("L").at(li).str("type"))]++;
      EngSampleBank bank; std::string hex; size_t n = 0; int nl = 0;
      const int st = renderOne(c, bank, c.num("sr"), c.num("hold"), c.num("bpm", 120), buf, &hex, &n, &nl);
      const auto g = cg.find(name); const auto r = cref.find(name); cran++;
      if (st != kEngOk || g == cg.end() || r == cref.end()) { CHECK(false, "%s: status %d", name.c_str(), st); if (firstOnly) break; continue; }
      const bool hashOk = hex == g->second->sha256 && (long)n == g->second->len, localOk = hex == r->second.first && (long)n == r->second.second;
      if (hashOk) cexact++;
      if (!localOk) cdiff++;
      if (!(hashOk && (localOk || !localMustMatch))) {
        fails++; std::printf("%-16s %s%s  (len %zu, want %ld; sr %g hold %g, %d layers on)\n", name.c_str(), hashOk ? "bit-exact (committed hash)" : "DIFFERS from the committed hash", localOk ? "" : "  [the local Node reference differs]", n, g->second->len, c.num("sr"), c.num("hold"), nl);
        firstDiff(refDir + "/c" + std::to_string(ci) + ".f32", buf, n);
        if (firstOnly) break;
      }
    }
    std::string tl; for (const auto& t : typeCount) tl += (tl.empty() ? "" : " ") + t.first + ":" + std::to_string(t.second);
    std::printf("engine u23 direct cases: %d of %d bit-exact against the committed hashes, local Node differs for %d (layers: %s)\n", cexact, cran, cdiff, tl.c_str());
    if (cexact != cran) fails++;
    if (filter.empty()) for (const char* t : {"click", "wavetable", "pd", "chip", "supersaw", "pwm", "sample", "grains"}) CHECK(typeCount[t] >= 12, "direct cases carry too few %s layers (%d)", t, typeCount[t]);
  }
  // the refusals: a type of a later unit is still refused; a sample layer with no sample is silent, not refused
  { EngVoice V; V.layers[0].on = true; V.layers[0].type = LType::Other; EngRenderOpts o; size_t n = 0; float tmp[64];
    CHECK(eng_render_voice(V, o, tmp, 64, &n) == kEngUnsupported, "a layer of a type of a later unit must be refused");
    EngVoice W; W.layers[0].on = true; W.layers[0].type = LType::Sample; const size_t b = eng_render_bound(W, o); std::vector<float> out(b + 1);
    CHECK(b > 1 && eng_render_voice(W, o, out.data(), out.size(), &n) == kEngOk, "a sample layer with no sample must render (silently)");
    CHECK(eng_layer_type("wavetable", 9) == LType::Wavetable && eng_layer_type("fm2", 3) == LType::Other, "layer type names"); }
  std::printf("engine u23: %d failed\n", fails);
  if (!fails) std::printf("PASS test_engine_u23\n");
  return fails ? 1 : 0;
}
