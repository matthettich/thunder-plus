// N2 (verify-n2 F9): a pad's sample block from thunder_live_alloc, owned by a holder the control TASK shares. A task the caller abandoned at a timeout (or the control thread never ran) is destroyed with its
// captures, and the holder then frees the block; once thunder_live_pad_take succeeded the block belongs to the core (owned = false). No Android, no JNI: test_android_live_stress.cpp uses it too.
#ifndef TPA_PAD_BLOCK_H
#define TPA_PAD_BLOCK_H
#include "thunder/live.h"

namespace tpa {
struct PadBlock {
  float* p;
  bool owned = true;   // written by the control thread only (after a successful pad_take); read by the destructor after the task is gone
  explicit PadBlock(float* q) : p(q) {}
  PadBlock(const PadBlock&) = delete;
  PadBlock& operator=(const PadBlock&) = delete;
  ~PadBlock() { if (owned && p) thunder_live_free(p); }
};
}  // namespace tpa
#endif
