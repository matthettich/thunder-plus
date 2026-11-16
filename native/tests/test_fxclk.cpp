// U15: the tempo-locked track effects (Flex 95, Step gate 94, Retrigger 93: core/src/fx/clkcores.*, hosted by FxHost) against the real ThunderFx worklet
// (tools/fx-clk-dump.mjs runs it in Node: every Flex pattern, the tests/flex.test.js scenarios, tempo changes, the clock stopping / restarting / jumping, odd block
// sizes, commands on block edges, chain sets and FXV mid-run, the node's sleep, the three in series). Per case: the output against the JS sample by sample, THE
// GATE IS BIT-EXACT (the cores are plain arithmetic: every libm call is V8's fdlibm, core/src/rt/fdlibm.cpp), and the committed FNV hash. Also the parameter mapping
// of the three kinds (normTfx + tfxChain) on 200 value sets each, and two hosts side by side. A reference that is missing is a FAILURE, never a skip.
// EXPOSURE (known, not restructured): the reference files in <dir> are made by the LOCAL Node (fxclk_ref), and only the FNV hashes are committed. On macOS and Windows the
// fxclk case outputs are compared with what that platform's Node gives and with the committed hash (a case must be exact AND match the hash, so a platform whose Node rounds
// differently fails the hash check rather than passing quietly); the cores' libm calls are covered by the committed golden of ctest `libm`.
//   test_fxclk <dir> [case-substring]
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "fx/fxhost.h"
#include "rt/json.h"
#include "golden/fxclk_golden.inc"

using namespace thunder;

static bool readFile(const std::string& path, std::string* out) { std::ifstream f(path, std::ios::binary); if (!f) return false; std::stringstream ss; ss << f.rdbuf(); *out = ss.str(); return true; }
static uint64_t fnv(const float* f, size_t n) { uint64_t h = 0xcbf29ce484222325ull; const unsigned char* u = (const unsigned char*)f; for (size_t i = 0; i < n * 4; i++) { h ^= u[i]; h *= 0x100000001b3ull; } return h; }
static int fails = 0;

static int laneIndex(std::string_view k) { if (k.size() == 3 && k.substr(0, 2) == "sw") return k[2] - '0'; if (k == "flx") return 8; if (k == "gat") return 9; if (k == "rtg") return 10; return -1; }
static void setChain(FxHost& H, Json ch) {
  std::vector<FxEntry> v;
  for (size_t i = 0; i < ch.size(); i++) { Json c = ch.at(i); FxEntry e; std::snprintf(e.key, kFxKey, "%s", std::string(c.str("key")).c_str()); e.t = (int)c.num("t", -1); e.slot = (int)c.num("slot", -1); e.on = c.boolean("on", true);
    Json p = c.arr("p"); for (int k = 0; k < 12; k++) e.p[k] = p.at((size_t)k).asNum(); v.push_back(e); }
  FxPlan* p = H.plan(v.data(), (int)v.size()); H.commit(p); delete p;
}

struct Res { bool ran = false, exact = false, hashOk = false; long nDiff = 0, n = 0, first = -1; double firstGot = 0, firstWant = 0; };
// A case: one host (or two: `shadow` runs a second host with the same chain on the same input just before the first, block by block; the first's output must not change).
static Res runCase(const std::string& dir, const FxClkGolden& G, bool shadow = false) {
  Res R; std::string js, in, ref, bpf;
  if (!readFile(dir + "/" + G.name + ".json", &js) || !readFile(dir + "/" + G.name + ".in.f32", &in) || !readFile(dir + "/" + G.name + ".f32", &ref) || !readFile(dir + "/" + G.name + ".bp.f32", &bpf)) return R;
  JsonDoc doc; if (!doc.parse(js)) return R; Json M = doc.root();
  const int n = (int)M.num("n"); if ((size_t)n * 8 != in.size() || (size_t)n * 8 != ref.size() || (size_t)n * 4 != bpf.size()) return R;
  const float* I = (const float*)in.data(); const float* J = (const float*)ref.data(); const float* BP = (const float*)bpf.data();
  const std::string_view mode = M.str("bpMode");   // full: one value per frame; const: one value for the block; none: the param at its default
  std::vector<float> oL(I, I + n), oR(I + n, I + 2 * (size_t)n), sL, sR;
  FxHost H(48000.0), H2(48000.0);
  if (shadow) { setChain(H2, M.arr("chain")); sL.assign(oL.begin(), oL.end()); sR.assign(oR.begin(), oR.end()); }
  setChain(H, M.arr("chain"));
  Json ops = M.arr("ops"), q = M.arr("q"), msgs = M.arr("msgs"), sz = M.arr("sizes");
  for (size_t i = 0; i < ops.size(); i++) { Json o = ops.at(i); const std::string_view k = o.at(0).asStr(); if (k == "reset") H.reset(o.at(1).asNum()); else H.lane(laneIndex(k), o.at(2).asNum(), o.at(1).asNum());
    if (shadow) { if (k == "reset") H2.reset(o.at(1).asNum()); else H2.lane(laneIndex(k), o.at(2).asNum(), o.at(1).asNum()); } }
  for (size_t i = 0; i < q.size(); i++) { Json e = q.at(i); Json pp = e.at(1).get("pset").arr("p"); double p[12]; for (int k = 0; k < 12; k++) p[k] = pp.at((size_t)k).asNum();
    H.psetAt(e.at(0).asNum(), (int)e.at(1).get("pset").num("slot"), p); if (shadow) H2.psetAt(e.at(0).asNum(), (int)e.at(1).get("pset").num("slot"), p); }
  int f0 = 0, b = 0, si = 0;
  while (f0 < n) {
    const int want = (int)sz.at((size_t)(si++ % (int)sz.size())).asNum(), m = std::min(want, n - f0); const double t0 = f0 / 48000.0;
    for (size_t i = 0; i < msgs.size(); i++) { Json e = msgs.at(i); if ((int)e.num("b") != b) continue; Json g = e.get("msg");
      if (g.isArr()) { setChain(H, g); if (shadow) setChain(H2, g); } else if (g.get("rest").isBool()) { H.setRest(g.boolean("rest"), t0); if (shadow) H2.setRest(g.boolean("rest"), t0); } }
    const float* bp = mode == "none" ? nullptr : BP + f0; const int bpn = mode == "none" ? 0 : mode == "const" ? 1 : m;
    if (shadow) H2.process(sL.data() + f0, sR.data() + f0, m, t0, bp, bpn);
    H.process(oL.data() + f0, oR.data() + f0, m, t0, bp, bpn);
    f0 += m; b++;
  }
  R.ran = true; R.n = n; bool exact = true;
  for (int c = 0; c < 2; c++) for (int i = 0; i < n; i++) { const float a = (c ? oR : oL)[(size_t)i], r = J[(size_t)c * n + i];
    if (std::memcmp(&a, &r, 4) == 0 || (std::isnan(a) && std::isnan(r))) continue;
    exact = false; R.nDiff++; if (R.first < 0) { R.first = (long)c * n + i; R.firstGot = a; R.firstWant = r; } }
  std::vector<float> all(oL); all.insert(all.end(), oR.begin(), oR.end());
  R.exact = exact; R.hashOk = fnv(all.data(), all.size()) == G.hash;
  return R;
}

// every map() of the three kinds against normTfx + tfxChain of the page, on 200 value sets each (missing, null, string, out of range values among them): equal as doubles
static void mapTest(const std::string& dir) {
  std::string js; if (!readFile(dir + "/maps.json", &js)) { std::printf("maps.json missing  FAIL\n"); fails++; return; }
  JsonDoc doc; if (!doc.parse(js)) { std::printf("maps.json unreadable  FAIL\n"); fails++; return; }
  Json A = doc.root(); int sets = 0, bad = 0;
  for (size_t e = 0; e < A.size(); e++) { Json E = A.at(e); const int id = (int)E.num("id"); Json S = E.arr("sets"); int eb = 0;
    for (size_t i = 0; i < S.size(); i++) { Json s = S.at(i); sets++; FxEntry en;
      if (!FxHost::entryFromSlot(s.get("obj"), 0, s.num("sr"), s.num("bpm"), s.num("restartN"), &en)) { eb++; continue; }
      Json v = s.arr("v"), p = s.arr("p"); bool b = false;
      for (int k = 0; k < en.nvals; k++) if (en.vals[k] != v.at((size_t)k).asNum()) b = true;
      for (int k = 0; k < 12; k++) if (en.p[k] != p.at((size_t)k).asNum()) b = true;
      if (std::string_view(en.key) != s.str("key") || en.on != s.boolean("on") || en.t != id) b = true;
      if (b) { if (!eb) std::printf("  first mismatch: set %zu of %s\n", i, std::string(E.str("key")).c_str()); eb++; } }
    if (eb) std::printf("  map %-8s %d of %d value sets differ  FAIL\n", std::string(E.str("key")).c_str(), eb, (int)S.size());
    bad += eb; }
  if (bad || sets != 600) fails++;
  std::printf("maps: %d value sets over Retrigger, Step gate and Flex (200 each), %d differ (values, mapped parameters as doubles, key, on, id)  %s\n", sets, bad, bad || sets != 600 ? "FAIL" : "ok");
}

int main(int argc, char** argv) {
  if (argc < 2) { std::printf("fxclk: no reference directory given: FAIL (a fixture that cannot run is a failure)\n"); return 1; }
  int ran = 0, exact = 0;
  if (argc <= 2) mapTest(argv[1]);
  for (int i = 0; i < kFxClkGoldenN; i++) {
    const FxClkGolden& G = kFxClkGolden[i]; if (argc > 2 && !std::strstr(G.name, argv[2])) continue;
    const Res R = runCase(argv[1], G);
    if (!R.ran) { std::printf("%-24s could not be run (reference files missing or the wrong size)  FAIL\n", G.name); fails++; continue; }
    ran++; const bool ok = R.exact && R.hashOk; if (R.exact) exact++;
    std::printf("%-24s %s%s%s\n", G.name, R.exact ? "bit-exact" : "DIFFERS", R.exact != R.hashOk ? "  [committed hash disagrees with the reference run]" : "", ok ? "" : "  FAIL");
    if (!R.exact) std::printf("    %ld of %ld samples differ; first at %ld: got %.9g want %.9g\n", R.nDiff, R.n * 2, R.first, R.firstGot, R.firstWant);
    if (!ok) fails++;
  }
  // two hosts side by side: the first host's output is the same with a second host running the same chain in the same process
  if (argc <= 2) for (const char* nm : {"flex_alt1", "flex_tempo", "sgate_custom", "rtg_pitch", "host_series", "host_set", "blk37", "flex_sweep0", "sleep_flex_on_stops"}) {
    const FxClkGolden* G = nullptr; for (int i = 0; i < kFxClkGoldenN; i++) if (!std::strcmp(kFxClkGolden[i].name, nm)) G = &kFxClkGolden[i];
    if (!G) { std::printf("two hosts: %-20s no such case  FAIL\n", nm); fails++; continue; }
    const Res B = runCase(argv[1], *G, true); const bool ok = B.ran && B.exact && B.hashOk;
    std::printf("two hosts: %-20s %s\n", nm, ok ? "bit-exact beside a second host" : "DIFFERS  FAIL"); if (!ok) fails++;
  }
  if (ran == 0 && argc <= 2) { std::printf("fxclk: no case ran  FAIL\n"); fails++; }
  std::printf("fxclk: %d of %d cases bit-exact, %d failed\n", exact, ran, fails);
  return fails ? 1 : 0;
}
