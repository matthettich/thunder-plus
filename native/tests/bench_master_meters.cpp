// M2: the cost of the master meters (docs/port/M2-NOTES.md). Not a test: prints ns per 128-frame quantum for the mastering MasterProcessor with its meters off and on, for three stage sets, on a loud stereo signal.
// Usage: bench_master_meters [seconds of audio, default 20]. Min of 5 repeats, steady_clock around whole runs (not per quantum).
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "master/chain.h"
#include "master/master.h"
using namespace thunder;
static double run(bool meters, bool sup, bool comp, bool lim, int quanta) {
  MasterProcessor p(48000, false); p.set(master_chain_fresh(false).set);
  MasterSet s; s.hasSup = s.hasComp = s.hasLim = true; s.sup.on = sup; s.comp.on = comp; s.lim.on = lim; s.comp.thr = -20.0; s.comp.ratio = 4.0; s.sup.depth = 0.6; p.set(s);
  p.setMeters(meters);
  std::vector<float> L(128), R(128); double best = 1e30; MasterMeter m; double sink = 0;
  for (int rep = 0; rep < 5; rep++) {
    const auto t0 = std::chrono::steady_clock::now();
    for (int q = 0; q < quanta; q++) {
      for (int i = 0; i < 128; i++) { const double x = (double)q * 128 + i; L[i] = (float)(0.9 * std::sin(0.0575 * x)); R[i] = (float)(0.7 * std::sin(0.0431 * x)); }
      p.process(L.data(), R.data(), 128); sink += L[5]; while (p.popMeter(&m)) sink += m.inLu;
    }
    best = std::min(best, std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / quanta);
  }
  if (sink == 12345.678) std::printf("%g\n", sink);
  return best;
}
int main(int argc, char** argv) {
  const int quanta = (int)((argc > 1 ? std::atof(argv[1]) : 20.0) * 48000 / 128);
  struct { const char* n; bool sup, comp, lim; } sets[3] = {{"comp + limiter (the default mastering)", false, true, true}, {"suppressor + comp + limiter", true, true, true}, {"limiter only", false, false, true}};
  std::printf("%-40s %12s %12s %12s\n", "stages", "off ns/q", "on ns/q", "delta ns/q");
  for (auto& s : sets) { const double a = run(false, s.sup, s.comp, s.lim, quanta), b = run(true, s.sup, s.comp, s.lim, quanta); std::printf("%-40s %12.0f %12.0f %12.0f   (a quantum is %.0f ns of real time)\n", s.n, a, b, b - a, 128.0 / 48000 * 1e9); }
  return 0;
}
