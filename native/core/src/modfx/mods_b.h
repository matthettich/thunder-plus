// U20: the MODFX module families of unit B (a header of its own, so the shared mods.h does not change).
#ifndef THUNDER_MODFX_MODS_B_H
#define THUNDER_MODFX_MODS_B_H
#include <vector>

#include "modfx/modfx.h"

namespace thunder {
namespace modfx {
void addSpectralMods(std::vector<const Def*>& v);   // spectral, stretch
void addConvolveMods(std::vector<const Def*>& v);   // convolve
}  // namespace modfx
}  // namespace thunder
#endif
