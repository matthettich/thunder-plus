// U25: the Plaits ("plaits", Macro osc) and Elements ("elements", Resonator) layer types (core/src/engine/layer_pe.cpp on the strict native build of the Mutable Instruments libraries,
// core/dsp/pe_host.cc) against the real ENGINE block of index.html running the web's fast-math thunder-dsp.wasm (tools/engine-u25-dump.mjs: the samples of every case, a fresh wasm instance per voice).
// THE GATE (the lead's rule for units whose reference is a fast-math wasm, TODAY.md "Evening, 16 Nov" 1 and 7):
//  (a) the native library is strict (-fno-fast-math -ffp-contract=off): the same on every platform;
//  (b) every case is within -100 dB of the reference's peak (max |difference| / peak; samples missing at the end count as zeros), the worst printed; a case that is bit-exact with the wasm is
//      listed (golden/engine_u25_native.inc, kPeExact) and must stay exact;
//  (c) the SHA-256 of the native render of every case (made on Linux x86-64) is committed in the same file and checked on every platform: a platform whose libm differs shows up here, as a finding;
//  (d) THE ONE-STEP RULE, a named rule for the class "Plaits int16 output" (the lead's decision, 17 Nov). Plaits' output is int16 (Voice::Frame::out / aux, scaled by 1/32768 into the pre-render): the strict
//      build and the fast-math build round a float differently at the int16 conversion, so a sample can land one step (1/32768) away from the wasm's, and one flipped sample is -80 dB of a 0.4 peak: the
//      -100 dB line cannot hold for it, while the flip is the smallest difference a quantiser can show. A case whose Plaits layers' RAW int16 stage (the pre-render, before any resampling, filter, envelope or
//      mix) is within ONE step of the wasm's pre-render in every sample, whose Elements layers are within the line, and without NaN, PASSES by this rule; the number of raw samples that differ is printed.
//      The comparison is made at the raw stage because the plumbing after it is proven bit-exact (e): with the wasm's own pre-renders injected, 313 of 313 cases equal the wasm's samples bit for bit, so the
//      raw stage is the only place where the two builds can differ. A case with any raw difference above ONE step stays out by name (class D, with its numbers).
//  (d2) every other case that is NOT within the line and not covered by the rule is left out BY NAME in kPeOut (same file, written by --write-native, never hand-edited) with its numbers and the reason class:
//        F  an Elements pre-render (float output) is itself beyond the line against the wasm (worst dB, first differing pre-render sample, and the early figure are printed)
//        D  a Plaits pre-render differs by more than one output step (2 to some hundreds: the feedback of the model carries a flip on; the largest ones are models whose trajectory parts ways)
//        N  one side is NaN where the other is not (which side is printed)
//      The test FAILS for a case that is beyond the line, outside the rule and not listed, for a listed case that is now within the line or the rule (stale), whose class changed, or that got worse than recorded by more than 1 dB.
//  (e) THE PLUMBING GATE, hard and exact: every case rendered with the wasm's OWN pre-renders injected (pe_test_hook) must equal the reference BIT FOR BIT: the resampling, the layer filters, envelopes, pitch
//      envelope, delay / burst, the voice stages are the JS's to the last bit; what is left of the difference between the native render and the wasm is the library's alone.
//  Also: the refusals, and rt_engine_u25 in rt_harness (the allocation guard).
//   test_engine_u25 <refDir> [name-substring] [--write-native FILE] [--dump DIR] [--list]
// Without the library (Windows / MSVC builds no DSP libraries) the two layer types are not supported: the test says so and checks only that.
#include <algorithm>
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
#include "engine/layer_pe.h"
#include "rt/json.h"
#include "sha256.h"
#include "golden/engine_u25_native.inc"

using namespace thunder;

static bool readFile(const std::string& path, std::string* out) { std::ifstream f(path, std::ios::binary); if (!f) return false; std::stringstream ss; ss << f.rdbuf(); *out = ss.str(); return true; }
static int fails = 0;
static const double kGateDb = -100.0, kLsb = 1.0 / 32768.0;

static bool inExact(const char* nm) { for (int i = 0; i < kPeExactN; i++) if (!std::strcmp(kPeExact[i], nm)) return true; return false; }
static const char* nativeSha(const char* nm) { for (int i = 0; i < kPeNativeN; i++) if (!std::strcmp(kPeNative[i].name, nm)) return kPeNative[i].sha256; return nullptr; }
static const PeOut* outOf(const char* nm) { for (int i = 0; i < kPeOutN; i++) if (!std::strcmp(kPeOut[i].name, nm)) return &kPeOut[i]; return nullptr; }
static const char* clsText(char c) {
  switch (c) {
    case 'F': return "an Elements pre-render is itself beyond the line (float arithmetic, fast-math vs strict)";
    case 'D': return "a Plaits pre-render differs by more than one output step: more than one quantiser flip (2 steps up to hundreds: the model's trajectory parts ways)";
    case 'N': return "NaN on one side only (a numerical blow-up the two builds do not share)";
  }
  return "?";
}

struct Cmp { bool exact = true; double db = -INFINITY, err = 0, peak = 0; long nDiff = 0, first = -1; double got = 0, want = 0; size_t nGot = 0, nRef = 0; bool nan = false, nanGot = false, nanWant = false; long firstNan = -1; };
static Cmp compareF(const float* a, size_t na, const float* r, size_t nr) {
  Cmp c; c.nGot = na; c.nRef = nr; const size_t n = std::max(na, nr);
  for (size_t i = 0; i < n; i++) {
    const float x = i < na ? a[i] : 0.0f, y = i < nr ? r[i] : 0.0f;
    if (!std::isnan(y)) c.peak = std::max(c.peak, (double)std::fabs(y));
    if (std::memcmp(&x, &y, 4) == 0 || (std::isnan(x) && std::isnan(y))) continue;
    c.exact = false; c.nDiff++; if (c.first < 0) { c.first = (long)i; c.got = x; c.want = y; }
    if (std::isnan(x) != std::isnan(y)) { c.nan = true; if (std::isnan(x)) c.nanGot = true; else c.nanWant = true; if (c.firstNan < 0) c.firstNan = (long)i; c.err = INFINITY; } else c.err = std::max(c.err, std::fabs((double)x - (double)y));
  }
  c.db = c.exact ? -INFINITY : (c.nan ? INFINITY : 20 * std::log10(c.err / std::max(c.peak, 1e-30)));
  return c;
}
struct Row { std::string name, sha; bool exact; char cls; double db; long nDiff, first; double rawLsb; bool out; };
// the worst error of a pre-render over the samples [lo, hi) against the peak of the whole reference
static double errDb(const float* a, const float* r, size_t n, size_t lo, size_t hi, double peak) { double e = 0; for (size_t i = lo; i < hi && i < n; i++) { if (std::isnan(a[i]) || std::isnan(r[i])) return INFINITY; e = std::max(e, std::fabs((double)a[i] - (double)r[i])); } return e == 0 ? -INFINITY : 20 * std::log10(e / std::max(peak, 1e-30)); }
static std::vector<Row> gRows;
static bool gWrite = false;

int main(int argc, char** argv) {
  if (!pe_available()) {
    std::printf("engine U25: NOT BUILT on this platform (no Plaits / Elements library: the DSP libraries are not built here): the two layer types are not supported; checking only that\n");
    int bad = 0; if (eng_layer_type("plaits", 6) != LType::Other || eng_layer_type("elements", 8) != LType::Other) { std::printf("FAIL: the types must be unknown without the library\n"); bad++; }
    EngVoice V; V.layers[0].on = true; V.layers[0].level = 1; V.nLayers = 1; V.layers[0].type = LType::Plaits; EngRenderOpts o; size_t n = 0; float tmp[64];
    if (eng_render_voice(V, o, tmp, 64, &n) != kEngUnsupported) { std::printf("FAIL: a Plaits layer must be refused without the library\n"); bad++; }
    if (!bad) std::printf("PASS test_engine_u25 (library not built)\n");
    return bad ? 1 : 0;
  }
  if (argc < 2) { std::printf("FAIL: usage: test_engine_u25 <refDir> [name-substring] [--write-native FILE] [--dump DIR] [--list]\n"); return 1; }
  const std::string refDir = argv[1]; std::string filt, dumpDir; const char* writeNative = nullptr; bool list = false;
  for (int i = 2; i < argc; i++) { if (!std::strcmp(argv[i], "--write-native") && i + 1 < argc) { writeNative = argv[++i]; gWrite = true; } else if (!std::strcmp(argv[i], "--dump") && i + 1 < argc) dumpDir = argv[++i];
    else if (!std::strcmp(argv[i], "--list")) list = true; else filt = argv[i]; }
  std::string cj, rj; if (!readFile(refDir + "/cases.json", &cj) || !readFile(refDir + "/ref.json", &rj)) { std::printf("FAIL: the reference (cases.json, ref.json) is missing: a fixture that cannot run is a failure\n"); return 1; }
  JsonDoc cdoc, rdoc; if (!cdoc.parse(cj) || !rdoc.parse(rj)) { std::printf("FAIL: a reference file does not parse\n"); return 1; }
  eng_init();
  Json C = cdoc.root().arr("cases"), R = rdoc.root().arr("cases");
  std::map<std::string, size_t> rowOf; for (size_t i = 0; i < R.size(); i++) rowOf[std::string(R.at(i).str("name"))] = i;
  if (C.size() == 0 || R.size() == 0 || R.size() > C.size()) { std::printf("FAIL: %zu cases, %zu reference rows\n", C.size(), R.size()); return 1; }
  std::printf("reference wasm: %s\n", std::string(rdoc.root().str("wasm")).c_str());
  int nRefused = 0, ran = 0, nVoices = 0, nIn = 0, nInVoices = 0, nExact = 0, nOut = 0, nPlumb = 0, nRule = 0, nF = 0; long ruleRaw = 0, ruleRawMax = 0; double worstF = -INFINITY; std::string worstFName; std::map<char, int> byCls; double worstIn = -INFINITY; std::string worstInName;
  for (size_t ci = 0; ci < C.size(); ci++) {
    Json c = C.at(ci); const std::string name(c.str("name")); if (!filt.empty() && name.find(filt) == std::string::npos) continue;
    const bool voice = c.str("kind") == "voice", refused = c.str("kind") == "refused"; ran++; if (voice) nVoices++;
    EngVoice V; if (!eng_voice_from_json(V, c.obj("v"), c.arr("L"), c.arr("fx"))) { std::printf("%s: loader refused  FAIL\n", name.c_str()); fails++; continue; }
    EngRenderOpts o; o.sr = c.num("sr"); o.hold = c.num("hold"); o.bpm = c.num("bpm", 120);
    if (refused) { static float tmp[1 << 16]; size_t nn = 0; nRefused++; if (eng_render_voice(V, o, tmp, sizeof tmp / 4, &nn) != kEngUnsupported) { std::printf("%-34s a knob outside the sliders' range must be refused  FAIL\n", name.c_str()); fails++; } continue; }
    if (rowOf.find(name) == rowOf.end()) { std::printf("%-34s no reference row  FAIL\n", name.c_str()); fails++; continue; }
    const size_t bound = eng_render_bound(V, o);
    std::string fb; if (!readFile(refDir + "/" + name + ".f32", &fb) || fb.size() % 4) { std::printf("%-34s reference samples missing  FAIL\n", name.c_str()); fails++; continue; }
    std::vector<float> ref(fb.size() / 4); std::memcpy(ref.data(), fb.data(), fb.size());
    Json rrow = R.at(rowOf[name]); if (std::string(rrow.str("name")) != name || (size_t)rrow.num("len") != ref.size()) { std::printf("%s: ref.json out of step with the samples  FAIL\n", name.c_str()); fails++; continue; }
    // the wasm's own pre-renders of this voice
    std::vector<std::vector<float>> wpre; Json pj = rrow.arr("pre"); bool preOk = true;
    for (size_t j = 0; j < pj.size(); j++) { std::string pb; if (!readFile(refDir + "/" + name + ".pre" + std::to_string(j) + ".f32", &pb) || pb.size() / 4 != (size_t)pj.at(j).asNum()) { preOk = false; break; }
      wpre.emplace_back(pb.size() / 4); std::memcpy(wpre.back().data(), pb.data(), pb.size()); }
    if (!preOk) { std::printf("%-34s the wasm's pre-renders are missing  FAIL\n", name.c_str()); fails++; continue; }
    // (e) the plumbing run: the wasm's pre-renders injected
    { std::vector<float> buf(bound + 8, 12345.0f); size_t n = 0; PeTest T; T.inject = &wpre; pe_test_hook(&T);
      const int st = eng_render_voice(V, o, buf.data(), bound, &n); pe_test_hook(nullptr);
      const Cmp k = compareF(buf.data(), n, ref.data(), ref.size());
      if (st != kEngOk || !k.exact || n != ref.size()) { std::printf("%-34s PLUMBING: the wasm's own pre-renders do not give the wasm's samples (status %d, %.1f dB, %ld samples differ, lengths %zu / %zu)  FAIL\n", name.c_str(), st, k.db, k.nDiff, n, ref.size()); fails++; }
      else nPlumb++; }
    // the native render, with the library
    std::vector<float> buf(bound + 8, 12345.0f); size_t n = 0; std::vector<std::vector<float>> npre; PeTest T; T.capture = &npre; pe_test_hook(&T);
    const int st = eng_render_voice(V, o, buf.data(), bound, &n); pe_test_hook(nullptr);
    if (st != kEngOk) { std::printf("%-34s status %d  FAIL\n", name.c_str(), st); fails++; continue; }
    if (buf[bound] != 12345.0f || buf[bound + 7] != 12345.0f) { std::printf("%s: wrote past the bound  FAIL\n", name.c_str()); fails++; }
    if (!dumpDir.empty()) { std::ofstream d(dumpDir + "/" + name + ".f32", std::ios::binary); d.write((const char*)buf.data(), (std::streamsize)(n * 4)); }
    const Cmp k = compareF(buf.data(), n, ref.data(), ref.size());
    Sha256 h; h.add((const char*)buf.data(), n * 4); const std::string sha = h.hex();
    // the library's own difference: per pre-render, Plaits in output steps (the largest), Elements against the line
    double rawLsb = 0; bool rawNan = false, elementsBeyond = false, plaitsBeyond = false; long rawN = 0, elFirst = -1; double elDb = -INFINITY, elEarly = -INFINITY; std::string nanInfo;
    if (npre.size() != wpre.size()) { std::printf("%-34s %zu native pre-renders, %zu of the wasm  FAIL\n", name.c_str(), npre.size(), wpre.size()); fails++; }
    else { size_t li = 0;
      for (int i = 0; i < V.nLayers && li < npre.size(); i++) { const EngLayer& L = V.layers[i]; if (!(L.on && L.level > 0) || !pe_is(L.type)) continue;
        const Cmp r = compareF(npre[li].data(), npre[li].size(), wpre[li].data(), wpre[li].size()); li++;
        if (r.nan) { rawNan = true; char b[160]; std::snprintf(b, sizeof b, " [layer %d %s: NaN in the %s pre-render from sample %ld]", i, L.type == LType::Plaits ? "plaits" : "elements", r.nanGot && !r.nanWant ? "NATIVE" : (r.nanWant && !r.nanGot ? "WASM" : "both (at different places)"), r.firstNan); nanInfo += b; }
        if (L.type == LType::Plaits) { const double steps = r.err / kLsb; if (steps > rawLsb) rawLsb = steps; if (steps > 1.0001) plaitsBeyond = true; rawN += r.nDiff; }
        else if (!(r.exact || r.db <= kGateDb)) { elementsBeyond = true; if (r.db > elDb) { elDb = r.db; elFirst = r.first; elEarly = errDb(npre[li - 1].data(), wpre[li - 1].data(), std::min(npre[li - 1].size(), wpre[li - 1].size()), 0, 3200, r.peak); } } } }
    const char cls = (k.nan || rawNan) ? 'N' : plaitsBeyond ? 'D' : elementsBeyond ? 'F' : 'Q';
    const bool within = k.exact && n == ref.size() ? true : k.db <= kGateDb;
    const bool exact = k.exact && n == ref.size();
    const bool byRule = !within && cls == 'Q';   // the one-step rule (d): every raw Plaits sample within one step, Elements within the line, no NaN
    gRows.push_back({name, sha, exact, cls, k.db, k.nDiff, k.first, rawLsb, !within && !byRule});
    bool ok = true; std::string why;
    const PeOut* po = outOf(name.c_str());
    if (!gWrite) {
      if (inExact(name.c_str()) && !exact) { ok = false; why = "was bit-exact: the exact list is enforced"; }
      const char* w = nativeSha(name.c_str()); if (!w || sha != w) { ok = false; why = w ? "NATIVE HASH DIFFERS from the committed one (golden/engine_u25_native.inc)" : "no committed native hash"; }
      if (!within && !byRule && !po) { ok = false; why = "beyond the line, outside the one-step rule and not left out by name (kPeOut)"; }
      if ((within || byRule) && po) { ok = false; why = "listed in kPeOut but within the line or the one-step rule now: the list is stale (regenerate with --write-native)"; }
      if (po && !within && !byRule && po->cls != cls) { ok = false; why = std::string("its reason class was ") + po->cls + ", now " + cls; }
      if (po && !within && !byRule && !((std::isinf(k.db) ? 999.0 : k.db) <= po->db + 1.0)) { ok = false; why = "worse than the recorded figure by more than 1 dB"; }
    }
    if (within) { nIn++; if (voice) nInVoices++; if (exact) nExact++; else if (k.db > worstIn) { worstIn = k.db; worstInName = name; } }
    else if (byRule) { nRule++; ruleRaw += rawN; if (rawN > ruleRawMax) ruleRawMax = rawN; if (rawLsb > 1.0001) { fails++; std::printf("%s: rule-gated with a raw step of %.2f  FAIL\n", name.c_str(), rawLsb); } }
    else { nOut++; byCls[cls]++; if (cls == 'F') { nF++; if (elDb > worstF) { worstF = elDb; worstFName = name; } } }
    if (!ok) { fails++; std::printf("%-34s %s  FAIL\n    %.1f dB, %ld of %zu samples differ, first at %ld: got %.9g want %.9g; largest difference %.3g against a peak of %.3g; class %c\n", name.c_str(), why.c_str(), k.db, k.nDiff, std::max(k.nGot, k.nRef), k.first, k.got, k.want, k.err, k.peak, cls); }
    else if (byRule) { if (list) std::printf("%-34s GATED BY THE ONE-STEP RULE: largest raw Plaits step %.2f, %ld raw samples differ; final output %.1f dB, %ld of %zu samples differ\n", name.c_str(), rawLsb, rawN, k.db, k.nDiff, std::max(k.nGot, k.nRef)); }
    else if (!within) std::printf("%-34s LEFT OUT by name, class %c (%s): %.1f dB, %ld of %zu samples differ (first at %ld: native %.9g, wasm %.9g); largest raw Plaits step %.2f, %ld raw Plaits samples differ%s; lengths %zu / %zu\n", name.c_str(), cls, clsText(cls), k.db, k.nDiff,
      std::max(k.nGot, k.nRef), k.first, k.got, k.want, rawLsb, rawN, nanInfo.c_str(), k.nGot, k.nRef);
    if (!within && !byRule && cls == 'F') std::printf("%-34s   Elements pre-render: worst %.1f dB of its peak, first differing pre-render sample %ld (block %ld of 32), over the first 3200 samples (0.1 s) %.1f dB\n", name.c_str(), elDb, elFirst, elFirst / 32, elEarly);
  }
  // the refusals
  if (filt.empty()) {
    EngRenderOpts o; size_t n = 0; static float tmp[1 << 14];
    EngVoice V; V.layers[0].on = true; V.layers[0].type = LType::Elements; V.layers[0].pe.eModel = 3; V.len = 0.05;
    if (eng_render_voice(V, o, tmp, 1 << 14, &n) != kEngUnsupported) { std::printf("FAIL: an Elements model outside 0..2 must be refused\n"); fails++; }
    for (const char* nm : {"plaits", "elements"}) if (!eng_type_supported(eng_layer_type(nm, std::strlen(nm)))) { std::printf("FAIL: %s is not a supported layer type\n", nm); fails++; }
  }
  if (gRows.empty()) { std::printf("FAIL: no case ran\n"); fails++; }
  std::printf("engine U25 plumbing: %d of %d rendered cases bit-exact with the wasm's own pre-renders injected; %d cases with a knob outside the sliders' range refused\n", nPlumb, ran - nRefused, nRefused);
  std::printf("engine U25 (native strict against the fast-math wasm): %d cases (%d factory-style voices): %d inside the %.0f dB line (%d bit-exact; %d factory-style voices inside), worst inside %s (%s); %d more gated by the one-step rule (%ld raw int16 samples differ in all, at most %ld in one case); %d left out by name (F %d, D %d, N %d); %d failed\n",
    ran, nVoices, nIn, kGateDb, nExact, nInVoices, std::isinf(worstIn) ? "none" : (std::to_string(worstIn) + " dB").c_str(), worstInName.c_str(), nRule, ruleRaw, ruleRawMax, nOut, byCls['F'], byCls['D'], byCls['N'], fails);
  if (nF) std::printf("engine U25: the largest Elements pre-render difference of the left-out F cases: %.1f dB of the pre-render's peak (%s)\n", worstF, worstFName.c_str());
  if (writeNative) {
    std::ofstream o(writeNative); int ne = 0; for (auto& r : gRows) if (r.exact) ne++;
    o << "// GENERATED by test_engine_u25 --write-native: the Plaits / Elements layer types, the native STRICT build against the fast-math thunder-dsp.wasm (docs/port/U25-NOTES.md). Never hand-edited.\n";
    o << "// kPeExact: the cases that are bit-exact with the wasm today (a case that leaves the list fails); kPeNative: the SHA-256 of the native render of every case (made on Linux x86-64, checked on every platform);\n";
    o << "// kPeOut: the cases that are beyond -100 dB, BY NAME, with the measured reason class (see test_engine_u25.cpp) and the figures.\n";
    o << "static const char* const kPeExact[] = {\n"; for (auto& r : gRows) if (r.exact) o << "  \"" << r.name << "\",\n"; o << "  \"\"};\nstatic const int kPeExactN = " << ne << ";\n";
    o << "struct PeNative { const char* name; const char* sha256; };\nstatic const PeNative kPeNative[] = {\n"; for (auto& r : gRows) o << "  {\"" << r.name << "\", \"" << r.sha << "\"},\n"; o << "  {\"\", \"\"}};\nstatic const int kPeNativeN = " << gRows.size() << ";\n";
    o << "struct PeOut { const char* name; char cls; double db; long nDiff; long first; double rawLsb; };\nstatic const PeOut kPeOut[] = {\n"; int no = 0;
    for (auto& r : gRows) if (r.out) { char b[200]; std::snprintf(b, sizeof b, "  {\"%s\", '%c', %.1f, %ld, %ld, %.2f},\n", r.name.c_str(), r.cls, std::isinf(r.db) ? 999.0 : r.db, r.nDiff, r.first, r.rawLsb); o << b; no++; }
    o << "  {\"\", ' ', 0, 0, 0, 0}};\nstatic const int kPeOutN = " << no << ";\n";
    std::printf("wrote %s: %d exact, %zu hashes, %d left out\n", writeNative, ne, gRows.size(), no);
  }
  if (!fails) std::printf("PASS test_engine_u25\n");
  return fails ? 1 : 0;
}
