// io/test/ring-test.cc: the input ring under a real producer thread (plan §7, A1.1). Build and run:
//   c++ -std=c++17 -O2 -pthread io/test/ring-test.cc -o /tmp/ring-test && /tmp/ring-test
// The producer writes callbacks of varying size whose every sample encodes (frame, channel); the reader takes runs at
// random moments and checks every sample, the frame numbers across runs, and that dropped callbacks show up as gaps of
// exactly their size (never a shift in time, never torn data).
#include "../src/ring.h"
#include <thread>
#include <atomic>
#include <vector>
#include <cstdio>
#include <random>
#include <chrono>

static float val(uint64_t f, uint32_t c) { return (float)((f % 100000) * 32 + c); }

int main() {
  const uint32_t CH = 28; const size_t CAP = 4800;   // a small ring (0.1 s at 48 kHz) so drops happen
  tio::InputRing ring; if (!ring.init(CH, CAP)) { puts("init failed"); return 1; }
  const uint64_t TOTAL = 3000000;
  std::atomic<bool> done{false};
  std::atomic<uint64_t> dropped{0};
  std::vector<std::pair<uint64_t, uint32_t>> drops; drops.reserve(100000);   // written only by the producer
  std::thread prod([&] {
    std::mt19937 rng(1); std::vector<float> buf(512 * CH); uint64_t f = 0;
    while (f < TOTAL) {
      uint32_t n = 16 + rng() % 497; if (f + n > TOTAL) n = (uint32_t)(TOTAL - f);
      for (uint32_t i = 0; i < n; i++) for (uint32_t c = 0; c < CH; c++) buf[i * CH + c] = val(f + i, c);
      if (!ring.put(f, buf.data(), n)) { drops.push_back({f, n}); dropped += n; }
      f += n;
      if (rng() % 64 == 0) std::this_thread::sleep_for(std::chrono::microseconds(rng() % 300));
    }
    done = true;
  });
  std::mt19937 rng(2); std::vector<float> out(CAP * CH + 512 * CH);
  uint64_t expect = 0, got = 0, gaps = 0, bad = 0, runs = 0;
  for (;;) {
    bool fin = done.load();
    uint64_t f0; uint32_t n = ring.take(out.data(), (uint32_t)(CAP + 512), &f0);
    if (n) {
      runs++;
      if (f0 < expect) { printf("went back in time: %llu < %llu\n", (unsigned long long)f0, (unsigned long long)expect); return 1; }
      if (f0 > expect) gaps += f0 - expect;
      for (uint32_t i = 0; i < n; i++) for (uint32_t c = 0; c < CH; c++) if (out[(size_t)i * CH + c] != val(f0 + i, c)) bad++;
      expect = f0 + n; got += n;
    } else if (fin) break;
    if (rng() % 8 == 0) std::this_thread::sleep_for(std::chrono::microseconds(rng() % 2000));
  }
  prod.join();
  uint64_t tail = TOTAL - expect;   // drops after the last delivered run don't show as a gap
  printf("frames %llu delivered %llu dropped %llu (gaps seen %llu + tail %llu) runs %llu lost() %llu bad samples %llu\n",
    (unsigned long long)TOTAL, (unsigned long long)got, (unsigned long long)dropped.load(), (unsigned long long)gaps,
    (unsigned long long)tail, (unsigned long long)runs, (unsigned long long)ring.lost(), (unsigned long long)bad);
  bool ok = bad == 0 && got + dropped == TOTAL && gaps + tail == dropped && ring.lost() == dropped && dropped > 0 && got > 0;
  puts(ok ? "ring ok" : "RING FAILED");
  return ok ? 0 : 1;
}
