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

int main(int argc, char** argv) {
  if (argc < 2) { std::printf("pdc: no reference file (Node not found): not run\n"); return 0; }
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
  return fails ? 1 : 0;
}
