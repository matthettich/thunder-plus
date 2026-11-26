// H7: the OS side of desktop MIDI (CoreMIDI on the Mac: midi_coremidi.mm; WinMM / WinRT MIDI on Windows later; a fake in the tests). Small on purpose: everything with a rule in it (ids, tokens, the port table,
// the scheduler, the clock, the parser, the events) is platform neutral (midi_registry.h, midi_manager.h) and tested on Linux.
#ifndef TPD_MIDI_BACKEND_H
#define TPD_MIDI_BACKEND_H

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "midi_out.h"      // tpa::PortIo (the Android shell's plain-C++ MIDI host, compiled in place)
#include "midi_registry.h"
#include "midi_rx.h"

namespace tpd {

class MidiBackend {
 public:
  virtual ~MidiBackend() = default;
  // The ports the app WRITES to (CoreMIDI destinations) and READS from (sources), one row each. key: the OS's stable number for it while the hardware stays connected. Control thread; may take a moment.
  virtual std::vector<MidiPortScan> listOutputs() = 0;
  virtual std::vector<MidiPortScan> listInputs() = 0;
  // An output port. null: it did not open. send() is called from the MIDI sender thread only (never real-time).
  virtual std::shared_ptr<tpa::PortIo> openOutput(int64_t key) = 0;
  // An input: from now the backend's callback thread pushes every packet into `ring` with push() and does NOTHING ELSE (midi_rx.h: no allocation, no lock, no system call, no log). The ring is shared:
  // it stays alive as long as the backend's callback can run. false: it did not open.
  virtual bool openInput(int64_t key, std::shared_ptr<MidiRxRing> ring) = 0;
  // After it returns, no callback runs for this input.
  virtual void closeInput(int64_t key) = 0;
  // Hot-plug: `f` is called from any OS thread when the set of ports changed; it only sets a flag.
  virtual void setChangeHandler(std::function<void()> f) = 0;
  // A packet's OS time (the value pushed into the ring) as host_clock.h ns; 0 = unknown (the reader uses "now").
  virtual int64_t ticksToNs(int64_t ticks) const { return ticks; }
  // Whether a send time is honoured by the OS (informational: listed to the core as THUNDER_MIDIOUT_DEV_TS).
  virtual bool outputStamped() const { return true; }
};

// midi_coremidi.cpp (macOS only: built into the shell): CoreMIDI. null on a failure to make the client.
std::shared_ptr<MidiBackend> makeCoreMidiBackend();

}  // namespace tpd
#endif
