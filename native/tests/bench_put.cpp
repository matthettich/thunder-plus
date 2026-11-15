// A4 (CI part): the core's own cost of taking a 53 MB buffer (13.25 M floats) in 1 MB chunks, with no web view in the way.
// This is the floor under the bridge paths' timings. Prints, never fails on speed.
#include <chrono>
#include <cstdio>
#include <vector>
#include "thunder/core.h"

int main() {
  thunder_core* c = thunder_create(48000, 512);
  const int total = 13250000, chunk = 262144;   // 1 MB of floats
  std::vector<float> src((size_t)total);
  for (int i = 0; i < total; i++) src[(size_t)i] = (float)(i % 1000) * 0.001f;
  char meta[64];
  auto t0 = std::chrono::steady_clock::now();
  for (int o = 0; o < total; o += chunk) {
    int n = total - o < chunk ? total - o : chunk;
    std::snprintf(meta, sizeof meta, "{\"o\":%d,\"n\":%d}", o, total);
    if (!thunder_put(c, 0, meta, src.data() + o, n)) { std::puts("FAIL: put"); return 1; }
  }
  double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  int64_t cnt = 0; double sum = 0;
  if (!thunder_put_info(c, 0, &cnt, &sum) || cnt != total) { std::puts("FAIL: info"); return 1; }
  std::printf("BENCH put 53 MB in 1 MB chunks: %.1f ms (core side only)\n", ms);
  thunder_destroy(c);
  return 0;
}
