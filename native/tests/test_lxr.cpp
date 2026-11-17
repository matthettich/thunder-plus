// U04: the LXR module (native/lxr) against the web app's own render path (tools/lxr-dump.mjs: lxr/lxr-engine.js LxrCore.render + the shipped lxr/lxr.wasm in Node, SAMPLES).
//   test_lxr <dir> [--write-gate FILE] [case-substring]
// THE GATE (the lead's rule for a fast-math-wasm reference, docs/port/TODAY.md "Evening, 16 Nov" items 1 and 7): lxr.wasm is built with -O3 -ffast-math (lxr/build.sh), which lets the
// compiler reorder float sums, and the native build is STRICT (-ffp-contract=off, no fast-math). So per case:
//   (a) the firmware parameter bytes the page loads (lxr_load's input) are EXACTLY the module's (integers: the page's parameter mapping, byte for byte);
//   (b) the firmware's own voice (the raw blocks at 44.14 kHz) and the finished pad sound are each within kGateDb (-100 dB: max |difference| / the reference's peak), with the same length;
//   (c) the cases that are bit-exact today stay bit-exact (golden/lxr_gate.inc, written by --write-gate, never hand-edited), enforced where it holds (Linux x86-64: kExactHoldsHere);
//   (d) a case that cannot be within -100 dB is NOT loosened: it is named in golden/lxr_leftout.inc with the reason and numbers, and gets a check that does not depend on the trajectory
//       (finite, RMS within kLevelDb of the reference's, the length within the stated percent). Not in the list, it must pass (b).
// Also: the module's defaults and tables against the page's (cases.json), the render is the same after any other (the cases again in reverse order, and with a hostile render in between:
// the firmware is global state and is put back to power-on), the live path (chunking does not change a bit, a trigger acts at the first block boundary at or after its offset, a second voice
// is independent), and a refusal for what the page cannot render.
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

#include "rt/json.h"
#include "thunder/lxr_module.h"
#include "golden/lxr_cases.inc"
#include "golden/lxr_gate.inc"
#include "golden/lxr_leftout.inc"

extern "C" int lxr_host_par(int which);
using namespace thunder;

static bool readFile(const std::string& path, std::string* out) { std::ifstream f(path, std::ios::binary); if (!f) return false; std::stringstream ss; ss << f.rdbuf(); *out = ss.str(); return true; }
static uint64_t fnv(const float* f, size_t n) { uint64_t h = 0xcbf29ce484222325ull; const unsigned char* u = (const unsigned char*)f; for (size_t i = 0; i < n * 4; i++) { h ^= u[i]; h *= 0x100000001b3ull; } return h; }
static int fails = 0;
static const double kGateDb = -100.0, kLevelDb = 1.0;
#if defined(__linux__) && defined(__x86_64__) && !defined(__ANDROID__) && !defined(__clang__)
static const bool kExactHoldsHere = true;
#else
static const bool kExactHoldsHere = false;
#endif
static const LxrLeftOutEntry* leftOut(const char* nm) { for (int i = 0; i < kLxrLeftOutN; i++) if (std::strcmp(kLxrLeftOut[i].name, nm) == 0) return &kLxrLeftOut[i]; return nullptr; }
static bool inExact(const char* nm) { for (int i = 0; i < kLxrGateExactN; i++) if (std::strcmp(kLxrGateExact[i], nm) == 0) return true; return false; }

struct Cmp { bool exact = true, lenOk = true, finite = true; double db = -INFINITY, rmsDb = 0, peak = 0, err = 0, lenPct = 0; long n = 0, nref = 0; };
static Cmp compare(const float* got, size_t n, const float* ref, size_t nref) {
  Cmp C; C.n = (long)n; C.nref = (long)nref; C.lenOk = n == nref; C.exact = C.lenOk;
  const size_t m = n < nref ? n : nref; double sa = 0, sb = 0;
  for (size_t i = 0; i < n; i++) if (!std::isfinite(got[i])) C.finite = false;
  for (size_t i = 0; i < nref; i++) C.peak = std::fmax(C.peak, std::fabs((double)ref[i]));
  for (size_t i = 0; i < m; i++) { const double a = got[i], r = ref[i]; sa += a * a; sb += r * r; if ((float)a == (float)r) continue; C.exact = false; const double d = std::fabs(a - r); if (!(d <= C.err)) C.err = d; }
  C.db = C.exact ? -INFINITY : (C.peak > 0 ? 20 * std::log10(C.err / C.peak) : (C.err > 0 ? INFINITY : -INFINITY));
  C.rmsDb = (sa > 0 && sb > 0) ? 10 * std::log10(sa / sb) : (sa == sb ? 0 : 99);
  C.lenPct = nref ? 100.0 * ((double)n - (double)nref) / (double)nref : 0;
  return C;
}

static ThunderLxrRequest requestOf(Json c) {
  ThunderLxrRequest R; std::memset(&R, 0, sizeof R);
  const std::string_view k = c.str("k"); R.kind = k == "lxdrum" ? 0 : k == "lxsnare" ? 1 : k == "lxcym" ? 2 : 3;
  Json P = c.obj("P");
  for (size_t i = 0; i < P.size(); i++) { const std::string_view key = P.keyAt(i); Json v = P.valAt(i);
    if (key.size() > 1 && key[0] == 'p' && v.isNum()) { const int n = std::atoi(std::string(key.substr(1)).c_str()); if (n >= 0 && n < THUNDER_LXR_MAX_PARAMS) R.p[n] = v.asNum(); } }
  R.vdst = P.num("vdst"); R.ldst = P.num("ldst"); R.vel = P.num("vel", 127); R.open = P.num("open"); R.tune = P.num("tune"); R.drive = P.num("drive"); R.level = P.num("level");
  R.vlen = c.num("vlen"); R.hold = c.num("hold"); R.bpm = c.num("bpm"); R.sr = c.num("sr");
  return R;
}

static std::map<std::string, Json> gCases;
struct Run { bool ran = false, kitOk = true; Cmp fin, raw; uint64_t hash = 0; std::string why; };
static Run runCase(const std::string& dir, const char* name, std::vector<float>* gotOut = nullptr) {
  Run R; auto it = gCases.find(name); if (it == gCases.end()) { R.why = "not in cases.json"; return R; }
  Json c = it->second; std::string ref, rraw; if (!readFile(dir + "/" + name + ".f32", &ref) || !readFile(dir + "/" + name + ".raw.f32", &rraw)) { R.why = "reference file missing"; return R; }
  const ThunderLxrRequest req = requestOf(c);
  { std::vector<uint8_t> kit((size_t)lxr::NumParams()); if (lxr::PadKitBytes(req, kit.data(), kit.size()) != kit.size()) { R.why = "no kit bytes"; return R; }
    Json want = c.arr("kit"); if ((int)want.size() != lxr::NumParams()) { R.why = "reference has no kit bytes"; return R; }
    for (size_t i = 0; i < kit.size(); i++) if ((int)want.at(i).asNum() != (int)kit[i]) { R.kitOk = false; std::printf("  %s: parameter byte %zu is %d here, %d in the page's call  FAIL\n", name, i, kit[i], (int)want.at(i).asNum()); break; } }
  const size_t need = lxr::RenderBound(req);
  std::vector<float> out(need ? need : 1, -777.f), raw(1 << 20);
  size_t n = 0, nraw = 0;
  const int st = lxr::RenderPad(req, out.data(), out.size(), &n, raw.data(), raw.size(), &nraw);
  if (st != THUNDER_LXR_OK) { R.why = "native render refused"; return R; }
  R.ran = true; R.hash = fnv(out.data(), n);
  R.fin = compare(out.data(), n, (const float*)ref.data(), ref.size() / 4);
  R.raw = compare(raw.data(), nraw, (const float*)rraw.data(), rraw.size() / 4);
  if (gotOut) gotOut->assign(out.begin(), out.begin() + (long)n);
  if (const char* dd = std::getenv("LXR_DUMP_DIR")) {   // diagnosis: the native renders beside the reference's (compare with a script)
    std::ofstream a(std::string(dd) + "/" + name + ".f32", std::ios::binary); a.write((const char*)out.data(), (std::streamsize)(n * 4));
    std::ofstream b(std::string(dd) + "/" + name + ".raw.f32", std::ios::binary); b.write((const char*)raw.data(), (std::streamsize)(nraw * 4)); }
  return R;
}

static void tablesTest(Json root) {
  int bad = 0, nk = 0;
  if ((int)root.num("nparams") != lxr::NumParams()) { bad++; std::printf("  parameter count: the page %d, the firmware %d  FAIL\n", (int)root.num("nparams"), lxr::NumParams()); }
  if (root.num("rate") != lxr::NativeRate()) { bad++; std::printf("  rate: the page %g, the firmware %g  FAIL\n", root.num("rate"), lxr::NativeRate()); }
  Json par = root.obj("par");
  if ((int)par.num("velDest1") != lxr_host_par(0) || (int)par.num("targetLfo1") != lxr_host_par(1) || (int)par.num("voiceLfo1") != lxr_host_par(2)) { bad++; std::printf("  the page's PAR_VEL_DEST_1 / PAR_TARGET_LFO1 / PAR_VOICE_LFO1 differ from the firmware's  FAIL\n"); }
  if (lxr::BlockFrames() != 64) { bad++; std::printf("  block %d, expected 64  FAIL\n", lxr::BlockFrames()); }
  const char* names[4] = {"lxdrum", "lxsnare", "lxcym", "lxhat"};
  Json defs = root.obj("defaults");
  for (int k = 0; k < 4; k++) {
    Json e = defs.obj(names[k]), d = e.obj("d"); ThunderLxrDescribe D; nk++;
    if (!lxr::Describe(k, &D)) { bad++; continue; }
    int nknob = 0;
    for (size_t i = 0; i < d.size(); i++) { const std::string_view key = d.keyAt(i); const double v = d.valAt(i).asNum();
      if (key.size() > 1 && key[0] == 'p') { const int n = std::atoi(std::string(key.substr(1)).c_str()); nknob++; if (n < 0 || n >= THUNDER_LXR_MAX_PARAMS || !D.present[n] || D.def[n] != v) { bad++; std::printf("  %s knob %s: default %g in the page, %s here  FAIL\n", names[k], std::string(key).c_str(), v, n >= 0 && n < THUNDER_LXR_MAX_PARAMS && D.present[n] ? std::to_string(D.def[n]).c_str() : "absent"); } } }
    if (nknob != lxr::KnobCount(k)) { bad++; std::printf("  %s: %d knobs in the page, %d here  FAIL\n", names[k], nknob, lxr::KnobCount(k)); }
    if (D.vdst != d.num("vdst") || D.ldst != d.num("ldst") || D.vel != d.num("vel") || D.len != e.num("len") || (D.hasOpen != 0) != d.get("open").isNum() || (D.hasOpen && D.open != d.num("open"))) { bad++; std::printf("  %s: vdst / ldst / vel / open / len defaults differ  FAIL\n", names[k]); }
  }
  if (bad) fails++;
  std::printf("tables: parameter count, rate, block, the pad's ids and the defaults of %d engines equal the page's  %s\n", nk, bad ? "FAIL" : "ok");
}

// ---- the live path (provisional) ------------------------------------------------------------------------------------------------------------------------------------------------------
static bool sameBits(const std::vector<float>& a, const std::vector<float>& b) { return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0); }
static void liveTest(const std::string& dir) {
  int bad = 0;
  std::string js; readFile(dir + "/cases.json", &js); JsonDoc doc; doc.parse(js);
  auto it = gCases.find("pre_lxdrum_1"); if (it == gCases.end()) { std::printf("live: no case to take a kit from  FAIL\n"); fails++; return; }
  std::vector<uint8_t> kit((size_t)lxr::NumParams()); const ThunderLxrRequest req = requestOf(it->second); lxr::PadKitBytes(req, kit.data(), kit.size());
  auto stream = [&](int voice, std::vector<size_t> chunks, std::vector<std::pair<size_t, ThunderLxrEvent>> evs) {   // chunks: frames per call; evs: (call index, event)
    std::vector<float> all; if (lxr::LiveOpen(kit.data(), kit.size(), 120) != THUNDER_LXR_OK) { bad++; return all; }
    for (size_t ci = 0; ci < chunks.size(); ci++) { std::vector<ThunderLxrEvent> e; for (auto& p : evs) if (p.first == ci) e.push_back(p.second);
      std::vector<float> buf(chunks[ci]); if (lxr::LiveProcess(voice, e.data(), e.size(), buf.data(), buf.size()) != THUNDER_LXR_OK) { bad++; return all; } all.insert(all.end(), buf.begin(), buf.end()); }
    return all; };
  const ThunderLxrEvent hit0 = {0, 0, 127, 60}, hit70 = {70, 0, 127, 60}, hit64 = {0, 0, 127, 60};
  auto A = stream(0, {1024}, {{0, hit0}});
  auto B = stream(0, {1, 3, 60, 64, 100, 7, 789}, {{0, hit0}}); if (!sameBits(A, B)) { bad++; std::printf("  live: rendering the same stream in other chunk sizes changed bits  FAIL\n"); }
  auto C = stream(0, {1024}, {{0, hit70}}), D = stream(0, {128, 896}, {{1, hit64}});   // an event at 70 acts at block 128; the same event at offset 0 of the call that starts at frame 128
  if (!sameBits(C, D)) { bad++; std::printf("  live: a trigger at offset 70 must act at the first block boundary at or after it (frame 128)  FAIL\n"); }
  double pk = 0; for (size_t i = 0; i < 128 && i < C.size(); i++) pk = std::fmax(pk, std::fabs(C[i])); if (pk != 0) { bad++; std::printf("  live: sound before the trigger  FAIL\n"); }
  pk = 0; for (size_t i = 128; i < 1024; i++) pk = std::fmax(pk, std::fabs(C[i])); if (!(pk > 0.01)) { bad++; std::printf("  live: no sound after the trigger  FAIL\n"); }
  auto E = stream(0, {1024}, {}); pk = 0; for (float x : E) pk = std::fmax(pk, std::fabs(x)); if (pk != 0) { bad++; std::printf("  live: sound without a trigger  FAIL\n"); }
  auto F = stream(0, {1024}, {{0, {0, 0, 127, 60}}, {0, {10, 0, 90, 64}}}), G = stream(0, {1024}, {{0, {0, 0, 127, 60}}, {0, {10, 0, 90, 64}}});
  if (!sameBits(F, G)) { bad++; std::printf("  live: not repeatable  FAIL\n"); }
  { ThunderLxrEvent other = {0, 3, 127, 60}; auto H = stream(0, {512}, {{0, other}}); pk = 0; for (float x : H) pk = std::fmax(pk, std::fabs(x)); if (pk != 0) { bad++; std::printf("  live: another voice's trigger sounded on this voice  FAIL\n"); } }
  { float one[8]; if (lxr::LiveProcess(9, nullptr, 0, one, 8) != THUNDER_LXR_REFUSED) { bad++; std::printf("  live: voice 9 must be refused  FAIL\n"); }
    if (lxr::LiveProcess(0, nullptr, 0, nullptr, 8) != THUNDER_LXR_REFUSED) { bad++; std::printf("  live: no buffer must be refused  FAIL\n"); }
    float pr[8]; size_t n = 0; ThunderLxrRequest r2 = req; lxr::RenderPad(r2, pr, 0, &n);   // too small: no state change... a successful pad render re-powers the firmware, so the live stream is closed:
    std::vector<float> big(lxr::RenderBound(req)); lxr::RenderPad(req, big.data(), big.size(), &n);
    if (lxr::LiveProcess(0, nullptr, 0, one, 8) != THUNDER_LXR_REFUSED) { bad++; std::printf("  live: a pad render must close the live stream (the firmware was re-powered)  FAIL\n"); } }
  if (bad) fails++;
  std::printf("live: chunking, trigger quantisation, repeatability, voice separation and refusals  %s\n", bad ? "FAIL" : "ok");
}

int main(int argc, char** argv) {
  if (argc < 2) { std::printf("lxr: no reference directory (Node not found): %d cases FAIL\n", kLxrCaseN); return 1; }
  const std::string dir = argv[1]; const char* writeGate = nullptr; const char* sub = nullptr;
  for (int i = 2; i < argc; i++) { if (!std::strcmp(argv[i], "--write-gate") && i + 1 < argc) writeGate = argv[++i]; else sub = argv[i]; }
  const bool all = !sub, enforceExact = kExactHoldsHere;
  std::string js; if (!readFile(dir + "/cases.json", &js)) { std::printf("lxr: cases.json missing in %s  FAIL\n", dir.c_str()); return 1; }
  JsonDoc doc; if (!doc.parse(js)) { std::printf("lxr: cases.json unreadable  FAIL\n"); return 1; } Json root = doc.root();
  { Json cs = root.arr("cases"); for (size_t i = 0; i < cs.size(); i++) gCases[std::string(cs.at(i).str("name"))] = cs.at(i); }
  if ((int)gCases.size() != kLxrCaseN) { std::printf("lxr: the reference has %zu cases, the committed list %d  FAIL\n", gCases.size(), kLxrCaseN); return 1; }
  if (thunder_lxr_module_register() != 0) { std::printf("lxr: the module did not register  FAIL\n"); return 1; }
  std::printf("lxr: reference = web LxrCore.render + %s (sha256 %.16s); gate %.0f dB below each reference's peak%s\n", std::string(root.str("wasm")).c_str(), std::string(root.str("wasmSha256")).c_str(), kGateDb, enforceExact ? ", the exact list enforced" : ", the exact list NOT enforced here (not Linux x86-64 GCC)");
  if (all) {
    tablesTest(root);
    for (int i = 0; i < kLxrLeftOutN; i++) { bool f = false; for (int k = 0; k < kLxrCaseN; k++) if (!std::strcmp(kLxrCases[k], kLxrLeftOut[i].name)) f = true; if (!f) { std::printf("left-out name %s is not a case  FAIL\n", kLxrLeftOut[i].name); fails++; } }
    for (int i = 0; i < kLxrGateExactN; i++) { bool f = false; for (int k = 0; k < kLxrCaseN; k++) if (!std::strcmp(kLxrCases[k], kLxrGateExact[i])) f = true; if (!f) { std::printf("exact-list name %s is not a case  FAIL\n", kLxrGateExact[i]); fails++; } }
  }
  int nRun = 0, nExact = 0, nWithin = 0, nLeft = 0, nOut = 0, nKit = 0; double worst = -INFINITY, worstRaw = -INFINITY; std::string worstName, worstRawName;
  std::vector<std::string> exactNow; std::vector<uint64_t> firstHash(kLxrCaseN, 0);
  for (int ci = 0; ci < kLxrCaseN; ci++) {
    const char* nm = kLxrCases[ci]; if (sub && !std::strstr(nm, sub)) continue;
    Run R = runCase(dir, nm); nRun++;
    if (!R.ran) { fails++; nOut++; std::printf("%-34s %s  FAIL\n", nm, R.why.c_str()); continue; }
    firstHash[ci] = R.hash; if (R.kitOk) nKit++; else fails++;
    const LxrLeftOutEntry* lo = leftOut(nm);
    if (lo) {
      const bool ok = R.fin.finite && std::fabs(R.fin.rmsDb) <= kLevelDb && std::fabs(R.fin.lenPct) <= lo->maxLenPct;
      nLeft++; if (!ok) fails++;
      std::printf("%-34s LEFT OUT (%s): %.1f dB, rms %+.2f dB, length %+.2f %%  %s\n", nm, lo->why, R.fin.db, R.fin.rmsDb, R.fin.lenPct, ok ? "structural check ok" : "structural check FAIL");
      continue;
    }
    const bool lenOk = R.fin.lenOk && R.raw.lenOk, within = lenOk && R.fin.finite && R.fin.db <= kGateDb && R.raw.db <= kGateDb;
    if (R.fin.exact && R.raw.exact) { nExact++; exactNow.push_back(nm); } else if (within) nWithin++;
    if (R.fin.db > worst && !R.fin.exact) { worst = R.fin.db; worstName = nm; }
    if (R.raw.db > worstRaw && !R.raw.exact) { worstRaw = R.raw.db; worstRawName = nm; }
    bool ok = within;
    if (enforceExact && inExact(nm) && !(R.fin.exact && R.raw.exact)) { ok = false; std::printf("%-34s was bit-exact and is not any more  FAIL\n", nm); }
    if (!ok) { fails++; std::printf("%-34s final %.1f dB (len %ld vs %ld), raw %.1f dB (len %ld vs %ld)  FAIL\n", nm, R.fin.db, R.fin.n, R.fin.nref, R.raw.db, R.raw.n, R.raw.nref); }
  }
  std::printf("cases: %d run (%d bit-exact, %d within %.0f dB, %d left out by name, %d not run); parameter bytes equal the page's in %d; worst final %.1f dB (%s), worst raw %.1f dB (%s)\n",
              nRun, nExact, nWithin, kGateDb, nLeft, nOut, nKit, worst, worstName.c_str(), worstRaw, worstRawName.c_str());
  if (writeGate) {
    FILE* f = std::fopen(writeGate, "w"); if (!f) { std::printf("cannot write %s\n", writeGate); return 1; }
    std::fprintf(f, "// The cases whose finished render is bit-identical to the reference's wasm on x86-64 Linux with GCC: they must stay so (enforced on that platform). Written by\n// `test_lxr <ref dir> --write-gate FILE`; never hand-edited.\nstatic const char* const kLxrGateExact[] = {\n");
    for (auto& s : exactNow) std::fprintf(f, "  \"%s\",\n", s.c_str()); if (exactNow.empty()) std::fprintf(f, "  nullptr,\n");
    std::fprintf(f, "};\nstatic const int kLxrGateExactN = %d;\n", (int)exactNow.size()); std::fclose(f); std::printf("wrote %s (%zu exact cases)\n", writeGate, exactNow.size());
  }
  if (all) {
    // ORDER AND HISTORY: the firmware is global state. Every case again in reverse order, with a hostile render (all knobs at 255, another engine) in between each, must give the same bits.
    int diff = 0, compared = 0;
    { const char* hostile = "max_lxcym"; for (int ci = kLxrCaseN - 1; ci >= 0; ci--) { runCase(dir, hostile); const char* nm = kLxrCases[ci]; Run R = runCase(dir, nm); if (!R.ran) continue; compared++; if (R.hash != firstHash[ci]) { diff++; if (diff < 8) std::printf("  %s: not the same bits after another render  FAIL\n", nm); } } }
    if (diff) fails++;
    std::printf("order: %d cases again in reverse order, each after a hostile render: %d differ  %s\n", compared, diff, diff ? "FAIL" : "ok");
    liveTest(dir);
  }
  if (fails) std::printf("lxr: %d FAIL\n", fails); else std::printf("lxr: PASS\n");
  return fails ? 1 : 0;
}
