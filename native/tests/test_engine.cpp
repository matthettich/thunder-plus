// U22: the engine foundation (core/src/engine/*) against the real ENGINE block of index.html (tools/engine-dump.mjs runs it in Node) and against tests/golden/engines.json of `beta`.
// For each of the 294 factory voices that are built only from sine / tri / saw / square / pulse and the noise layer types: renderVoice(v, 48000, null, hold) in C++ gives the SAME SHA-256 of
// the float32 bytes as beta's golden (that is THE GATE, on every platform: tests/golden/engine_golden.inc holds beta's rows, copied, never edited), and the same hash as the local Node's run
// of this repo's ENGINE block (ref.json): the second comparison must hold on x86-64 Linux, elsewhere it is reported (Node on macOS arm64 rounds Math.sin / cos / exp / pow / tan / tanh differently).
// Also: the tables (SIN bit for bit, RATIOS, CHORDS, MEM, BELL, BELLA, BAR, HAT, SYNC_BEATS), mkVoice's and freshLayer's defaults, the layer seed counter, sinT / rng / blep / hashRnd samples,
// and the refusals (a layer type of a later unit: kEngUnsupported; a buffer that is too small). A reference that is missing is a FAILURE, never a skip.
//   test_engine <refDir> <engine_voices.json> [voice-substring] [--first]    (refDir holds ref.json and tables.json, and <n>.f32 if made with --samples: then the first differing sample is printed)
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

#include "engine/engine.h"
#include "engine/engine_json.h"
#include "rt/json.h"
#include "sha256.h"
#include "golden/engine_golden.inc"
#include "golden/engine_cases_golden.inc"
#include "golden/engine_probes_golden.inc"

using namespace thunder;

static bool readFile(const std::string& path, std::string* out) { std::ifstream f(path, std::ios::binary); if (!f) return false; std::stringstream ss; ss << f.rdbuf(); *out = ss.str(); return true; }
static int fails = 0;
static double ev(Json j) { return j.isNull() ? (double)NAN : j.asNum(); }   // (JSON.stringify writes NaN as null: sinT of a hair below 0 is NaN in the JS)
#define CHECK(c, ...) do { if (!(c)) { fails++; std::printf("FAIL: " __VA_ARGS__); std::printf("\n"); } } while (0)
static bool same(double a, double b) { return std::memcmp(&a, &b, sizeof a) == 0 || (a != a && b != b); }
static uint32_t bits32(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }

static void checkTables(Json T) {
  const float* S = eng_sin_table(); Json sj = T.arr("sin");
  CHECK(sj.size() == (size_t)kSinTS + 1, "SIN has %zu entries", sj.size());
  size_t bad = 0; for (size_t i = 0; i <= (size_t)kSinTS; i++) if (bits32(S[i]) != bits32((float)sj.at(i).asNum())) { if (!bad) std::printf("  SIN[%zu] differs: %.9g vs %.9g\n", i, (double)S[i], sj.at(i).asNum()); bad++; }
  CHECK(bad == 0, "SIN: %zu entries differ", bad);
  auto arr = [&](const char* k, const double* v, size_t n) { Json a = T.arr(k); bool ok = a.size() == n; for (size_t i = 0; ok && i < n; i++) ok = same(a.at(i).asNum(), v[i]); CHECK(ok, "table %s differs", k); };
  arr("RATIOS", kRatios, 24); arr("MEM", kMem, 12); arr("BELL", kBell, 12); arr("BELLA", kBellA, 12); arr("BAR", kBar, 6); arr("HAT", kHat, 6); arr("SYNC_BEATS", kSyncBeats, 9);
  CHECK(same(T.num("BASE"), kBase) && T.num("TS") == kSinTS, "BASE / TS");
  Json C = T.arr("CHORDS"); bool ok = C.size() == 14;
  for (size_t i = 0; ok && i < 14; i++) { Json c = C.at(i); Json s = c.at(1); ok = c.at(0).asStr() == kChords[i].name && s.size() == (size_t)kChords[i].n; for (size_t k = 0; ok && k < s.size(); k++) ok = (int)s.at(k).asNum() == kChords[i].semis[k]; }
  CHECK(ok, "CHORDS differs");
  // the layer seed counter
  Json sd = T.arr("seeds"); for (size_t i = 0; i < sd.size(); i++) { EngSeedCounter c; c.state = (uint32_t)sd.at(i).at(0).asNum(); Json v = sd.at(i).at(1); for (size_t k = 0; k < v.size(); k++) { const uint32_t g = c.next(); if (g != (uint32_t)v.at(k).asNum()) { CHECK(false, "seed counter from %.0f, step %zu: %u vs %.0f", sd.at(i).at(0).asNum(), k, g, v.at(k).asNum()); break; } } }
  // sinT, rng, blep, hashRnd
  Json sm = T.obj("samples");
  Json a = sm.arr("sinT"); for (size_t i = 0; i < a.size(); i++) CHECK(same(eng_sinT(a.at(i).at(0).asNum()), ev(a.at(i).at(1))), "sinT(%.17g)", a.at(i).at(0).asNum());
  a = sm.arr("rng"); for (size_t i = 0; i < a.size(); i++) { EngRng r(a.at(i).at(0).asNum()); Json v = a.at(i).at(1); for (size_t k = 0; k < v.size(); k++) if (!same(r.next(), v.at(k).asNum())) { CHECK(false, "rng(%.0f) step %zu", a.at(i).at(0).asNum(), k); break; } }
  a = sm.arr("blep"); for (size_t i = 0; i < a.size(); i++) CHECK(same(eng_blep(a.at(i).at(0).asNum(), a.at(i).at(1).asNum()), a.at(i).at(2).asNum()), "blep(%g, %g)", a.at(i).at(0).asNum(), a.at(i).at(1).asNum());
  a = sm.arr("hashRnd"); for (size_t i = 0; i < a.size(); i++) CHECK(same(eng_hash_rnd(a.at(i).at(0).asNum(), a.at(i).at(1).asNum()), a.at(i).at(2).asNum()), "hashRnd(%g, %g)", a.at(i).at(0).asNum(), a.at(i).at(1).asNum());
}

// mkVoice / freshLayer defaults: every key the engine reads, against the JS object's value
static void checkDefaults(Json D) {
  Json DL = D.obj("layer"), DV = D.obj("voice"); const EngLayer L; const EngVoice V;
  auto lay = [&](const char* k, double v) { CHECK(same(DL.num(k, NAN), v), "freshLayer default %s: JS %.17g, C++ %.17g", k, DL.num(k, NAN), v); };
  lay("a", L.a); lay("b", L.b); lay("level", L.level); lay("tune", L.tune); lay("fine", L.fine); lay("pAmt", L.pAmt); lay("pDec", L.pDec); lay("atk", L.atk); lay("hold", L.hold); lay("dec", L.dec); lay("sus", L.sus); lay("rel", L.rel);
  lay("lpCut", L.lpCut); lay("lpRes", L.lpRes); lay("lpEnv", L.lpEnv); lay("hpCut", L.hpCut); lay("hpRes", L.hpRes); lay("hpEnv", L.hpEnv); lay("fAtk", L.fAtk); lay("fDec", L.fDec); lay("delay", L.delay); lay("burst", L.burst); lay("spacing", L.spacing);
  CHECK(DL.str("type") == "sine" && L.type == LType::Sine && DL.str("mode") == "AHD" && !L.adsr && !DL.boolean("on", true) && !L.on, "freshLayer default type / mode / on");
  auto vo = [&](const char* k, double v) { CHECK(same(DV.num(k, NAN), v), "mkVoice default %s: JS %.17g, C++ %.17g", k, DV.num(k, NAN), v); };
  vo("level", V.level); vo("tune", V.tune); vo("drive", V.drive); vo("gate", V.gate); vo("len", V.len); vo("tail", V.tail); vo("gPAmt", V.gPAmt); vo("gPDec", V.gPDec);
  vo("gLpCut", V.gLpCut); vo("gLpRes", V.gLpRes); vo("gLpEnv", V.gLpEnv); vo("gFAtk", V.gFAtk); vo("gFDec", V.gFDec); vo("gAtk", V.gAtk); vo("gHold", V.gHold); vo("gDec", V.gDec); vo("gSus", V.gSus); vo("gRel", V.gRel); vo("gAmpAmt", V.gAmpAmt);
  CHECK(!DV.boolean("gAmpOn", true) && !V.gAmpOn && DV.str("gMode") == "ADSR" && V.gAdsr && V.nFx == 0 && V.nLayers == 8 && DV.arr("fx").size() == 0, "mkVoice default gAmpOn / gMode / fx / layers");
  static const char* kRate[3] = {"lfo1Rate", "lfo2Rate", "lfo3Rate"}, *kShape[3] = {"lfo1Shape", "lfo2Shape", "lfo3Shape"}, *kDepth[3] = {"lfo1Depth", "lfo2Depth", "lfo3Depth"};
  static const char* kA[3] = {"env1A", "env2A", "env3A"}, *kD[3] = {"env1D", "env2D", "env3D"}, *kS[3] = {"env1S", "env2S", "env3S"}, *kR[3] = {"env1R", "env2R", "env3R"};
  for (int i = 0; i < 3; i++) { vo(kRate[i], V.lfoRate[i]); vo(kShape[i], V.lfoShape[i]); vo(kDepth[i], V.lfoDepth[i]); vo(kA[i], V.envA[i]); vo(kD[i], V.envD[i]); vo(kS[i], V.envS[i]); vo(kR[i], V.envR[i]); }
  vo("seqRate", V.seqRate); vo("seqDepth", V.seqDepth);
  Json sq = DV.arr("seq"); bool ok = sq.size() == 32; for (size_t i = 0; ok && i < 32; i++) ok = sq.at(i).asNum() == V.seq[i]; CHECK(ok, "mkVoice default seq");
  static const char* kDst[7] = {"r1dst", "r2dst", "r3dst", "r4dst", "r5dst", "r6dst", "r7dst"}, *kAmt[7] = {"r1amt", "r2amt", "r3amt", "r4amt", "r5amt", "r6amt", "r7amt"};
  for (int i = 0; i < 7; i++) { vo(kDst[i], V.rdst[i]); vo(kAmt[i], V.ramt[i]); }
}

int main(int argc, char** argv) {
  if (argc < 3) { std::printf("FAIL: usage: test_engine <refDir> <engine_voices.json> [voice-substring] [--first]\n"); return 1; }
  std::string refDir = argv[1], fixPath = argv[2], filter; bool firstOnly = false;
  for (int i = 3; i < argc; i++) { if (!std::strcmp(argv[i], "--first")) firstOnly = true; else filter = argv[i]; }
  std::string tj, rj, fj;
  if (!readFile(refDir + "/tables.json", &tj) || !readFile(refDir + "/ref.json", &rj) || !readFile(fixPath, &fj)) { std::printf("FAIL: the reference (tables.json, ref.json) or the fixture is missing\n"); return 1; }
  JsonDoc tdoc, rdoc, fdoc; if (!tdoc.parse(tj) || !rdoc.parse(rj) || !fdoc.parse(fj)) { std::printf("FAIL: a reference file does not parse\n"); return 1; }
  eng_init();
  checkTables(tdoc.root()); checkDefaults(tdoc.root().obj("defaults"));
  std::map<std::string, const EngineGolden*> gold; for (int i = 0; i < kEngineGoldenN; i++) gold[kEngineGolden[i].key] = &kEngineGolden[i];
  std::map<std::string, std::pair<std::string, long>> ref; Json R = rdoc.root().arr("voices"); for (size_t i = 0; i < R.size(); i++) ref[std::string(R.at(i).str("k"))] = {std::string(R.at(i).str("sha256")), (long)R.at(i).num("len")};
  Json F = fdoc.root().arr("voices");
  CHECK(F.size() == (size_t)kEngineGoldenN && F.size() == R.size() && F.size() > 0, "fixture %zu voices, golden %d, reference %zu", F.size(), kEngineGoldenN, R.size());
#if defined(__linux__) && (defined(__x86_64__) || defined(_M_X64))
  const bool localMustMatch = true;
#else
  const bool localMustMatch = false;
#endif
  int ran = 0, exact = 0, localDiff = 0, passFx = 0;
  std::vector<float> buf;
  for (size_t vi = 0; vi < F.size(); vi++) {
    Json fv = F.at(vi); const std::string key(fv.str("k"));
    if (!filter.empty() && key.find(filter) == std::string::npos) continue;
    EngVoice V; if (!eng_voice_from_json(V, fv.obj("v"), fv.arr("L"), fv.arr("fx"))) { CHECK(false, "%s: too many layers / effects for the loader", key.c_str()); continue; }
    EngRenderOpts o; o.libFx = false; o.sr = 48000; o.hold = fv.num("hold");
    for (int i = 0; i < V.nFx; i++) if (V.fx[i].on && V.fx[i].kind == EngFx::Kind::Other) { passFx++; break; }
    const size_t bound = eng_render_bound(V, o); buf.assign(bound + 8, 12345.0f);
    size_t n = 0; const int st = eng_render_voice(V, o, buf.data(), bound, &n);
    ran++;
    const auto g = gold.find(key); const auto r = ref.find(key);
    if (st != kEngOk || g == gold.end() || r == ref.end()) { CHECK(false, "%s: status %d, golden %s, reference %s", key.c_str(), st, g == gold.end() ? "missing" : "there", r == ref.end() ? "missing" : "there"); if (firstOnly) break; continue; }
    CHECK(buf[bound] == 12345.0f && buf[bound + 7] == 12345.0f, "%s: wrote past the bound", key.c_str());
    Sha256 h; h.add((const char*)buf.data(), n * 4); const std::string hex = h.hex();
    const bool hashOk = hex == g->second->sha256 && (long)n == g->second->len, localOk = hex == r->second.first && (long)n == r->second.second;
    if (hashOk) exact++;
    if (!localOk) localDiff++;
    const bool ok = hashOk && (localOk || !localMustMatch);
    if (!ok) fails++;
    if (!hashOk || !localOk || !ok) {
      std::printf("%-44s %s%s  (len %zu, want %ld)\n", key.c_str(), hashOk ? "bit-exact (beta's golden)" : "DIFFERS from beta's golden", localOk ? "" : "  [the local Node reference differs]", n, g->second->len);
      // the first differing sample against the local Node's render, if the reference was made with --samples
      std::string f32; if (readFile(refDir + "/" + std::to_string(vi) + ".f32", &f32)) { const float* w = (const float*)f32.data(); const size_t nw = f32.size() / 4; size_t d = 0, first = (size_t)-1;
        for (size_t i = 0; i < (n < nw ? n : nw); i++) if (bits32(buf[i]) != bits32(w[i])) { if (first == (size_t)-1) first = i; d++; }
        if (first != (size_t)-1) std::printf("    %zu of %zu samples differ; first at %zu: got %.9g want %.9g\n", d, nw, first, (double)buf[first], (double)w[first]); }
      if (firstOnly) break;
    }
  }
  // the direct cases (random voices through the same render: every branch the factory voices leave out) and the voiceMod probes
  {
    std::string cj; JsonDoc cdoc; if (!readFile(refDir + "/cases.json", &cj) || !cdoc.parse(cj)) { std::printf("FAIL: cases.json is missing\n"); return 1; }
    Json C = cdoc.root().arr("cases"), P = cdoc.root().arr("probes");
    std::map<std::string, const EngineCaseGolden*> cg; for (int i = 0; i < kEngineCaseGoldenN; i++) cg[kEngineCaseGolden[i].name] = &kEngineCaseGolden[i];
    std::map<std::string, std::pair<std::string, long>> cref; Json RR = rdoc.root().arr("cases"); for (size_t i = 0; i < RR.size(); i++) cref[std::string(RR.at(i).str("name"))] = {std::string(RR.at(i).str("sha256")), (long)RR.at(i).num("len")};
    CHECK(C.size() == (size_t)kEngineCaseGoldenN && C.size() == cref.size() && C.size() > 0, "direct cases: %zu, golden %d, reference %zu", C.size(), kEngineCaseGoldenN, cref.size());
    int cran = 0, cexact = 0, cfx = 0;
    for (size_t ci = 0; ci < C.size(); ci++) {
      Json c = C.at(ci); const std::string name(c.str("name"));
      if (!filter.empty() && name.find(filter) == std::string::npos) continue;
      EngVoice V; if (!eng_voice_from_json(V, c.obj("v"), c.arr("L"), c.arr("fx"))) { CHECK(false, "%s: loader", name.c_str()); continue; }
      EngRenderOpts o; o.libFx = false; o.sr = c.num("sr"); o.hold = c.num("hold"); o.bpm = c.num("bpm", 120);
      if (V.nFx) cfx++;
      const size_t bound = eng_render_bound(V, o); buf.assign(bound + 8, 12345.0f); size_t n = 0; const int st = eng_render_voice(V, o, buf.data(), bound, &n);
      const auto g = cg.find(name); const auto r = cref.find(name); cran++;
      if (st != kEngOk || g == cg.end() || r == cref.end()) { CHECK(false, "%s: status %d", name.c_str(), st); if (firstOnly) break; continue; }
      CHECK(buf[bound] == 12345.0f && buf[bound + 7] == 12345.0f, "%s: wrote past the bound", name.c_str());
      Sha256 h; h.add((const char*)buf.data(), n * 4); const std::string hex = h.hex();
      const bool hashOk = hex == g->second->sha256 && (long)n == g->second->len, localOk = hex == r->second.first && (long)n == r->second.second;
      if (hashOk) cexact++;
      if (!localOk) localDiff++;
      if (!(hashOk && (localOk || !localMustMatch))) {
        fails++; std::printf("%-12s %s%s  (len %zu, want %ld; sr %g hold %g, %d layers on, %d effects)\n", name.c_str(), hashOk ? "bit-exact (committed hash)" : "DIFFERS from the committed hash", localOk ? "" : "  [the local Node reference differs]", n, g->second->len, o.sr, o.hold, (int)c.arr("L").size(), V.nFx);
        std::string f32; if (readFile(refDir + "/case" + std::to_string(ci) + ".f32", &f32)) { const float* w = (const float*)f32.data(); const size_t nw = f32.size() / 4; size_t d = 0, first = (size_t)-1;
          for (size_t i = 0; i < (n < nw ? n : nw); i++) if (bits32(buf[i]) != bits32(w[i])) { if (first == (size_t)-1) first = i; d++; }
          if (first != (size_t)-1) std::printf("    %zu of %zu samples differ; first at %zu: got %.9g want %.9g\n", d, nw, first, (double)buf[first], (double)w[first]); }
        if (firstOnly) break;
      }
    }
    std::printf("engine direct cases: %d of %d bit-exact (%d with pad effects)\n", cexact, cran, cfx);
    if (cexact != cran) fails++;
    // voiceMod: THE GATE is the committed hash of the real outputs (Node 22, x86-64 Linux); the local Node's values must agree on x86-64 Linux, elsewhere they are reported
    int pn = 0, pbad = 0; Sha256 ph;
    for (size_t pi = 0; pi < P.size(); pi++) {
      Json p = P.at(pi); EngVoice V; eng_voice_from_json(V, p.obj("v"), Json(), Json()); const double bpm = p.num("bpm", 120); Json ts = p.arr("t"), ms = p.arr("m");
      for (size_t k = 0; k < ts.size(); k++) { const EngMod m = eng_voice_mod(V, ts.at(k).asNum(), bpm); Json e = ms.at(k); pn++;
        const double want[4] = {ev(e.at(0)), ev(e.at(1)), ev(e.at(2)), ev(e.at(3))}, got[4] = {m.pitch, m.level, m.lpcut, m.hpcut};
        for (int q = 0; q < 4; q++) { const double c = got[q] == got[q] ? got[q] : (double)NAN; double cn = c; if (c != c) { const uint64_t nb = 0x7ff8000000000000ull; std::memcpy(&cn, &nb, 8); } ph.add((const char*)&cn, 8); }
        for (int q = 0; q < 4; q++) if (!same(got[q], want[q])) { if (!pbad) std::printf("voiceMod probe %zu t=%.17g output %d: got %.17g want(local Node) %.17g\n", pi, ts.at(k).asNum(), q, got[q], want[q]); pbad++; break; } }
    }
    const bool probeGold = ph.hex() == kEngineProbesGolden && pn == kEngineProbesGoldenN;
    CHECK(probeGold, "voiceMod: the outputs do not hash to the committed golden (%d probes)", pn);
    if (localMustMatch) CHECK(pbad == 0, "voiceMod: %d of %d probes differ from the local Node", pbad, pn);
    std::printf("engine voiceMod probes: %d, committed hash %s, local Node differs for %d\n", pn, probeGold ? "matches" : "DIFFERS", pbad);
  }
  // the refusals
  { EngVoice V; V.layers[0].on = true; V.layers[0].type = LType::Other; EngRenderOpts o; size_t n = 0; float tmp[64];
    CHECK(eng_render_voice(V, o, tmp, 64, &n) == kEngUnsupported, "a layer of a type of a later unit must be refused");
    EngVoice W; W.layers[0].on = true; const size_t b = eng_render_bound(W, o); CHECK(b > 64 && eng_render_voice(W, o, tmp, 64, &n) == kEngTooSmall, "a buffer that is too small must be refused"); }
  std::printf("engine: %d of %d voices bit-exact against beta's golden (%d with an effect that is not ready here, as in the golden's Node), local Node reference differs for %d, %d failed\n", exact, ran, passFx, localDiff, fails);
  if (exact != ran) fails++;
  if (!fails) std::printf("PASS test_engine\n");
  return fails ? 1 : 0;
}
