// H6: the desktop shells' LIVE HOST: the engine (thunder_live_*, live.h) behind one audio output, with the loads the page's messages need (song, kit, pads, variations, chop). The core-side C code path is the
// Android shell's: LiveEngine / SongFit / PadFiller / the event JSON are the Android files' own sources (native/android/app/src/main/cpp: plain C++, no Android header), compiled into this library;
// what live_jni.cpp holds as JNI glue and global state (start, teardown, song / kit / pad loads, fit) is ported here as one class. DUPLICATION noted in docs/port/H6-NOTES.md.
// NOT here (H7): MIDI, device choice, buffer size, stream reopen on a lost device, the A5 clock logger. Audio thread: AudioOut's callback runs LiveEngine::render only (flush-to-zero, thunder_live_process).
#ifndef TPD_LIVE_HOST_H
#define TPD_LIVE_HOST_H

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "live_engine.h"
#include "rt/json.h"
#include "songfit.h"

namespace tpd {

// One stereo f32 output. The shell's miniaudio device (audio_miniaudio.cpp), a real-time null device (audio_null.cpp: CI, headless), or a test's hand-driven one.
class AudioOut {
 public:
  using Callback = void (*)(void* user, float* interleavedStereo, int frames, int64_t wallNs);
  virtual ~AudioOut() = default;
  virtual bool open(Callback cb, void* user, double* sampleRate, std::string* err) = 0;   // does not start; the device's own rate
  virtual bool start(std::string* err) = 0;
  virtual void close() = 0;                                                              // stops; no callback runs after it returns
  virtual int state() const { return 0; }                                                // 3 = started (miniaudio's numbering)
  virtual std::string name() const { return "audio"; }
};
using AudioOutFactory = std::function<std::unique_ptr<AudioOut>()>;

struct LoadResult { bool ok = false; int rc = 0; std::string err; int pads = 0, skipped = 0, dry = 0; double renderMs = 0; std::string empty; };

class LiveHost {
 public:
  explicit LiveHost(AudioOutFactory f) : factory_(std::move(f)) {}
  ~LiveHost() { shutdown(); }
  LiveHost(const LiveHost&) = delete;
  LiveHost& operator=(const LiveHost&) = delete;

  // The output opens at the device's rate, the engine is made at that rate, the threads start, then the stream. The rate, or a negative code (-1 no device, -2 engine, -3 stream start). err says why.
  int start(bool chains, std::string* err);
  void shutdown();
  bool active() const { return eng_.get() != nullptr; }
  double sampleRate() const { return sr_.load(); }
  std::string audioName() const;

  LoadResult songLoad(const std::string& raw);            // song.load {S, vars, rev}: the whole text to thunder_live_song_publish
  LoadResult kitLoad(const std::string& raw);             // kit.load {kit}: the pads the core can render are rendered and published
  // SongFit over the last song and kit (flags: bit 0 lxp on, bit 1 clock follow). false: no song yet (json then says so).
  bool fit(int64_t rev, int flags, tpa::FitResult* out, std::string* json);
  int padTake(int pad, const uint8_t* l, const uint8_t* r, int frames, double rate);
  int padRemove(int pad);
  int padsPublish();
  int varSet(int ii, int kind, int num, int pad, const uint8_t* l, int frames, double rate);
  int padChop(int pad, double loopSecs, int nSlices, const std::vector<double>& framePts);
  // Every other control call: runs `fn` on the control thread (E_BUSY retried). -2 when no engine.
  int call(std::function<int(thunder_live*)> fn, int timeoutMs = 5000);
  int play(int mode, int row, int track, int sect, int phrase, int hasLoop, int y0, int y1, int64_t t0, int hasSeed, int seed, int pdc, uint64_t onlyBits, int hasOnly);

  std::string drainEvents();
  double load() const { return load_.load(std::memory_order_relaxed); }
  bool chainsOn() const;
  static bool hasLxr();
  uint32_t liveCaps() const;
  std::string statsJson() const;
  std::string statusJson() const;
  std::shared_ptr<tpa::LiveEngine> engine() const { return eng_.get(); }

 private:
  static void audioCb(void* user, float* out, int frames, int64_t wallNs);
  void teardownLocked();

  // the Android's engine_slot.h in two lines: a copy under a small mutex
  struct Slot {
    mutable std::mutex m; std::shared_ptr<tpa::LiveEngine> p;
    std::shared_ptr<tpa::LiveEngine> get() const { std::lock_guard<std::mutex> g(m); return p; }
    void set(std::shared_ptr<tpa::LiveEngine> q) { std::lock_guard<std::mutex> g(m); p = std::move(q); }
    std::shared_ptr<tpa::LiveEngine> take() { std::lock_guard<std::mutex> g(m); auto r = p; p.reset(); return r; }
  };
  AudioOutFactory factory_;
  Slot eng_;
  std::atomic<tpa::LiveEngine*> audioEng_{nullptr};
  std::unique_ptr<AudioOut> out_;
  std::atomic<double> sr_{0};
  std::atomic<double> load_{0};
  mutable std::mutex life_;
  std::mutex loadM_;
  int64_t rev_ = 0;
  std::vector<int> lastSlots_;
  std::shared_ptr<std::string> songText_, kitText_;
  std::shared_ptr<thunder::JsonDoc> kitDoc_;
  double bpm_ = 120, lpb_ = 4;
};

}  // namespace tpd
#endif
