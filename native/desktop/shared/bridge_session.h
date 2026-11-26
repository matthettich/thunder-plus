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

#include "bridge_translator.h"
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

class BridgeSession {
 public:
  using Emit = std::function<void(const std::string& lines)>;
  // `recovery`: the recovery copy (null or not enabled: caps.recoveryPut is false and recovery.* answer "no recovery copy").
  BridgeSession(LiveHost& host, Emit emit, RecoveryStore* recovery = nullptr);
  ~BridgeSession();
  // A page message. Returns at once; the work runs on the worker in order. false: refused (over its limit, or the queue is full): the page has been told.
  bool post(const std::string& msg);
  // Waits until every message posted so far has been handled (tests, the shell's selftest).
  void drain();
  void shutdown();   // joins the threads; the host is NOT shut down (the shell owns it)
  bool liveSession() const { return session_.load(); }

 private:
  void worker();
  void pump();
  void run(const std::string& raw);
  void execute(const Call& c, const std::string& type);
  void hello(const Call& c);
  void engineReset();
  void emit(const std::string& json) { emit_(json); }
  void err(const std::string& where, const std::string& msg, bool fatal = false);
  void reply(int id, const std::string& fields);
  void check(const std::string& where, int rc);
  void startPump();
  void announce();
  int fitFlags() const { return (lxpActive_ ? 1 : 0) | (clockFollow_ ? 2 : 0); }

  LiveHost& host_;
  Emit emit_;
  RecoveryStore* rec_;
  PutStore puts_;
  std::atomic<bool> session_{false}, flag_{false}, lxpActive_{false}, clockFollow_{false};
  std::atomic<int64_t> lastRev_{0};

  std::mutex qm_; std::condition_variable qcv_, idleCv_;
  std::deque<std::string> q_; size_t qBytes_ = 0; bool busy_ = false, stop_ = false;
  std::thread worker_;
  std::mutex pm_; std::condition_variable pcv_; bool pumpRun_ = false, pumpStop_ = false, closing_ = false;
  std::thread pump_;
};

}  // namespace tpd
#endif
