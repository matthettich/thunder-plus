// FX1 (private to core/src/fx): the MODFX slot kind and the song reader's pump / MODFX entries, both in fxsong.cpp.
#ifndef THUNDER_FX_FXMOD_H
#define THUNDER_FX_FXMOD_H
#include "fx/fxhost.h"
#include "modfx/modfx.h"

namespace thunder {

// tfxChain's pump branch and its MODFX branch, from one slot object of the song (the page's normTfx has run on it when the song was loaded: the same clamps are made here).
// true: the entry is made. false: left out, and *why (when given) says FxRefusal: kRefSidechain (a Source / SC FROM other than none: the taps and sends are not wired yet), kRefNotesFrom
// (NOTES FROM: the notes of a track are not routed to a pump effect yet), kRefUnknown (not a pump / MODFX type). `id` is the type's kind (>= kPumpT0).
bool fx_entry_pump(Json o, int slot, int pt, FxEntry* out, FxSlotText* txt, int* why);
bool fx_entry_mod(Json o, int slot, int id, double bpm, FxEntry* out, int* why);

// The Fx object of a MODFX slot (made on the control thread, with the module and its spare; deleted there). seeds: the shared counter.
Fx* fx_make_mod(const FxEntry& e, double sr, modfx::Seeds& seeds);
void fx_mod_set(Fx* f, const FxEntry& e);   // the slot's module values (the worklet's `s.io.p[k] = ...`)

}  // namespace thunder
#endif
