// H7: the PURE logic of the desktop MIDI device lists: ids, tokens, scan diffs and the bridge's `midi.ports` event. A C++ port of the Android shell's MidiModel.kt (MidiIds, TokenAllocator, MidiRegistry,
// MidiPortsEvent), which the JVM unit tests cover; tests/test_midi_registry.cpp mirrors MidiModelTest. No OS call, no thread, no I/O.
// Ids are the web app's (U36-SPEC 5.3): "n:" + the port's name; a second port with the same id gets " #2" (a third " #3"). The core refuses an id of 96 bytes or more (the whole list with it), so the name is
// shortened (never the suffix) to fit 95 bytes. Tokens: a counter, never reused for another port in this process (midiport.h THE DEVICE TOKEN).
#ifndef TPD_MIDI_REGISTRY_H
#define TPD_MIDI_REGISTRY_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace tpd {

constexpr int kMidiMaxIdBytes = 95;

// One port as the OS lists it. `key` is the OS's own stable number for it for as long as the hardware stays connected (CoreMIDI's kMIDIPropertyUniqueID): it ties a port to its token while it stays listed.
struct MidiPortScan { int64_t key = 0; std::string name; };

struct MidiEntry {
  uint64_t token = 0; int64_t key = 0; std::string id, name;
  bool operator==(const MidiEntry& o) const { return token == o.token && key == o.key && id == o.id && name == o.name; }
};

struct MidiDiff {
  std::vector<MidiEntry> added, removed, changed, kept;
  bool empty() const { return added.empty() && removed.empty() && changed.empty(); }
};

// The rows of a scan in a stable order (by key): (key, id, shown name).
struct MidiRow { int64_t key; std::string id, name; };
std::vector<MidiRow> midiBuildIds(const std::vector<MidiPortScan>& scan);

// `s` cut to at most maxBytes UTF-8 bytes, never inside a character.
std::string midiClampUtf8(const std::string& s, int maxBytes);

class MidiRegistry {
 public:
  MidiDiff update(const std::vector<MidiPortScan>& scan);
  const std::vector<MidiEntry>& entries() const { return current_; }
 private:
  uint64_t next_ = 1;
  std::map<int64_t, uint64_t> tokens_;
  std::vector<MidiEntry> current_;
};

// One port letter as the core reports it (thunder_midiout_port).
struct MidiPortState { int state = 0; std::string id, name; double gear = 0; int clock = 0; };

// The `midi.ports` event: {t, devs:[{id,name}], ins:[{id,name,port}], ports:[{state,id,name,gear,clock} x4], late, sent}. `port` in `ins` is the token `midi.in` events carry.
std::string midiPortsEventJson(const std::vector<MidiEntry>& outs, const std::vector<MidiEntry>& ins, const MidiPortState ports[4], uint64_t late, uint64_t sent);

}  // namespace tpd
#endif
