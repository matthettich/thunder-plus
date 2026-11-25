// N2 H1a: fills the live host's pads WITHOUT the page, from a .thunder file's kit, with the core's own C++ pad renderers (engine/engine.h, engine/xeng.h). Docs: docs/port/N2-PLAN.md section 3.
// What it is: the page's ensure(i) (index.html:8856-8862: renderVoice(v, padSR(), st, isSyn(i) ? v.len : 0)) done in the shell. What it is NOT: the page's pad effects other than the four
// the core has (Degrade, Reson, Pitch delay, Pitch reverb: engine.h EngFx::Kind::Other is a no-op), SoundFont / wasm-backed engines, sample layers' decoding, MUT / GEN variations, chop data.
// Pure compute plus thunder_live_alloc: it may run on ANY thread (the JNI caller's), NOT the audio thread. No Android header: a Linux test runs it.
#ifndef TPA_PAD_FILLER_H
#define TPA_PAD_FILLER_H

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "rt/json.h"
#include "thunder/live.h"

namespace tpa {

// The raw text of the top-level member `key` of a JSON object text (a string-aware scan, no allocation of a tree), e.g. "song" of a .thunder file. Empty when absent or the text is malformed.
std::string_view extractMember(std::string_view json, std::string_view key);

struct RenderedPad {
  int slot = 0;
  float* block = nullptr;     // from thunder_live_alloc, `frames` mono floats; ownership moves to the host by thunder_live_pad_take, else PadSet frees it
  int64_t frames = 0;
  int choke = 0; bool syn = false; double rel = 0;   // the kit entry (coreKitV)
  bool kitDone = false;       // thunder_live_pad_kit succeeded (set by the publishing lambda: an E_BUSY retry repeats the lambda, and a pad whose take worked but whose kit did not still needs its kit)
};

struct PadSet {
  std::vector<RenderedPad> pads;
  std::vector<std::string> notes;   // one line per pad the core could not render or rendered without something
  int dry = 0;                      // pads rendered WITHOUT a pad effect the core does not have
  int skipped = 0;                  // pads left empty (engine not rendered by the core, a refusal, no voice)
  double ms = 0;                    // time spent rendering
  PadSet() = default;
  PadSet(const PadSet&) = delete;
  PadSet& operator=(const PadSet&) = delete;
  ~PadSet();                        // frees every block not taken (block == nullptr after a take)
};

// Renders every voice of kit `kitJson` (the `kit` member of a .thunder file) at `sr` Hz. bpm / lpb: the song's (songBpm(), songLpb()). Returns false only for an unreadable kit.
bool renderPads(std::string_view kitJson, double bpm, double lpb, double sr, PadSet* out);

// The kit entry of slot i as the page's coreKitV makes it (index.html:23537): choke, syn (isSyn, index.html:4924: kit.held[i] when the project has no voice-per-track, else i >= 8), rel (relOf, seconds, 0 unless syn).
// false for a slot with no voice. `kit` is the kit object (voices array, optional held).
bool kitEntryOf(thunder::Json kit, size_t slot, int* choke, bool* syn, double* rel);

// The page's engFx rule (index.html:4967, applied in renderVoice index.html:4981-4983): pad effects run on Layers, Blip and every X engine that is not `live`; Bass, Voice, Acid, Machine (and the waves live
// engine) play live in the song, so their pads are DRY on the page too. `engine` is the voice's `engine` string ("" = layers). The core's own twin is anyFxOn in xeng_voice.cpp.
bool padFxApplies(std::string_view engine);

// The page's relOf(V) (index.html:4930), in seconds, for the kit entry's `rel` (an approximation: the engine-specific `rel` of the pump synths is not read).
// Declared here so the JVM-free test can check it.
double relOfVoice(thunder::Json voice);

}  // namespace tpa
#endif
