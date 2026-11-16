// The MODFX registry and the shared helpers (the sinT table, nextSeed).
#include "modfx/modfx.h"

#include <cstring>
#include <vector>

#include "modfx/mods.h"
#include "modfx/util.h"

namespace thunder {
namespace modfx {

namespace {
struct SinTab {
  float t[kTS + 1];
  SinTab() { for (int i = 0; i <= kTS; i++) t[i] = (float)js_sin((double)i / kTS * 2 * kPi); }   // SIN[i] = Math.sin(i / TS * 2 * Math.PI)
};
const SinTab gSin;
std::vector<const Def*>& reg() {
  static std::vector<const Def*> v = [] { std::vector<const Def*> r; addStereoMods(r); addReverbMods(r); addCvMods(r); addFilterMods(r); addDelayMods(r); addMiscMods(r); addClkMods(r); return r; }();
  return v;
}
}  // namespace
const float* const kSin = gSin.t;


const Def* find(const char* type) { for (const Def* d : reg()) if (!std::strcmp(d->type, type)) return d; return nullptr; }
int count() { return (int)reg().size(); }
const Def& at(int i) { return *reg()[(size_t)i]; }

}  // namespace modfx
}  // namespace thunder
