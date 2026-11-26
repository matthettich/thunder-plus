// H6: the desktop shells' LIVE HOST: the engine (thunder_live_*, live.h) behind one audio output, with the loads the page's messages need (song, kit, pads, variations, chop). The core-side C code path is the
// Android shell's: LiveEngine / SongFit / PadFiller / the event JSON are the Android files' own sources (native/android/app/src/main/cpp: plain C++, no Android header), compiled into this library;
// what live_jni.cpp holds as JNI glue and global state (start, teardown, song / kit / pad loads, fit) is ported here as one class. DUPLICATION noted in docs/port/H6-NOTES.md.
// H7 adds here: the output's configuration (device, rate, buffer: AudioConfig), the MIDI service hooks (MidiService: the CoreMIDI side lives in midi_manager.*), the A5 clock logger (rtlog.h) and a fall-back to the
// default device when the chosen one will not open. Audio thread: AudioOut's callback runs LiveEngine::render, then (MIDI on) MidiService::audioBlock (the clock map, no lock, no allocation) and the rtlog
// calls (atomics). Nothing else.
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
#include "thunder/rtlog.h"
#include "rt/json.h"
#include "songfit.h"

namespace tpd {

// H7: what the page chose in Settings > Audio for the OUTPUT (the input is the capture side: input_capture.h). "" = the system default; 0 = the device's own rate / buffer.
struct AudioConfig {
  std::string outId;
  int sampleRate = 0;
  int bufferFrames = 0;
  bool operator==(const AudioConfig& o) const { return outId == o.outId && sampleRate == o.sampleRate && bufferFrames == o.bufferFrames; }
};

// H7: the MIDI side of a running engine, implemented by MidiManager (midi_manager.h). LiveHost calls it in the order of the Android shell's live_jni.cpp start / teardown; every method but audioBlock and the
// any-thread ones is CONTROL (the thread that starts and stops the engine, under LiveHost's life lock).
class MidiService {
 public:
  virtual ~MidiService() = default;
  virtual bool engineAttach(const std::shared_ptr<tpa::LiveEngine>& eng, double sampleRate) = 0;   // after create, BEFORE the engine's threads start. false: MIDI stays off for this engine
  virtual void engineStarted() = 0;                                                                // after the engine's threads started (control runner, MIDI-in reader)
  virtual void engineStopping() = 0;                                                               // the stream is closed, the engine's threads still run: MIDI in off
  virtual void engineDetached() = 0;                                                               // the engine's threads have stopped, the engine is not destroyed yet: the sender is joined, thunder_midiout freed
  virtual void streamRemap() = 0;                                                                  // ANY thread: a new output stream (the clock map is published again at the next block)
  virtual void setOutLatencyNs(int64_t ns) = 0;                                                    // ANY thread (the engine's poller): the output latency floor, applied through the control thread
  virtual void songEnded() = 0;                                                                    // the engine's poller thread: the song ended by itself (the transport flag goes off; waits for the control thread)
  virtual void transport(bool playing) = 0;                                                        // CONTROL, inside a LiveEngine::call task: the MIDI clock boxes' transport flag
  virtual void audioBlock(int frames, int64_t hostFrame0, int64_t engineOffset, int64_t wallNs) = 0;   // AUDIO thread, once per callback after render: no lock, no allocation, no log
};

// H7: the page's MIDI calls (the session talks to this, never to the CoreMIDI classes: a build without MIDI, as Windows is for now, has none).
class MidiControl {
 public:
  virtual ~MidiControl() = default;
  virtual std::string applyCfg(const std::vector<std::string>& outs, const bool clockOut[4], const double gear[4]) = 0;   // `midi.cfg`: "" = applied, else the first refusal's text
  virtual void publish() = 0;                                                                                              // the `midi.ports` event now
  virtual bool running() const = 0;                                                                                        // the MIDI host runs on the current engine
};

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
  // H7. configure() before open(); the readers below are ANY thread, after open() succeeded and until close() has returned (atomics or constants only: the engine's poller reads them).
  virtual void configure(const AudioConfig&) {}
  virtual int periodFrames() const { return 0; }                                         // frames per callback as the device runs them (0: not known)
  virtual int latencyFrames() const { return 0; }                                        // what the output adds between the callback and the speaker (0: not known)
  virtual bool lost() const { return false; }                                            // the stream stopped by itself (the device went away)
  virtual int reroutes() const { return 0; }                                             // how many times the OS moved the stream to another device by itself (the default output changed); the clock map is republished
  virtual std::string deviceId() const { return std::string(); }                         // the id the output opened ("" = not known)
};
using AudioOutFactory = std::function<std::unique_ptr<AudioOut>()>;

struct LoadResult { bool ok = false; int rc = 0; std::string err; int pads = 0, skipped = 0, dry = 0; double renderMs = 0; std::string empty; };

class LiveHost {
 public:
  explicit LiveHost(AudioOutFactory f) : factory_(std::move(f)) { rtlog_reset(&rt_, 0); }
  ~LiveHost() { shutdown(); }
  LiveHost(const LiveHost&) = delete;
  LiveHost& operator=(const LiveHost&) = delete;

  // The output opens at the device's rate, the engine is made at that rate, the threads start, then the stream. The rate, or a negative code (-1 no device, -2 engine, -3 stream start). err says why.
  int start(bool chains, std::string* err);
  void shutdown();
  // H7: the output's configuration for the NEXT start (a change takes a restart: the engine is made at the device's rate). If the configured device will not open, start() opens the default one and
  // fellBack() says so (the requested id stays configured: the page decides whether to forget it).
  void setAudioConfig(const AudioConfig& c) { std::lock_guard<std::mutex> g(life_); cfg_ = c; }
  AudioConfig audioConfig() const { std::lock_guard<std::mutex> g(life_); return cfg_; }
  struct OpenNote { bool fellBack = false; std::string requested, actualName, actualId; };
  OpenNote openNote() const { std::lock_guard<std::mutex> g(life_); return note_; }
  struct AudioInfo { bool open = false; std::string name, id; double sampleRate = 0; int periodFrames = 0, latencyFrames = 0; bool lost = false; };
  AudioInfo audioInfo() const;
  // MIDI (null: no MIDI). Set before start(); not owned.
  void setMidi(MidiService* m) { std::lock_guard<std::mutex> g(life_); midi_ = m; }
  // The transport stop (thunder_live_stop at `at`), and the MIDI clock boxes' flag with it (play() sets the flag itself).
  int stop(int64_t at);
  // A5: the clock-drift logger over the audio callback (rtlog.h). reset: a run begins now. snapshotJson: the Android shell's field names.
  void rtReset();
  std::string rtSnapshotJson() const;
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
  std::atomic<AudioOut*> outRaw_{nullptr};       // the poller's tick reads the output's latency through this; cleared before the output is closed
  std::atomic<MidiService*> audioMidi_{nullptr}; // the audio callback's copy of midi_ (set before the stream starts, cleared after it stopped)
  MidiService* midi_ = nullptr;
  bool midiOn_ = false;
  AudioConfig cfg_;
  OpenNote note_;
  rtlog rt_;
  std::atomic<int> lastReroutes_{0};
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
