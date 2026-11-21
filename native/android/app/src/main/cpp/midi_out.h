// U36 T9: the Android shell's MIDI OUT, the part with NO Android in it (so it is built and tested on Linux: native/tests/test_android_midi_sink.cpp, ctest
// `android_midi_sink`). The Android-only parts (AMidi calls, the JNI entry points, the AAudio timestamp) are in midi_jni.cpp and jni.cpp and reach this file through two
// tiny interfaces: PortIo (one open output port: send bytes) and TsFn (read the AAudio timestamp pair).
//
// WHAT IT OWNS: one thunder_midi (the scheduler) + one thunder_midiout (the port table and send queue) per ENGINE RUN, the sender thread, the token -> port table, the
// shell side of the clock map, and a debug song that proves the chain (see DebugSong in midi_out.cpp). Contract: native/core/include/thunder/midiport.h and midi.h.
//
// THREADS (state them exactly; the core's contract is the reference):
//   CONTROL   every public method marked CONTROL below. They all take `ctl_` (a std::mutex), so however many Java threads call (main thread, the device callbacks, the
//             probe thread's latency update) the core sees ONE producer at a time, as thunder_midi_set_* / thunder_midiout_* control calls demand. `ctl_` is never taken by the
//             sender thread or the audio thread.
//   AUDIO     audioBlock(): the AAudio callback's thread. No lock, no allocation, no JNI, no I/O: thunder_midi commands, the debug sequencer, thunder_midiout_clock_map (wait-free),
//             an optional AAudio timestamp read at most every 20 ms (TsFn: dlsym'd function pointers, called with a stream pointer; it must be bounded and lock-free).
//   SENDER    senderMain(): created in engineStart, joined in engineStop. Calls thunder_midiout_pump in a loop and the sink's send (-> PortTable lookup, PortIo::send).
//   TABLE     PortTable (token -> PortIo): ONE std::mutex, held for a vector lookup / swap only; NEVER across a send, an open, a close or a destructor. The sender copies the
//             shared_ptr out and releases the lock before it sends; the control thread swaps entries under the lock and lets the OLD shared_ptr die AFTER the unlock. So a port is
//             closed (PortIo destructor) by whichever thread drops the last reference, never under the lock, never while a send on it is running (the sender's copy keeps it alive).
//             The audio thread never takes this mutex (clock_map and thunder_midi_process do not reach the table).
//   SHUTDOWN  engineStop(): the CALLER has already stopped the audio callback (ma_device_uninit); engineStop then stops and joins the sender, THEN frees thunder_midiout, THEN
//             thunder_midi: midiport.h's LIFETIME rule (free after the sender AND every clock_map caller have stopped).
#ifndef THUNDER_ANDROID_MIDI_OUT_H
#define THUNDER_ANDROID_MIDI_OUT_H

#include <stdint.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "thunder/midi.h"
#include "thunder/midiport.h"

namespace tpa {

int64_t monoNs();   // clock_gettime(CLOCK_MONOTONIC): the host clock of the sink (AMidi's timestamps are on it). Safe on the audio thread.

// One open output port of a device (the app WRITES to the device's INPUT port). Implemented over AMidi in midi_jni.cpp, by a fake in the tests.
class PortIo {
 public:
  virtual ~PortIo() {}
  // at_ns <= 0: send now. > 0: a CLOCK_MONOTONIC time (only called for a port the shell declared timestamp-capable). Returns 0 on success, < 0 = the device is gone.
  virtual int send(const uint8_t* bytes, int len, int64_t at_ns) = 0;
};

// token -> open port. See TABLE above.
class PortTable {
 public:
  struct Entry { std::shared_ptr<PortIo> io; bool ts = false; };
  // CONTROL
  void put(uint64_t token, std::shared_ptr<PortIo> io, bool ts);   // replaces an existing token's port
  void remove(uint64_t token);
  void clear();
  // SENDER (and any thread): a copy of the entry; io is null for an unknown token.
  Entry find(uint64_t token) const;
  size_t size() const;
 private:
  mutable std::mutex mu_;
  struct Slot { uint64_t token; Entry e; };
  std::vector<Slot> v_;
};

// The AAudio timestamp as the shell reads it: stream position `pos` was presented at CLOCK_MONOTONIC time `ns`; `written` = AAudioStream_getFramesWritten, read in the SAME
// callback. false: no timestamp (AAudio returns an error for a while after start).
struct TsPair { int64_t pos = 0, ns = 0, written = 0; };
typedef bool (*TsFn)(void* user, TsPair* out);

// What the shell lists (Kotlin builds these; ids are the web's, see MidiModel.kt).
struct DevInfo { std::string id, name; uint64_t token = 0; bool ts = false; };

struct DebugSong;

class MidiHost {
 public:
  typedef int64_t (*NowFn)();
  MidiHost();
  ~MidiHost();
  MidiHost(const MidiHost&) = delete;
  MidiHost& operator=(const MidiHost&) = delete;

  // ---- CONTROL ----------------------------------------------------------------------------------------------------------------------------------------------
  // Creates the scheduler, the port table and the sender thread for an engine run (sample rate sr), applies the device list and the four letters saved so far, and starts the
  // sender. tsFn / tsUser: the AAudio timestamp source (null: the fallback map). Returns false (nothing left running) on a bad rate or no memory.
  bool engineStart(double sr, TsFn tsFn, void* tsUser);
  // After the audio callback has stopped. Joins the sender, frees the core's objects. Safe when not started.
  void engineStop();
  bool running() const { return running_.load(std::memory_order_acquire); }
  // The whole device list (after every scan / open / close). Saved, and applied now when an engine runs. Returns 0 or a THUNDER_MIDIOUT_E_* code (E_ARG: nothing changed).
  int setDevices(const std::vector<DevInfo>& devs);
  // Port letter 0..3 -> a device id ("" = none). Saved and applied now when an engine runs.
  int assign(int port, const std::string& id);
  // The clock map's floor: the output latency, ns (0 .. 2 s). Applied now when an engine runs.
  void setOutLatencyNs(int64_t ns);
  // The token -> port table (the sender reads it). attach / detach do NOT change the device list: setDevices does.
  void attach(uint64_t token, std::shared_ptr<PortIo> io, bool ts) { table_.put(token, std::move(io), ts); }
  void detach(uint64_t token) { table_.remove(token); }
  PortTable& table() { return table_; }
  // DEBUG PATH (no live song host yet: U37). Posts thunder_midi_start, a live ping (raw note on / off) and a two-bar song of its own through the real scheduler, pump and sink
  // to port A, with the MIDI clock on. 0 = started; -1 no engine; -2 port A has no listed device; -3 a test is already running; -4 the song did not load / no memory; -5 no clock map yet (the first audio callbacks have not run: try again).
  int debugTest();
  // Housekeeping for a finished test (turns the transport flag off, frees the song). Call from the UI's timer. Returns true while a test runs.
  bool debugPoll();
  // A text for the debug screen: the letters, the device list, thunder_midiout_stats, the map counters. Never empty.
  std::string status();
  int senderTid() const { return tid_.load(std::memory_order_acquire); }
  // Called by the sender thread when it starts / before it ends (the Android shell attaches it to the JVM: AMidiDevice_release, which the last port reference may trigger on that
  // thread, needs an attached thread). Set once, before engineStart.
  static void setSenderThreadHooks(void (*enter)(), void (*leave)());

  // ---- AUDIO ------------------------------------------------------------------------------------------------------------------------------------------------
  // Once per audio callback, AFTER the block was rendered, with the callback's frame count. Counts the frames itself (it must equal the core's audio clock: it starts at 0 when
  // the engine starts). Runs the scheduler's block (thunder_midi_process, or the debug song's steps), then publishes the clock map at most every 20 ms (TsFn pair, else the
  // callback-start fallback). The pair math is spec 3.4: frame0 = pos - (written - F_cb), host_ns0 = ns.
  void audioBlock(int frames);

  // ---- test hooks (the Linux test) --------------------------------------------------------------------------------------------------------------------------
  void setNowFn(NowFn f) { nowFn_ = f ? f : &monoNs; }
  thunder_midi* coreMidi() { return h_; }
  thunder_midiout* coreOut() { return o_; }

  // counters (any thread, relaxed)
  std::atomic<uint64_t> deferred{0}, deferFull{0}, deferFail{0}, shortWrite{0}, mapExact{0}, mapFallback{0}, mapOdd{0}, sendUnknown{0};
  std::atomic<int64_t> lastOffsetFrames{0};   // written - F_cb at the last exact map: the owner's report says whether it is 0 (UNVERIFIED on a device)

 private:
  friend struct SinkGlue;
  int sendTo(uint64_t dev, const uint8_t* b, int len, int64_t at);   // SENDER
  void senderMain();
  int64_t flushDeferred(int64_t now);                               // SENDER: sends the held messages that are due; returns the next due time (INT64_MAX: none)
  int applyDevicesLocked();                                         // ctl_ held
  int applyAssignLocked(int port);
  void freeDebugLocked(bool force);

  std::mutex ctl_;
  PortTable table_;
  NowFn nowFn_;
  std::atomic<bool> running_{false};
  thunder_midi* h_ = nullptr;
  thunder_midiout* o_ = nullptr;
  thunder_midiout_sink sink_;
  double sr_ = 0;
  TsFn tsFn_ = nullptr; void* tsUser_ = nullptr;
  std::vector<DevInfo> devs_;               // saved (control)
  std::string assign_[4];                   // saved (control)
  int64_t outLatNs_ = 0;                    // saved (control)
  std::thread sender_;
  std::atomic<bool> stop_{false};
  std::atomic<int> tid_{0};
  std::atomic<int> prio_{-1};               // the sender's setpriority result: 0 ok, 1 refused, -1 not tried
  // audio-thread owned
  int64_t frames_ = 0, lastMapNs_ = 0; bool mapped_ = false;
  std::atomic<bool> mapReady_{false};   // the first clock_map has been published (the pump drops everything before it)
  // the debug song: control creates, publishes the pointer (release); the audio thread uses it, and when it is finished clears the pointer and sets the state to FINISHED; control frees only then
  std::atomic<DebugSong*> dbg_{nullptr};
  DebugSong* dbgOwned_ = nullptr;
  // sender-owned: messages for a port without timestamps, held until their time (the pump hands everything to the sink up to 20 ms early, as CAP_TS says)
  struct Held { std::shared_ptr<PortIo> io; uint8_t b[3]; uint8_t len; int64_t at; };
  static constexpr int kHeld = 256;
  std::unique_ptr<Held[]> held_;
  int nHeld_ = 0;
};

MidiHost& host();   // the process's one (the JNI layer's); function-local static

}  // namespace tpa

#endif
