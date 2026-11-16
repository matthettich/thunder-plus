// THE ENGINE FOUNDATION (U22): the pad render of index.html's ENGINE block, line for line: the tables (SIN, RATIOS, CHORDS, MEM, BELL, BELLA, BAR, HAT), `rng`, `blep`,
// `sinT`, the layer oscillator scaffolding (`makeOsc` / `newOsc`), `renderLayer`, `voiceMod` (LFOs, envelopes, mod sequence), `voiceFilter`, `voiceAmp`, `renderLayersOnly` /
// `renderVoice` (drive, level, the pad effect chain, the silent tail trimmed, the 2 ms fade) and `mkVoice` / `freshLayer`'s parameters (EngVoice / EngLayer with their defaults).
// THE JS IS THE SPECIFICATION, quirks included. Same rules as the other ports: every JS Number is a double in the JS's own arithmetic order (the core is built with
// floating-point contraction off), the only float32 values are the ones the JS writes into a Float32Array (the render buffer, SIN), Math.sin / cos / exp / pow / tan / tanh
// are the js_* functions (fdlibm as V8 12.4 has them), Math.round is js_round, Math.min / max keep NaN (and the sign of zero), `x | 0` is js_to_int32.
//
// WHAT IS IN: the layer types sine, tri, saw, square, pulse and the noise family (white, pink, brown, blue, metal, lfsr, dust, grit). Every other type (click, wavetable, FM,
// additive, chip, pd, supersaw, karplus, vosim, formant, chaos, sample, grains, pwm, the wasm-backed ones) is a LATER unit (U23 / U24 / U25 ...): they add a case to
// `Osc::init` and a name to `eng_layer_type`; until then a layer of such a type makes eng_render_voice return kEngUnsupported (the JS has them; silence would be a lie).
// The pad effect chain runs the four JS effects (Pitch delay, Pitch reverb, Resonator, Degrade) through U17's pad_* cores. Every other effect type (EQ, compressor, reverb,
// delay ... = the tfx wasm; rings / clouds / density ... = the macro wasm; the MODFX pad effects = WavesDSP) is NOT READY here, as it is not in the golden's Node (no wasm, no
// WavesDSP loaded: `fxReady` is false, applyFx returns the sound as it is). U14 / U19 / U31 own giving them a home; EngFx::Kind::Other is that no-op.
//
// INTERFACE IS PROVISIONAL (like U19's module block): the caller hands in the buffer, nothing here owns a pad buffer (that decision is the lead's). Rendering allocates
// nothing for a voice without effects (the layers, the filter, the amp and the drive work in the caller's buffer and on the stack); the pad effects return std::vector
// (U17's pad_*), so a voice WITH effects allocates: it is an offline, control-thread render (a pad is rendered once), never an audio-callback one.
#ifndef THUNDER_ENGINE_ENGINE_H
#define THUNDER_ENGINE_ENGINE_H
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "engine/layer_params.h"

namespace thunder {

// ---- tables (index.html ENGINE block, top) ----------------------------------------------------------------------------------------------------------------
constexpr double kBase = 65.40639;     // BASE: C2
constexpr int kSinTS = 4096;           // TS
void eng_init();                       // builds the tables now: call once off the audio thread (the lazy path below stays: the first render builds them if nobody did)
const float* eng_sin_table();          // SIN: Float32Array(TS + 1) of Math.sin(i / TS * 2 * Math.PI); made on first use (call once before a guarded render)
extern const double kRatios[24];       // RATIOS
extern const double kMem[12], kBell[12], kBellA[12], kBar[6], kHat[6];
extern const double kSyncBeats[9];     // SYNC_BEATS
struct EngChord { const char* name; int n; int semis[5]; };
extern const EngChord kChords[14];     // CHORDS
constexpr int kSmpMax = 300;           // SMP_MAX

// ---- helpers ------------------------------------------------------------------------------------------------------------------------------------------
double eng_sinT(double p);             // sinT: the table read with linear interpolation (a read past the table is NaN, as undefined is)
inline double eng_frac(double x) { return x - std::floor(x); }   // frac
double eng_blep(double t, double dt);  // blep
double eng_hash_rnd(double n, double k);   // hashRnd (the S&H LFO values)
struct EngRng {                        // rng(seed): xorshift32, returns s / 2^32
  uint32_t s;
  explicit EngRng(double seed = 7) { reseed(seed); }
  void reseed(double seed);            // (seed >>> 0) || 1
  double next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (double)s / 4294967296.0; }
};
// freshLayer's seed counter: seed = (_seed = (_seed * 1103515245 + 12345) >>> 0) || 1, the product in doubles (it exceeds 2^53: the rounding is the JS's)
struct EngSeedCounter { uint32_t state = 1000; uint32_t next(); };

// ---- parameters (mkVoice, freshLayer) -----------------------------------------------------------------------------------------------------------------
enum class LType : uint8_t { Sine, Tri, Saw, Square, Pulse, White, Pink, Brown, Blue, Metal, Lfsr, Dust, Grit, Click, Wavetable, Pd, Chip, Supersaw, Pwm, Sample, Grains, Other };
LType eng_layer_type(const char* name, size_t n);   // the layer's `type` string; Other for a type this unit does not port (the JS's own names for all of them: see TYPES)
bool eng_type_supported(LType t);

struct EngLayer {                       // freshLayer's fields that render (the rest of the JS object is for other layer types and the UI)
  bool on = false;
  LType type = LType::Sine;
  double a = 0, b = 0, level = 0.8, tune = 0, fine = 0, pAmt = 0, pDec = 60;
  bool adsr = false;                    // mode === 'ADSR' (else AHD)
  double atk = 0.5, hold = 0, dec = 300, sus = 0.6, rel = 150;
  double lpCut = 20000, lpRes = 0, lpEnv = 0, hpCut = 20, hpRes = 0, hpEnv = 0, fAtk = 0.5, fDec = 200;
  double delay = 0, burst = 0, spacing = 10;
  EngU23 u23;                           // U23: wavetable / sample / vint / loop fields and the sample (layer_params.h)
  double seed = 1;                      // the number the JS holds (a uint32 from the counter, or whatever a song says)
};

struct EngFx {                          // one entry of V.fx: the four JS effects' values as the page's own (bits, rate in Hz, time in ms ...); a value the JS leaves undefined is NaN
  enum class Kind : uint8_t { Other, Degrade, Reson, PitchDelay, PitchReverb };
  Kind kind = Kind::Other;
  bool on = true;
  double bits, rate, noise, color, follow, dry, wet;            // degrade
  double note, chord, decay, bright, mode, spread, mix;         // reson (mode and mix also pitch fx)
  double time, fb, pitch, pitch2, tone, size, wow;              // pitch delay / reverb
  EngFx();
};

constexpr int kEngLayers = 8, kEngFx = 8;
struct EngVoice {                       // mkVoice's fields that render (index.html mkVoice: the defaults are those of the JS)
  EngLayer layers[kEngLayers];          // mkVoice pads to 8; a voice with more layers is not representable yet (the loader refuses it)
  int nLayers = kEngLayers;
  EngFx fx[kEngFx];
  int nFx = 0;
  double level = 0, tune = 0, drive = 0, gate = 200, len = 4, tail = 2;
  double gPAmt = 0, gPDec = 60;
  double gLpCut = 20000, gLpRes = 0, gLpEnv = 0, gFAtk = 0.5, gFDec = 200;
  bool gAmpOn = false, gAdsr = true;    // gMode === 'ADSR'
  double gAtk = 0.5, gHold = 0, gDec = 300, gSus = 1, gRel = 1, gAmpAmt = 1;   // (gAmpAmt NaN = the JS's `== null`: 1)
  double lfoRate[3] = {2, 0.5, 8}, lfoDepth[3] = {0, 0, 0}, lfoShape[3] = {0, 0, 0}, lfoSync[3] = {0, 0, 0};
  double envA[3] = {10, 10, 1}, envD[3] = {300, 300, 500}, envS[3] = {0.7, 0, 0.5}, envR[3] = {200, 200, 300};
  double seqRate = 8, seqDepth = 0, seqSync = 0, seq[32] = {};
  double rdst[7] = {}, ramt[7] = {};    // r1dst .. r7dst, r1amt .. r7amt
};

// ---- the render ----------------------------------------------------------------------------------------------------------------------------------------
struct EngRenderOpts {
  double sr = 48000;
  double hold = 0;                      // renderVoice's `hold` (seconds; 0 = none): a synth voice renders with its gate held for that long
  double bpm = 120;                     // songBpm(): the tempo the synced LFOs and the mod sequence read (the JS's fallback is 120)
};
enum EngStatus { kEngOk = 0, kEngTooSmall = -1, kEngUnsupported = -2, kEngFxThrew = -3 };
// How many floats the caller's buffer must hold for this voice (an upper bound: the render is at most this long), or 0 if the voice cannot be rendered.
size_t eng_render_bound(const EngVoice& V, const EngRenderOpts& o);
// renderVoice(V, sr, null, hold) for a voice of this unit's layer types: the finished sound (mono) into `out`, its length in *n. `cap` is the buffer's size in floats.
int eng_render_voice(const EngVoice& V, const EngRenderOpts& o, float* out, size_t cap, size_t* n);
// The same without the pad effects, the trailing trim and the fade: renderLayersOnly's `out` before them (N floats): what tests and later units may want to probe.
size_t eng_layers_len(const EngVoice& V, const EngRenderOpts& o);   // N

struct EngMod { double pitch, level, lpcut, hpcut; };
EngMod eng_voice_mod(const EngVoice& V, double t, double bpm);   // voiceMod

}  // namespace thunder
#endif
