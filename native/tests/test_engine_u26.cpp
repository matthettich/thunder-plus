// U26: the bass and voice engines (BassCore, VoxCore: core/src/engine/bass_core.cpp, vox_core.cpp) against the real ENGINE block of index.html (tools/engine-u26-dump.mjs runs it in Node) and against tests/golden/engines.json of `beta`.
// For each factory voice whose engine is bass or vox: renderVoice(v, 48000, null, hold) in C++ gives the SAME SHA-256 of the float32 bytes as beta's golden (THE GATE, on every platform: golden/engine_u26_voices_golden.inc holds beta's
// rows, copied, never edited). Then the direct cases (random parameters from the engine cards' ranges, every switch, other sample rates, the pad effects, through the real renderVoice), the core scripts (BassCore / VoxCore driven
// directly: legato, glide, set() between notes, syllable changes, `same`, rests, odd and fractional bounds, the NaN case of tests/bass-core.test.js, calls the JS refuses) and the lyric parse batches: the SAME hash as the committed
// Linux golden (golden/engine_u26_golden.inc) on every platform. All are also compared with the local Node's run of this repo's ENGINE block: that comparison must hold on x86-64 Linux, elsewhere it is reported (Node on macOS arm64
// rounds Math.sin / cos / exp / pow / tan differently). A reference that is missing is a FAILURE, never a skip. The live-rule sequences ("seq_*" cases) are run by test_live_u26.
//   test_engine_u26 <refDir> <engine_u26_voices.json> [voice-or-case-substring] [--first]    (refDir holds ref.json and cases.json)
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "engine/bass_core.h"
#include "engine/engine.h"
#include "engine/vox_core.h"
#include "engine/xeng.h"
#include "rt/json.h"
#include "sha256.h"
#include "golden/engine_u26_voices_golden.inc"
#include "golden/engine_u26_golden.inc"

using namespace thunder;

static bool readFile(const std::string& path, std::string* out) { std::ifstream f(path, std::ios::binary); if (!f) return false; std::stringstream ss; ss << f.rdbuf(); *out = ss.str(); return true; }
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; std::printf("FAIL: " __VA_ARGS__); std::printf("\n"); } } while (0)

#if defined(__linux__) && (defined(__x86_64__) || defined(_M_X64))
static const bool localMustMatch = true;
#else
static const bool localMustMatch = false;
#endif

static int renderOne(Json c, double sr, double hold, double bpm, std::string* hex, size_t* n, std::string* kind) {
  XengVoice V; if (!xeng_voice_from_json(V, c.obj("v"), c.arr("fx"))) return -100;
  *kind = std::string(c.obj("v").str("engine"));
  EngRenderOpts o; o.sr = sr; o.hold = hold; o.bpm = bpm;
  const size_t bound = xeng_render_bound(V, o); std::vector<float> buf(bound + 8, 12345.0f);
  *n = 0; const int st = xeng_render_voice(V, o, buf.data(), bound, n);
  if (st != kEngOk) return st;
  CHECK(buf[bound] == 12345.0f && buf[bound + 7] == 12345.0f, "wrote past the bound");
  CHECK(*n <= bound, "length %zu above the bound %zu", *n, bound);
  Sha256 h; h.add((const char*)buf.data(), *n * 4); *hex = h.hex();
  return kEngOk;
}

#define XP_GETP(n, d) if (o.get(#n).isNum()) P.n = o.get(#n).asNum();
static void applyBass(BassP& P, Json o) { XP_BASS(XP_GETP) }
static void applyVox(VoxP& P, Json o) { XP_VOX(XP_GETP) if (o.get("txt").isStr()) P.txt = std::string(o.get("txt").asStr()); }
static double numOr(Json v, double d) { return v.isNum() ? v.asNum() : d; }   // JSON has no NaN / Infinity: a null stands for them in the case file (every such op is one the JS throws on)

// the two scripts of tests/bass-core.test.js (the NaN case of the page's core test), the same generator as the dump's nanScript: every block must be finite, the output of each block is hashed
static bool nanScript(Json op, Sha256& h) {
  const bool one = op.str("o") == "nan1"; const double det = op.num("det"); bool allOk = true;
  const float dummy = 0; (void)dummy;
  auto snap = [&](const std::vector<float>& L, const std::vector<float>& R) { h.add((const char*)L.data(), 128 * 4); h.add((const char*)R.data(), 128 * 4); };
  auto fin = [&](const std::vector<float>& L, const std::vector<float>& R) { for (int k = 0; k < 128; k++) if (!std::isfinite(L[k]) || !std::isfinite(R[k])) return false; return true; };
  std::vector<float> L(128), R(128);
  if (one) {
    int64_t seed = 7; auto rnd = [&]() { seed = (seed * 16807) % 2147483647; return (double)seed / 2147483647.0; };
    static const double rates[] = {0.25, 0.5, 1, 2, 3, 4, 6, 8, 12, 16};
    const int trials = (int)op.num("trials");
    for (int t = 0; t < trials; t++) {
      BassCore c(48000); BassP p; p.parts = 64; p.det = det; c.set(p); (void)rnd();
      std::fill(L.begin(), L.end(), 0.0f); std::fill(R.begin(), R.end(), 0.0f); bool ok = true;
      for (int blk = 0; blk < 60 && ok; blk++) {
        if (blk % 10 == 0) c.on(rnd() < .3);
        if (blk % 10 == 6 && rnd() < .5) c.off();
        const double r = rates[(int)std::floor(rnd() * 10)]; const int s = (int)std::floor(rnd() * 128);
        if (s > 0) c.render(L.data(), R.data(), 128, 0, s, r);
        c.render(L.data(), R.data(), 128, s, 128, r); ok = fin(L, R); snap(L, R);
      }
      if (!ok) allOk = false;
    }
  } else {
    BassCore c(48000); BassP p; p.parts = 64; p.det = det; c.set(p);
    std::fill(L.begin(), L.end(), 0.0f); std::fill(R.begin(), R.end(), 0.0f);
    c.on(false); for (int k = 0; k < 20; k++) { c.render(L.data(), R.data(), 128, 0, 128, 1); snap(L, R); }
    c.off(); c.on(false);
    for (int k = 0; k < 5; k++) { c.render(L.data(), R.data(), 128, 0, 128, 16); if (!fin(L, R)) allOk = false; snap(L, R); }
    for (int k = 0; k < 300; k++) { c.render(L.data(), R.data(), 128, 0, 128, 1); if (!fin(L, R)) allOk = false; snap(L, R); }
  }
  return allOk;
}

// A core script: BassCore / VoxCore driven directly (ops on, off, set, bpm, wob, r = render). L and R are `len` floats (a guard of 8 floats behind them); the hash is of L then R (or of the NaN script's stream).
static int runScript(Json c, std::string* hex, size_t* n, std::string* kind) {
  *kind = std::string(c.str("kind")); const bool vox = *kind == "vox"; const double sr = c.num("sr"); const size_t len = (size_t)c.num("len");
  std::vector<float> L(len + 8, 12345.0f), R(len + 8, 12345.0f); std::fill(L.begin(), L.begin() + (long)len, 0.0f); std::fill(R.begin(), R.begin() + (long)len, 0.0f);
  BassP bp; VoxP vp; if (vox) applyVox(vp, c.obj("p")); else applyBass(bp, c.obj("p"));
  BassCore bc(sr); VoxCore vc(sr); if (vox) { if (!vc.set(vp)) return -103; } else bc.set(bp);
  Json ops = c.arr("ops"); bool stream = false; Sha256 sh;
  for (size_t i = 0; i < ops.size(); i++) {
    Json op = ops.at(i); const std::string_view o = op.str("o"); const bool thr = op.get("thr").asBool();
    if (o == "nan1" || o == "nan2") { stream = true; if (!nanScript(op, sh)) CHECK(false, "the NaN case of the page's bass-core test: a block is not finite (%s)", std::string(c.str("name")).c_str()); continue; }
    if (o == "on") {
      if (vox) { const bool ok = vc.on(op.get("leg").asBool(), numOr(op.get("n"), (double)NAN), op.get("same").asBool()); CHECK(ok == !thr, "%s: on() returned %d, the JS %s", std::string(c.str("name")).c_str(), (int)ok, thr ? "throws" : "does not throw"); }
      else bc.on(op.get("leg").asBool());
    } else if (o == "off") { if (vox) vc.off(); else bc.off(); }
    else if (o == "set") {
      if (vox) { VoxP q = vc.p(); applyVox(q, op.obj("p")); const bool ok = vc.set(q); CHECK(ok == !thr, "%s: set() returned %d, the JS %s", std::string(c.str("name")).c_str(), (int)ok, thr ? "throws" : "does not throw"); }
      else { BassP q = bc.p(); applyBass(q, op.obj("p")); bc.set(q); }
    }
    else if (o == "bpm") bc.bpm = op.num("v");
    else if (o == "wob") bc.wob = op.num("v");
    else if (o == "r") { if (vox) vc.render(L.data(), R.data(), len, op.num("a"), op.num("b"), op.num("rate")); else bc.render(L.data(), R.data(), len, op.num("a"), op.num("b"), op.num("rate")); }
  }
  if (stream) { *n = 128; *hex = sh.hex(); return kEngOk; }
  CHECK(L[len] == 12345.0f && L[len + 7] == 12345.0f && R[len] == 12345.0f && R[len + 7] == 12345.0f, "a core script wrote past its array");
  if (vox) for (size_t i = 0; i < len; i++) if (std::memcmp(&L[i], &R[i], 4) != 0 && !(std::isnan(L[i]) && std::isnan(R[i]))) { CHECK(false, "a voice script: R differs from L at %zu", i); break; }
  if (const char* d = getenv("U26_DUMP")) { std::ofstream f(std::string(d) + "/" + std::string(c.str("name")) + ".f32", std::ios::binary); f.write((const char*)L.data(), (long)(len * 4)); }   // debugging: the left channel of a script
  Sha256 h; h.add((const char*)L.data(), len * 4); h.add((const char*)R.data(), len * 4); *hex = h.hex(); *n = len; return kEngOk;
}

static int runParse(Json c, std::string* hex, size_t* n) {
  Json T = c.arr("texts"); std::string all;
  for (size_t i = 0; i < T.size(); i++) { if (i) all += '\n'; bool threw = false; const std::string s = vox_syllables_text(std::string(T.at(i).asStr()), &threw); all += threw ? "THROW" : s; }
  Sha256 h; h.add(all.data(), all.size()); *hex = h.hex(); *n = all.size(); return kEngOk;
}

int main(int argc, char** argv) {
  if (argc < 3) { std::printf("FAIL: usage: test_engine_u26 <refDir> <engine_u26_voices.json> [substring] [--first]\n"); return 1; }
  std::string refDir = argv[1], fixPath = argv[2], filter; bool firstOnly = false;
  for (int i = 3; i < argc; i++) { if (!std::strcmp(argv[i], "--first")) firstOnly = true; else filter = argv[i]; }
  std::string rj, fj, cj;
  if (!readFile(refDir + "/ref.json", &rj) || !readFile(refDir + "/cases.json", &cj) || !readFile(fixPath, &fj)) { std::printf("FAIL: the reference (ref.json, cases.json) or the fixture is missing\n"); return 1; }
  JsonDoc rdoc, fdoc, cdoc; if (!rdoc.parse(rj) || !fdoc.parse(fj) || !cdoc.parse(cj)) { std::printf("FAIL: a reference file does not parse\n"); return 1; }
  eng_init();
  // ---- the factory voices
  {
    std::map<std::string, const U26Golden*> gold; for (int i = 0; i < kU26GoldenN; i++) gold[kU26Golden[i].key] = &kU26Golden[i];
    std::map<std::string, std::pair<std::string, long>> ref; Json R = rdoc.root().arr("voices"); for (size_t i = 0; i < R.size(); i++) ref[std::string(R.at(i).str("k"))] = {std::string(R.at(i).str("sha256")), (long)R.at(i).num("len")};
    Json F = fdoc.root().arr("voices");
    CHECK(F.size() == (size_t)kU26GoldenN && F.size() == R.size() && F.size() > 0, "fixture %zu voices, golden %d, reference %zu", F.size(), kU26GoldenN, R.size());
    int ran = 0, exact = 0, localDiff = 0; std::map<std::string, std::pair<int, int>> per;
    for (size_t vi = 0; vi < F.size(); vi++) {
      Json fv = F.at(vi); const std::string key(fv.str("k"));
      if (!filter.empty() && key.find(filter) == std::string::npos) continue;
      std::string hex, kind; size_t n = 0;
      const int st = renderOne(fv, 48000, fv.num("hold"), 120, &hex, &n, &kind); ran++;
      const auto g = gold.find(key); const auto r = ref.find(key); per[kind].second++;
      if (st != kEngOk || g == gold.end() || r == ref.end()) { CHECK(false, "%s: status %d, golden %s, reference %s", key.c_str(), st, g == gold.end() ? "missing" : "there", r == ref.end() ? "missing" : "there"); if (firstOnly) break; continue; }
      const bool hashOk = hex == g->second->sha256 && (long)n == g->second->len, localOk = hex == r->second.first && (long)n == r->second.second;
      if (hashOk) { exact++; per[kind].first++; }
      if (!localOk) localDiff++;
      if (!(hashOk && (localOk || !localMustMatch))) { fails++; std::printf("%-44s %s%s  (len %zu, want %ld)\n", key.c_str(), hashOk ? "bit-exact (beta's golden)" : "DIFFERS from beta's golden", localOk ? "" : "  [the local Node reference differs]", n, g->second->len); if (firstOnly) break; }
    }
    std::string tl; for (const auto& t : per) tl += (tl.empty() ? "" : " ") + t.first + ":" + std::to_string(t.second.first) + "/" + std::to_string(t.second.second);
    std::printf("engine u26: %d of %d factory voices bit-exact against beta's golden (%s), local Node reference differs for %d\n", exact, ran, tl.c_str(), localDiff);
    if (exact != ran) fails++;
    if (filter.empty()) for (const char* k : {"bass", "vox"}) CHECK(per[k].second >= 20, "too few factory voices of engine %s (%d)", k, per[k].second);
  }
  // ---- the direct cases, the core scripts, the parse batches
  {
    Json C = cdoc.root().arr("cases");
    std::map<std::string, const U26CaseGolden*> cg; for (int i = 0; i < kU26CaseGoldenN; i++) cg[kU26CaseGolden[i].name] = &kU26CaseGolden[i];
    std::map<std::string, std::pair<std::string, long>> cref; Json RR = rdoc.root().arr("cases"); for (size_t i = 0; i < RR.size(); i++) cref[std::string(RR.at(i).str("name"))] = {std::string(RR.at(i).str("sha256")), (long)RR.at(i).num("len")};
    CHECK(C.size() == (size_t)kU26CaseGoldenN && C.size() == cref.size() && C.size() > 0, "cases: %zu, golden %d, reference %zu", C.size(), kU26CaseGoldenN, cref.size());
    int cran = 0, cexact = 0, cdiff = 0, live = 0; std::map<std::string, std::pair<int, int>> per;
    for (size_t ci = 0; ci < C.size(); ci++) {
      Json c = C.at(ci); const std::string name(c.str("name"));
      if (c.get("live").asBool()) { live++; continue; }   // test_live_u26
      if (!filter.empty() && name.find(filter) == std::string::npos) continue;
      std::string hex, kind; size_t n = 0; int st;
      if (c.get("script").asBool()) { st = runScript(c, &hex, &n, &kind); kind += "-script"; }
      else if (c.get("parse").asBool()) { st = runParse(c, &hex, &n); kind = "parse"; }
      else st = renderOne(c, c.num("sr"), c.num("hold"), c.num("bpm", 120), &hex, &n, &kind);
      const auto g = cg.find(name); const auto r = cref.find(name); cran++; per[kind].second++;
      if (st != kEngOk || g == cg.end() || r == cref.end()) { CHECK(false, "%s: status %d", name.c_str(), st); if (firstOnly) break; continue; }
      const bool hashOk = hex == g->second->sha256 && (long)n == g->second->len, localOk = hex == r->second.first && (long)n == r->second.second;
      if (hashOk) { cexact++; per[kind].first++; }
      if (!localOk) cdiff++;
      if (!(hashOk && (localOk || !localMustMatch))) { fails++; std::printf("%-26s %s%s  (len %zu, want %ld)\n", name.c_str(), hashOk ? "bit-exact (committed hash)" : "DIFFERS from the committed hash", localOk ? "" : "  [the local Node reference differs]", n, g->second->len); if (firstOnly) break; }
    }
    std::string tl; for (const auto& t : per) tl += (tl.empty() ? "" : " ") + t.first + ":" + std::to_string(t.second.first) + "/" + std::to_string(t.second.second);
    std::printf("engine u26 cases: %d of %d bit-exact against the committed hashes, local Node differs for %d (%s); %d live-rule sequences are test_live_u26's\n", cexact, cran, cdiff, tl.c_str(), live);
    if (cexact != cran) fails++;
    if (filter.empty()) { CHECK(per["bass"].second >= 40 && per["vox"].second >= 40, "direct cases carry too few bass / vox voices (%d, %d)", per["bass"].second, per["vox"].second);
      CHECK(per["bass-script"].second >= 20 && per["vox-script"].second >= 15 && per["parse"].second >= 100, "too few scripts or parse batches (%d, %d, %d)", per["bass-script"].second, per["vox-script"].second, per["parse"].second); }
  }
  // ---- the refusals and the names
  {
    CHECK(xeng_kind("bass", 4) == XKind::Bass && xeng_kind("vox", 3) == XKind::Vox && xeng_kind("acid", 4) == XKind::Acid && xeng_kind("sf", 2) == XKind::Other && xeng_kind("layers", 6) == XKind::None, "engine names");
    for (XKind k : {XKind::Bass, XKind::Vox}) { XengVoice M; M.kind = k; EngRenderOpts oo; size_t n = 0; const size_t b = xeng_render_bound(M, oo); std::vector<float> small(b ? b - 1 : 1);
      CHECK(b > 1 && xeng_render_voice(M, oo, small.data(), small.size(), &n) == kEngTooSmall, "a buffer below the bound must be refused"); }
    { XengVoice V; V.kind = XKind::Vox; V.vox.txt = "constructor"; EngRenderOpts o; size_t n = 0; float t[8]; CHECK(xeng_render_bound(V, o) == 0 || xeng_render_voice(V, o, t, 8, &n) == kEngUnsupported, "the lyric word \"constructor\": the JS throws, the voice is refused"); }
    { XengVoice V; V.kind = XKind::Bass; V.bass.vowel = NAN; EngRenderOpts o; size_t n = 0; std::vector<float> t(xeng_render_bound(V, o) + 1); CHECK(xeng_render_bound(V, o) == 0 || xeng_render_voice(V, o, t.data(), t.size() - 1, &n) == kEngUnsupported, "a NaN vowel: the JS throws, the voice is refused"); }
    { VoxCore vc(48000); VoxP p; p.txt = "la na"; CHECK(vc.set(p), "set");
      CHECK(!vc.on(false, NAN, false) && !vc.on(false, 0.5, false) && !vc.on(false, INFINITY, false), "a syllable number that is not a whole number: the JS throws");
      CHECK(vc.on(false, 1, false) && vc.on(false, -3, false) && vc.on(false, 1e300, false), "wrapping syllable numbers are accepted"); }
  }
  std::printf("engine u26: %d failed\n", fails);
  if (!fails) std::printf("PASS test_engine_u26\n");
  return fails ? 1 : 0;
}
