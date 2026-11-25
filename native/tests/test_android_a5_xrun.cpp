// N2 verify-n2 F2: an underrun the live stream reports makes the A5 gate FAIL. The shell's wiring (tpa::XrunGate -> rtlog_host_underruns) against the real rtlog: a clean 4 s run passes, the same run with
// one xrun reported does not, a run begun after an old xrun counts from its own start, and a new stream's counter restarts at 0.
#include <cstdio>

#include "a5_xrun.h"
#include "thunder/rtlog.h"

static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { g_fail++; std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

// a perfect device: 480-frame callbacks every 10 ms of wall time, each taking 1 ms of work, for `secs` seconds; `status` is polled every 100 ms with the xrun count `xr(t)`
template <class F> static rtlog_snapshot run(rtlog* lg, tpa::XrunGate* gate, int secs, F xr) {
  int64_t t = 1000000000LL;
  for (int i = 0; i < secs * 100; i++) {
    rtlog_begin(lg, t); rtlog_end(lg, t + 1000000LL, 480); t += 10000000LL;
    if (i % 10 == 0) rtlog_host_underruns(lg, gate->onStream(xr(i)));
  }
  return rtlog_read(lg);
}

int main() {
  rtlog lg; tpa::XrunGate gate;
  rtlog_reset(&lg, 48000); gate.newStream();
  rtlog_snapshot s = run(&lg, &gate, 4, [](int) { return 0; });
  CHECK(s.pass == 1 && s.underruns == 0, "a clean run passes: pass %d underruns %lld ratio %.1f", s.pass, (long long)s.underruns, s.ratio_ppm);
  rtlog_reset(&lg, 48000); gate.newStream();
  s = run(&lg, &gate, 4, [](int i) { return i >= 150 ? 1 : 0; });   // one xrun in the middle of the run
  CHECK(s.pass == 0 && s.underruns == 1, "ONE xrun fails the gate: pass %d underruns %lld", s.pass, (long long)s.underruns);
  // an old xrun before the run began does not count: the run starts (nativeRtReset) with the stream at 5 xruns
  rtlog_reset(&lg, 48000); gate.newStream(); gate.onStream(5); gate.beginRun();
  s = run(&lg, &gate, 4, [](int) { return 5; });
  CHECK(s.pass == 1 && s.underruns == 0, "xruns before the run are not the run's: pass %d underruns %lld", s.pass, (long long)s.underruns);
  s = run(&lg, &gate, 1, [](int) { return 7; });
  CHECK(s.underruns == 2, "two more since the run began: %lld", (long long)s.underruns);
  gate.newStream();
  CHECK(gate.onStream(0) == 0 && gate.onStream(3) == 3, "a new stream counts from 0");
  std::printf(g_fail ? "FAILED: %d\n" : "OK\n", g_fail);
  return g_fail ? 1 : 0;
}
