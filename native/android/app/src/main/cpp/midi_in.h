// N2 H2: MIDI IN on the Android shell, the part with NO Android in it (tested on Linux: native/tests/test_android_midi_in.cpp, ctest `android_midi_in`). The AMidi reader (midi_jni.cpp) hands
// the raw bytes of one device port to a MidiInParser; each complete message becomes one bridge event {t:"midi.in", f, wallNs, port, bytes} (CORE-BRIDGE 4) through a sink the live engine installs.
// WHAT THE PAGE DOES WITH IT TODAY (index.html onMidi, line 16528) and what the shell therefore forwards: every channel message (notes, CC, bend, aftertouch, program change), the System
// Real-Time bytes Clock / Start / Continue / Stop (F8, FA, FB, FC: `clkIn` follows an external clock), and Song Position Pointer (F2). Left out: Active Sensing (FE) and Reset (FF) (the page drops them),
// System Exclusive (the page ignores everything from F0 up that is not F2 or realtime: "nothing here uses them"), MTC quarter frames (F1), Tune Request and the undefined bytes.
#ifndef THUNDER_ANDROID_MIDI_IN_H
#define THUNDER_ANDROID_MIDI_IN_H

#include <stdint.h>

#include <string>

namespace tpa {

// One complete MIDI message (1..3 bytes).
struct MidiInMsg { uint8_t b[3]; uint8_t len; };

// Turns the byte stream of ONE input port into messages. Running status for channel messages; System Real-Time bytes may arrive inside another message and pass at once without breaking it;
// SysEx and system common messages the page does not use are skipped. A data byte with no status to belong to is dropped (counted). Never allocates; one instance per port, one thread.
class MidiInParser {
 public:
  typedef void (*Fn)(void* user, const MidiInMsg& m);
  void feed(const uint8_t* bytes, int n, Fn fn, void* user);
  void reset() { status_ = 0; need_ = 0; have_ = 0; inSysex_ = false; skipping_ = false; }
  uint64_t sysexSkipped = 0, strayData = 0, otherSkipped = 0;
 private:
  uint8_t status_ = 0;     // the running status (a channel message), 0 = none
  uint8_t cur_ = 0;        // the message being collected
  uint8_t data_[2] = {0, 0};
  int need_ = 0, have_ = 0;
  bool inSysex_ = false, skipping_ = false;   // skipping_: a system common message the page does not use: its data bytes are dropped
};

// True for a message the page's notes / pads depend on (a note-on or note-off): the shell's queue keeps these when it must drop something.
bool midiInIsNote(const MidiInMsg& m);

// {"t":"midi.in","f":F,"wallNs":N,"port":P,"bytes":[a,b,c]}
std::string midiInJson(int64_t f, int64_t wallNs, uint64_t port, const MidiInMsg& m);

// The sink: called from the reader thread for every complete message of port `token`, with the message's CLOCK_MONOTONIC time (0 = unknown: the sink uses now). Set once by the live engine; null drops
// everything (the N0 test path has no MIDI in).
typedef void (*MidiInSink)(uint64_t token, int64_t ns, const MidiInMsg& m);
void setMidiInSink(MidiInSink s);
MidiInSink midiInSink();

}  // namespace tpa
#endif
