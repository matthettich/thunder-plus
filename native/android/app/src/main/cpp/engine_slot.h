// N2 (verify-n2 F1): the ONE way a JNI entry point reaches the live engine. The engine object is replaced by engine.reset, a failed stream reopen and shutdown, on one thread, while Kotlin's pump,
// ticker and notification threads call in on others. A plain shared_ptr read beside a reset is a data race and a use-after-free. Here: every reader takes a COPY under a small mutex (get()), uses the
// copy for the whole call, and the owner swaps it out under the same mutex (take()). A copy that outlives the swap is safe because LiveEngine's own any-thread getters are (call() answers -1000 once
// destroyed, statsJson() "{}", statusJson() the last state; destroy() waits for readers). Header-only, no Android: test_android_live_stress.cpp runs it.
#ifndef TPA_ENGINE_SLOT_H
#define TPA_ENGINE_SLOT_H
#include <memory>
#include <mutex>
#include <utility>

namespace tpa {

template <class T>
class Slot {
 public:
  std::shared_ptr<T> get() const { std::lock_guard<std::mutex> g(m_); return p_; }                      // ANY thread: a copy that stays valid for the whole call (null: no engine)
  void set(std::shared_ptr<T> p) { std::lock_guard<std::mutex> g(m_); p_ = std::move(p); }
  std::shared_ptr<T> take() { std::lock_guard<std::mutex> g(m_); return std::exchange(p_, nullptr); }   // new readers see null at once; the caller destroys what it took
 private:
  mutable std::mutex m_;
  std::shared_ptr<T> p_;
};

}  // namespace tpa
#endif
