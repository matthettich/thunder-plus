// U31: dsp/mods/mod_tides.cc as it is (included), plus the object made in storage the host owns (see mods_marbles_inplace.cc).
#include <new>
#include "mod_tides.cc"
size_t mods_tides_size() { return sizeof(TidesMod); }
Mod* mods_tides_make_at(void* mem, float sr) { memset(mem, 0, sizeof(TidesMod)); return ::new (mem) TidesMod(sr); }
