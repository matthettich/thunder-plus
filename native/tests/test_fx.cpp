// U14: the track-effect host (core/src/fx) against the real ThunderFx worklet (tools/fx-dump.mjs runs it in Node with tfx.wasm: the 34 effects
// at default and one other setting, then host cases: slots in series, a slot switched off and on, FXO / FLX / GAT / RTG, fxreset, FXV mid-block,
// resting and sleeping, block sizes, telemetry, junk). Per case: the output against the JS, gate -80 dB below the reference's peak, the figure
// printed (exact where it is), the telemetry the worklet posts, the committed FNV hash. Also the parameter mapping: every effect's map() against
// the JS on 200 value sets (normTfx + tfxChain), and the song reader's normTfx rules.
//   test_fx <dir>
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
#include "golden/fx_golden.inc"

using namespace thunder;

static bool readFile(const std::string& path, std::string* out) { std::ifstream f(path, std::ios::binary); if (!f) return false; std::stringstream ss; ss << f.rdbuf(); *out = ss.str(); return true; }
static uint64_t fnv(const float* f, size_t n) { uint64_t h = 0xcbf29ce484222325ull; const unsigned char* u = (const unsigned char*)f; for (size_t i = 0; i < n * 4; i++) { h ^= u[i]; h *= 0x100000001b3ull; } return h; }
static int fails = 0, steps = 0;
// No case is exempt from the gate. (U14 had e_beads_def, e_beads_alt and e_pshift_alt open; the causes are in third_party/prepare.sh and core/dsp/host_fx_state.h: an argument
// evaluation order GCC and clang choose differently, and two statics that are per wasm instance in the web app but were per process here.)
// Host builds compile the effects without -ffast-math (core/dsp/CMakeLists.txt, OPT_TFX), so every native compiler gives the same samples; the reference is still the web
// app's tfx.wasm, built with -ffast-math. Two cases differ from it by more than -80 dB and are gated on their own measured figure (16 Nov, GCC 13, GCC 15 and clang 20 on
// Linux x86-64, all three identical), never on a blanket allowance:
//   e_clouds_alt: Clouds hands its output over as int16 and 44 of 65536 samples land on the neighbouring int16 value (one step of 1/32768 = -75.9 dB below the case's peak,
//     nothing accumulating). It passes when no sample is further than that one step from the reference.
//   e_pshift_def: -68.5 dB (the pitch shifter's two crossfaded delay taps). Gate -66 dB.
// Everything else: -80 dB.
static bool oneInt16Step(const char* n, double err) { return std::strstr(n, "clouds") && err <= 1.0001 / 32768.0; }
static double gateOf(const char* n) { return std::strcmp(n, "e_pshift_def") ? -80.0 : -66.0; }

static int laneIndex(std::string_view k) { if (k.size() == 3 && k.substr(0, 2) == "sw") return k[2] - '0'; if (k == "flx") return 8; if (k == "gat") return 9; if (k == "rtg") return 10; return -1; }
static void entriesOf(Json ch, std::vector<FxEntry>* out) {
  out->clear();
  for (size_t i = 0; i < ch.size(); i++) { Json c = ch.at(i); FxEntry e; std::snprintf(e.key, kFxKey, "%s", std::string(c.str("key")).c_str()); e.t = (int)c.num("t", -1); e.slot = (int)c.num("slot", -1); e.on = c.boolean("on", true); Json p = c.arr("p"); for (int k = 0; k < 12; k++) e.p[k] = p.at((size_t)k).asNum(); out->push_back(e); }
}
static void setChain(FxHost& H, Json ch) { std::vector<FxEntry> v; entriesOf(ch, &v); if (std::getenv("FX_DBG")) for (auto& e : v) std::fprintf(stderr, "entry %s t=%d slot=%d on=%d p=%.17g %.17g %.17g %.17g %.17g\n", e.key, e.t, e.slot, e.on, e.p[0], e.p[1], e.p[2], e.p[3], e.p[4]); FxPlan* p = H.plan(v.data(), (int)v.size()); H.commit(p); delete p; }

struct Res { bool ran = false, exact = false, hashOk = false, postOk = true; double db = -INFINITY, err = 0; int posts = 0; long first = -1, worstAt = -1; double firstGot = 0, firstWant = 0, worstGot = 0, worstWant = 0; long nDiff = 0, n = 0; };
// shadow: a second host with the same chain processes the same input just before the first, block by block. In the web app that is another node with its own wasm instance, so
// the first host's output must not change (the statics of the effect sources are per host: core/dsp/host_fx_state.h, wasi_rand.cpp).
static Res runCase(const std::string& dir, const FxGolden& G, bool shadow = false) {
  Res R; std::string js, in, ref;
  if (!readFile(dir + "/" + G.name + ".json", &js) || !readFile(dir + "/" + G.name + ".in.f32", &in) || !readFile(dir + "/" + G.name + ".f32", &ref)) return R;
  JsonDoc doc; if (!doc.parse(js)) return R; Json M = doc.root();
  const int n = (int)M.num("n"); if ((size_t)n * 8 != in.size() || (size_t)n * 8 != ref.size()) return R;
  const float* I = (const float*)in.data(); const float* J = (const float*)ref.data();
  std::vector<float> oL(I, I + n), oR(I + n, I + 2 * (size_t)n);
  FxHost H(48000.0), H2(48000.0); std::vector<float> sL, sR;
  if (shadow) { setChain(H2, M.arr("chain")); sL.assign(oL.begin(), oL.end()); sR.assign(oR.begin(), oR.end()); }
  setChain(H, M.arr("chain"));
  Json ops = M.arr("ops"), q = M.arr("q"), msgs = M.arr("msgs"), posted = M.arr("posted"), sz = M.arr("sizes");
  for (size_t i = 0; i < ops.size(); i++) { Json o = ops.at(i); const std::string_view k = o.at(0).asStr(); if (k == "reset") H.reset(o.at(1).asNum()); else H.lane(laneIndex(k), o.at(2).asNum(), o.at(1).asNum()); }
  for (size_t i = 0; i < q.size(); i++) { Json e = q.at(i); Json pp = e.at(1).get("pset").arr("p"); double p[12]; for (int k = 0; k < 12; k++) p[k] = pp.at((size_t)k).asNum(); H.psetAt(e.at(0).asNum(), (int)e.at(1).get("pset").num("slot"), p); }
  size_t pk = 0; int f0 = 0, b = 0, si = 0;
  while (f0 < n) {
    const int want = (int)sz.at((size_t)(si++ % (int)sz.size())).asNum(), m = std::min(want, n - f0); const double t0 = f0 / 48000.0;
    for (size_t i = 0; i < msgs.size(); i++) { Json e = msgs.at(i); if ((int)e.num("b") != b) continue; Json g = e.get("msg");
      if (g.isArr()) setChain(H, g); else if (g.get("rest").isBool()) H.setRest(g.boolean("rest"), t0); else if (g.get("lvl").isBool()) H.setLevels(g.boolean("lvl")); else if (g.get("tel").isBool()) H.setTelemetry(g.boolean("tel")); }
    if (shadow) H2.process(sL.data() + f0, sR.data() + f0, m, t0);
    H.process(oL.data() + f0, oR.data() + f0, m, t0);
    float lv[8]; FxDyn dy[8]; const int nd0 = H.popLevels(lv) ? 1 : 0; const int nd = H.popDyn(dy, 8);
    // the posts of this call: lvl first, then dyn (the worklet's order); a failed block is counted separately
    bool wantFail = false; int nfail = 0;
    for (;;) { if (pk >= posted.size() || (int)posted.at(pk).num("b") != b) break; Json pm = posted.at(pk).get("m"); bool done = false;
      if (pm.get("failed").isStr()) { wantFail = true; nfail++; pk++; continue; }
      if (pm.get("lvl").isArr()) { if (!nd0) R.postOk = false; else for (int k = 0; k < 8; k++) if (std::fabs(lv[k] - pm.get("lvl").at((size_t)k).asNum()) > 1e-4 + 1e-4 * std::fabs(lv[k])) R.postOk = false; R.posts++; pk++; done = true; }
      else if (pm.get("dyn").isArr()) { Json d = pm.get("dyn"); if ((int)d.size() != nd) R.postOk = false; else for (int k = 0; k < nd; k++) { Json e = d.at((size_t)k); Json v = e.at(2); if ((int)e.at(0).asNum() != dy[k].slot) R.postOk = false; for (int c = 0; c < 3; c++) if (std::fabs(dy[k].v[c] - v.at((size_t)c).asNum()) > 0.05) R.postOk = false; } R.posts++; pk++; done = true; }
      if (!done) pk++; }
    if (wantFail != (H.failedBlock() > 0 && m > 4096)) { /* counted below */ }
    if (m > 4096 && H.failedBlock() == 1 && nfail == 0) R.postOk = false;
    if (nfail > 1 || (m <= 4096 && nfail)) R.postOk = false;
    if ((nd0 && !(pk > 0)) ) R.postOk = false;
    f0 += m; b++;
  }
  if (pk != posted.size()) R.postOk = false;
  R.ran = true;
  double peak = 0, err = 0; bool exact = true;
  for (int c = 0; c < 2; c++) for (int i = 0; i < n; i++) { const double a = (c ? oR : oL)[(size_t)i], r = J[(size_t)c * n + i]; peak = std::max(peak, std::fabs(r)); if ((float)a == (float)r || (std::isnan(a) && std::isnan(r))) continue; exact = false; if (R.first < 0) { R.first = (long)c * n + i; R.firstGot = a; R.firstWant = r; } R.nDiff++; if (std::fabs(a - r) > err) { R.worstAt = (long)c * n + i; R.worstGot = a; R.worstWant = r; } err = std::max(err, std::fabs(a - r)); }
  std::vector<float> all(oL); all.insert(all.end(), oR.begin(), oR.end());
  if (const char* od = std::getenv("FX_OUT")) { std::ofstream o(std::string(od) + "/" + G.name + ".cpp.f32", std::ios::binary); o.write((const char*)all.data(), (std::streamsize)(all.size() * 4)); }
  R.n = n; R.exact = exact; R.hashOk = fnv(all.data(), all.size()) == G.hash;
  R.db = exact ? -INFINITY : 20 * std::log10(err / std::max(peak, 1e-30)); R.err = err;
  return R;
}

static void mapTest(const std::string& dir) {
  std::string js; if (!readFile(dir + "/maps.json", &js)) { std::printf("maps.json missing  FAIL\n"); fails++; return; }
  JsonDoc doc; if (!doc.parse(js)) { std::printf("maps.json unreadable  FAIL\n"); fails++; return; }
  Json A = doc.root(); int sets = 0, bad = 0, dexact = 0, keyBad = 0;
  for (size_t e = 0; e < A.size(); e++) { Json E = A.at(e); const int id = (int)E.num("id"); Json S = E.arr("sets"); int eb = 0, ed = 0;
    for (size_t i = 0; i < S.size(); i++) { Json s = S.at(i); sets++;
      FxEntry en; const double sr = s.num("sr"), bpm = s.num("bpm"), rn = s.num("restartN");
      if (!FxHost::entryFromSlot(s.get("obj"), 0, sr, bpm, rn, &en)) { eb++; continue; }
      Json v = s.arr("v"); bool vb = false; for (int k = 0; k < en.nvals; k++) if (en.vals[k] != v.at((size_t)k).asNum()) vb = true;
      Json p = s.arr("p"); bool pb = false, de = true; for (int k = 0; k < 12; k++) { const double r = p.at((size_t)k).asNum(); if ((float)en.p[k] != (float)r) pb = true; if (en.p[k] != r) de = false; }
      if (std::string_view(en.key) != s.str("key") || en.on != s.boolean("on") || en.t != id) keyBad++;
      if (vb || pb) eb++; else if (de) ed++;
      }
    if (eb) std::printf("  map %-10s %d of %d value sets differ  FAIL\n", std::string(E.str("key")).c_str(), eb, (int)S.size());
    bad += eb; dexact += ed; }
  if (bad || keyBad) fails++;
  std::printf("maps: %d value sets over 34 effects (200 each), %d differ, %d key / on / id mismatches, %d equal as doubles (the rest equal as float32)  %s\n", sets, bad, keyBad, dexact, bad || keyBad ? "FAIL" : "ok");
}

int main(int argc, char** argv) {
#ifndef THUNDER_HAVE_TFX
  (void)argc; (void)argv;
  std::printf("fx: not run on this platform: no DSP library (the effects are not built here), %d cases not run\n", kFxGoldenN);
  return 77;   // ctest: SKIP_RETURN_CODE 77 (shown as not run, never as passed)
#endif
  if (argc < 2) { std::printf("fx: not run: no reference directory (Node not found): %d cases not run\n", kFxGoldenN); return 77; }
  int exact = 0, ran = 0; double worst = -INFINITY; const char* worstName = "";
  if (argc <= 2) mapTest(argv[1]);
  for (int i = 0; i < kFxGoldenN; i++) {
    const FxGolden& G = kFxGolden[i]; if (argc > 2 && !std::strstr(G.name, argv[2])) continue; const Res R = runCase(argv[1], G);
    if (!R.ran) { std::printf("%-22s could not be run  FAIL\n", G.name); fails++; continue; }
    ran++;
    const double gate = gateOf(G.name); const bool step = R.db > gate && oneInt16Step(G.name, R.err);
    const bool ok = (R.db <= gate || step) && R.postOk; if (step) steps++;
    char db[24]; if (R.exact) std::snprintf(db, sizeof db, "exact"); else std::snprintf(db, sizeof db, "%.1f dB", R.db);
    if (R.exact) exact++; else if (R.db > worst) { worst = R.db; worstName = G.name; }
    std::printf("%-22s %-10s posts %-3d%s%s%s%s\n", G.name, db, R.posts, R.postOk ? "" : " DIFF", R.exact != R.hashOk ? "  [committed hash disagrees with the reference run]" : "", step ? "  (one int16 step of Clouds' output)" : R.db > -80.0 && ok ? "  (own gate, see the top of test_fx.cpp)" : "", ok ? "" : "  FAIL");
    if (!ok) { fails++; std::printf("    where: %ld of %ld samples differ (n = %ld per channel); first at %ld: got %.9g want %.9g; largest at %ld: got %.9g want %.9g\n", R.nDiff, R.n * 2, R.n, R.first, R.firstGot, R.firstWant, R.worstAt, R.worstGot, R.worstWant); }
  }
  // Two hosts side by side: the effects that keep state in a static of their sources (DaisySP myrand: pshift; stmlib::Random: Beads, Clouds; rand(): the Airwindows reverb, Cloud Seed)
  // give the same figure with a second host running the same chain in the same process, on the same thread.
  if (argc <= 2) for (const char* nm : {"e_pshift_alt", "e_beads_def", "e_beads_alt", "e_clouds_alt", "e_reverb_def", "e_space_def"}) {
    const FxGolden* G = nullptr; for (int i = 0; i < kFxGoldenN; i++) if (!std::strcmp(kFxGolden[i].name, nm)) G = &kFxGolden[i];
    if (!G) { std::printf("two hosts: %-14s no such case  FAIL\n", nm); fails++; continue; }
    const Res A = runCase(argv[1], *G), B = runCase(argv[1], *G, true);
    const bool ok = A.ran && B.ran && (B.db <= gateOf(nm) || oneInt16Step(nm, B.err)) && B.db <= A.db + 1e-9;
    std::printf("two hosts: %-14s alone %.1f dB, beside a second host %.1f dB  %s\n", nm, A.db, B.db, ok ? "ok" : "FAIL"); if (!ok) fails++;
  }
  std::printf("fx: %d cases, %d bit-exact, worst %s (%s), %d failed (gate -80 dB, e_pshift_def -66 dB; %d passed as one int16 step of Clouds' output)\n", ran, exact, std::isinf(worst) ? "exact" : (std::to_string(worst) + " dB").c_str(), worstName, fails, steps);
  return fails ? 1 : 0;
}
