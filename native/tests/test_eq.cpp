// U12: the master EQ (core/src/master/eq.*), the master chain's configuration (chain.*) and its order.
//  1. every case of tools/eq-dump.mjs (the page's eqBuild on stand-in nodes + a JS model of Chromium's BiquadFilterNode chain, itself checked against a
//     real OfflineAudioContext by tools/eq-webaudio-check.mjs): the node list the C++ builds equals eqBuild's (type, float32 f / Q / gain), the output
//     within -80 dB of the model (the figure is printed; exact where it is), the committed hash.
//  2. master_chain_from_song against the page's own normMst on 9 hand-made `mst` objects (junk values, clamps, band limits).
//  3. the chain: the order EQ -> worklet -> safety limiter, which limiter is in the path, the latencies pass() trims.
//   test_eq <dir>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "master/chain.h"
#include "master/eq.h"
#include "rt/json.h"
#include "golden/eq_golden.inc"

using namespace thunder;
static bool readFile(const std::string& path, std::string* out) { std::ifstream f(path, std::ios::binary); if (!f) return false; std::stringstream ss; ss << f.rdbuf(); *out = ss.str(); return true; }
static uint64_t fnv(const float* f, size_t n) { uint64_t h = 0xcbf29ce484222325ull; const unsigned char* u = (const unsigned char*)f; for (size_t i = 0; i < n * 4; i++) { h ^= u[i]; h *= 0x100000001b3ull; } return h; }
static int fails = 0;
static void check(bool ok, const char* what) { std::printf("%-90s %s\n", what, ok ? "ok" : "FAIL"); if (!ok) fails++; }

static EqType typeOf(std::string_view t) { return t == "lowcut" ? EqType::LowCut : t == "lowshelf" ? EqType::LowShelf : t == "bell" ? EqType::Bell : t == "highshelf" ? EqType::HighShelf : EqType::HighCut; }
static BqType bqOf(std::string_view t) { return t == "lowpass" ? BqType::Lowpass : t == "highpass" ? BqType::Highpass : t == "peaking" ? BqType::Peaking : t == "lowshelf" ? BqType::Lowshelf : BqType::Highshelf; }

static bool runCase(const std::string& dir, const EqGolden& G, double* dbOut, bool* exactOut, bool* hashOk) {
  std::string js, in, ref; if (!readFile(dir + "/" + G.name + ".json", &js) || !readFile(dir + "/" + G.name + ".in.f32", &in) || !readFile(dir + "/" + G.name + ".f32", &ref)) return false;
  JsonDoc doc; if (!doc.parse(js)) return false; Json M = doc.root(); const int n = (int)(in.size() / 8);
  EqBand bands[kEqMaxBands]; int nb = 0; Json bs = M.arr("bands");
  for (size_t i = 0; i < bs.size() && nb < kEqMaxBands; i++) { Json b = bs.at(i); EqBand& e = bands[nb++]; e.type = typeOf(b.str("type")); e.f = b.num("f"); e.g = b.num("g"); e.q = b.num("q"); e.slope = (int)b.num("slope", 24); e.on = b.boolean("on", true); }
  MasterEq eq; eq.configure(M.num("sr"), M.boolean("eqOn", true), bands, nb);
  Json nodes = M.arr("nodes"); bool same = (size_t)eq.nodes() == nodes.size();
  for (int i = 0; same && i < eq.nodes(); i++) { Json d = nodes.at((size_t)i); same = eq.nodeType(i) == bqOf(d.str("type")) && eq.nodeF(i) == (float)d.num("f") && eq.nodeQ(i) == (float)d.num("Q") && eq.nodeG(i) == (float)d.num("g"); }
  if (!same) { std::printf("%-22s the node list differs from eqBuild's\n", G.name); return false; }
  std::vector<float> L(in.size() / 8), R(in.size() / 8); std::memcpy(L.data(), in.data(), (size_t)n * 4); std::memcpy(R.data(), in.data() + (size_t)n * 4, (size_t)n * 4);
  const int sizes[] = {128, 128, 7, 512, 1, 300, 128};   // the state carries across blocks of any size
  for (int o = 0, k = 0; o < n; k++) { const int c = std::min(sizes[k % 7], n - o); eq.process(L.data() + o, R.data() + o, c); o += c; }
  const float* J = (const float*)ref.data(); double err = 0, peak = 0; bool exact = true;
  for (int c = 0; c < 2; c++) for (int i = 0; i < n; i++) { const float a = (c ? R : L)[(size_t)i], r = J[(size_t)c * n + i]; peak = std::max(peak, (double)std::fabs(r)); if (a != r) { exact = false; err = std::max(err, (double)std::fabs((double)a - r)); } }
  std::vector<float> all(L); all.insert(all.end(), R.begin(), R.end());
  *exactOut = exact; *hashOk = fnv(all.data(), all.size()) == G.hash; *dbOut = exact ? -INFINITY : 20 * std::log10(err / std::max(peak, 1e-30));
  return true;
}

static double jn(Json v) { return v.isNum() ? v.asNum() : std::nan(""); }
static bool same(MOpt a, Json b) { return a && *a == jn(b); }
static bool chainNorm(Json raw, Json want) {
  const MasterChainConfig c = master_chain_from_song(raw, true); bool ok = c.mstOn == want.boolean("on") && c.eqOn == want.get("eq").boolean("on");
  Json bs = want.get("eq").arr("bands"); ok = ok && (size_t)c.nBands == bs.size();
  for (int i = 0; ok && i < c.nBands; i++) { Json b = bs.at((size_t)i); const EqBand& e = c.bands[i]; ok = e.type == typeOf(b.str("type")) && e.f == b.num("f") && e.g == b.num("g") && e.q == b.num("q") && e.slope == (int)b.num("slope") && e.on == b.boolean("on"); }
  Json s = want.get("sup"), p = want.get("comp"), l = want.get("lim"); const SupSet& u = c.set.sup; const CompSet& q = c.set.comp; const LimSet& m = c.set.lim;
  ok = ok && u.on == s.boolean("on") && same(u.depth, s.get("depth")) && same(u.sharp, s.get("sharp")) && same(u.sel, s.get("sel")) && same(u.atk, s.get("atk")) && same(u.rel, s.get("rel")) && same(u.lo, s.get("lo")) && same(u.hi, s.get("hi")) && same(u.mix, s.get("mix")) && u.delta && *u.delta == s.boolean("delta");
  ok = ok && q.on == p.boolean("on") && same(q.thr, p.get("thr")) && same(q.ratio, p.get("ratio")) && same(q.atk, p.get("atk")) && same(q.rel, p.get("rel")) && same(q.knee, p.get("knee")) && same(q.makeup, p.get("makeup")) && same(q.mix, p.get("mix")) && same(q.hpf, p.get("hpf")) && q.autoRel && *q.autoRel == p.boolean("auto") && q.autoMakeup && *q.autoMakeup == p.boolean("automk");
  ok = ok && m.on == l.boolean("on") && same(m.drive, l.get("drive")) && same(m.ceil, l.get("ceil")) && same(m.rel, l.get("rel")) && same(m.look, l.get("look")) && m.tp && *m.tp == l.boolean("tp");
  return ok;
}

int main(int argc, char** argv) {
  if (argc < 2) { std::printf("eq: no reference directory (Node not found): %d cases not run\n", kEqGoldenN); return 0; }
  int exact = 0, ran = 0; double worst = -INFINITY;
  for (int i = 0; i < kEqGoldenN; i++) {
    double db = 0; bool ex = false, hok = false;
    if (!runCase(argv[1], kEqGolden[i], &db, &ex, &hok)) { std::printf("%-22s could not be run  FAIL\n", kEqGolden[i].name); fails++; continue; }
    ran++; if (ex) exact++; else worst = std::max(worst, db);
    const bool ok = db <= -80.0; char buf[24]; if (ex) std::snprintf(buf, sizeof buf, "exact"); else std::snprintf(buf, sizeof buf, "%.1f dB", db);
    std::printf("%-22s %-10s%s%s\n", kEqGolden[i].name, buf, ex != hok ? "  [committed hash disagrees with the reference run]" : "", ok ? "" : "  FAIL"); if (!ok) fails++;
  }
  std::printf("eq: %d cases, %d bit-exact, worst %s (gate -80 dB; the model = Chromium's filter, Chrome 153 measured by tools/eq-webaudio-check.mjs)\n", ran, exact, std::isinf(worst) ? "exact" : (std::to_string(worst) + " dB").c_str());
  // ---- normMst ----
  { std::string js; if (!readFile(std::string(argv[1]) + "/normmst.json", &js)) check(false, "normmst.json readable");
    else { JsonDoc doc; doc.parse(js); Json A = doc.root(); int ok = 0; for (size_t i = 0; i < A.size(); i++) { if (chainNorm(A.at(i).get("raw"), A.at(i).get("want"))) ok++; else std::printf("   normMst case %zu differs\n", i); }
      char b[96]; std::snprintf(b, sizeof b, "master_chain_from_song = the page's normMst on %zu cases (%d equal)", A.size(), ok); check(ok == (int)A.size(), b); } }
  // ---- the chain ----
  {
    MasterChainConfig d = master_chain_fresh(true);   // a song as it is by default: mastering off, the safety limiter on
    MasterChain C(48000, 128); C.configure(d);
    check(!C.mstActive() && !C.eqActive() && !C.workletActive() && C.safetyActive(), "default song: mastering off, safety limiter on");
    check(C.latency() == 144 && C.latencySafety() == 144, "default song: latency is the safety limiter's 3 ms = 144 samples");
    d.mstOn = true; C.configure(d);   // freshMst's own stages: comp + lim on, EQ flat, sup off
    check(C.mstActive() && C.eqActive() && C.workletActive() && !C.safetyActive() && C.latency() == 144 && C.latencyMastering() == 144, "mastering on (comp + lim on): its limiter replaces the safety one");
    d.set.lim.on = false; C.configure(d);
    check(C.workletActive() && C.safetyActive() && C.latency() == 144 && C.latencyMastering() == 0, "mastering on, its limiter off: the safety limiter is back");
    d.set.sup.on = true; C.configure(d);
    check(C.latencyMastering() == 2560 && C.latency() == 2560 + 144, "suppressor on: 2560 + 144 samples");
    d.set.sup.on = d.set.comp.on = false; C.configure(d);
    check(!C.workletActive() && C.eqActive() && C.latency() == 144, "mastering on with nothing but the EQ: no worklet");
    // order: EQ before the worklet before the safety limiter: a full-scale impulse is shaped by the EQ, and the final clamp is the safety ceiling (-1 dB)
    d = master_chain_fresh(true); d.mstOn = true; d.set.comp.on = false; d.set.lim.on = false; d.bands[2].g = 12; C.configure(d);
    std::vector<float> L(2048, 0.0f), R(2048, 0.0f); for (int i = 0; i < 2048; i += 100) L[(size_t)i] = R[(size_t)i] = 1.0f;
    C.process(L.data(), R.data(), 2048); double pk = 0; for (float v : L) pk = std::max(pk, (double)std::fabs(v));
    check(pk <= std::pow(10.0, -1.0 / 20) + 1e-6, "the safety limiter's ceiling (-1 dB) holds after an EQ boost");
  }
  std::printf("eq: %d check(s) failed\n", fails);
  return fails ? 1 : 0;
}
