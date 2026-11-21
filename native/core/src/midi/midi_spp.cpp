// U36 T1: see midi_spp.h. Moved from native/tests/midi_drive.h (sppFor, U35); the body is the same. Built with exceptions (CMakeLists.txt): the only allocation failure
// it can meet is a std::bad_alloc from the Seq or the event buffer, answered with -1.
#include "midi/midi_spp.h"

#include <algorithm>
#include <cmath>
#include <new>
#include <vector>

namespace thunder {

int midi_spp_for(const Song& song, const SeqVars& vars, int row) {
  try {
    SeqOpts o; o.once = true; o.sim = true; Seq sq(song, vars, SeqMode::Song, o);
    std::vector<SeqEvent> evs(4096); SeqStep st;
    for (int n = 0; n < 100000 && sq.alive(); n++) {
      const int k = sq.step(evs.data(), (int)evs.size(), &st);
      int r = -1; for (int t = 0; t < kSeqTracks; t++) if (st.pos[t].valid && st.pos[t].row > r) r = st.pos[t].row;
      if (r >= row) { for (int i = 0; i < k; i++) if (evs[i].type == SEV_BEAT) return std::max(0, std::min(16383, (int)std::llround(evs[i].v * 4))); return 0; }
    }
    return 0;
  } catch (const std::bad_alloc&) { return -1; }
}

}  // namespace thunder
