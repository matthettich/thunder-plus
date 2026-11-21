// U10: pdc_plan / pdc_mode_of against the web app's own pdcPlan (tools/pdc-dump.mjs runs it in a Node vm on hand-made songs and every song
// in sounds/): T, d, f, c compared as doubles, exactly. The mode comes from the case's `aset` through pdc_mode_of, not from the JS's answer.
//   test_pdc <dir>/pdc.json
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "mix/pdc.h"
#include "rt/json.h"

using namespace thunder;

static bool readFile(const std::string& path, std::string* out) { std::ifstream f(path, std::ios::binary); if (!f) return false; std::stringstream ss; ss << f.rdbuf(); *out = ss.str(); return true; }
static bool same(double a, double b) { return std::memcmp(&a, &b, sizeof a) == 0; }

// BUS INTO BUS (web build 651, tests/bus-into-bus.test.js "latency plan follows the chain"): the owner's patch (tracks 1-6, 9, 13 -> mixer -> Chain Out 8; Track In 8 -> Chain Out 14),
// tracks 01 10 ms, 02 100 ms, 08 50 ms, 14 25 ms: T = 175 ms, f[01] = 90 ms, c[14] = 150 ms, c[08] = 100 ms. Needs no reference file.
static int chainCase() {
  std::string m = "{\"format\":\"chains.patch\",\"modules\":[", c = "\"cables\":[";
  const int src[] = {1, 2, 3, 4, 5, 6, 9, 13};
  for (int t : src) { m += "{\"id\":\"ti" + std::to_string(t) + "\",\"type\":\"trackIn\",\"params\":{\"track\":" + std::to_string(t) + "}},"; c += "{\"from\":[\"ti" + std::to_string(t) + "\",\"out\"],\"to\":[\"mx\",\"in\"]},"; }
  m += "{\"id\":\"mx\",\"type\":\"smixer\",\"params\":{}},{\"id\":\"co8\",\"type\":\"chainOut\",\"params\":{\"track\":8}},{\"id\":\"ti8\",\"type\":\"trackIn\",\"params\":{\"track\":8}},{\"id\":\"co14\",\"type\":\"chainOut\",\"params\":{\"track\":14}}],";
  c += "{\"from\":[\"mx\",\"out\"],\"to\":[\"co8\",\"in\"]},{\"from\":[\"ti8\",\"out\"],\"to\":[\"co14\",\"in\"]}]}";
  JsonDoc doc; if (!doc.parse(m + c)) { std::printf("pdc chain: bad patch\n"); return 1; }
  bool on[kSongNT] = {false}; for (int t : src) on[t - 1] = true; on[7] = on[13] = true;
  const BusPlan P = bus_plan(doc.root(), on);
  const double sr = 48000; const PdcSlot sl[] = {{0, 0, true, false, 0.010 * sr}, {1, 0, true, false, 0.100 * sr}, {7, 0, true, false, 0.050 * sr}, {13, 0, true, false, 0.025 * sr}};
  const PdcPlan R = pdc_plan(P, sl, 4, sr, PdcMode::On);
  auto near = [](double a, double b) { return std::fabs(a - b) < 1e-9; };
  const bool ok = near(R.T, 0.175) && near(R.f[0], 0.090) && near(R.c[13], 0.150) && near(R.c[7], 0.100);
  std::printf("pdc chain (bus into bus): T=%.6f f[01]=%.6f c[14]=%.6f c[08]=%.6f %s\n", R.T, R.f[0], R.c[13], R.c[7], ok ? "ok" : "FAIL");
  return ok ? 0 : 1;
}

int main(int argc, char** argv) {
  const int chainFail = chainCase();
  if (argc < 2) { std::printf("pdc: no reference file (Node not found): not run\n"); return chainFail; }
  std::string js; if (!readFile(argv[1], &js)) { std::printf("pdc: cannot read %s\n", argv[1]); return 1; }
  JsonDoc doc; if (!doc.parse(js)) { std::printf("pdc: bad JSON\n"); return 1; }
  Json cases = doc.root(); int fails = 0;
  for (size_t i = 0; i < cases.size(); i++) {
    Json C = cases.at(i), W = C.get("want"), S = C.get("S"), A = C.get("aset"); const std::string id(C.str("id"));
    bool chainOn[kSongNT]; for (int t = 0; t < kSongNT; t++) chainOn[t] = S.arr("chainOn").at((size_t)t).asBool();
    const BusPlan P = bus_plan(S.get("chainPatch"), chainOn);
    std::vector<PdcSlot> slots; Json SL = C.arr("slots");
    for (size_t k = 0; k < SL.size(); k++) { Json s = SL.at(k); slots.push_back(PdcSlot{(int)s.num("track"), (int)s.num("slot"), s.boolean("on"), s.boolean("bypass"), s.num("latency")}); }
    std::string pm(A.str("pdcMode")); const bool hasPm = !A.get("pdcMode").missing() && A.get("pdcMode").isStr();
    const PdcMode mode = pdc_mode_of(hasPm ? pm.c_str() : nullptr, A.boolean("pdc"));
    const char* want = std::string(C.str("mode")) == "off" ? "off" : std::string(C.str("mode")) == "on" ? "on" : "auto";
    std::string why;
    if ((mode == PdcMode::Off ? "off" : mode == PdcMode::On ? "on" : "auto") != std::string(want)) why += " mode";
    const PdcPlan R = pdc_plan(P, slots.data(), (int)slots.size(), C.num("sr"), mode);
    if (R.on != W.boolean("on")) why += " on";
    if (!same(R.T, W.num("T"))) why += " T";
    for (int t = 0; t <= kSongNT; t++) {
      if (!same(R.d[t], W.arr("d").at((size_t)t).asNum())) why += " d[" + std::to_string(t) + "]";
      if (!same(R.f[t], W.arr("f").at((size_t)t).asNum())) why += " f[" + std::to_string(t) + "]";
      if (!same(R.c[t], W.arr("c").at((size_t)t).asNum())) why += " c[" + std::to_string(t) + "]";
    }
    std::printf("%-34s T=%-22.17g %s%s\n", id.c_str(), R.T, why.empty() ? "ok" : "DIFFERS:", why.empty() ? "" : (why + "  FAIL").c_str());
    if (!why.empty()) fails++;
  }
  std::printf("pdc: %zu cases, %d failed\n", cases.size(), fails);
  return fails || chainFail ? 1 : 0;
}
