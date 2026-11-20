// THE CHOP DATA of a sliced / synced instrument's pad (docs/port/CHOP-NOTES.md). The page's `applyChop` (index.html) reads two things of a pad that
// the sequencer cannot compute from the song: `loopSecs(pad)` (from the kit's sample layers) and, with SMODE on, `slicePoints` = findHits over the pad's
// samples. The host hands both in, per pad, at set_pad time; Seq reads them. With SMODE off the points are the grid loop * k / n, which Seq computes
// itself (the same expression).
//
// Control thread builds it; it is read-only while a Seq runs (no lock). Lookups allocate nothing.
#ifndef THUNDER_SEQ_CHOP_H
#define THUNDER_SEQ_CHOP_H
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace thunder {

constexpr int kSeqChopPads = 256;        // pad slots (a pad number is one byte: kRenderPads)
constexpr int kSeqChopMaxSlices = 64;    // the instrument's slc is 0..64
constexpr double kSeqChopMaxSecs = 1e6;  // a loop or a point beyond this is refused
constexpr double kSeqChopMinSecs = 1e-6; // a loop shorter than this is refused (the JS never makes one below 0.01 s, or 0 for an empty pad)

class SeqChop {
 public:
  // The pad's loop length in seconds (loopSecs). Finite and > 0 and <= kSeqChopMaxSecs, else false and nothing changes. loop == 0 removes the pad's chop data.
  bool setLoop(int pad, double loop) {
    if (pad < 0 || pad >= kSeqChopPads) return false;
    if (loop == 0) { loop_[pad] = 0; has_[pad] = false; dropHits(pad); return true; }
    if (!(loop >= kSeqChopMinSecs) || !(loop <= kSeqChopMaxSecs)) return false;
    loop_[pad] = loop; has_[pad] = true; return true;
  }
  // findHits' points for a pad cut into n slices (SMODE on). n 2..64, count 1..n, every point finite and in [0, kSeqChopMaxSecs]. Unsorted is accepted (the JS plays what it is
  // given). count == 0 removes. Returns false (nothing changes) for a bad argument; a std::bad_alloc propagates (the C API's E_MEMORY).
  bool setHits(int pad, int n, const double* pts, int count) {
    if (pad < 0 || pad >= kSeqChopPads || n < 2 || n > kSeqChopMaxSlices || count < 0 || count > n) return false;
    if (count == 0) { erase(key(pad, n)); return true; }
    if (!pts) return false;
    for (int i = 0; i < count; i++) if (!(pts[i] >= 0) || !(pts[i] <= kSeqChopMaxSecs)) return false;   // NaN fails both
    const uint32_t k = key(pad, n); size_t lo = lower(k);
    if (lo < hits_.size() && hits_[lo].key == k) hits_[lo].pts.assign(pts, pts + count);
    else { Entry e; e.key = k; e.pts.assign(pts, pts + count); hits_.insert(hits_.begin() + (std::ptrdiff_t)lo, std::move(e)); }
    return true;
  }
  bool has(int pad) const { return pad >= 0 && pad < kSeqChopPads && has_[pad]; }
  double loop(int pad) const { return has(pad) ? loop_[pad] : 0; }
  // the handed-in points for (pad, n), or null; *count their number
  const double* hits(int pad, int n, int* count) const {
    if (pad < 0 || pad >= kSeqChopPads || n < 2 || n > kSeqChopMaxSlices) return nullptr;
    const uint32_t k = key(pad, n); const size_t lo = lower(k);
    if (lo < hits_.size() && hits_[lo].key == k) { *count = (int)hits_[lo].pts.size(); return hits_[lo].pts.data(); }
    return nullptr;
  }
  size_t hitSets() const { return hits_.size(); }

 private:
  struct Entry { uint32_t key = 0; std::vector<double> pts; };
  static uint32_t key(int pad, int n) { return (uint32_t)pad * 65u + (uint32_t)n; }
  size_t lower(uint32_t k) const { size_t lo = 0, hi = hits_.size(); while (lo < hi) { const size_t m = (lo + hi) / 2; if (hits_[m].key < k) lo = m + 1; else hi = m; } return lo; }
  void erase(uint32_t k) { const size_t lo = lower(k); if (lo < hits_.size() && hits_[lo].key == k) hits_.erase(hits_.begin() + (std::ptrdiff_t)lo); }
  void dropHits(int pad) { for (size_t i = hits_.size(); i-- > 0;) if (hits_[i].key / 65u == (uint32_t)pad) hits_.erase(hits_.begin() + (std::ptrdiff_t)i); }
  double loop_[kSeqChopPads] = {};
  bool has_[kSeqChopPads] = {};
  std::vector<Entry> hits_;   // sorted by key
};

}  // namespace thunder
#endif
