// H7: recording from the audio input, platform neutral (tests/test_input_capture.cpp drives it with a fake input). One object: the capture device, a lock-free ring (input_ring.h) between the OS's capture
// thread and ONE drain thread, per-channel peaks for the page's meters, and the takes.
//   CAPTURE THREAD  AudioIn's callback -> onCapture(): one ring push (input_ring.h: no lock, no allocation, no system call; a full ring drops the callback's frames and counts them). Nothing else.
//   DRAIN THREAD    reads the ring, updates the peaks, appends the armed channels to the take while recording (it allocates: it is not a real-time thread), emits the `rec` meter event ~15 times a second.
//   CONTROL         open / close / arm / start / stop / take: any thread, one mutex (never taken by the capture thread).
// Timestamps: a take carries hostNs0 = host_clock.h time of its first frame = the capture start + (frame / rate) - the input latency (the interface's own delay, when the OS says it), the Electron helper's rule
// (docs/NATIVE-IO-HELPER-PLAN.md section 4): the page places a PATTERN-sync take on the audio clock with it. A dropped callback is filled with silence so a take never loses its place (counted in `gaps`).
#ifndef TPD_INPUT_CAPTURE_H
#define TPD_INPUT_CAPTURE_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "audio_in.h"
#include "input_ring.h"

namespace tpd {

struct Take {
  int id = 0; double sr = 0; int channels = 0; int64_t frames = 0; int64_t hostNs0 = 0; int latencyFrames = 0; int64_t gaps = 0; bool full = false;
  std::vector<std::vector<float>> data;   // one vector per recorded channel
};

constexpr int64_t kMaxTakeBytes = (int64_t)1 << 30;   // a take stops itself here (decision for the lead: ~11 minutes of 28 channels at 48 kHz, 1.5 hours of a stereo pair)

class InputCapture {
 public:
  using Emit = std::function<void(const std::string& json)>;
  explicit InputCapture(AudioInFactory f, Emit emit = nullptr) : factory_(std::move(f)), emit_(std::move(emit)) {}
  ~InputCapture() { close(); }
  InputCapture(const InputCapture&) = delete;
  InputCapture& operator=(const InputCapture&) = delete;

  // Opens and starts the input. false: err says why (the previous input, if any, stays closed).
  bool open(const AudioInConfig& cfg, std::string* err);
  void close();
  bool isOpen() const;
  bool lost() const;                     // the device stopped by itself
  AudioInInfo info() const;
  uint64_t dropped() const { return dropped_.load(std::memory_order_relaxed); }   // frames the capture thread had to drop (ring full)
  uint32_t availableFrames() const;      // frames waiting in the ring (tests)

  // The channels (0-based) the page records and meters. false: no input open, or a channel number the device does not have.
  bool arm(const std::vector<int>& chans, std::string* err);
  bool armed() const;
  bool start(std::string* err);          // begins a take now (armed first)
  // Ends the take: its id, or -1 (err says why). The take stays until freeTake.
  int stop(std::string* err);
  bool recording() const;
  std::shared_ptr<const Take> take(int id) const;
  void freeTake(int id);
  // The meter peaks since the last call (linear, one per ARMED channel; empty when not armed).
  std::vector<float> readPeaks();

 private:
  static void onCapture(void* user, const float* data, int frames, int channels, int64_t hostNs);
  void drain();
  void emitRec();

  AudioInFactory factory_;
  Emit emit_;
  mutable std::mutex m_;
  std::unique_ptr<AudioIn> in_;
  AudioInInfo info_;
  InputRing ring_;
  std::atomic<uint64_t> frame_{0};        // the capture thread's frame counter (device frames since open)
  std::atomic<int64_t> startNs_{0};       // host time of the stream's frame 0: the lower envelope over the callbacks (the helper's rule: input_capture.cpp onCapture)
  double rate_ = 0; bool stamped_ = false; int64_t lastNs_ = 0;   // capture thread only (rate_ is set before the stream starts)
  int64_t curStartNs_ = 0;                // startNs_ when the take's first frame was seen (m_)
  std::atomic<uint64_t> dropped_{0};
  std::thread th_; std::atomic<bool> run_{false};
  std::vector<int> chans_;
  std::vector<float> peaks_;              // drain thread writes, readPeaks reads: under m_
  bool recording_ = false, takeFull_ = false;
  std::shared_ptr<Take> cur_;
  uint64_t curFrame0_ = 0;                // device frame of the take's first frame
  int nextId_ = 1;
  std::map<int, std::shared_ptr<Take>> takes_;
  uint64_t nextFrame_ = 0;                // the device frame the drain expects next (a jump is a gap)
  int64_t lastEmitNs_ = 0;
};

}  // namespace tpd
#endif
