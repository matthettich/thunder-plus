// H7: the lock-free hand-over from an OS MIDI-in callback (CoreMIDI's read block: a thread the OS owns and the shell must not stall) to the shell's reader thread. One byte ring per input port, one record per
// packet: [u32 length][i64 OS time][length bytes]. Single producer (the port's callback), single consumer (the reader thread).
//   PRODUCER  push(): no allocation, no lock, no system call, no log; all-or-nothing: a packet that does not fit is dropped and COUNTED (the reader reports it), never a partial record. A packet longer than
//             kMaxRecord is cut (SysEx is dropped by the parser anyway: midi_in.h).
//   CONSUMER  pop(): the reader thread only. It then runs the parser (tpa::MidiInParser), makes the JSON of the `midi.in` event and pushes it to the engine's event queue (a mutex and an allocation: never on the
//             callback's thread). This is the desktop twin of the Android reader thread over AMidi (midi_jni.cpp): MIDI in reaches the PAGE as events, not the core (the core has no MIDI input: live.h NOT IN THIS API).
#ifndef TPD_MIDI_RX_H
#define TPD_MIDI_RX_H

#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>

namespace tpd {

class MidiRxRing {
 public:
  static constexpr uint32_t kMaxRecord = 256;
  bool init(size_t minBytes) {
    size_t c = 64; while (c < minBytes) c <<= 1;
    buf_.reset(new uint8_t[c]());
    cap_ = c; mask_ = c - 1; w_.store(0); r_.store(0); dropped_.store(0);
    return true;
  }
  // PRODUCER (the OS callback thread)
  bool push(int64_t ts, const uint8_t* b, uint32_t len) {
    if (len > kMaxRecord) len = kMaxRecord;
    const size_t need = 12 + (size_t)len;
    const uint64_t w = w_.load(std::memory_order_relaxed);
    if (cap_ - (size_t)(w - r_.load(std::memory_order_acquire)) < need) { dropped_.fetch_add(1, std::memory_order_relaxed); return false; }
    uint8_t head[12]; std::memcpy(head, &len, 4); std::memcpy(head + 4, &ts, 8);
    put(w, head, 12); put(w + 12, b, len);
    w_.store(w + need, std::memory_order_release);
    return true;
  }
  // CONSUMER: the next record; returns its length, or -1 when the ring is empty.
  int pop(int64_t* ts, uint8_t* out, uint32_t cap) {
    const uint64_t r = r_.load(std::memory_order_relaxed);
    if ((size_t)(w_.load(std::memory_order_acquire) - r) < 12) return -1;
    uint8_t head[12]; get(r, head, 12);
    uint32_t len; std::memcpy(&len, head, 4); std::memcpy(ts, head + 4, 8);
    const uint32_t n = len < cap ? len : cap;
    get(r + 12, out, n);
    r_.store(r + 12 + len, std::memory_order_release);
    return (int)n;
  }
  uint64_t dropped() const { return dropped_.load(std::memory_order_relaxed); }
 private:
  void put(uint64_t pos, const uint8_t* s, size_t n) { const size_t a = (size_t)(pos & mask_), f = n < cap_ - a ? n : cap_ - a; std::memcpy(buf_.get() + a, s, f); if (n > f) std::memcpy(buf_.get(), s + f, n - f); }
  void get(uint64_t pos, uint8_t* d, size_t n) const { const size_t a = (size_t)(pos & mask_), f = n < cap_ - a ? n : cap_ - a; std::memcpy(d, buf_.get() + a, f); if (n > f) std::memcpy(d + f, buf_.get(), n - f); }
  std::unique_ptr<uint8_t[]> buf_;
  size_t cap_ = 0, mask_ = 0;
  alignas(64) std::atomic<uint64_t> w_{0};
  alignas(64) std::atomic<uint64_t> r_{0};
  std::atomic<uint64_t> dropped_{0};
};

}  // namespace tpd
#endif
