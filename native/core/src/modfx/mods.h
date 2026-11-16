// The MODFX module families: each file adds its Defs to the registry (modfx.cpp).
#ifndef THUNDER_MODFX_MODS_H
#define THUNDER_MODFX_MODS_H
#include <vector>

#include "modfx/modfx.h"

namespace thunder {
namespace modfx {
void addStereoMods(std::vector<const Def*>& v);
void addReverbMods(std::vector<const Def*>& v);
void addCvMods(std::vector<const Def*>& v);
void addFilterMods(std::vector<const Def*>& v);
void addMiscMods(std::vector<const Def*>& v);
void addClkMods(std::vector<const Def*>& v);
void addDelayMods(std::vector<const Def*>& v);
void addBasicMods(std::vector<const Def*>& v);   // U21
void addVoiceMods(std::vector<const Def*>& v);   // U21
void addGenMods(std::vector<const Def*>& v);     // U21
void addModsMods(std::vector<const Def*>& v);    // U21 (Marbles, Tides)
}  // namespace modfx
}  // namespace thunder
#endif
