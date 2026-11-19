// Immutable snapshot swap (U05). The song, the kit and anything else the audio thread only READS is built whole on a control
// thread, handed over as one pointer, adopted by the audio thread at the top of a block, and the one it replaces travels back
// to the control thread to be freed there. The audio thread never allocates, frees or waits.
//   control thread:  publish(new T(...));  collect();      (one thread: the one that calls thunder_post)
//   audio thread:    if (slot.adopt()) { ...the snapshot changed... }   const T* s = slot.get();
// adopt() takes a new snapshot only while there is room to send the old one back, so nothing is ever dropped on the audio
// thread; with a control thread that calls collect() on every post the return ring cannot stay full.
#ifndef THUNDER_RT_SNAP_H
#define THUNDER_RT_SNAP_H
#include "spsc.h"

namespace thunder {

template <class T, uint32_t N = 16>
class SnapSlot {
 public:
  ~SnapSlot() { T* p; while (in_.pop(p)) delete p; while (out_.pop(p)) delete p; delete cur_; }   // after the audio thread has stopped
  bool publish(T* p) { return in_.push(p); }                 // control thread; false: ring full, the caller still owns p
  void collect() { T* p; while (out_.pop(p)) delete p; }     // control thread
  bool adopt() {                                             // AUDIO THREAD
    bool changed = false; T* p;
    while (out_.space() > 0 && in_.pop(p)) { if (cur_) out_.push(cur_); cur_ = p; changed = true; }
    return changed;
  }
  // AUDIO THREAD: adopt() for a snapshot that must take state over from the one that was running. Pops everything pending. The snapshots that
  // never ran are retired at once; handover(prev, next) is called ONCE, with the snapshot that was current before the call (when there was
  // none: the oldest of those popped, which has not run either) and the newest, BEFORE prev goes back to the control thread. Not called when
  // there is nothing to hand over from. No allocation (the retire list is on the stack).
  template <class F>
  bool adopt(F&& handover) {
    T* ret[N]; uint32_t nr = 0; T* newest = cur_; T* p; bool changed = false;
    while (out_.space() > nr && in_.pop(p)) { if (newest) ret[nr++] = newest; newest = p; changed = true; }
    if (!changed) return false;
    if (nr > 0) handover(*ret[0], *newest);
    cur_ = newest;
    for (uint32_t i = 0; i < nr; i++) out_.push(ret[i]);
    return true;
  }
  const T* get() const { return cur_; }                      // AUDIO THREAD
 private:
  Spsc<T*, N> in_, out_;
  T* cur_ = nullptr;
};

}  // namespace thunder
#endif
