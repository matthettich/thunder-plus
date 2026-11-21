// ring.h: lock-free single-producer / single-consumer rings for the audio input (plan §3.1, §4).
//
// The producer is the OS audio callback (miniaudio's capture thread); the consumer is read() on the helper's JS thread.
// No locks, no allocation and no system calls on the producer side. Each callback becomes one block: a header (the device
// frame its first frame is, and how many frames) in a small header ring, and its samples, interleaved, in the sample ring.
// A block goes in whole or not at all: if either ring is full, the callback's frames are dropped and counted, and the
// headers keep the true device frame numbers, so the reader sees exactly where the gap is (never a silent shift in time).
#pragma once
#include <atomic>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

namespace tio {

// A power-of-two ring of T. push / pop are all-or-nothing; positions are free-running 64-bit counters (never wrap in practice).
template <typename T>
class Spsc {
 public:
  bool init(size_t minCap) {
    size_t c = 1;
    while (c < minCap) c <<= 1;
    buf_ = (T*)calloc(c, sizeof(T));
    if (!buf_) return false;
    cap_ = c; mask_ = c - 1;
    w_.store(0, std::memory_order_relaxed); r_.store(0, std::memory_order_relaxed);
    return true;
  }
  void free_() { ::free(buf_); buf_ = nullptr; cap_ = mask_ = 0; }
  ~Spsc() { free_(); }
  size_t cap() const { return cap_; }

  // producer
  size_t space() const { return cap_ - (size_t)(w_.load(std::memory_order_relaxed) - r_.load(std::memory_order_acquire)); }
  bool push(const T* src, size_t n) {
    uint64_t w = w_.load(std::memory_order_relaxed);
    if (cap_ - (size_t)(w - r_.load(std::memory_order_acquire)) < n) return false;
    copyIn(w, src, n);
    w_.store(w + n, std::memory_order_release);
    return true;
  }
  bool room(size_t n) const { return space() >= n; }   // producer: only the producer adds, so room only grows until its push

  // consumer
  size_t avail() const { return (size_t)(w_.load(std::memory_order_acquire) - r_.load(std::memory_order_relaxed)); }
  const T* peek(size_t i) const { return &buf_[(r_.load(std::memory_order_relaxed) + i) & mask_]; }   // i < avail()
  void pop(T* dst, size_t n) {   // n <= avail()
    uint64_t r = r_.load(std::memory_order_relaxed);
    size_t a = (size_t)(r & mask_), first = n < cap_ - a ? n : cap_ - a;
    if (dst) { memcpy(dst, buf_ + a, first * sizeof(T)); memcpy(dst + first, buf_, (n - first) * sizeof(T)); }
    r_.store(r + n, std::memory_order_release);
  }

 private:
  void copyIn(uint64_t w, const T* src, size_t n) {
    size_t a = (size_t)(w & mask_), first = n < cap_ - a ? n : cap_ - a;
    memcpy(buf_ + a, src, first * sizeof(T));
    memcpy(buf_, src + first, (n - first) * sizeof(T));
  }
  T* buf_ = nullptr;
  size_t cap_ = 0, mask_ = 0;
  alignas(64) std::atomic<uint64_t> w_{0};
  alignas(64) std::atomic<uint64_t> r_{0};
};

struct Block { uint64_t frame0; uint32_t frames; };   // a callback's first device frame, and its frame count

// The input's two rings plus the counters the reader reports.
class InputRing {
 public:
  bool init(uint32_t channels, size_t frames) {
    ch_ = channels;
    return samples_.init(frames * channels) && blocks_.init(4096);
  }
  void free_() { samples_.free_(); blocks_.free_(); }
  uint32_t channels() const { return ch_; }

  // AUDIO THREAD: one callback's frames (interleaved). Returns false when it was dropped (rings full).
  bool put(uint64_t frame0, const float* data, uint32_t frames) {
    Block b{frame0, frames};
    if (!blocks_.room(1) || !samples_.room((size_t)frames * ch_)) { lost_.fetch_add(frames, std::memory_order_relaxed); return false; }
    samples_.push(data, (size_t)frames * ch_);   // samples first, then the header that makes them visible
    blocks_.push(&b, 1);
    return true;
  }

  // READER: the next run of contiguous frames, up to maxFrames, into out (frames * channels floats). Returns the frame
  // count (0: nothing new); *frame0 is the run's first device frame. A gap (dropped callbacks) ends a run: the next read
  // starts after it, at the true frame number.
  uint32_t take(float* out, uint32_t maxFrames, uint64_t* frame0) {
    size_t nb = blocks_.avail();
    if (nb == 0) return 0;
    const Block* b = blocks_.peek(0);
    *frame0 = b->frame0;
    uint64_t next = b->frame0;
    uint32_t got = 0;
    // a block is never split across reads (maxFrames is far larger than a callback)
    while (nb > 0) {
      b = blocks_.peek(0);
      if (b->frame0 != next || (got > 0 && got + b->frames > maxFrames)) break;
      uint32_t n = b->frames;
      samples_.pop(out ? out + (size_t)got * ch_ : nullptr, (size_t)n * ch_);
      blocks_.pop(nullptr, 1);
      got += n; next += n; nb--;
    }
    return got;
  }
  // READER: frames waiting (for sizing the output buffer)
  size_t framesWaiting() const { return samples_.avail() / (ch_ ? ch_ : 1); }
  uint64_t lost() const { return lost_.load(std::memory_order_relaxed); }

 private:
  uint32_t ch_ = 0;
  Spsc<float> samples_;
  Spsc<Block> blocks_;
  std::atomic<uint64_t> lost_{0};
};

}  // namespace tio
