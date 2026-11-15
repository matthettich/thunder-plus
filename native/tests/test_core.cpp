#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include "thunder/core.h"

static int fails = 0;
#define CHECK(c, msg) do { if (!(c)) { std::printf("FAIL: %s\n", msg); fails++; } } while (0)

int main() {
  const int sr = 48000, blk = 512;
  thunder_core* c = thunder_create(sr, blk);
  CHECK(c, "create");
  std::vector<float> l(blk), r(blk);

  // 1. Stopped: silence.
  for (int i = 0; i < 10; i++) thunder_process(c, l.data(), r.data(), blk);
  bool silent = true; for (int i = 0; i < blk; i++) if (l[i] != 0.0f || r[i] != 0.0f) silent = false;
  CHECK(silent, "silent while stopped");

  // 2. Play at 120 bpm: 16th steps are 0.125 s = 6000 frames. Collect the frames where hits start (pos events at steps 0,4,8,12).
  CHECK(thunder_post(c, "{\"t\":\"bpm\",\"v\":120}", 18), "post bpm");
  CHECK(thunder_post(c, "{\"t\":\"play\"}", 12), "post play");
  CHECK(!thunder_post(c, "{\"t\":\"nope\"}", 12), "unknown command rejected");
  std::vector<long long> posf; int clocks = 0; long long lastClockF = 0; bool echoed = false;
  const int blocks = (sr * 4) / blk;   // 4 s
  thunder_post(c, "{\"t\":\"echo\",\"n\":77}", 18);
  for (int b = 0; b < blocks; b++) {
    thunder_process(c, l.data(), r.data(), blk);
    char buf[256];
    while (thunder_poll(c, buf, sizeof buf)) {
      long long f = 0, n = 0;
      if (std::strstr(buf, "\"t\":\"pos\"")) { std::sscanf(std::strstr(buf, "\"f\":"), "\"f\":%lld", &f); posf.push_back(f); }
      else if (std::strstr(buf, "\"t\":\"clock\"")) { clocks++; std::sscanf(std::strstr(buf, "\"f\":"), "\"f\":%lld", &lastClockF); }
      else if (std::strstr(buf, "\"t\":\"echo\"")) { std::sscanf(std::strstr(buf, "\"n\":"), "\"n\":%lld", &n); echoed = (n == 77); }
    }
  }
  CHECK(echoed, "echo comes back");
  CHECK(posf.size() >= 7, "pos events (one per quarter note, 2 a second)");
  bool exact = true;
  for (size_t i = 0; i < posf.size(); i++) if (posf[i] != posf[0] + (long long)(i * 24000)) exact = false;   // a quarter note is 24000 frames at 120 bpm
  CHECK(exact, "hits land exactly on the core's own sample grid");
  CHECK(clocks >= 38 && clocks <= 41, "clock events ten a second");
  CHECK(lastClockF > 0, "clock carries the frame counter");

  // 3. Tempo change takes effect; stop silences after the tails.
  thunder_post(c, "{\"t\":\"stop\"}", 12);
  for (int i = 0; i < 20; i++) thunder_process(c, l.data(), r.data(), blk);
  silent = true; for (int i = 0; i < blk; i++) if (l[i] != 0.0f) silent = false;
  CHECK(silent, "silent after stop");

  // 4. Bulk put (the A4 proof's core side): two chunks.
  std::vector<float> a(1000, 0.5f), bb(1000, 0.25f);
  CHECK(thunder_put(c, 1, "{\"o\":0,\"n\":2000}", a.data(), 1000), "put chunk 1");
  CHECK(thunder_put(c, 1, "{\"o\":1000,\"n\":2000}", bb.data(), 1000), "put chunk 2");
  int64_t cnt = 0; double sum = 0;
  CHECK(thunder_put_info(c, 1, &cnt, &sum) && cnt == 2000 && std::fabs(sum - 750.0) < 1e-6, "put holds the data");

  thunder_destroy(c);
  if (fails) return 1;
  std::puts("PASS");
  return 0;
}
