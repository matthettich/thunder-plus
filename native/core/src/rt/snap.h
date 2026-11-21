// Immutable snapshot swap (U05). The song, the kit and anything else the audio thread only READS is built whole on a control
// thread, handed over as one pointer, adopted by the audio thread at the top of a block, and the one it replaces travels back
// to the control thread to be freed there. The audio thread never allocates, frees or waits.
//   control thread:  publish(new T(...));  collect();      (one thread: the one that calls thunder_post)
//   audio thread:    if (slot.adopt()) { ...the snapshot changed... }   const T* s = slot.get();
// adopt() takes a new snapshot only while there is room to send the old one back, so nothing is ever dropped on the audio
// thread; with a control thread that calls collect() on every post the return ring cannot stay full.
#ifndef THUNDER_RT_SNAP_H
#define THUNDER_RT_SNAP_H
#include <type_traits>
#include <utility>

#include "spsc.h"

namespace thunder {

template <class T> struct SnapNeedsHandover : std::false_type {};   // specialize to true: SnapSlot<T>::adopt() (plain) is not callable

template <class T, uint32_t N = 16>
class SnapSlot {
 public:
  ~SnapSlot() { T* p; while (in_.pop(p)) delete p; while (out_.pop(p)) delete p; delete cur_; }   // after the audio thread has stopped
  bool publish(T* p) { return in_.push(p); }                 // control thread; false: ring full, the caller still owns p
  size_t collect() { size_t n = 0; T* p; while (out_.pop(p)) { delete p; n++; } return n; }     // control thread (U37 P2: returns how many it freed)
  // The plain form is for snapshots that need nothing from the one they replace. A type that must hand its state over (SongSnap: the
  // running sequencer) specializes SnapNeedsHandover to true, and this form is then not callable (it would retire the snapshot the runner
  // still points at). Compile-time: `can_adopt_plain` below is false for it.
  template <class U = T, class = std::enable_if_t<!SnapNeedsHandover<U>::value>>
  bool adopt() {                                             // AUDIO THREAD
    bool changed = false; T* p;
    while (out_.space() > 0 && in_.pop(p)) { if (cur_) out_.push(cur_); cur_ = p; changed = true; }
    return changed;
  }
  // AUDIO THREAD: adopt for a snapshot that must take state over from the one that was running. Pops everything pending. Everything that is
  // not the newest goes back to the control thread, AFTER handover has been called ONCE (when there is anything older than the newest) with
  //   handover(T* const* chain, uint32_t n)      n >= 2; chain = [the snapshot that was current (if any), then every one popped, oldest first];
  // chain[n-1] is the newest and becomes current; the callee picks the one to take over from (SongSnap: the oldest of the newest's session).
  // No allocation (the chain is on the stack), no lock.
  template <class F>
  bool adopt(F&& handover) {
    T* ch[N + 1]; uint32_t n = 0; T* p; bool changed = false; if (cur_) ch[n++] = cur_;
    while (out_.space() > (n ? n - 1 : 0) && in_.pop(p)) { ch[n++] = p; changed = true; }
    if (!changed) return false;
    if (n >= 2) handover(static_cast<T* const*>(ch), n);
    cur_ = ch[n - 1];
    for (uint32_t i = 0; i + 1 < n; i++) out_.push(ch[i]);
    return true;
  }
  const T* get() const { return cur_; }                      // AUDIO THREAD
  bool pending() const { return !in_.empty(); }              // AUDIO THREAD (U37 P2): a snapshot is published and not yet adopted (the consumer side of the ring: an acquire load)
 private:
  Spsc<T*, N> in_, out_;
  T* cur_ = nullptr;
};

// true when the plain adopt() can be called on a SnapSlot<T>
template <class S, class = void> struct can_adopt_plain : std::false_type {};
template <class S> struct can_adopt_plain<S, std::void_t<decltype(std::declval<S&>().adopt())>> : std::true_type {};

}  // namespace thunder
#endif
