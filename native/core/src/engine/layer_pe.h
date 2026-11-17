// U25: the layer types "plaits" (Macro osc) and "elements" (Resonator): index.html's plaitsPre / elementsPre (a pre-render of the whole layer through the Mutable Instruments library in 48 kHz /
// 32 kHz blocks, the note following the layer's pitch envelope) and makeOsc's playback of the pre-render (linear interpolation, zero after the end). The library is thunder_dsp_pe
// (core/dsp/pe_host.h): the upstream Plaits / Elements built STRICT, against the web's fast-math thunder-dsp.wasm gated at -100 dB (docs/port/U25-NOTES.md).
// ONE Session per rendered voice: the page keeps one thunder-dsp instance whose statics (stmlib::Random, Plaits' shared RAM, Elements' dither) go on from one layer to the next, so the layers of a
// voice share a Session that starts zeroed, as a fresh instance does. The workspace (Session + the pre-render buffer, up to 1.5 MB) is per thread and made once: eng_init() makes it for the calling
// thread (pe_prepare); a thread that renders a Plaits / Elements layer without it makes it on its first use (that first render allocates; every later one does not).
// Without the library (MSVC builds no DSP libraries) the types are not supported: eng_layer_type gives Other and a voice with such a layer is refused (kEngUnsupported).
#ifndef THUNDER_ENGINE_LAYER_PE_H
#define THUNDER_ENGINE_LAYER_PE_H
#include <cstddef>
#include <vector>

#include "engine/engine.h"
#include "rt/json.h"

namespace thunder {

bool pe_available();                                   // the library is built into this core
void pe_prepare();                                     // makes the calling thread's workspace (allocates): call once off the audio thread
bool pe_is(LType t);                                   // plaits or elements
LType pe_layer_type(const char* name, size_t n);       // "plaits" / "elements" (Other when the library is not built, or for any other name)
bool pe_layer_ok(const EngLayer& L);                   // false: a knob outside the range the app's sliders have (non-finite too), or an Elements model outside 0..2: refused, see layer_pe.cpp
void eng_layer_pe_from_json(EngLayer& L, Json j);      // pEng pHarm pTim pMor pDecay pLpg pFm pTimEnv pMorEnv pStrike pAux, eModel eStrength ... eSpace

// TEST HOOK (control thread only, off by default): `inject` makes the layers use the given pre-renders in order instead of calling the library (the plumbing gate: the web's own pre-renders through this core's
// resampling, filters, envelopes and voice stages must give the web's samples bit for bit); `capture` receives a copy of every pre-render the library made.
struct PeTest { const std::vector<std::vector<float>>* inject = nullptr; std::vector<std::vector<float>>* capture = nullptr; size_t next = 0; };
void pe_test_hook(PeTest* t);

struct PeUse {                                         // one per eng_render_voice: the first layer that needs the Session starts it
  bool started = false, bad = false;                   // bad: a pre-render length the JS cannot allocate (new Float32Array(negative) throws): the voice is refused
};
// The playback of one pre-rendered layer (makeOsc's `case 'plaits': case 'elements'`).
struct PeOsc {
  const float* d = nullptr; size_t n = 0; double k = 0, pos = 0;
  void init(PeUse& use, const EngLayer& L, const EngVoice& V, double sr, double dur, double pA, double ptau);
  double s();
};

}  // namespace thunder
#endif
