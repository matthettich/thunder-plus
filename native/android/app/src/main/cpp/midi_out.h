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
//   SENDER    senderMain(): created in engineStart, joined in engineStop. Calls thunder_midiout_pump in a loop and the sink's send and clear (the core calls both from inside the pump, so
//             on this thread: midiport.h THREADS). Send -> PortTable lookup, PortIo::send. A message for a port without timestamps is HELD (sender-owned array, no lock) until its time; the
//             core's CLEAR calls sink.clear(dev), which drops that device's held messages that are not yet due and RETURNS the time up to which nothing was recalled (midiport.h): for a
//             port without timestamps now + 50 us (what it sent, or was about to send under the 50 us rule, was not recalled: the core does not replay it, and releases its note-ons), for a
//             stamped port INT64_MAX (nothing is recalled from the OS: no AMidi flush, it could discard a note-off the core will not replay; what the OS holds plays and the core sends the
//             note-off of every note-on it still holds, midiport.cpp applyClear). When the sender is told to stop it sends every held message at once, in order, before it ends.
//   TABLE     PortTable (token -> PortIo): ONE std::mutex, held for a vector lookup / swap only; NEVER across a send, an open, a close or a destructor. The sender copies the
//             shared_ptr out and releases the lock before it sends; the control thread swaps entries under the lock and lets the OLD shared_ptr die AFTER the unlock. So a port is
//             closed (PortIo destructor) by whichever thread drops the last reference, never under the lock, never while a send on it is running (the sender's copy keeps it alive).
//             The audio thread never takes this mutex (clock_map and thunder_midi_process do not reach the table).
//   SHUTDOWN  engineStop(): the CALLER has already stopped the audio callback (ma_device_uninit); engineStop then waits (bounded) until the core's queue has gone quiet and the sender holds
//             no message (heldCount_, an atomic the sender updates), stops and joins the sender (which flushes what is still held), THEN frees thunder_midiout, THEN
//             thunder_midi: midiport.h's LIFETIME rule (free after the sender AND every clock_map caller have stopped).
#ifndef THUNDER_ANDROID_MIDI_OUT_H
#define THUNDER_ANDROID_MIDI_OUT_H

#include <stdint.h>

#include <atomic>
#include <functional>
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
  // N2 H2: the LIVE host's scheduler. `core` = thunder_live_midi(L): it belongs to the live host, which runs midi_begin / onStep / midi_end inside thunder_live_process itself (U37 P5), so there is no debug song
  // and audioBlock is NOT called: the audio callback calls audioBlockLive (the clock map only) after each process. thunder_midiout_create + devices + ports are CONTROL calls of the live handle's ONE control thread:
  // call this before the live control thread starts, then give the host the control thread with setControlRunner. engineStop frees the thunder_midiout only (never the core's thunder_midi).
  bool engineStartExternal(thunder_midi* core, double sr, TsFn tsFn, void* tsUser);
  // After the audio callback has stopped. Joins the sender, frees the core's objects. Safe when not started.
  void engineStop();
  // N2 H2: every CONTROL method below (setDevices, assign, setClock, setGear, setPlaying, setOutLatencyNs) runs through `r` when one is set: the live host's control thread (LiveEngine::call), so the core's
  // thunder_midi_set_* never run beside a thunder_live_* control call. `r` returns the function's result, or -1000 when that thread is not running (the function then runs here: no engine, nothing to race).
  void setControlRunner(std::function<int(std::function<int()>)> r);
  // The clock map is published again at the next audioBlockLive (after a stream rebuild: the AAudio timestamp pair of a new stream is a new one).
  void remap() { remap_.store(true, std::memory_order_release); }
  int setClock(int port, bool on);
  int setGear(int port, double ms);
  // The host's transport (thunder_midiout_set_playing). Through the control runner.
  int setPlaying(bool on);
  // The same call from the control thread itself (inside a LiveEngine::call task): no runner.
  int setPlayingDirect(bool on);
  // The four ports as JSON for the bridge's `midi.ports` event: {"ports":[{"state":0|1|2,"id":"","name":"","gear":0,"clock":0}x4],"late":N,"sent":N}. Any thread.
  std::string portsJson();
  bool running() const { return running_.load(std::memory_order_acquire); }
  // The whole device list (after every scan / open / close). Saved, and applied now when an engine runs. Returns 0 or a THUNDER_MIDIOUT_E_* code (E_ARG: nothing changed).
  int setDevices(const std::vector<DevInfo>& devs);
  // Port letter 0..3 -> a device id ("" = none). Saved and applied now when an engine runs.
  int assign(int port, const std::string& id);
  // The clock map's floor: the output latency, ns (0 .. 2 s). Applied now when an engine runs.
  // Returns the control call's result (0, or THUNDER_LIVE_E_BUSY from the runner: retry later).
  int setOutLatencyNs(int64_t ns);
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
  // t0: CLOCK_MONOTONIC at the START of the callback (spec 3.4 takes the pair there); 0 = read the clock now (the Linux test).
  void audioBlock(int frames, int64_t t0 = 0);
  // N2 H2: the LIVE host's block. Called after thunder_live_process of the callback. `hostFrame0` = the host frame of the block's first frame (what process was given), `offset` =
  // thunder_live_engine_offset (host frame - engine frame); the core's scheduler talks ENGINE frames, so the map gets engine frame = host frame - offset (U37-P5-NOTES). No lock, no allocation.
  void audioBlockLive(int frames, int64_t hostFrame0, int64_t offset, int64_t t0 = 0);

  // ---- test hooks (the Linux test) --------------------------------------------------------------------------------------------------------------------------
  void setNowFn(NowFn f) { nowFn_ = f ? f : &monoNs; }
  thunder_midi* coreMidi() { return h_; }
  thunder_midiout* coreOut() { return o_; }

  // counters (any thread, relaxed)
  std::atomic<uint64_t> deferred{0}, deferFull{0}, deferFail{0}, shortWrite{0}, mapExact{0}, mapFallback{0}, mapOdd{0}, sendUnknown{0};
  std::atomic<uint64_t> earlyLong{0};   // a message of more than 3 bytes (SysEx) for a port without timestamps: sent at once, up to 20 ms before its time (not held)
  std::atomic<uint64_t> heldCleared{0}; // held messages dropped by a CLEAR
  std::atomic<int64_t> lastOffsetFrames{0};   // written - F_cb at the last exact map: the owner's report says whether it is 0 (UNVERIFIED on a device)

 private:
  friend struct SinkGlue;
  int sendTo(uint64_t dev, const uint8_t* b, int len, int64_t at);   // SENDER
  int64_t clearDev(uint64_t dev);                                    // SENDER (sink.clear): returns the time up to which nothing was recalled
  void flushAllHeld();                                               // SENDER: sends every held message now, in order
  void senderMain();
  int64_t flushDeferred(int64_t now);                               // SENDER: sends the held messages that are due; returns the next due time (INT64_MAX: none)
  bool startLocked(thunder_midi* core, bool external, double sr, TsFn tsFn, void* tsUser);   // ctl_ held
  void publishMap(int64_t coreFrame, int64_t hostFrame, int64_t mono);                      // AUDIO: the clock map (pair math of spec 3.4)
  int viaControl(std::function<int()> fn);
  int setDevicesImpl(const std::vector<DevInfo>& devs);
  int assignImpl(int port, const std::string& id);
  int applyDevicesLocked();                                         // ctl_ held
  int applyAssignLocked(int port);
  void freeDebugLocked(bool force);

  std::mutex ctl_;
  PortTable table_;
  NowFn nowFn_;
  std::atomic<bool> running_{false};
  bool external_ = false;                   // h_ belongs to the live host
  std::atomic<bool> remap_{false};
  std::mutex runMu_;
  std::function<int(std::function<int()>)> runner_;
  bool clock_[4] = {false, false, false, false};   // saved (control)
  double gear_[4] = {0, 0, 0, 0};
  thunder_midi* h_ = nullptr;
  thunder_midiout* o_ = nullptr;
  thunder_midiout_sink sink_;
  double sr_ = 0;
  TsFn tsFn_ = nullptr; void* tsUser_ = nullptr;
  std::vector<DevInfo> devs_;               // saved (control)
  std::string assign_[4];                   // saved (control)
  std::atomic<int64_t> outLatNs_{0};        // saved (control writes; the audio thread reads it for the fallback map)
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
  struct Held { std::shared_ptr<PortIo> io; uint64_t dev; uint8_t b[3]; uint8_t len; int64_t at; };
  static constexpr int kHeld = 256;
  std::unique_ptr<Held[]> held_;
  int nHeld_ = 0;
  std::atomic<int> heldCount_{0};           // nHeld_ as the control thread may read it (engineStop's drain); written only by the sender (and engineStart / engineStop outside its life)
};

// A copy of s the JVM's NewStringUTF accepts (MODIFIED UTF-8): a 4-byte sequence becomes a surrogate pair of two 3-byte sequences, an invalid byte '?', U+0000 the two bytes C0 80.
std::string toJniUtf8(const std::string& s);

MidiHost& host();   // the process's one (the JNI layer's); function-local static

}  // namespace tpa

#endif
