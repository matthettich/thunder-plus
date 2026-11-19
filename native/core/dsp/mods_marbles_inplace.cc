// U31: dsp/mods/mod_marbles.cc as it is (included), plus the object made IN storage the host owns. The sources in dsp/ are not edited (they are the web's mods.wasm sources); this unit adds the two
// functions in a native-only translation unit that replaces mod_marbles.cc in the thunder_dsp_mods library. mods_marbles_size / mods_marbles_make_at: memory zeroed, then the constructor, with the
// global placement new (the class's own operator new is the zeroing malloc). The host (core/src/modfx/mod_mods.cpp) makes the storage when a patch is prepared, so Marbles' first block (its seed is
// drawn then) and the "start afresh" after a bad value allocate nothing.
#include <new>
#include "mod_marbles.cc"
size_t mods_marbles_size() { return sizeof(MarblesMod); }
Mod* mods_marbles_make_at(void* mem, float sr, unsigned seed) { memset(mem, 0, sizeof(MarblesMod)); return ::new (mem) MarblesMod(sr, seed); }
