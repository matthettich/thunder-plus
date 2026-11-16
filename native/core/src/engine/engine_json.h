// Fills an EngVoice from the JSON a voice has in a song file / the test fixture: the keys of index.html's mkVoice / freshLayer objects. Only what differs from the defaults needs to
// be there (the object starts from the EngVoice defaults, as mkVoice's `Object.assign(defaults, o)` does). Control thread: it reads a parsed document, nothing here is for the audio thread.
#ifndef THUNDER_ENGINE_ENGINE_JSON_H
#define THUNDER_ENGINE_ENGINE_JSON_H
#include "engine/engine.h"
#include "rt/json.h"

namespace thunder {

void eng_layer_from_json(EngLayer& L, Json j);   // keys: on, type, a, b, level, tune, fine, pAmt, pDec, mode, atk, hold, dec, sus, rel, lpCut ... delay, burst, spacing, seed
void eng_fx_from_json(EngFx& f, Json j);         // keys: type, on and the effect's own values (an absent value stays NaN)
// A voice: `v` is the voice's own fields, `layers` the layers that render in order (the layers after them are the defaults, off), `fx` the pad effects.
// Returns false (and leaves V as far as it got) if there are more layers than kEngLayers or more effects than kEngFx.
bool eng_voice_from_json(EngVoice& V, Json v, Json layers, Json fx);

}  // namespace thunder
#endif
