// Single-producer / single-consumer ring. The only way anything crosses between the audio thread and another thread
// (NATIVE-DESKTOP-PLAN 4.1): no locks, no allocation, wait-free on both sides. N must be a power of two.
#ifndef THUNDER_RT_SPSC_H
#define THUNDER_RT_SPSC_H
#include <atomic>
#include <cstdint>

namespace thunder {

template <class T, uint32_t N>
struct Spsc {
  static_assert((N & (N - 1)) == 0, "N must be a power of two");
  T buf[N];
  std::atomic<uint32_t> head{0}, tail{0};   // head: next write (producer), tail: next read (consumer)
  bool push(const T& v) {                   // producer
    const uint32_t h = head.load(std::memory_order_relaxed);
    if (h - tail.load(std::memory_order_acquire) >= N) return false;
    buf[h & (N - 1)] = v;
    head.store(h + 1, std::memory_order_release);
    return true;
  }
  bool pop(T& v) {                          // consumer
    const uint32_t t = tail.load(std::memory_order_relaxed);
    if (t == head.load(std::memory_order_acquire)) return false;
    v = buf[t & (N - 1)];
    tail.store(t + 1, std::memory_order_release);
    return true;
  }
  uint32_t space() const {                  // producer: free slots (a lower bound: the consumer may free more meanwhile)
    return N - (head.load(std::memory_order_relaxed) - tail.load(std::memory_order_acquire));
  }
  uint32_t avail() const {                  // consumer: what can be popped now (a lower bound: the producer may push more meanwhile). Used to take a SNAPSHOT of the ring before other work (C2 L1)
    return head.load(std::memory_order_acquire) - tail.load(std::memory_order_relaxed);
  }
  bool empty() const {                      // consumer
    return tail.load(std::memory_order_relaxed) == head.load(std::memory_order_acquire);
  }
};

}  // namespace thunder
#endif
