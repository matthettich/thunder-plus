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
void addDelayMods(std::vector<const Def*>& v);
}  // namespace modfx
}  // namespace thunder
#endif
