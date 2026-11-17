// U19: the MODFX modules (core/src/modfx/*) against the real chains/chains-worklet.js. THREE CHECKS, every module of the registry:
//  1. the tables: ports (names, kinds, order) and parameters (name, default, min, max, step) equal the JS's `inputs` / `outputs` / `params` (tools/modfx-dump.mjs writes defs.json);
//  2. THE UNIT'S GATE, the engine run: the module driven as tests/chains.test.js drives it (trackIn -> module -> chainOut, 400 blocks of H.testSignal, the three parameter sets default /
//     stressed / switches-on), hashed as that test hashes it, equals the hash committed in tests/golden/chains-modules.json (carried in golden/modfx_golden.inc; ctest modfx_golden_current
//     proves the .inc still equals what the JS gives). That hash was made by Node on x86-64 Linux and is the gate on every platform (as U16);
//  3. the direct cases: the module's create() / process() driven with random parameters changing mid-run, bursts, gates, CV, linked and unlinked inputs, block sizes below 128, tempo and
//     start changes, other sample rates, tails and silences: the output compared sample by sample with the JS (bit-exact is the gate) and with the committed FNV-1a 64. A reference that is
//     missing is a FAILURE. The sample-by-sample comparison with the LOCAL Node must also hold on x86-64 Linux; elsewhere it is reported (macOS arm64 Node rounds Math.sin / cos differently).
//   test_modfx <dir> [module-substring] [--write-native FILE]
// MARBLES and TIDES (the lead's decision, 16 Nov; docs/port/U21-NOTES.md, "Marbles / Tides: the decision"): their golden comes from the web's mods.wasm, built with -ffast-math, no native build
// can reproduce its order of float sums; the native mods library is STRICT (-fno-fast-math -ffp-contract=off). So for kTolTypes the gate is not the hash but: each render / case within kGateDb
// (-100 dB: max |difference| / the reference's peak) of the reference samples (<dir>/engine_<type>_<set>.f32, <dir>/<case>.f32), the worst printed; a case that is bit-exact today stays
// bit-exact (golden/modfx_mods_native.inc, kModsExact); and the native render itself is pinned by the SHA-256 committed in the same file (made on Linux x86-64), checked on every platform.
// A case that cannot meet the line is left out BY NAME in kTolSkip with the reason printed; there is no wildcard. --write-native writes that golden (never hand-edited; rebuild after).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "modfx/bbexpr.h"
#include "modfx/modfx.h"
#include "rt/json.h"
#include "rt/jsmath.h"
#include "golden/modfx_golden.inc"
#include "golden/modfx_mods_native.inc"
#include "sha256.h"

using namespace thunder;
using namespace thunder::modfx;

static bool readFile(const std::string& path, std::string* out) { std::ifstream f(path, std::ios::binary); if (!f) return false; std::stringstream ss; ss << f.rdbuf(); *out = ss.str(); return true; }
static int fails = 0;
// ---- Marbles / Tides: the tolerance gate (see the header) ------------------------------------------------------------------------------------------------------------------------------
static const double kGateDb = -100.0;
#ifdef THUNDER_MODFX_MODS
static const bool kModsBuilt = true;
#else
static const bool kModsBuilt = false;   // no mods library on this platform (MSVC: the DSP libraries are not built): Marbles / Tides are not in the registry
#endif
static bool isTolCase(const char* nm) { return !std::strncmp(nm, "marbles_", 8) || !std::strncmp(nm, "tides_", 6); }
static int gTolNotBuilt = 0;
static bool isTol(const char* type) { return !std::strcmp(type, "marbles") || !std::strcmp(type, "tides"); }
struct TolSkip { const char* name; const char* why; };
static const TolSkip kTolSkip[] = { {"", ""} };   // none: every case of Marbles / Tides is within the line (measured)
static const TolSkip* tolSkipOf(const char* nm) { for (const TolSkip& k : kTolSkip) if (k.name[0] && !std::strcmp(k.name, nm)) return &k; return nullptr; }
static bool inExact(const char* nm) { for (int i = 0; i < kModsExactN; i++) if (!std::strcmp(kModsExact[i], nm)) return true; return false; }
static const char* nativeSha(const char* nm) { for (int i = 0; i < kModsNativeN; i++) if (!std::strcmp(kModsNative[i].name, nm)) return kModsNative[i].sha256; return nullptr; }
static std::string shaOf(const float* f, size_t n) { Sha256 h; h.add((const char*)f, n * 4); return h.hex(); }
struct Cmp { bool exact = true; double db = -INFINITY, err = 0, peak = 0; long nDiff = 0, first = -1; double got = 0, want = 0; };
static Cmp compareF(const float* a, const float* r, size_t n) {
  Cmp c;
  for (size_t i = 0; i < n; i++) { c.peak = std::max(c.peak, (double)std::fabs(r[i])); if (std::memcmp(&a[i], &r[i], 4) == 0 || (std::isnan(a[i]) && std::isnan(r[i]))) continue;
    c.exact = false; c.nDiff++; if (c.first < 0) { c.first = (long)i; c.got = a[i]; c.want = r[i]; } c.err = std::max(c.err, std::fabs((double)a[i] - (double)r[i])); }
  c.db = c.exact ? -INFINITY : 20 * std::log10(c.err / std::max(c.peak, 1e-30)); return c;
}
struct TolRow { std::string name, sha; bool exact; };
static std::vector<TolRow> gTol; static int gTolOk = 0, gTolExact = 0; static double gTolWorst = -INFINITY; static std::string gTolWorstName; static bool gWrite = false;
// one Marbles / Tides render or case against its reference samples: the line, the exact list, the native hash. Returns false (and counts a failure) when one of them is broken.
static void tolCheck(const std::string& name, const float* got, const float* ref, size_t n) {
  const Cmp c = compareF(got, ref, n); const std::string sha = shaOf(got, n); gTol.push_back({name, sha, c.exact});
  if (const TolSkip* k = tolSkipOf(name.c_str())) { std::printf("%-28s SKIPPED: %s\n", name.c_str(), k->why); return; }
  bool ok = c.exact || c.db <= kGateDb; const char* why = ok ? "" : "  beyond the line";
  if (!gWrite) { if (inExact(name.c_str()) && !c.exact) { ok = false; why = "  was bit-exact: the exact list is enforced"; }
    const char* w = nativeSha(name.c_str()); if (!w || sha != w) { ok = false; why = w ? "  NATIVE HASH DIFFERS from the committed one (golden/modfx_mods_native.inc)" : "  no committed native hash"; } }
  if (ok) { gTolOk++; if (c.exact) gTolExact++; else if (c.db > gTolWorst) { gTolWorst = c.db; gTolWorstName = name; } }
  if (!c.exact || !ok) std::printf("%-28s %.1f dB (%ld of %zu samples differ)%s%s\n", name.c_str(), c.db, c.nDiff, n, why, ok ? "" : "  FAIL");
  if (!ok) { fails++; std::printf("    first differing sample %ld: got %.9g want %.9g; largest difference %.3g against a peak of %.3g\n", c.first, c.got, c.want, c.err, c.peak); }
}
static uint64_t fnv64(const void* d, size_t n, uint64_t h = 0xcbf29ce484222325ull) { const unsigned char* u = (const unsigned char*)d; for (size_t i = 0; i < n; i++) { h ^= u[i]; h *= 0x100000001b3ull; } return h; }
static uint32_t fnv32(const void* d, size_t n, uint32_t h) { const unsigned char* u = (const unsigned char*)d; for (size_t i = 0; i < n; i++) { h ^= u[i]; h *= 0x01000193u; } return h; }
#if defined(__linux__) && (defined(__x86_64__) || defined(_M_X64))
static const bool kLocalMustMatch = true;
#else
static const bool kLocalMustMatch = false;
#endif

class ListSeeds : public Seeds {
 public:
  ListSeeds(const unsigned* v, int n) : v_(v, v + n) {}
  uint32_t next() override { return i_ < v_.size() ? v_[i_++] : 0; }
 private:
  std::vector<unsigned> v_; size_t i_ = 0;
};

// U21: ctx.key (the Source of the keyed modules). The engine run: the real ChainsEngine's key() gives its silent buffer for any Source above 0 (no track taps, no sends); the direct cases with `key`:
// the input reversed in time times .7 (tools/modfx-dump.mjs makes the same buffer). A Source of 0 (or rounding to it) is null (Self).
static float gKeyBuf[128];
static const float* keyEngine(void*, double v) { v = js_round(v); if (!(v > 0)) return nullptr; std::memset(gKeyBuf, 0, sizeof gKeyBuf); return gKeyBuf; }
static const float* keyDirect(void*, double v) { v = js_round(v); return v > 0 ? gKeyBuf : nullptr; }

// ---- 1. the tables -------------------------------------------------------------------------------------------------------------------------------------------------------------
static void tableTest(const std::string& dir) {
  std::string js; if (!readFile(dir + "/defs.json", &js)) { std::printf("defs.json missing  FAIL\n"); fails++; return; }
  JsonDoc doc; if (!doc.parse(js)) { std::printf("defs.json unreadable  FAIL\n"); fails++; return; }
  Json A = doc.root(); int bad = 0;
  const int expect = (int)A.size() - (kModsBuilt ? 0 : 2);   // the JS list holds Marbles and Tides; a platform without the mods library has neither
  if (!kModsBuilt) std::printf("tables: Marbles / Tides are NOT BUILT on this platform (no mods library): the 2 modules are left out of this comparison\n");
  if (expect != count()) { std::printf("tables: the JS list holds %d modules, the registry %d  FAIL\n", (int)A.size(), count()); bad++; }
  for (int m = 0; m < count(); m++) {
    const Def& d = at(m); Json J = A.get(d.type); bool b = false;
    if (J.missing()) { std::printf("tables: %s is not in the JS list  FAIL\n", d.type); fails++; continue; }
    Json in = J.arr("in"), out = J.arr("out"), pa = J.arr("params");
    if ((int)in.size() != d.nin || (int)out.size() != d.nout || (int)pa.size() != d.np) b = true;
    for (int k = 0; !b && k < d.nin; k++) if (in.at((size_t)k).at(0).asStr() != d.in[k].name || in.at((size_t)k).at(1).asStr() != d.in[k].kind) b = true;
    for (int k = 0; !b && k < d.nout; k++) if (out.at((size_t)k).at(0).asStr() != d.out[k].name || out.at((size_t)k).at(1).asStr() != d.out[k].kind) b = true;
    for (int k = 0; !b && k < d.np; k++) { Json p = pa.at((size_t)k); if (p.at(0).asStr() != d.params[k].name || p.at(1).asNum() != d.params[k].def || p.at(2).asNum() != d.params[k].mn || p.at(3).asNum() != d.params[k].mx || p.at(4).asNum() != d.params[k].st) b = true; }
    if (b) { std::printf("tables: %s differs from the JS (ports or parameters)  FAIL\n", d.type); bad++; }
  }
  if (bad) fails++;
  std::printf("tables: %d modules, ports and parameters equal the JS's  %s\n", count(), bad ? "FAIL" : "ok");
}

// ---- 2. the engine run ---------------------------------------------------------------------------------------------------------------------------------------------------------
// H.testSignal(n): .4 sin(2 pi 220 i / sr) + .2 sin(2 pi 1337 i / sr) + noise in the first quarter, an xorshift32 (seed 12345), through Math.fround
static void testSignal(float* out, int n) {
  uint32_t s = 12345;
  for (int i = 0; i < n; i++) {
    s ^= s << 13; s ^= s >> 17; s ^= s << 5; const double noise = ((double)s / 4294967296.0 - .5) * .1;
    out[i] = (float)(.4 * js_sin(2 * 3.141592653589793 * 220 * i / 48000) + .2 * js_sin(2 * 3.141592653589793 * 1337 * i / 48000) + (i < (n >> 2) ? noise : 0));
  }
}
static void paramSet(const Def& d, int set, double* p) {   // default / stress / switchesOn of tests/chains.test.js
  for (int k = 0; k < d.np; k++) {
    const ParamDef& q = d.params[k]; const double mn = q.mn, mx = q.mx, st = q.st;
    if (set == 0) { p[k] = q.def; continue; }
    if (set == 2 && !std::strcmp(q.name, "flip")) { p[k] = q.def; continue; }
    if (mx - mn == 1 && st == 1) { p[k] = set == 1 ? mn : (!std::strcmp(q.name, "mute") ? mn : mx); continue; }
    double v = mn + (mx - mn) * (set == 1 ? .7 : .3); if (st >= 1) v = js_round((v - mn) / st) * st + mn; p[k] = v;
  }
}
static uint32_t engineHash(const Def& d, int set, const ModfxEngineGolden& G, std::vector<float>* cat = nullptr) {
  const int BL = 400; std::vector<float> sig((size_t)BL * 128), L((size_t)BL * 128), R((size_t)BL * 128); testSignal(sig.data(), BL * 128);
  std::vector<std::vector<float>> rec((size_t)d.nout, std::vector<float>((size_t)BL * 128));
  double p[128] = {}; paramSet(d, set, p);
  ListSeeds seeds(G.seeds, G.nseeds); std::unique_ptr<Module> m = d.make(48000.0, seeds);
  float inb[kMaxPorts][128] = {}, outb[kMaxPorts][128] = {}; IO io; io.p = p;
  bool hasIn = false, hasInR = false;
  for (int k = 0; k < d.nin; k++) { io.in[k] = inb[k]; if (!std::strcmp(d.in[k].kind, "audio")) { if (!std::strcmp(d.in[k].name, "in")) hasIn = true; if (!std::strcmp(d.in[k].name, "inR")) hasInR = true; } }
  for (int k = 0; k < d.nout; k++) io.out[k] = outb[k];
  for (int k = 0; k < d.nin; k++) { if (!std::strcmp(d.in[k].name, "in") && hasIn) io.linked[k] = true; if (!std::strcmp(d.in[k].name, "inR") && hasInR) io.linked[k] = true; }
  Ctx c; c.sr = 48000; c.tempo = 120; c.lpb = 4; c.ticks = 6; c.playing = true; c.startN = 0; c.key = keyEngine;
  for (int b = 0; b < BL; b++) {
    for (int k = 0; k < d.nin; k++) { const bool fed = (!std::strcmp(d.in[k].name, "in") && hasIn) || (!std::strcmp(d.in[k].name, "inR") && hasInR);
      for (int i = 0; i < 128; i++) inb[k][i] = fed ? (float)(0.0 + (double)sig[(size_t)b * 128 + i]) : 0.0f; }   // trackIn's out into the zeroed input (0 + x: a -0 becomes +0)
    m->run(io, c);
    for (int i = 0; i < 128; i++) {
      const float a = d.nout > 0 ? outb[0][i] : 0.f, bb = d.nout > 1 ? outb[1][i] : a;   // chainOut: the first output into In, the second into In R (else In plays on both sides)
      L[(size_t)b * 128 + i] = (float)(0.0 + (double)(float)(0.0 + (double)a) * 1.0); R[(size_t)b * 128 + i] = (float)(0.0 + (double)(float)(0.0 + (double)bb) * 1.0);
      for (int k = 0; k < d.nout; k++) rec[(size_t)k][(size_t)b * 128 + i] = outb[k][i];
    }
  }
  uint32_t h = 0x811c9dc5u; h = fnv32(L.data(), L.size() * 4, h); h = fnv32(R.data(), R.size() * 4, h); for (int k = 0; k < d.nout; k++) h = fnv32(rec[(size_t)k].data(), rec[(size_t)k].size() * 4, h);
  if (cat) { cat->clear(); cat->insert(cat->end(), L.begin(), L.end()); cat->insert(cat->end(), R.begin(), R.end()); for (int k = 0; k < d.nout; k++) cat->insert(cat->end(), rec[(size_t)k].begin(), rec[(size_t)k].end()); }
  return h;
}
static std::string gDir;
static void engineTest(const std::string& filt) {
  int ran = 0, bad = 0;
  for (int i = 0; i < kModfxEngineN; i++) {
    const ModfxEngineGolden& G = kModfxEngine[i]; if (!filt.empty() && !std::strstr(G.type, filt.c_str())) continue;
    if (!kModsBuilt && isTol(G.type)) { gTolNotBuilt++; continue; }
    const Def* d = find(G.type); if (!d) { std::printf("engine: %s is not in the registry  FAIL\n", G.type); bad++; continue; }
    if (isTol(G.type)) {   // Marbles / Tides: the reference is the wasm's samples, the gate the line (not the hash)
      const std::string nm = std::string("engine_") + G.type + "_" + (G.set == 0 ? "default" : G.set == 1 ? "stressed" : "switches-on"); std::string rf;
      std::vector<float> cat; engineHash(*d, G.set, G, &cat); ran++;
      if (!readFile(gDir + "/" + nm + ".f32", &rf) || rf.size() != cat.size() * 4) { std::printf("%-28s reference samples missing or the wrong size  FAIL\n", nm.c_str()); bad++; continue; }
      const int f0 = fails; tolCheck(nm, cat.data(), (const float*)rf.data(), cat.size()); if (fails != f0) { bad++; fails = f0; } continue; }
    const uint32_t h = engineHash(*d, G.set, G); ran++;
    if (h != G.hash) { std::printf("engine: %-10s @%-12s %08x, tests/golden/chains-modules.json has %08x  FAIL\n", G.type, G.set == 0 ? "default" : G.set == 1 ? "stressed" : "switches-on", h, G.hash); bad++; }
  }
  if (filt.empty()) for (int m = 0; m < count(); m++) { int n = 0; for (int i = 0; i < kModfxEngineN; i++) if (!std::strcmp(kModfxEngine[i].type, at(m).type)) n++; if (n != 3) { std::printf("engine: %s has %d golden hashes, not 3  FAIL\n", at(m).type, n); bad++; } }
  if (bad) fails++;
  std::printf("engine run (tests/golden/chains-modules.json): %d of %d renders reproduce the committed hash, %d differ  %s\n", ran - bad, ran, bad, bad ? "FAIL" : "ok");
}


// ---- 4. U21: the formula compiler BBExpr on its own (bytebeat) --------------------------------------------------------------------------------------------------------------------------
// every formula of the reference list (the 16 built-in and the extra ones: operators, constant folding, the && / || with a constant, limits, every error): ok / error text and position / node count /
// bytecode size and run() / s8() on a grid, equal to the JS's; the canonical stream hashes to the committed kBbHash (integer maths: the hash is the gate on every platform, the local comparison too).
static void bbTest(const std::string& dir) {
  std::string js, bin; if (!readFile(dir + "/bbexpr.json", &js) || !readFile(dir + "/bbexpr.i32", &bin)) { std::printf("bbexpr: reference files missing  FAIL\n"); fails++; return; }
  JsonDoc doc; if (!doc.parse(js)) { std::printf("bbexpr: bbexpr.json unreadable  FAIL\n"); fails++; return; }
  Json meta = doc.root().arr("meta"), grid = doc.root().arr("grid"); const int* ref = (const int*)bin.data(); const size_t nref = bin.size() / 4; size_t ri = 0;
  std::vector<unsigned char> bytes; auto i32le = [&](int32_t v) { for (int k = 0; k < 4; k++) bytes.push_back((unsigned char)((uint32_t)v >> (8 * k))); };
  int bad = 0;
  for (size_t m = 0; m < meta.size(); m++) {
    Json M = meta.at(m); const std::string src(M.str("src")); BBProg P; std::string err; int pos = 0;
    const bool ok = BBProg::compile(src, &P, &err, &pos);
    if (ok != (M.num("ok") != 0) || (!ok && (pos != (int)M.num("pos") || err != std::string(M.str("msg")))) || (ok && (P.nodes() != (int)M.num("nodes") || P.size() != (int)M.num("size")))) {
      if (bad++ < 12) std::printf("bbexpr: '%.40s': ok %d pos %d nodes %d size %d '%s' differs from the JS (ok %d pos %d nodes %d size %d '%s')  FAIL\n", src.c_str(), ok, pos, ok ? P.nodes() : 0, ok ? P.size() : 0, err.c_str(),
        (int)M.num("ok"), (int)M.num("pos"), (int)M.num("nodes"), (int)M.num("size"), std::string(M.str("msg")).c_str()); }
    i32le(ok ? 1 : 0); i32le(ok ? 0 : pos); i32le(ok ? P.nodes() : 0); i32le(ok ? P.size() : 0); for (char ch : err) bytes.push_back((unsigned char)ch); bytes.push_back(0);
    if (!ok) continue;
    for (size_t g = 0; g < grid.size(); g++) {
      Json G = grid.at(g); const double t = G.at(0).asNum(), a = G.at(1).asNum(), b = G.at(2).asNum(), c = G.at(3).asNum();
      const int32_t v[4] = {P.run(t, a, b, c), P.s8((int32_t)t, (int32_t)a, (int32_t)b, (int32_t)c, 0), P.s8((int32_t)t, (int32_t)a, (int32_t)b, (int32_t)c, 1), P.s8((int32_t)t, (int32_t)a, (int32_t)b, (int32_t)c, 2)};
      for (int k = 0; k < 4; k++) { i32le(v[k]); if (ri >= nref || ref[ri] != v[k]) { if (bad++ < 12) std::printf("bbexpr: '%.40s' grid %zu value %d: got %d want %d  FAIL\n", src.c_str(), g, k, v[k], ri < nref ? ref[ri] : 0); } ri++; }
    }
  }
  const uint64_t h = fnv64(bytes.data(), bytes.size());
  if ((int)meta.size() != kBbN || h != kBbHash) { std::printf("bbexpr: %d formulas, hash %016llx, the committed kBbHash is %016llx  FAIL\n", (int)meta.size(), (unsigned long long)h, kBbHash); bad++; }
  if (ri != nref) { std::printf("bbexpr: %zu reference values, %zu used  FAIL\n", nref, ri); bad++; }
  if (bad) fails++;
  std::printf("bbexpr: %d formulas (compile result, error text, run and s8 on the grid) equal the JS's and the committed hash  %s\n", (int)meta.size(), bad ? "FAIL" : "ok");
}

// Web build 633 (tests/bbexpr-const-logic.test.js): `x && constant` / `x || constant` always read the constant, so the right value comes back and every t returns (51 used to jump: a hang).
static void bbConstTest() {
  struct C { const char* src; double t, a, want; };
  static const C cases[] = {{"(t && 51) + a", 0, 7, 7}, {"(t && 51) + a", 3, 7, 8}, {"(t || 51) + a", 3, 7, 8}, {"(t || 51) + a", 0, 7, 8}, {"(t || 50) + a", 2, 7, 8},
    {"(t && 5) + a", 0, 7, 7}, {"(t || 0) + a", 0, 7, 7}, {"(t && 50) * 3 + a", 0, 7, 7}, {"t && 10", 0, 7, 0}, {"(t || 0) + (t && 51) + a", 0, 1, 1}};
  int bad = 0;
  for (const C& c : cases) {
    BBProg P; std::string err; int pos = 0;
    if (!BBProg::compile(c.src, &P, &err, &pos)) { std::printf("bbexpr const: '%s' does not compile  FAIL\n", c.src); bad++; continue; }
    const int32_t v = P.run(c.t, c.a, 0, 0);
    if (v != (int32_t)c.want) { std::printf("bbexpr const: '%s' at t=%g: got %d want %d  FAIL\n", c.src, c.t, v, (int)c.want); bad++; }
    for (int t = -4; t < 600; t++) P.run(t, 3, 5, 9);   // every t returns
  }
  if (bad) fails++;
  std::printf("bbexpr const (build 633, %d cases of tests/bbexpr-const-logic.test.js): %s\n", (int)(sizeof cases / sizeof cases[0]), bad ? "FAIL" : "ok");
}

// ---- 3. the direct cases
// U20: the song's impulse responses of a case: made here exactly as tools/modfx-b-cases.mjs makes them (an integer xorshift and basic arithmetic only: no Math.*, so the same on every platform)
struct IrObj { std::vector<float> L, R; SongIR ir; };
static std::unique_ptr<IrObj> irMake(double n_, double sr, bool st, double seedD, bool nan) {
  std::unique_ptr<IrObj> o(new IrObj); const int n = (int)n_; const uint32_t seed = (uint32_t)seedD;
  for (int ch = 0; ch < (st ? 2 : 1); ch++) {
    std::vector<float>& a = ch ? o->R : o->L; a.assign((size_t)n, 0.f); uint32_t s = seed * 2654435761u + (uint32_t)ch * 40503u; if (!s) s = 1; double env = 1; const double d = 1 - 9.0 / n;
    for (int i = 0; i < n; i++) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; const double noise = (double)s / 4294967296.0 - .5; a[(size_t)i] = (float)(i == 0 ? 1.0 : noise * env * (i % 50 == 0 ? 3.0 : 1.0)); env *= d; }
    if (nan) a[5] = std::nanf("");
  }
  o->ir.L = o->L.data(); o->ir.nL = n; o->ir.R = st ? o->R.data() : nullptr; o->ir.nR = st ? n : 0; o->ir.sr = sr; o->ir.id = seed;
  return o;
}
// The inputs of a direct case, made here as tools/modfx-dump.mjs makes them (signal() and, for sig "bad", mkBad of modfx-b-cases.mjs): doubles, an integer xorshift and js_sin (Node's Math.sin, bit-exact on
// every platform), rounded to float at the end. The fixture no longer writes them (run the tool with --inputs to get <case>.in.f32; runCase then checks these against that file, bit for bit).
static uint32_t jsLcgNext(uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
static void makeSignal(const char* kind, const std::string& sigKind, size_t n, uint32_t seed, bool pitch, float* o) {
  uint32_t s = seed ? seed : 1; auto r = [&]() { return (double)jsLcgNext(s) / 4294967296.0; };
  if (!std::strcmp(kind, "gate")) {
    size_t i = 0; bool hi = false; const double lo = r() < .2 ? .4 : 0, up = r() < .2 ? .6 : 1;
    while (i < n) { const int len = (int)std::floor(200 + r() * 6000); const int w = hi ? std::max(1, (int)std::floor(len * (.1 + r() * .5))) : len; for (int j = 0; j < w && i < n; j++, i++) o[i] = (float)(hi ? up : lo); hi = !hi; }
    return;
  }
  if (!std::strcmp(kind, "cv")) {
    size_t i = 0; double v = r() * 1.2 - .1;
    while (i < n) { const int len = (int)std::floor(100 + r() * 2000); const double nv = pitch ? r() * 1.2 - .1 : r() * 2.4 - 1.2; const bool slew = r() < .5;
      for (int j = 0; j < len && i < n; j++, i++) o[i] = (float)(slew ? v + (nv - v) * ((double)j / (double)len) : v); v = nv; }
    return;
  }
  const double amp = sigKind == "loud" ? 3.2 : sigKind == "quiet" ? .004 : 1; const double f1 = 80 + r() * 900, f2 = 1000 + r() * 4000;
  uint32_t ns = (uint32_t)(uint64_t)((double)seed * 2654435761.0);   /* the JS multiplies in doubles: above 2^53 (U20's seeds) the product is rounded first */ if (!ns) ns = 1; double vph = 0; const double PI2 = 2 * 3.141592653589793;
  for (size_t i = 0; i < n; i++) {
    const double noise = (double)jsLcgNext(ns) / 4294967296.0 - .5; const double di = (double)i; double v;
    if (sigKind == "impulse") v = i % 9973 == 0 ? 1 : 0;
    else if (sigKind == "voiced") { const double t = di / 48000, f = (100 + 250 * (.5 + .5 * js_sin(PI2 * (.9 + (double)(seed % 5) * .1) * t))) * (1 + .012 * js_sin(PI2 * 5.5 * t)); vph += f / 48000;
      double h = 0; for (int k = 1; k <= 6; k++) h += js_sin(PI2 * k * vph) / k; const int seg = (int)(std::floor(t / .3)) % 4; v = seg == 3 ? .002 * noise : seg == 1 ? .5 * noise : .3 * h + .02 * noise; }
    else if (sigKind == "burst") v = i < 128 * 24 ? .5 * noise + .3 * js_sin(PI2 * f1 * di / 48000) : 0;
    else if (sigKind == "longsleep") v = (i < 128 * 24 || (i >= 128 * 220 && i < 128 * 240)) ? .5 * noise + .3 * js_sin(PI2 * f1 * di / 48000) : 0;
    else { const double env = (i / 4000) % 5 == 3 ? 0 : 1; v = env * (.4 * js_sin(PI2 * f1 * di / 48000) + .2 * js_sin(PI2 * f2 * di / 48000) + (di < (double)n / 3 ? noise * .2 : 0)); }
    o[i] = (float)(v * amp);
  }
}
static int gInputsChecked = 0;
struct Res { bool ran = false, exact = false, hashOk = false; long nDiff = 0, n = 0, first = -1; double got = 0, want = 0; std::vector<float> out, ref; };
static Res runCase(const std::string& dir, const ModfxCaseGolden& G) {
  Res R; std::string js, in, ref;
  if (!readFile(dir + "/" + G.name + ".json", &js) || !readFile(dir + "/" + G.name + ".f32", &ref)) return R;
  JsonDoc doc; if (!doc.parse(js)) return R; Json M = doc.root();
  const Def* d = find(std::string(M.str("type")).c_str()); if (!d) return R;
  const int nb = (int)M.num("nblocks"); const size_t inN = (size_t)nb * d->nin * 128, outN = (size_t)nb * d->nout * 128;
  if (ref.size() != outN * 4) return R;
  Json linkedJ = M.arr("linked"); std::vector<float> I(inN, 0.f);   // unlinked inputs stay zero
  { const size_t total = (size_t)nb * 128; const std::string sk(M.str("sig")); const bool pitch = std::string(M.str("hint")) == "pitch", bad = sk == "bad"; std::vector<float> a(total);
    for (int k = 0; k < d->nin; k++) { if (!linkedJ.at((size_t)k).asBool()) continue;
      if (bad) { makeSignal(d->in[k].kind, "mix", total, (uint32_t)(4242 + k * 7), false, a.data());
        for (size_t i = 0; i < total; i += 331) { const uint32_t q = 0x7fc00000u; std::memcpy(&a[i], &q, 4); } for (size_t i = 17; i < total; i += 1511) a[i] = INFINITY; for (size_t i = 29; i < total; i += 1801) a[i] = (i & 1) ? 9.5f : -8.f; }
      else makeSignal(d->in[k].kind, sk, total, (uint32_t)(M.num("seed") * 17 + k * 131 + 1), pitch, a.data());
      for (int b = 0; b < nb; b++) std::memcpy(&I[((size_t)b * d->nin + k) * 128], &a[(size_t)b * 128], 512); }
    if (readFile(dir + "/" + G.name + ".in.f32", &in)) { const bool same = in.size() == inN * 4 && std::memcmp(in.data(), I.data(), inN * 4) == 0; if (!same) { std::printf("%s: the inputs made here differ from %s.in.f32  FAIL\n", G.name, G.name); return R; } ++gInputsChecked; } }
  const float* J = (const float*)ref.data();
  Json seedsJ = M.arr("seeds"), sizes = M.arr("sizes"), linked = M.arr("linked"), params = M.arr("params"), tr = M.arr("transport");
  unsigned sd[8] = {}; int ns = 0; for (size_t i = 0; i < seedsJ.size() && i < 8; i++) { sd[i] = (unsigned)seedsJ.at(i).asNum(); ns++; }
  ListSeeds seeds(sd, ns); std::unique_ptr<Module> m = d->make(M.num("sr"), seeds);
  std::vector<float> out(outN); float inb[kMaxPorts][128] = {}, outb[kMaxPorts][128] = {}; IO io; double p[128] = {}; io.p = p;
  for (int k = 0; k < d->nin; k++) { io.in[k] = inb[k]; io.linked[k] = linked.at((size_t)k).asBool(); }
  for (int k = 0; k < d->nout; k++) io.out[k] = outb[k];
  Ctx c; c.sr = M.num("sr"); size_t pe = 0, te = 0; const bool keyed = M.get("key").isBool() && M.boolean("key"); if (keyed) c.key = keyDirect;
  Json irsJ = M.arr("irs"); size_t ie = 0; std::vector<std::unique_ptr<IrObj>> irHold; const SongIR* irTab[4] = {nullptr, nullptr, nullptr, nullptr};   // each entry makes NEW objects, as the JS does
  for (int b = 0; b < nb; b++) {
    while (ie < irsJ.size() && irsJ.at(ie).num("b") <= b) {
      Json set = irsJ.at(ie++).arr("set");
      for (int k = 0; k < 4; k++) { Json e = set.at((size_t)k); if (!e.isObj()) { irTab[k] = nullptr; continue; }
        irHold.push_back(irMake(e.num("n"), e.num("sr"), e.boolean("st"), e.num("seed"), e.boolean("nan"))); irTab[k] = &irHold.back()->ir; }
      c.irs = irTab;
    }
    while (pe < params.size() && params.at(pe).num("b") <= b) { Json pp = params.at(pe).arr("p"); for (int k = 0; k < d->np; k++) p[k] = pp.at((size_t)k).asNum(); pe++; }
    while (te < tr.size() && tr.at(te).num("b") <= b) { Json t = tr.at(te++); if (t.get("tempo").isNum()) c.tempo = t.num("tempo"); if (t.get("playing").isBool()) c.playing = t.boolean("playing"); if (t.get("startN").isNum()) c.startN = t.num("startN"); }
    c.block = (int)sizes.at((size_t)b).asNum();
    for (int k = 0; k < d->nin; k++) std::memcpy(inb[k], I.data() + ((size_t)b * d->nin + k) * 128, 512);
    if (keyed) for (int i = 0; i < 128; i++) gKeyBuf[i] = (float)(.7 * (double)inb[0][127 - i]);
    m->run(io, c);
    for (int k = 0; k < d->nout; k++) std::memcpy(&out[((size_t)b * d->nout + k) * 128], outb[k], 512);
  }
  R.ran = true; R.n = (long)outN; bool exact = true;
  for (size_t i = 0; i < outN; i++) { const float a = out[i], r = J[i]; if (std::memcmp(&a, &r, 4) == 0 || (std::isnan(a) && std::isnan(r))) continue; exact = false; R.nDiff++; if (R.first < 0) { R.first = (long)i; R.got = a; R.want = r; } }
  R.exact = exact; R.hashOk = fnv64(out.data(), outN * 4) == G.hash;
  if (isTol(d->type)) { R.out = out; R.ref.assign(J, J + outN); }
  return R;
}

int main(int argc, char** argv) {
  if (argc < 2) { std::printf("modfx: no reference directory given: FAIL (a fixture that cannot run is a failure)\n"); return 1; }
  gDir = argv[1]; std::string filt; const char* writeNative = nullptr;
  for (int i = 2; i < argc; i++) { if (!std::strcmp(argv[i], "--write-native") && i + 1 < argc) { writeNative = argv[++i]; gWrite = true; } else filt = argv[i]; }
  if (filt.empty()) { tableTest(argv[1]); bbTest(argv[1]); bbConstTest(); }
  engineTest(filt);
  int ran = 0, exact = 0, shown = 0;
  for (int i = 0; i < kModfxCasesN; i++) {
    const ModfxCaseGolden& G = kModfxCases[i]; if (!filt.empty() && !std::strstr(G.name, filt.c_str())) continue;
    if (!kModsBuilt && isTolCase(G.name)) { gTolNotBuilt++; continue; }
    const Res R = runCase(argv[1], G);
    if (!R.ran) { std::printf("%-24s could not be run (reference files missing or the wrong size)  FAIL\n", G.name); fails++; continue; }
    if (!R.out.empty()) { ran++; tolCheck(G.name, R.out.data(), R.ref.data(), R.out.size()); continue; }   // Marbles / Tides
    ran++; const bool ok = R.hashOk && (R.exact || !kLocalMustMatch); if (R.hashOk) exact++;
    if (!ok || !R.exact) { if (shown++ < 60) { std::printf("%-24s %s%s%s\n", G.name, R.hashOk ? "bit-exact (committed hash)" : "DIFFERS from the committed hash", R.exact ? "" : R.hashOk ? "  [the local Node reference differs: its Math.* rounds otherwise]" : "", ok ? "" : "  FAIL");
      if (!R.exact) std::printf("    %ld of %ld samples differ; first at %ld: got %.9g want %.9g\n", R.nDiff, R.n, R.first, R.got, R.want); } }
    if (!ok) fails++;
  }
  std::printf("modfx: inputs made in C++; %d of the cases were also checked bit for bit against a written <case>.in.f32\n", gInputsChecked);
  if (filt.empty()) { int unc = 0; for (int m = 0; m < count(); m++) { int n = 0; for (int i = 0; i < kModfxCasesN; i++) if (!std::strncmp(kModfxCases[i].name, at(m).type, std::strlen(at(m).type)) && kModfxCases[i].name[std::strlen(at(m).type)] == '_') n++; if (n < 14) { std::printf("direct: %s has %d cases  FAIL\n", at(m).type, n); unc++; } } if (unc) fails++; }
  if (ran == 0) { std::printf("modfx: no case ran  FAIL\n"); fails++; }
  std::printf("modfx: %d of %d direct cases bit-exact (committed hash), %d failed\n", exact, ran, fails);
  if (gTolNotBuilt) std::printf("Marbles / Tides: NOT RUN, %d renders and cases skipped: this platform has no mods library (THUNDER_MODFX_MODS is off here); they are gated on every platform that builds it\n", gTolNotBuilt);
  if (!gTol.empty()) std::printf("Marbles / Tides (native strict against the fast-math wasm): %zu renders and cases, %d inside the %.0f dB line (%d of them bit-exact); worst of the rest %s (%s)\n", gTol.size(), gTolOk, kGateDb, gTolExact,
    std::isinf(gTolWorst) ? "none" : (std::to_string(gTolWorst) + " dB").c_str(), gTolWorstName.c_str());
  if (writeNative) { std::ofstream o(writeNative); int ne = 0; for (auto& r : gTol) if (r.exact) ne++;
    o << "// GENERATED by test_modfx --write-native: Marbles / Tides, the native STRICT build against the fast-math wasm (docs/port/U21-NOTES.md). kModsExact: the renders and cases that are bit-exact with the wasm today (a\n// case that leaves the list fails); kModsNative: the SHA-256 of the native render of each (made on Linux x86-64, checked on every platform).\n";
    o << "static const char* const kModsExact[] = {\n"; for (auto& r : gTol) if (r.exact) o << "  \"" << r.name << "\",\n"; o << "  \"\"};\nstatic const int kModsExactN = " << ne << ";\n";
    o << "struct ModsNative { const char* name; const char* sha256; };\nstatic const ModsNative kModsNative[] = {\n"; for (auto& r : gTol) o << "  {\"" << r.name << "\", \"" << r.sha << "\"},\n"; o << "  {\"\", \"\"}};\nstatic const int kModsNativeN = " << gTol.size() << ";\n";
    std::printf("wrote %s: %d exact, %zu hashes\n", writeNative, ne, gTol.size()); }
  return fails ? 1 : 0;
}
