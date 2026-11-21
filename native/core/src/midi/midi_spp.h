// U36 T1: the Song Position Pointer for a PLAY from a row. CONTROL THREAD ONLY: it builds a Seq and steps it (allocates). The result goes to thunder_midi_start(h, spp).
#ifndef THUNDER_MIDI_MIDI_SPP_H
#define THUNDER_MIDI_MIDI_SPP_H
#include "seq/seq.h"
#include "song/song.h"

namespace thunder {

// index.html sppFor: a Seq run from the top with sim = true, stepped to the first step whose playhead is on `row` (or beyond); the SPP is that step's beat event's
// beat * 4 (16ths), rounded and clamped 0..16383. 0 when no step reaches the row or the song has no beat event there. -1 only if memory ran out.
int midi_spp_for(const Song& song, const SeqVars& vars, int row);

}  // namespace thunder
#endif
