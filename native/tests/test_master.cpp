// U11: the master processor (core/src/master) against master-worklet.js itself (tools/master-dump.mjs runs it in a Node vm: 54 cases, each
// stage alone and together, settings changed mid-stream, silence, a full-scale square, junk samples and junk settings, and the 18
// configurations of tests/golden/master.json). Every case: the output within -80 dB of the JS (the figure is printed; exact where it is),
// the meter messages the worklet posts (same blocks, same numbers), the committed FNV hash.
//   test_master <dir>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "master/master.h"
#include "rt/json.h"
#include "golden/master_golden.inc"

using namespace thunder;

static bool readFile(const std::string& path, std::string* out) { std::ifstream f(path, std::ios::binary); if (!f) return false; std::stringstream ss; ss << f.rdbuf(); *out = ss.str(); return true; }
static uint64_t fnv(const float* f, size_t n) { uint64_t h = 0xcbf29ce484222325ull; const unsigned char* u = (const unsigned char*)f; for (size_t i = 0; i < n * 4; i++) { h ^= u[i]; h *= 0x100000001b3ull; } return h; }
static uint32_t fnv32(const float* f, size_t n) { uint32_t h = 0x811c9dc5u; const unsigned char* u = (const unsigned char*)f; for (size_t i = 0; i < n * 4; i++) { h ^= u[i]; h *= 0x01000193u; } return h; }

// JSON -> the worklet's message (a string "NaN" / "Infinity" / "-Infinity" is that number)
static MOpt opt(Json o, const char* k) {
  Json v = o.get(k);
  if (v.isNum()) return v.asNum();
  if (v.isStr()) { std::string_view s = v.asStr(); if (s == "NaN") return std::nan(""); if (s == "Infinity") return INFINITY; if (s == "-Infinity") return -INFINITY; }
  return std::nullopt;
}
static MBool bl(Json o, const char* k) { Json v = o.get(k); return v.isBool() ? MBool(v.asBool()) : std::nullopt; }
static MasterSet parseSet(Json s) {
  MasterSet m; Json u;
  if ((u = s.get("sup")).isObj()) { m.hasSup = true; SupSet& x = m.sup; x.on = u.boolean("on"); x.depth = opt(u, "depth"); x.sharp = opt(u, "sharp"); x.sel = opt(u, "sel"); x.atk = opt(u, "atk"); x.rel = opt(u, "rel"); x.lo = opt(u, "lo"); x.hi = opt(u, "hi"); x.mix = opt(u, "mix"); x.delta = bl(u, "delta"); }
  if ((u = s.get("comp")).isObj()) { m.hasComp = true; CompSet& x = m.comp; x.on = u.boolean("on"); x.thr = opt(u, "thr"); x.ratio = opt(u, "ratio"); x.atk = opt(u, "atk"); x.rel = opt(u, "rel"); x.knee = opt(u, "knee"); x.makeup = opt(u, "makeup"); x.mix = opt(u, "mix"); x.hpf = opt(u, "hpf"); x.autoRel = bl(u, "auto"); x.autoMakeup = bl(u, "automk"); }
  if ((u = s.get("lim")).isObj()) { m.hasLim = true; LimSet& x = m.lim; x.on = u.boolean("on"); x.drive = opt(u, "drive"); x.ceil = opt(u, "ceil"); x.rel = opt(u, "rel"); x.look = opt(u, "look"); x.tp = bl(u, "tp"); }
  return m;
}
static double jnum(Json v) { if (v.isNum()) return v.asNum(); if (v.isStr()) { std::string_view s = v.asStr(); return s == "NaN" ? std::nan("") : s == "Infinity" ? INFINITY : -INFINITY; } return std::nan(""); }
static bool close(double a, double b) { if (std::isinf(a) || std::isinf(b)) return a == b; return std::fabs(a - b) <= 1e-6 + 1e-6 * std::fabs(b); }

struct Res { bool ran = false, exact = false, hashOk = false, metersOk = true, goldOk = true; double db = -INFINITY; int nm = 0; double worstMeter = 0; };
static int fails = 0;

static Res runCase(const std::string& dir, const MasterGolden& G) {
  Res R; std::string js, in, ref;
  if (!readFile(dir + "/" + G.name + ".json", &js) || !readFile(dir + "/" + G.name + ".in.f32", &in) || !readFile(dir + "/" + G.name + ".f32", &ref)) return R;
  JsonDoc doc; if (!doc.parse(js)) return R; Json M = doc.root();
  const int n = (int)M.num("n"); if ((size_t)n * 8 != in.size() || (size_t)n * 8 != ref.size()) return R;
  const float* I = (const float*)in.data(); const float* J = (const float*)ref.data();
  std::vector<float> oL(I, I + n), oR(I + n, I + 2 * (size_t)n);
  MasterProcessor P(48000, M.boolean("meters", true)); P.set(parseSet(M.get("settings")));
  Json ev = M.arr("events"), posted = M.arr("posted"); size_t k = 0, pk = 0; int nm = 0;
  for (int b = 0; b * 128 < n; b++) {
    while (k < ev.size() && (int)ev.at(k).num("b") == b) { Json e = ev.at(k++); if (e.get("set").isObj()) P.set(parseSet(e.get("set"))); else if (e.boolean("resetPeak")) P.resetPeak(); }
    P.process(oL.data() + (size_t)b * 128, oR.data() + (size_t)b * 128, 128);
    MasterMeter m;
    while (P.popMeter(&m)) {
      if (pk >= posted.size() || (int)posted.at(pk).num("b") != b) { R.metersOk = false; pk++; continue; }
      Json w = posted.at(pk++).get("m"); nm++;
      auto cmp = [&](const char* key, double v) { const double r = jnum(w.get(key)); if (!close(v, r)) { R.metersOk = false; } else if (std::isfinite(r)) R.worstMeter = std::max(R.worstMeter, std::fabs(v - r)); };
      cmp("inPk", m.inPk); cmp("outPk", m.outPk); cmp("tp", m.tp); cmp("inLu", m.inLu); cmp("outLu", m.outLu); cmp("comp", m.comp); cmp("compIn", m.compIn); cmp("lim", m.lim);   // (load: wall clock, not compared)
      Json s = w.get("sup"); if (s.isArr() != m.hasSup) R.metersOk = false; else if (m.hasSup) { for (int i = 0; i < kSupBands; i++) if (!close(m.sup[i], jnum(s.at((size_t)i)))) R.metersOk = false; }
    }
  }
  if (pk != posted.size()) R.metersOk = false;
  R.nm = nm; R.ran = true;
  double peak = 0, err = 0; bool exact = true;
  for (int c = 0; c < 2; c++) for (int i = 0; i < n; i++) { const double a = (c ? oR : oL)[(size_t)i], r = J[(size_t)c * n + i]; peak = std::max(peak, std::fabs(r)); if ((float)a == (float)r) continue; exact = false; err = std::max(err, std::fabs(a - r)); }
  std::vector<float> all(oL); all.insert(all.end(), oR.begin(), oR.end());
  R.exact = exact; R.hashOk = fnv(all.data(), all.size()) == G.hash;
  R.db = exact ? -INFINITY : 20 * std::log10(err / std::max(peak, 1e-30));
  Json jh = M.get("jsHash"); if (jh.isStr()) { char buf[16]; std::snprintf(buf, sizeof buf, "%08x", fnv32(all.data(), all.size())); R.goldOk = jh.asStr() == buf; }
  return R;
}

int main(int argc, char** argv) {
  if (argc < 2) { std::printf("master: no reference directory (Node not found): %d cases not run\n", kMasterGoldenN); return 0; }
  int exact = 0, goldRun = 0, goldExact = 0; double worst = -INFINITY;
  for (int i = 0; i < kMasterGoldenN; i++) {
    const MasterGolden& G = kMasterGolden[i]; const Res R = runCase(argv[1], G);
    if (!R.ran) { std::printf("%-26s could not be run  FAIL\n", G.name); fails++; continue; }
    const bool isGold = !std::strncmp(G.name, "gold_", 5); if (isGold) { goldRun++; if (R.goldOk) goldExact++; }
    const bool ok = R.db <= -80.0 && R.metersOk;
    char db[24]; if (R.exact) std::snprintf(db, sizeof db, "exact"); else std::snprintf(db, sizeof db, "%.1f dB", R.db);
    if (R.exact) exact++; else worst = std::max(worst, R.db);
    std::printf("%-26s %-10s meters %-4s (%3d)%s%s%s\n", G.name, db, R.metersOk ? "ok" : "DIFF", R.nm, R.exact != R.hashOk ? "  [committed hash disagrees with the reference run]" : "", isGold && !R.goldOk ? "  [tests/golden/master.json hash not reproduced]" : "", ok ? "" : "  FAIL");
    if (!ok) fails++;
  }
  std::printf("master: %d cases, %d bit-exact, worst %s, %d of %d gold_ cases reproduce tests/golden/master.json's hash, %d failed (gate -80 dB)\n", kMasterGoldenN, exact, std::isinf(worst) ? "exact" : (std::to_string(worst) + " dB").c_str(), goldExact, goldRun, fails);
  return fails ? 1 : 0;
}
