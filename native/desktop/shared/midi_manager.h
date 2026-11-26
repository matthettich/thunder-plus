// H7: desktop MIDI, platform neutral. The desktop twin of the Android shell's MidiPorts.kt + LiveMidi.kt + midi_jni.cpp's reader, over a MidiBackend (CoreMIDI) instead of Android's MidiManager / AMidi:
//   * keeps the port lists (ids "n:" + name, tokens: midi_registry.h) current on every hot-plug and every 3 s, opens new ports, closes gone ones;
//   * attaches output ports to the Android shell's tpa::MidiHost (compiled in place: the core's own MIDI pump, sender thread, token table; it is the one thing that sends), gives it the device list and the
//     four letters (`midi.cfg`), runs its clock map and transport flag (the MidiService calls of LiveHost);
//   * reads MIDI in: each input's callback thread pushes packets into a lock-free ring (midi_rx.h); ONE reader thread parses them (tpa::MidiInParser: running status, real-time bytes, SysEx dropped) and
//     hands each message to the page as a `midi.in {f, wallNs, port, bytes}` event through the engine's event queue (a mutex: never on the OS's thread), with the packet's own OS time;
//   * builds the `midi.ports` event.
// THREADS: scan thread (ports, every 3 s or on a hot-plug flag), reader thread (rings -> events), the host's sender thread (tpa::MidiHost: core pump -> PortIo::send), CONTROL calls from LiveHost / the
// session (apply, publish), and the AUDIO thread's one call, audioBlock() -> tpa::MidiHost::audioBlockLive (the clock map: no lock, no allocation).
#ifndef TPD_MIDI_MANAGER_H
#define TPD_MIDI_MANAGER_H

#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "live_host.h"
#include "midi_backend.h"
#include "midi_in.h"

namespace tpd {

class MidiManager : public MidiService, public MidiControl {
 public:
  using Emit = std::function<void(const std::string& json)>;
  MidiManager(std::shared_ptr<MidiBackend> backend, Emit emit);
  ~MidiManager() override;
  MidiManager(const MidiManager&) = delete;
  MidiManager& operator=(const MidiManager&) = delete;

  // ---- MidiService (LiveHost) ----
  bool engineAttach(const std::shared_ptr<tpa::LiveEngine>& eng, double sampleRate) override;
  void engineStarted() override;
  void engineStopping() override;
  void engineDetached() override;
  void streamRemap() override;
  void setOutLatencyNs(int64_t ns) override;
  void songEnded() override;
  void transport(bool playing) override;
  void audioBlock(int frames, int64_t hostFrame0, int64_t engineOffset, int64_t wallNs) override;

  // ---- the page ----
  // `midi.cfg`: the four letters, clock boxes, Gear delays. "" = applied; else the first refusal's text.
  std::string applyCfg(const std::vector<std::string>& outs, const bool clockOut[4], const double gear[4]) override;
  // The `midi.ports` event now (any thread); the scan thread also sends it on every change and every 3 s.
  void publish() override;
  std::string portsEvent();
  bool running() const override;              // the MIDI host runs on the current engine
  // One scan now (tests, hot-plug): lists, opens, closes, tells the core. Returns true when something changed.
  bool rescan();
  uint64_t inDropped() const;                 // packets the OS callbacks had to drop (ring full)
  uint64_t inMessages() const { return inMsgs_.load(); }

 private:
  struct InPort { MidiEntry e; std::shared_ptr<MidiRxRing> ring; tpa::MidiInParser parser; };
  void scanMain();
  void readerMain();
  void pushList();                            // the core's device list = the registry's outputs that are open
  static int64_t nowFn();

  std::shared_ptr<MidiBackend> be_;
  Emit emit_;

  std::mutex mu_;                             // the registries and port tables (control + scan)
  MidiRegistry regOut_, regIn_;
  std::map<uint64_t, bool> outOpen_;          // token -> attached to the host's table
  std::map<uint64_t, std::shared_ptr<InPort>> in_;
  std::mutex rxMu_;                           // the reader's pass over in_
  std::vector<std::shared_ptr<InPort>> rx_;   // a copy of in_ for the reader (rxMu_)

  std::mutex engMu_;
  std::shared_ptr<tpa::LiveEngine> eng_;      // the engine MIDI in pushes to
  std::atomic<bool> on_{false};
  std::atomic<int64_t> lastLat_{-1};

  std::thread scan_, reader_;
  std::mutex wakeMu_; std::condition_variable wakeCv_; bool wake_ = false, stop_ = false, scanStarted_ = false;
  std::atomic<bool> readerRun_{false};
  std::atomic<uint64_t> inMsgs_{0};
  std::string lastPortsJson_;
};

}  // namespace tpd
#endif
