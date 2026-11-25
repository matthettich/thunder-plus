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
  // status poll: the count since the run began = what earlier streams of this run counted (carried_) + this stream's count since the run (or the stream) began
  int64_t onStream(int xruns) { last_.store(xruns, std::memory_order_relaxed); const int b = base_.load(std::memory_order_relaxed); return carried_.load(std::memory_order_relaxed) + (xruns > b ? xruns - b : 0); }
  void beginRun() { carried_.store(0, std::memory_order_relaxed); base_.store(last_.load(std::memory_order_relaxed), std::memory_order_relaxed); }   // nativeRtReset: the run counts from here
  // A reroute / reopen: the new stream's counter begins at 0, but the xruns the old stream counted since the run began are KEPT (ANR fix 25 Nov: they were lost). Calling it twice in a row adds nothing the second time.
  // Atomics only (miniaudio's notification thread calls it); a status poll running at the same instant can read the total once too high or too low for one round, never more.
  void newStream() {
    const int l = last_.load(std::memory_order_relaxed), b = base_.load(std::memory_order_relaxed);
    carried_.fetch_add(l > b ? l - b : 0, std::memory_order_relaxed);
    base_.store(0, std::memory_order_relaxed); last_.store(0, std::memory_order_relaxed);
  }
  void restart() { carried_.store(0, std::memory_order_relaxed); base_.store(0, std::memory_order_relaxed); last_.store(0, std::memory_order_relaxed); }   // a fresh engine start: nothing carries over from the last one
 private:
  std::atomic<int> base_{0}, last_{0};
  std::atomic<int64_t> carried_{0};
};
}  // namespace tpa
#endif
