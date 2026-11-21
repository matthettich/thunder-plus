// U36 T2: the host-side entry points of the MIDI scheduler that a C++ host (the live core, SongRenderer::setMidi in PR B) calls on the AUDIO THREAD, in this order
// every block:   midi_begin   ->   (per played step)  midi_sched(h).onStep(...)   ->   midi_end.
// Internal: not installed, not part of the C API (SeqEvent / Seq are not in a C header). thunder_midi_process is these two with the posted fx latency and audible mask kept.
// Implemented in midi_api.cpp, where struct thunder_midi is visible.
#ifndef THUNDER_MIDI_MIDI_HOST_H
#define THUNDER_MIDI_MIDI_HOST_H
#include <cstdint>

#include "midi/midisched.h"
#include "thunder/midi.h"

namespace thunder {

// AUDIO THREAD. Pops and applies the posted commands (a Start posted by the control thread runs here, before the step loop), setBlock(blockSec), then the host's own
// figures for this block: setFxLatency(fxLatSec) and setAudible(audible), which overwrite what thunder_midi_set_fx_latency / _set_audible posted. now = the block's start
// in the core's audio time (frames / rate); blockSec = the block's length. Returns the number of commands applied (>= 0) or THUNDER_MIDI_E_ARG (a NaN, a negative now,
// blockSec outside 0..1, fxLatSec outside 0..2) with nothing applied.
int midi_begin(thunder_midi* h, double now, double blockSec, double fxLatSec, uint64_t audible);
// The scheduler for onStep / onEvent. The caller owns the call order. AUDIO THREAD.
MidiSched& midi_sched(thunder_midi* h);
// AUDIO THREAD. book(now, seqTime, bpm, alive) and publishes the counters and the block's start (the clock map's floor reads it). false on a bad argument (nothing booked).
bool midi_end(thunder_midi* h, double now, double seqTime, double bpm, bool alive);
// ANY thread (relaxed atomic): the `now` of the last midi_end, seconds; negative before the first.
double midi_last_now(const thunder_midi* h);

}  // namespace thunder
#endif
