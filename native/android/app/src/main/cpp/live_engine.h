// N2 H1a: the Android shell's side of the LIVE host (thunder_live_*), with no Android header in it so that a Linux test can run it (native/tests/test_android_live_engine.cpp).
//   AUDIO     LiveEngine::render: the body of the device callback. flush-to-zero, then thunder_live_process in pieces of at most max_block. No allocation, no lock, no log, no JNI.
//   CONTROL   ONE thread (the control thread) makes every CONTROL call of live.h: callers hand it a function with call() and wait for the result; it retries E_BUSY after the next audio
//             callback and runs thunder_live_collect every few ms (live.h: "a host that makes no call for a while must call it every few milliseconds").
//   POLLER    ONE thread calls thunder_live_poll and keeps the latest state for the UI; it also hands the host's latency and underrun counts to thunder_live_host_stats every 100 ms.
// Threads: see docs/port/N2-PLAN.md section 5 and live.h THREAD LEGEND. Nothing here is on the audio thread but render().
#ifndef TPA_LIVE_ENGINE_H
#define TPA_LIVE_ENGINE_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <vector>

#include "thunder/live.h"

namespace tpa {

// The poller's STATE record carries THUNDER_LIVE_ST_* (1 playing, 2 stopped, 3 ended); everything above the poller (statusJson, LiveState.kt, the MIDI hook) speaks THUNDER_LIVE_STATE_* (0 idle, 1 playing, 2 ended, 3 held).
// ONE mapping, here (verify-n2 F3): STOPPED -> IDLE (a user Stop shows "stopped"), ENDED -> ENDED (a natural end shows "song ended"). The HELD record sets HELD directly.
inline int stateOfRecord(int st) { return st == THUNDER_LIVE_ST_PLAYING ? THUNDER_LIVE_STATE_PLAYING : st == THUNDER_LIVE_ST_ENDED ? THUNDER_LIVE_STATE_ENDED : THUNDER_LIVE_STATE_IDLE; }

void setFlushToZero();   // arm64 FPCR.FZ / x86 FTZ+DAZ for the calling thread (the audio thread is the system's: set at the top of every callback; cheap)

class LiveEngine {
 public:
  struct Config {
    double sampleRate = 48000;
    int maxBlock = 4096;                      // the most frames one render() piece passes to the core (render() splits a larger callback)
    uint64_t padBytes = (uint64_t)128 << 20;  // opts.max_pad_bytes: a phone host passes 128 MiB (live.h THUNDER_LIVE_DEFAULT_PAD_BYTES)
    uint64_t chainsBytes = (uint64_t)256 << 20;   // opts.chains_bytes, used only when the header has F_CHAINS (GUESS: 256 MiB on a phone; the lead decides in H1d)
    bool chains = true;                       // ask for F_CHAINS when the header has it
  };

  LiveEngine() = default;
  ~LiveEngine() { destroy(); }
  LiveEngine(const LiveEngine&) = delete;
  LiveEngine& operator=(const LiveEngine&) = delete;

  // CONTROL (once). F_EFFECTS | F_MASTER | F_METERS | F_MIDI, plus F_CHAINS when built against a header that has it. false: `err` says why.
  bool create(const Config& c, std::string* err);
  // Starts / stops the control and poller threads (create first; stopThreads before destroy).
  void startThreads();
  void stopThreads();
  // After the audio callback has stopped and the threads are joined.
  void destroy();
  bool created() const { return L_ != nullptr; }
  thunder_live* handle() const { return L_; }

  // AUDIO. `out` is interleaved stereo, `frames` frames. Never allocates, never blocks. Silence when the engine is not created or the handle is dead.
  void render(float* out, int frames, int64_t wallNs);

  // CONTROL from any thread, one at a time or many: `fn` runs on the control thread. E_BUSY (-8) is retried after the next audio callback until timeoutMs; the result is fn's last return.
  // Returns -1000 when the control thread is not running, or the wait timed out BEFORE the control thread started `fn` (the task is dropped: it never runs, nothing changed).
  // Returns -1001 when the wait timed out while `fn` was already RUNNING (or its E_BUSY retries were): UNKNOWN, the work may still be applied (a load may be half or fully in). The caller must not assume "nothing changed".
  int call(std::function<int(thunder_live*)> fn, int timeoutMs = 5000);

  // The host's output latency (frames) and xrun count as the stream knows them; the poller passes them to thunder_live_host_stats.
  void setHostStats(int outLatencyFrames, int underruns) { outLat_.store(outLatencyFrames, std::memory_order_relaxed); under_.store(underruns, std::memory_order_relaxed); }

  // The latest state the poller has seen, as one JSON object (state, row, step, bpm, frames, voices, meters, errors since create, the last records). Any thread.
  std::string statusJson();
  // thunder_live_stats as JSON (control side may call: stats_get is ANY).
  std::string statsJson();
  int64_t audioCalls() const { return calls_.load(std::memory_order_relaxed); }

 private:
  struct Task {
    std::function<int(thunder_live*)> fn;
    std::mutex m; std::condition_variable cv; bool done = false; bool abandoned = false; bool started = false; int rc = 0;   // started / abandoned: under m
    int64_t deadlineNs = 0;
  };
  void controlLoop();
  void pollLoop();
  static int64_t monoNs();

  thunder_live* L_ = nullptr;
  std::shared_mutex lifeM_;                   // destroy() takes it exclusively to free the host; the ANY-thread getters (statsJson) take it shared (verify-n2 F10)
  Config cfg_;
  std::vector<float> l_, r_;                  // the audio thread's de-interleave scratch (max_block each), made at create
  int64_t frame0_ = 0;                        // the running host frame: the audio thread's alone
  std::atomic<int64_t> calls_{0};
  std::atomic<int> outLat_{0}, under_{0};

  std::mutex qm_;
  std::condition_variable qcv_;
  std::deque<std::shared_ptr<Task>> q_;
  std::atomic<bool> run_{false};
  std::thread control_, poller_;

  std::mutex sm_;                             // the status (poller writes, anyone reads)
  struct Status {
    int state = 0; int row = 0, step = 0; double bpm = 0; int64_t clockF = 0; int voices = 0; uint32_t under = 0; int outLat = 0;
    int64_t songRev = -1, padsId = -1; uint32_t lastSeq = 0; uint64_t records = 0, gaps = 0;
    float masterPk[2] = {0, 0};
    int errors = 0, fatal = 0; std::string lastError;
    uint32_t notesLate = 0, runnerSkips = 0;
  } st_;
};

}  // namespace tpa
#endif
