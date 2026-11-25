// N2 (verify-n2 F2): the live stream's xrun count for the A5 gate. AAudio's count belongs to the STREAM (it restarts at 0 on a new one); the gate's "underruns" must be the count since the RUN began
// (nativeRtReset). The status poll hands every reading to onStream(), which returns what goes to rtlog_host_underruns; without this the gate's `underruns == 0` was vacuous (the logger was never told).
// No Android, no lock: a host test (test_android_a5_xrun.cpp) runs it against the real rtlog.
#ifndef TPA_A5_XRUN_H
#define TPA_A5_XRUN_H
#include <atomic>
#include <cstdint>

namespace tpa {
class XrunGate {
 public:
  int64_t onStream(int xruns) { last_.store(xruns, std::memory_order_relaxed); const int b = base_.load(std::memory_order_relaxed); return xruns > b ? xruns - b : 0; }   // status poll: count since the run began
  void beginRun() { base_.store(last_.load(std::memory_order_relaxed), std::memory_order_relaxed); }                                                                  // nativeRtReset: the run counts from here
  void newStream() { base_.store(0, std::memory_order_relaxed); last_.store(0, std::memory_order_relaxed); }                                                             // start / reopen: the new stream's counter begins at 0
 private:
  std::atomic<int> base_{0}, last_{0};
};
}  // namespace tpa
#endif
