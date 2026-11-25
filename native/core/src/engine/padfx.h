// PFX: the pad effects the page runs through its wasm and module libraries (index.html applyFx: txRun, modFxRun, the density / tape / chorus / crush
// calls), offline on the finished pad: the same Fx objects the track host makes (fx/fxhost.h fx_make_tfx), the same MODFX modules (modfx/), driven the
// way the page drives them, with the PAD's own parameter maps (real values, not the track slots' bytes). Control-thread / offline code: it allocates.
// The four JS effects (degrade, reson, pdelay, preverb) are NOT here (u17cores.h); rings and clouds are not here either (docs/port/PFX-NOTES.md).
#ifndef THUNDER_ENGINE_PADFX_H
#define THUNDER_ENGINE_PADFX_H
#include <vector>

#include "engine/engine.h"
#include "rt/json.h"

namespace thunder {

// A fx object of the voice's `fx` list: sets f.kind = Kind::Lib, f.lib and f.lv when `type` is one of the hosted library effects (else leaves the kind alone).
void pad_fx_parse(EngFx& f, Json j);
// applyFx for a Kind::Lib effect: the whole sound `a` (n samples) in, the result (also n samples) in `out`. false: the effect could not be made (the page returns the sound as it is).
// Clamps the controls it is given to the page's row ranges (a hand-built EngFx is as safe as a parsed one) and catches std::bad_alloc: the effect then is not applied (false, counted in pad_fx_oom_count()).
unsigned long pad_fx_oom_count();
bool pad_fx_lib(const EngFx& f, const float* a, size_t n, double sr, double bpm, std::vector<float>& out);
// The types hosted (for tests and tools): the count, and index i's page key.
int pad_fx_count();
const char* pad_fx_key(int i);
// The page's own parameter map of a tfx-class effect (txRun's d.map(f, sr)), for the tests: p12 = the 12 floats' doubles. false for a class without a map.
bool pad_fx_map(int i, const double* v, double sr, double bpm, double* p12);
int pad_fx_nvals(int i);
const char* pad_fx_valkey(int i, int k);

}  // namespace thunder
#endif
