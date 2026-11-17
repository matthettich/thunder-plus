// U33: the …pump synths hosted natively (core/src/engine/pumpsynth.{h,cpp}: PumpSynth, xeng_psyn) against the web app's own render path (tools/psyn-dump.mjs: pump/synth/pump-synth-engine.js
// PsynCore.render + pump-synth.wasm in Node, SAMPLES): 507 cases (every row of tests/golden/pump-synth.json; every factory preset of the 13 synths; every synth with all parameters at their minimum, at
// their maximum, and three seeded random sets; the pad's macros: Tune, Hit, Level + Drive, hold, a kit's Drum, WOB's tempo, FREEZE's File; sample rates 8000 to 96000).
//   test_pumpsynth <dir> [--write-gate FILE] [case-substring]
// THE GATE (the lead's rule for a fast-math-wasm reference, docs/port/TODAY.md "Evening, 16 Nov" items 1 and 7; the pump effects' gate, U18-NOTES.md): the web's pump-synth.wasm is built with -ffast-math
// (float sums reordered, which no native build reproduces) and the native build is STRICT. So per case:
//   (a) within kGateDb (-100 dB: max |difference| / the reference's peak), the same length; the worst printed;
//   (b) the cases that are bit-exact today stay bit-exact (golden/psyn_gate.inc, written by --write-gate, never hand-edited), enforced where it holds: Linux x86-64 (kExactHoldsHere, as test_pump.cpp);
//   (c) NO committed native hashes: the strict build calls the system libm;
//   (d) a synth that cannot be within -100 dB (chaotic / noise driven) is NOT loosened: it is named in kLeftOut with the printed reason and numbers, and it gets a check that does not depend on the
//       trajectory: finite, its RMS level over the held note (the hold window) within kLevelDb of the reference's, its length within the percent stated per case (0.3 to 30 % measured; the ring-out of
//       a chaotic synth ends in another 4096-sample block). Not in kLeftOut, it must pass (a), with the same length.
// Also: the parameter table and counts against the page's meta (the reference writes it), the page's KINDS (type, len, trim, kit, drums), latency of every synth at five rates, an order check (all
// cases again in reverse order give the same bits: pump keeps process-global state), and that a note rendered twice is the same render.
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

#include "engine/pumpsynth.h"
#include "rt/json.h"
#include "golden/psyn_cases.inc"
#include "golden/psyn_gate.inc"

using namespace thunder;

static bool readFile(const std::string& path, std::string* out) { std::ifstream f(path, std::ios::binary); if (!f) return false; std::stringstream ss; ss << f.rdbuf(); *out = ss.str(); return true; }
static uint64_t fnv(const float* f, size_t n) { uint64_t h = 0xcbf29ce484222325ull; const unsigned char* u = (const unsigned char*)f; for (size_t i = 0; i < n * 4; i++) { h ^= u[i]; h *= 0x100000001b3ull; } return h; }
static int fails = 0;

static const double kGateDb = -100.0, kLevelDb = 1.0;
struct LeftOut { const char* name; double maxLenPct; const char* why; };
// Cases left out of the -100 dB gate BY NAME, each with the reason and the measured numbers (the structural check replaces the gate). Filled from a run; see docs/port/U33-NOTES.md.
#include "golden/psyn_leftout.inc"
static const LeftOut* leftOut(const char* nm) { for (int i = 0; i < kPsynLeftOutN; i++) if (std::strcmp(kPsynLeftOut[i].name, nm) == 0) return &kPsynLeftOut[i]; return nullptr; }
static bool inList(const char* const* l, int n, const char* nm) { for (int i = 0; i < n; i++) if (std::strcmp(l[i], nm) == 0) return true; return false; }
#if defined(__linux__) && defined(__x86_64__) && !defined(__ANDROID__)
static const bool kExactHoldsHere = true;
#else
static const bool kExactHoldsHere = false;
#endif

struct Res { bool ran = false, exact = false, finite = true, lenOk = true; double db = -INFINITY, peak = 0, err = 0, rmsDb = 0, holdDb = 0, lenPct = 0; long n = 0, nref = 0, first = -1; double firstGot = 0, firstWant = 0; uint64_t hash = 0; std::string why; };

static const char* const kAlias[][2] = {{"drive", "o_drive"}, {"tune", "o_tune"}, {"level", "o_level"}};
static std::string padKey(const char* sym) { for (auto& a : kAlias) if (!std::strcmp(a[0], sym)) return a[1]; return sym; }

static void padOf(Json c, PsynPad* P) {
  P->type = (int)c.num("type");
  Json pad = c.obj("pad");
  for (int i = 0, n = psyn_param_count(P->type); i < n; i++) { Json v = pad.get(padKey(psyn_param(P->type, i)->sym)); P->v[i] = v.isNum() ? v.asNum() : std::nan(""); }
  P->tune = pad.num("tune"); P->drive = pad.num("drive"); P->level = pad.num("level"); P->vel = pad.num("vel", 100); P->kd = pad.num("kd");
}

static std::map<std::string, Json> gCases;   // by name (the documents live in main)
static Res runCase(const std::string& dir, const char* name, std::vector<float>* got = nullptr) {
  Res R; auto it = gCases.find(name); if (it == gCases.end()) { R.why = "not in cases.json"; return R; }
  Json c = it->second; std::string ref; if (!readFile(dir + "/" + name + ".f32", &ref)) { R.why = "reference file missing"; return R; }
  PsynPad P; padOf(c, &P);
  std::string fileData; PsynFile F; const PsynFile* pf = nullptr;
  if (c.obj("file").isObj()) { if (!readFile(dir + "/" + name + ".file.f32", &fileData)) { R.why = "file source missing"; return R; } F.data = (const float*)fileData.data(); F.len = (double)(fileData.size() / 4); F.sr = c.obj("file").num("sr"); pf = &F; }
  const double sr = c.num("sr"), hold = c.num("hold"), vlen = c.num("vlen"), bpm = c.num("bpm");
  const size_t need = xeng_psyn(P, sr, hold, vlen, bpm, pf, nullptr, 0);
  std::vector<float> out(need ? need : 1, -777.f);
  const size_t len = xeng_psyn(P, sr, hold, vlen, bpm, pf, out.data(), out.size());
  if (len == 0) { R.why = "native render refused (0)"; return R; }
  const size_t nref = ref.size() / 4; const float* J = (const float*)ref.data();
  R.ran = true; R.n = (long)len; R.nref = (long)nref; R.lenOk = len == nref; R.hash = fnv(out.data(), len);
  const size_t m = len < nref ? len : nref; double peak = 0, err = 0, sa = 0, sb = 0; bool exact = R.lenOk;
  for (size_t i = 0; i < len; i++) if (!std::isfinite(out[i])) R.finite = false;
  for (size_t i = 0; i < nref; i++) peak = std::fmax(peak, std::fabs((double)J[i]));
  for (size_t i = 0; i < m; i++) { const double a = out[i], r = J[i]; sa += a * a; sb += r * r; if ((float)a == (float)r) continue; exact = false; const double d = std::fabs(a - r);
    if (R.first < 0) { R.first = (long)i; R.firstGot = a; R.firstWant = r; } if (!(d <= err)) err = d; }
  R.exact = exact; R.peak = peak; R.err = err;
  R.db = exact ? -INFINITY : (peak > 0 ? 20 * std::log10(err / peak) : (err > 0 ? INFINITY : -INFINITY));
  R.rmsDb = (sa > 0 && sb > 0) ? 10 * std::log10(sa / sb) : (sa == sb ? 0 : 99);
  { // the note: the hold window (what the key is held for), the RMS of each render over it; the tail after it (where a chaotic synth rings on for a different time) is the length figure
    const PsynKind* K = psyn_kind(P.type); const double h = std::fmin(12.0, std::fmax(0.02, hold != 0 ? hold : (vlen != 0 ? vlen : K->len)));
    const size_t w = std::min(m, (size_t)(h * sr)); double ha = 0, hb = 0; for (size_t i = 0; i < w; i++) { ha += (double)out[i] * out[i]; hb += (double)J[i] * J[i]; }
    R.holdDb = (ha > 0 && hb > 0) ? 10 * std::log10(ha / hb) : (ha == hb ? 0 : 99);
    R.lenPct = nref ? 100.0 * ((double)len - (double)nref) / (double)nref : 0; }
  if (got) *got = std::vector<float>(out.begin(), out.begin() + (long)len);
  return R;
}

// the page's meta holds doubles (0.0799999982), the table floats: compared as floats (the table is the float the plugin has)
static bool nearD(double a, double b) { return (float)a == (float)b || (std::isnan(a) && std::isnan(b)); }

// the parameter tables, the page's KINDS and the latencies against what the page says (cases.json)
static void metaTest(Json root) {
  int bad = 0, nk = 0, np = 0;
  Json eng = root.obj("engines"), meta = root.obj("meta");
  for (size_t i = 0; i < eng.size(); i++) {
    const std::string key(eng.keyAt(i)); Json e = eng.valAt(i); const PsynKind* K = psyn_kind_by_key(key.c_str()); nk++;
    if (!K || K->type != (int)e.num("type") || K->len != e.num("len") || K->trim != e.num("trim") || K->kit != e.boolean("kit") || K->drums != (int)e.num("drums") || K->bpm != e.boolean("bpm") || std::string_view(K->name) != e.str("key")) {
      bad++; std::printf("  KINDS %s differs from the page's (type %d len %g trim %g kit %d drums %d bpm %d)  FAIL\n", key.c_str(), K ? K->type : -1, K ? K->len : 0, K ? K->trim : 0, K ? K->kit : 0, K ? K->drums : 0, K ? K->bpm : 0); continue; }
    Json m = meta.get(key); Json ps = m.arr("params");
    if ((int)ps.size() != psyn_param_count(K->type)) { bad++; std::printf("  %s: %d parameters in the page's meta, %d in the native table  FAIL\n", key.c_str(), (int)ps.size(), psyn_param_count(K->type)); continue; }
    for (size_t j = 0; j < ps.size(); j++) { Json p = ps.at(j); const PsynParamInfo* q = psyn_param(K->type, (int)j); np++;
      if (std::string_view(q->sym) != p.at(0).asStr() || !nearD((double)q->mn, p.at(1).asNum()) || !nearD((double)q->mx, p.at(2).asNum()) || !nearD((double)q->def, p.at(3).asNum()) || q->flags != (int)p.at(4).asNum()) {
        bad++; std::printf("  %s parameter %zu: %s [%g %g] def %g flags %d here, %s [%g %g] def %g flags %d in the page's meta  FAIL\n", key.c_str(), j, q->sym, q->mn, q->mx, q->def, q->flags, std::string(p.at(0).asStr()).c_str(), p.at(1).asNum(), p.at(2).asNum(), p.at(3).asNum(), (int)p.at(4).asNum()); } }
  }
  if (nk != kPsynTypes) { bad++; std::printf("  the page has %d engines, the native table %d  FAIL\n", nk, kPsynTypes); }
  Json lat = root.arr("latency"); int nl = 0, nz = 0, lbad = 0;
  for (size_t i = 0; i < lat.size(); i++) { Json x = lat.at(i); const PsynKind* K = psyn_kind_by_key(std::string(x.str("k")).c_str()); nl++; if (!K) { lbad++; continue; }
    const int want = (int)x.num("lat"), got = xeng_psyn_latency(K->type, x.num("sr")); if (want) nz++; if (got != want) { lbad++; std::printf("  latency %s at %g Hz: %d here, %d in the page  FAIL\n", K->key, x.num("sr"), got, want); } }
  bad += lbad;
  if (bad) fails++;
  std::printf("meta: %d synths, %d parameters (symbol, range, default, flags) and the page's KINDS equal; latency %d pairs (%d above 0), %d differ  %s\n", nk, np, nl, nz, lbad, bad ? "FAIL" : "ok");
}

int main(int argc, char** argv) {
#ifndef THUNDER_HAVE_PUMP
  (void)argc; (void)argv;
  std::printf("pumpsynth: not run on this platform: no pump library (it is not built here), %d cases not run\n", kPsynCaseN);
  return 77;
#endif
  if (argc < 2) { std::printf("pumpsynth: no reference directory (Node not found): %d cases FAIL\n", kPsynCaseN); return 1; }
  const std::string dir = argv[1]; const char* writeGate = nullptr; const char* sub = nullptr;
  for (int i = 2; i < argc; i++) { if (!std::strcmp(argv[i], "--write-gate") && i + 1 < argc) writeGate = argv[++i]; else sub = argv[i]; }
  const bool all = !sub, enforceExact = kExactHoldsHere;
  std::string js; if (!readFile(dir + "/cases.json", &js)) { std::printf("pumpsynth: cases.json missing in %s  FAIL\n", dir.c_str()); return 1; }
  JsonDoc doc; if (!doc.parse(js)) { std::printf("pumpsynth: cases.json unreadable  FAIL\n"); return 1; } Json root = doc.root();
  { Json cs = root.arr("cases"); for (size_t i = 0; i < cs.size(); i++) gCases[std::string(cs.at(i).str("name"))] = cs.at(i); }
  if ((int)gCases.size() != kPsynCaseN) { std::printf("pumpsynth: the reference has %zu cases, the committed list %d  FAIL\n", gCases.size(), kPsynCaseN); return 1; }
  std::printf("pumpsynth: reference = web PsynCore.render + %s; gate %.0f dB below each reference's peak%s\n", std::string(root.str("wasm")).c_str(), kGateDb, enforceExact ? ", the exact list enforced" : ", the exact list NOT enforced here (not Linux x86-64)");
  if (all) {
    metaTest(root);
    if ((int)root.num("goldenBad") != 0) { std::printf("the reference's hash of %d of the %d rows of tests/golden/pump-synth.json differs from the golden: the reference is not the web's  FAIL\n", (int)root.num("goldenBad"), (int)root.num("goldenRows")); fails++; }
  }
  int ran = 0, exact = 0, within = 0, left = 0, regress = 0, failedCases = 0; double worst = -INFINITY; const char* worstName = "";
  std::map<std::string, uint64_t> hashes; std::map<std::string, Res> results;
  for (int i = 0; i < kPsynCaseN; i++) {
    const char* nm = kPsynCaseNames[i]; if (sub && !std::strstr(nm, sub)) continue;
    const Res R = runCase(dir, nm);
    if (!R.ran) { std::printf("%-22s could not be run: %s  FAIL\n", nm, R.why.c_str()); fails++; failedCases++; continue; }
    ran++; hashes[nm] = R.hash; results[nm] = R;
    const LeftOut* lo = leftOut(nm); const bool wasExact = inList(kPsynGateExact, kPsynGateExactN, nm);
    bool ok = R.finite && (lo || R.lenOk); const char* extra = ""; char db[48];
    if (lo) { ok = ok && std::fabs(R.holdDb) <= kLevelDb && std::fabs(R.lenPct) <= lo->maxLenPct; std::snprintf(db, sizeof db, "%.1f dB (left out)", R.db); left++; }
    else { ok = ok && (R.exact || R.db <= kGateDb); if (R.exact) std::snprintf(db, sizeof db, "exact"); else std::snprintf(db, sizeof db, "%.1f dB", R.db);
      if (R.exact) exact++; else if (R.db <= kGateDb) within++; if (!R.exact && R.db > worst) { worst = R.db; worstName = nm; } }
    if (enforceExact && wasExact && !R.exact) { ok = false; regress++; extra = "  (was bit-exact: the exact list is enforced)"; }
    std::printf("%-22s %-22s len %ld/%ld (%+.1f%%) rms %+.2f dB, held %+.2f dB%s%s%s%s\n", nm, db, R.n, R.nref, R.lenPct, R.rmsDb, R.holdDb, R.finite ? "" : " NOT FINITE", R.lenOk ? "" : " LENGTH DIFFERS", extra, ok ? "" : "  FAIL");
    if (!ok || lo) std::printf("    %s: first difference at sample %ld: got %.9g want %.9g; largest %.3g against a peak of %.3g%s%s\n", lo ? "left out" : "where", R.first, R.firstGot, R.firstWant, R.err, R.peak, lo ? "; reason: " : "", lo ? lo->why : "");
    if (!ok) { fails++; failedCases++; }
  }
  if (writeGate) {
    std::ofstream o(writeGate); o << "// GENERATED by test_pumpsynth --write-gate (native/tools/parity-u33.sh with GATE_WRITE=<file>): the cases that are bit-exact against the web's pump-synth.wasm today (kPsynGateExact: a case that\n"
      "// leaves the list is a regression and fails where it holds: Linux x86-64) (made on Linux x86-64, GCC 13). Never hand-edited.\n";
    int ne = 0; for (auto& kv : results) if (kv.second.exact) ne++;
    o << "static const char* const kPsynGateExact[] = {\n"; for (auto& kv : results) if (kv.second.exact) o << "  \"" << kv.first << "\",\n"; o << "  \"\"\n};\nstatic const int kPsynGateExactN = " << ne << ";\n";
    std::printf("wrote %s: %d exact\n", writeGate, ne);
  }
  if (all) {   // reverse order: the same bits (pump's statics are shared by every instance and case of the process)
    int moved = 0, n2 = 0;
    for (int i = kPsynCaseN - 1; i >= 0; i--) { const Res R = runCase(dir, kPsynCaseNames[i]); if (!R.ran) continue; n2++; if (hashes.count(kPsynCaseNames[i]) && hashes[kPsynCaseNames[i]] != R.hash) { moved++; std::printf("  second pass: %s gives other bits than the first time  FAIL\n", kPsynCaseNames[i]); } }
    if (moved) fails++;
    std::printf("order: %d cases run again in reverse order, %d gave other bits than the first time  %s\n", n2, moved, moved ? "FAIL" : "ok");
  }
  std::printf("pumpsynth: %d cases run, %d bit-exact, %d within %.0f dB, %d left out by name (finite, level over the held note within %.1f dB, length within its stated percent); worst of the gated %s (%s); %d exact-list regressions, %d failed\n", ran, exact, within, kGateDb, left,
    kLevelDb, std::isinf(worst) ? "exact" : (std::to_string(worst) + " dB").c_str(), worstName, regress, failedCases);
  return fails ? 1 : 0;
}
