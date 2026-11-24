// FX1: the page player's beat clock (BeatClock page mode, mix/beat.h) against what headless Chrome gives for the same events: the `bp` input of the master's
// Flex node during a render of ELECTRO (golden/beat_page.txt: blocks 1..40, 1500..1510 and 2740..2760, which holds the wrap past 16 beats), every float32 bit for bit.
// The clock is a ConstantSource's `offset` ramped row to row; Chrome's linear-ramp arithmetic (per chunk four lanes, then value += inc) is what the model reproduces.
//   test_beat_page <golden/beat_page.txt>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "mix/beat.h"

using namespace thunder;

int main(int argc, char** argv) {
  if (argc < 2) { std::printf("usage: test_beat_page <beat_page.txt>\n"); return 2; }
  std::ifstream f(argv[1]); if (!f) { std::printf("no golden %s\n", argv[1]); return 2; }
  struct Ev { double t, at, b; }; std::vector<Ev> evs; std::map<int, std::vector<uint32_t>> want; std::string line;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream is(line); std::string k; is >> k;
    if (k == "E") { Ev e; is >> e.t >> e.at >> e.b; evs.push_back(e); }
    else if (k == "B") { int nb; is >> nb; std::vector<uint32_t> v(128); for (auto& x : v) { std::string h; is >> h; x = (uint32_t)std::strtoul(h.c_str(), nullptr, 16); } want[nb] = v; }
  }
  const int last = want.rbegin()->first; BeatClock clk; clk.setPage(true, 48000);
  size_t k = 0; int64_t checked = 0, bad = 0, firstBad = -1; double scratch[256]; float C[128];
  for (int nb = 1; nb <= last; nb++) {
    const double t0 = (double)((int64_t)(nb - 1) * 128) / 48000;
    while (k < evs.size() && evs[k].t < t0 + 0.25) { SeqEvent e; e.type = SEV_BEAT; e.t = evs[k].t; e.v = evs[k].b; clk.event(e, t0); k++; }
    clk.render(C, 128, t0, 1.0 / 48000, scratch);
    auto it = want.find(nb); if (it == want.end()) continue;
    for (int z = 0; z < 128; z++) { uint32_t u; std::memcpy(&u, &C[z], 4); checked++; if (u != it->second[z]) { bad++; if (firstBad < 0) { firstBad = (int64_t)(nb - 1) * 128 + z; std::printf("first difference at frame %lld: Chrome %08x, native %08x\n", (long long)firstBad, it->second[z], u); } } }
  }
  std::printf("page clock vs Chrome: %lld values checked, %lld differ, dropped events %u\n", (long long)checked, (long long)bad, clk.dropped());
  const bool ok = checked > 0 && bad == 0 && clk.dropped() == 0; std::printf("%s\n", ok ? "ok" : "FAIL"); return ok ? 0 : 1;
}
