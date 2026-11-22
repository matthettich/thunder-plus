// C1: shared by test_chains_song.cpp and test_chains_host.cpp: the golden's synthetic input generator (docs/port/C1G-NOTES.md, "Input generator"; bit for bit, compile with -ffp-contract=off: the core's
// PUBLIC option already does) and the FNV-1a 32 of the golden's hash.
#pragma once
#include <cstdint>
#include <vector>

namespace c1 {
// Track t (0-based), samples 0..n-1 from the start of the render. R is filled even for a mono track (its state advances and a mono input only reads L).
inline void makeInput(int t, int n, std::vector<float>& L, std::vector<float>& R) {
  L.assign((size_t)n, 0.f); R.assign((size_t)n, 0.f);
  auto xs = [](uint32_t s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; };
  auto wave = [](uint32_t p) { const double x = (double)p / 4294967296.0, w = x < 0.5 ? x : x - 1.0, a = w < 0 ? -w : w; return (16.0 * w) * (0.5 - a); };
  const uint32_t inc = 25769803u + 3231531u * (uint32_t)t;
  uint32_t sL = ((uint32_t)(t + 1) * 2654435761u) ^ 0x2545F491u; if (!sL) sL = 1;
  uint32_t sR = sL ^ 0xA5A5A5A5u; if (!sR) sR = 1;
  for (int i = 0; i < n; i++) {
    sL = xs(sL); sR = xs(sR);
    const double uL = (double)sL / 4294967296.0, uR = (double)sR / 4294967296.0;
    const uint32_t ph = (uint32_t)i * inc, phR = ph + 0x2AAAAAABu;
    const bool burst = ((((uint32_t)i >> 11) + (uint32_t)t) & 3u) == 0;
    const double xl = 0.25 * wave(ph) + (burst ? 0.2 * (2.0 * uL - 1.0) : 0.0);
    const double xr = 0.25 * wave(phR) + (burst ? 0.2 * (2.0 * uR - 1.0) : 0.0);
    L[(size_t)i] = (float)xl; R[(size_t)i] = (float)xr;
  }
}
inline uint32_t fnv(uint32_t h, const float* a, int n) {
  const unsigned char* u = (const unsigned char*)a;
  for (int i = 0; i < n * 4; i++) { h ^= u[i]; h *= 0x01000193u; }
  return h;
}
inline void hex8(uint32_t h, char* o) { static const char* d = "0123456789abcdef"; for (int i = 0; i < 8; i++) o[i] = d[(h >> (28 - 4 * i)) & 15]; o[8] = 0; }
}  // namespace c1
