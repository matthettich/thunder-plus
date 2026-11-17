// U28: the rendered-only engines tone, modal, swarm, drift, piano, ep, organ, breath, strings, fmdrum, wind, phase, partials, skin and chip (core/src/engine/xeng_*.cpp) against the real ENGINE block of index.html
// (tools/engine-xeng-dump.mjs runs it in Node) and against tests/golden/engines.json of `beta`.
// For each factory voice whose engine is one of those: renderVoice(v, 48000, null, hold) in C++ gives the SAME SHA-256 of the float32 bytes as beta's golden (THE GATE, on every platform: golden/engine_xeng_voices_golden.inc
// holds beta's rows, copied, never edited). Then the direct cases (random parameters of every engine, the clamps, other sample rates, the pad effects, through the real renderVoice): the SAME hash as the committed Linux
// golden (golden/engine_xeng_golden.inc) on every platform. Both are also compared with the local Node's run of this repo's ENGINE block: that comparison must hold on x86-64 Linux, elsewhere it is reported (Node on
// macOS arm64 rounds Math.sin / cos / exp / pow / tan differently). A reference that is missing is a FAILURE, never a skip.
//   test_engine_xeng <refDir> <engine_xeng_voices.json> [voice-or-case-substring] [--first]    (refDir holds ref.json and cases.json, and u<n>.f32 / c<n>.f32 if made with --samples)
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
#include "engine/xeng.h"
#include "rt/json.h"
#include "sha256.h"
#include "golden/engine_xeng_voices_golden.inc"
#include "golden/engine_xeng_golden.inc"

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
static int renderOne(Json c, double sr, double hold, double bpm, std::vector<float>& buf, std::string* hex, size_t* n, std::string* kind) {
  XengVoice V; if (!xeng_voice_from_json(V, c.obj("v"), c.arr("fx"))) return -100;
  *kind = std::string(c.obj("v").str("engine"));
  EngRenderOpts o; o.sr = sr; o.hold = hold; o.bpm = bpm;
  const size_t bound = xeng_render_bound(V, o); buf.assign(bound + 8, 12345.0f);
  *n = 0; const int st = xeng_render_voice(V, o, buf.data(), bound, n);
  if (st != kEngOk) return st;
  CHECK(buf[bound] == 12345.0f && buf[bound + 7] == 12345.0f, "wrote past the bound");
  CHECK(*n <= bound, "length %zu above the bound %zu", *n, bound);
  Sha256 h; h.add((const char*)buf.data(), *n * 4); *hex = h.hex();
  return kEngOk;
}

int main(int argc, char** argv) {
  if (argc < 3) { std::printf("FAIL: usage: test_engine_xeng <refDir> <engine_xeng_voices.json> [substring] [--first]\n"); return 1; }
  std::string refDir = argv[1], fixPath = argv[2], filter; bool firstOnly = false;
  for (int i = 3; i < argc; i++) { if (!std::strcmp(argv[i], "--first")) firstOnly = true; else filter = argv[i]; }
  std::string rj, fj, cj;
  if (!readFile(refDir + "/ref.json", &rj) || !readFile(refDir + "/cases.json", &cj) || !readFile(fixPath, &fj)) { std::printf("FAIL: the reference (ref.json, cases.json) or the fixture is missing\n"); return 1; }
  JsonDoc rdoc, fdoc, cdoc; if (!rdoc.parse(rj) || !fdoc.parse(fj) || !cdoc.parse(cj)) { std::printf("FAIL: a reference file does not parse\n"); return 1; }
  eng_init();
  std::vector<float> buf;
  static const char* const kKinds[15] = {"tone", "modal", "swarm", "drift", "piano", "ep", "organ", "breath", "strings", "fmdrum", "wind", "phase", "partials", "skin", "chip"};
  // ---- the factory voices
  {
    std::map<std::string, const XengGolden*> gold; for (int i = 0; i < kXengGoldenN; i++) gold[kXengGolden[i].key] = &kXengGolden[i];
    std::map<std::string, std::pair<std::string, long>> ref; Json R = rdoc.root().arr("voices"); for (size_t i = 0; i < R.size(); i++) ref[std::string(R.at(i).str("k"))] = {std::string(R.at(i).str("sha256")), (long)R.at(i).num("len")};
    Json F = fdoc.root().arr("voices");
    CHECK(F.size() == (size_t)kXengGoldenN && F.size() == R.size() && F.size() > 0, "fixture %zu voices, golden %d, reference %zu", F.size(), kXengGoldenN, R.size());
    int ran = 0, exact = 0, localDiff = 0; std::map<std::string, std::pair<int, int>> per;
    for (size_t vi = 0; vi < F.size(); vi++) {
      Json fv = F.at(vi); const std::string key(fv.str("k"));
      if (!filter.empty() && key.find(filter) == std::string::npos) continue;
      std::string hex, kind; size_t n = 0;
      const int st = renderOne(fv, 48000, fv.num("hold"), 120, buf, &hex, &n, &kind); ran++;
      const auto g = gold.find(key); const auto r = ref.find(key);
      per[kind].second++;
      if (st != kEngOk || g == gold.end() || r == ref.end()) { CHECK(false, "%s: status %d, golden %s, reference %s", key.c_str(), st, g == gold.end() ? "missing" : "there", r == ref.end() ? "missing" : "there"); if (firstOnly) break; continue; }
      const bool hashOk = hex == g->second->sha256 && (long)n == g->second->len, localOk = hex == r->second.first && (long)n == r->second.second;
      if (hashOk) { exact++; per[kind].first++; }
      if (!localOk) localDiff++;
      if (!(hashOk && (localOk || !localMustMatch))) {
        fails++; std::printf("%-44s %s%s  (len %zu, want %ld)\n", key.c_str(), hashOk ? "bit-exact (beta's golden)" : "DIFFERS from beta's golden", localOk ? "" : "  [the local Node reference differs]", n, g->second->len);
        firstDiff(refDir + "/u" + std::to_string(vi) + ".f32", buf, n);
        if (firstOnly) break;
      }
    }
    std::string tl; for (const auto& t : per) tl += (tl.empty() ? "" : " ") + t.first + ":" + std::to_string(t.second.first) + "/" + std::to_string(t.second.second);
    std::printf("engine xeng: %d of %d factory voices bit-exact against beta's golden (%s), local Node reference differs for %d\n", exact, ran, tl.c_str(), localDiff);
    if (exact != ran) fails++;
    if (filter.empty()) for (const char* k : kKinds) CHECK(per[k].second >= 8, "too few factory voices of engine %s (%d)", k, per[k].second);
  }
  // ---- the direct cases
  {
    Json C = cdoc.root().arr("cases");
    std::map<std::string, const XengCaseGolden*> cg; for (int i = 0; i < kXengCaseGoldenN; i++) cg[kXengCaseGolden[i].name] = &kXengCaseGolden[i];
    std::map<std::string, std::pair<std::string, long>> cref; Json RR = rdoc.root().arr("cases"); for (size_t i = 0; i < RR.size(); i++) cref[std::string(RR.at(i).str("name"))] = {std::string(RR.at(i).str("sha256")), (long)RR.at(i).num("len")};
    CHECK(C.size() == (size_t)kXengCaseGoldenN && C.size() == cref.size() && C.size() > 0, "direct cases: %zu, golden %d, reference %zu", C.size(), kXengCaseGoldenN, cref.size());
    int cran = 0, cexact = 0, cdiff = 0; std::map<std::string, std::pair<int, int>> per;
    for (size_t ci = 0; ci < C.size(); ci++) {
      Json c = C.at(ci); const std::string name(c.str("name"));
      if (!filter.empty() && name.find(filter) == std::string::npos) continue;
      std::string hex, kind; size_t n = 0;
      const int st = renderOne(c, c.num("sr"), c.num("hold"), c.num("bpm", 120), buf, &hex, &n, &kind);
      const auto g = cg.find(name); const auto r = cref.find(name); cran++; per[kind].second++;
      if (st != kEngOk || g == cg.end() || r == cref.end()) { CHECK(false, "%s: status %d", name.c_str(), st); if (firstOnly) break; continue; }
      const bool hashOk = hex == g->second->sha256 && (long)n == g->second->len, localOk = hex == r->second.first && (long)n == r->second.second;
      if (hashOk) { cexact++; per[kind].first++; }
      if (!localOk) cdiff++;
      if (!(hashOk && (localOk || !localMustMatch))) {
        fails++; std::printf("%-22s %s%s  (len %zu, want %ld; sr %g hold %g)\n", name.c_str(), hashOk ? "bit-exact (committed hash)" : "DIFFERS from the committed hash", localOk ? "" : "  [the local Node reference differs]", n, g->second->len, c.num("sr"), c.num("hold"));
        firstDiff(refDir + "/c" + std::to_string(ci) + ".f32", buf, n);
        if (firstOnly) break;
      }
    }
    std::string tl; for (const auto& t : per) tl += (tl.empty() ? "" : " ") + t.first + ":" + std::to_string(t.second.first) + "/" + std::to_string(t.second.second);
    std::printf("engine xeng direct cases: %d of %d bit-exact against the committed hashes, local Node differs for %d (%s)\n", cexact, cran, cdiff, tl.c_str());
    if (cexact != cran) fails++;
    if (filter.empty()) for (const char* k : kKinds) CHECK(per[k].second >= 14, "direct cases carry too few %s voices (%d)", k, per[k].second);
  }
  // the refusals: an engine of a later unit is refused, not silent; a layers voice is not ours
  { XengVoice V; JsonDoc d; d.parse(std::string("{\"engine\":\"sf\"}")); CHECK(!xeng_voice_from_json(V, d.root(), Json()) && V.kind == XKind::Other, "an XENG engine of a later unit must be refused (Other)");
    JsonDoc e; e.parse(std::string("{\"engine\":\"layers\"}")); XengVoice W; CHECK(!xeng_voice_from_json(W, e.root(), Json()) && W.kind == XKind::None, "a layers voice is not an XENG voice");
    EngRenderOpts o; size_t n = 0; float tmp[8]; CHECK(xeng_render_voice(V, o, tmp, 8, &n) == kEngUnsupported, "rendering an Other voice must say unsupported");
    CHECK(xeng_kind("modal", 5) == XKind::Modal && xeng_kind("tone", 4) == XKind::Tone && xeng_kind("sf", 2) == XKind::Other && xeng_kind("bass", 4) == XKind::Bass, "engine names");
    XengVoice M; M.kind = XKind::Modal; EngRenderOpts oo; const size_t b = xeng_render_bound(M, oo); std::vector<float> small(b ? b - 1 : 1);
    CHECK(b > 1 && xeng_render_voice(M, oo, small.data(), small.size(), &n) == kEngTooSmall, "a buffer below the bound must be refused"); }
  std::printf("engine xeng: %d failed\n", fails);
  if (!fails) std::printf("PASS test_engine_xeng\n");
  return fails ? 1 : 0;
}
