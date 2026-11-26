// H6: the `thunderCore` side of the desktop shells (CORE-BRIDGE v1): a page message in, calls on the live host and events / replies out. The desktop twin of the Android LiveBridge.kt (same message rules through
// bridge_translator.h; same replies and events). One worker thread runs the messages in the order the page sent them; one timer drains the core's events every 20 ms. Nothing here is on the audio thread.
// Transport (the shells): page -> shell: a text message. "B:<base64>" is the bytes of the `put` chunk whose header came just before; anything else is a JSON message. Shell -> page: Emit gets one or more JSON
// objects joined by "\n" (the page's bootstrap splits them).
#ifndef TPD_BRIDGE_SESSION_H
#define TPD_BRIDGE_SESSION_H

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "audio_devices.h"
#include "bridge_translator.h"
#include "input_capture.h"
#include "live_host.h"
#include "put_store.h"
#include "recovery_store.h"

namespace tpd {

// R-1: the limits of what a page message may be (CORE-BRIDGE: `B:` 24 M chars, song.load = maxSongBytes). A message over its limit is refused at once (an err, and an error reply for an ask) and never queued.
constexpr size_t kMaxPutChars = 24000000;            // one `B:` chunk (the page sends ~1 MB ones)
constexpr size_t kMaxSongBytes = (size_t)16 << 20;   // song.load: the hello's maxSongBytes
constexpr size_t kMaxOtherBytes = (size_t)64 << 20;  // any other JSON message (recovery.put carries up to 24 M chars)
constexpr size_t kMaxQueueMsgs = 8192;               // the worker's queue: messages ...
constexpr size_t kMaxQueueBytes = (size_t)256 << 20; // ... and bytes; a full queue refuses (an err, an error reply for an ask)

// H7: the audio / MIDI parts the shell gives the session (all optional: a session without them, as the H6 tests make, answers dev.list with the null device, has no input and caps.midi false).
struct SessionIo {
  std::shared_ptr<AudioEnumerator> devices;     // dev.list, hot-plug; null: the null device only
  const AudioSettingsStore* settings = nullptr; // the saved choice (audio.json); null: not kept
  InputCapture* input = nullptr;                // the audio input and its takes; null: caps.inputs false, rec.* refused
  MidiControl* midi = nullptr;                  // null: caps.midi false (the host must have been given the same manager with setMidi)
  int devWatchMs = 1000;                        // how often the hot-plug watcher looks (tests make it short; 0: no watcher)
};

class BridgeSession {
 public:
  using Emit = std::function<void(const std::string& lines)>;
  // `recovery`: the recovery copy (null or not enabled: caps.recoveryPut is false and recovery.* answer "no recovery copy").
  BridgeSession(LiveHost& host, Emit emit, RecoveryStore* recovery = nullptr, SessionIo io = SessionIo());
  ~BridgeSession();
  // A page message. Returns at once; the work runs on the worker in order. false: refused (over its limit, or the queue is full): the page has been told.
  bool post(const std::string& msg);
  // Waits until every message posted so far has been handled (tests, the shell's selftest).
  void drain();
  // joins the threads; the host is NOT shut down (the shell owns it). timeoutMs >= 0: wait at most that long for the worker (it may be inside a long song load); false then, and the worker is still running: the caller must
  // leave the process without destroying this object or the host (L-2). -1: wait for ever.
  bool shutdown(int timeoutMs = -1);
  bool liveSession() const { return session_.load(); }

 private:
  void worker();
  void pump();
  void run(const std::string& raw);
  void execute(const Call& c, const std::string& type);
  void hello(const Call& c);
  void engineReset();
  // H7
  bool rebuild(const AudioConfig& cfg, std::string* err);   // devMu_ held: stop, new output config, start; the engine events; the input is kept
  void devOpen(const Call& c);
  void devList(int id);
  void recOp(const Call& c);
  void takeFetch(const Call& c);
  void devWatch();
  void devEvents(bool initial);
  void devFallback();
  std::string audioBody();
  void closeInputKeepTake(const char* why);
  void emit(const std::string& json) { emit_(json); }
  void err(const std::string& where, const std::string& msg, bool fatal = false);
  void reply(int id, const std::string& fields);
  void check(const std::string& where, int rc);
  void startPump();
  void announce();
  int fitFlags() const { return (lxpActive_ ? 1 : 0) | (clockFollow_ ? 2 : 0); }

  LiveHost& host_;
  SessionIo io_;
  Emit emit_;
  RecoveryStore* rec_;
  PutStore puts_;
  std::atomic<bool> session_{false}, flag_{false}, lxpActive_{false}, clockFollow_{false};
  std::atomic<int64_t> lastRev_{0};

  std::mutex qm_; std::condition_variable qcv_, idleCv_, doneCv_;
  std::deque<std::string> q_; size_t qBytes_ = 0; bool busy_ = false, stop_ = false, workerDone_ = false, abandoned_ = false;
  std::thread worker_;
  std::mutex pm_; std::condition_variable pcv_; bool pumpRun_ = false, pumpStop_ = false, closing_ = false;
  std::thread pump_;
  // H7
  std::string wantedOut_, wantedIn_;            // what the page chose (kept when a device is gone and the default plays: it is reported, not forgotten)
  AudioInConfig inCfg_;
  std::string lastDevList_;
  std::thread watch_; std::mutex wm_; std::condition_variable wcv_; bool watchStop_ = false;
};

}  // namespace tpd
#endif
