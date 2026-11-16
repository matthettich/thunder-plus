// U15: js_cos / js_sin / js_tan / js_log (core/src/rt/fdlibm.cpp) against the COMMITTED golden of Node 22's Math.* on 400000 arguments (tests/golden/libm_golden.inc, made by
// tools/libm-dump.mjs), on every platform, never skipped: the hash of each function's results and 400 spot values with their arguments, bit for bit.
// THE REFERENCE IS NODE 22 ON x86-64 LINUX. Node's Math.* is not the same on every platform: Node on macOS arm64 is built by Apple clang with floating-point contraction (FMA) and
// rounds some results differently (CI saw 0.4% to 0.9% of the arguments differ), so a reference made by the local Node is no reference. Chrome builds V8 without contraction but
// may use its libm-trig path for sin / cos: to be checked against real Chrome at step 11. Also prints how many results the C library's own functions get wrong against the spots.
// (The arguments are made here as in the tool: an integer LCG, bit patterns, no Math.*. libm_golden_current, Linux x86-64, checks that the committed golden is what Node gives.)
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "rt/jsmath.h"
#include "golden/libm_golden.inc"

using namespace thunder;
static uint64_t canon(double x) { if (x != x) return 0x7ff8000000000000ull; uint64_t w; std::memcpy(&w, &x, 8); return w; }
int main() {
  const char* nm[4] = {"cos", "sin", "tan", "log"}; uint64_t h[4], libc = 0; for (auto& v : h) v = 0xcbf29ce484222325ull;
  long spotBad[4] = {0}; long firstSpot[4] = {-1, -1, -1, -1}; uint32_t s = 12345; auto nx = [&]() { s = s * 1664525u + 1013904223u; return s; };
  for (int i = 0; i < kLibmN; i++) {
    const uint32_t r1 = nx(), r2 = nx(), r3 = nx(); const int e = -20 + (int)(r3 % 31); const uint32_t neg = i % 3 == 0 && ((r3 >> 8) & 1) ? 0x80000000u : 0u;
    const uint32_t hi = neg | (uint32_t)((1023 + e) << 20) | (r1 >> 12); const uint64_t xb = ((uint64_t)hi << 32) | r2; double x; std::memcpy(&x, &xb, 8);
    const double r[4] = {js_cos(x), js_sin(x), js_tan(x), js_log(x)};
    for (int k = 0; k < 4; k++) h[k] = (h[k] ^ canon(r[k])) * 0x100000001b3ull;
    if (i % kLibmStep == 0) { const unsigned long long* sp = kLibmSpot[i / kLibmStep]; const double g[4] = {std::cos(x), std::sin(x), std::tan(x), std::log(x)};
      if (sp[0] != xb) { std::printf("libm: the argument generator differs from the tool's at %d: FAIL\n", i); return 1; }
      for (int k = 0; k < 4; k++) { if (canon(r[k]) != sp[1 + k]) { spotBad[k]++; if (firstSpot[k] < 0) firstSpot[k] = i; } if (canon(g[k]) != sp[1 + k]) libc++; } }
  }
  int fails = 0;
  for (int k = 0; k < 4; k++) { const bool ok = h[k] == kLibmHash[k] && !spotBad[k]; if (!ok) fails++;
    std::printf("js_%s: %d arguments, hash %s, %d spot values (%ld differ%s)  %s\n", nm[k], kLibmN, h[k] == kLibmHash[k] ? "as committed" : "DIFFERS", kLibmSpots, spotBad[k], firstSpot[k] >= 0 ? ", the first at argument " : "", ok ? "ok" : "FAIL");
    if (firstSpot[k] >= 0) std::printf("    first differing spot: argument %ld\n", firstSpot[k]); }
  std::printf("(the C library's cos, sin, tan, log differ from the golden on %llu of %d spot results)\n", (unsigned long long)libc, kLibmSpots * 4);
  return fails ? 1 : 0;
}
