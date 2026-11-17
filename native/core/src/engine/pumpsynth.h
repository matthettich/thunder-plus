// U33: the …pump synths (MONO, POLY, COIL, ORBIT, FOLD, GLOSS, TRACT, STRIKE, BOING, X0X, LIFE, FREEZE, WOB) hosted natively: pump's own instruments (the pinned matthettich/pump,
// wasm/pump_synth.cpp: its C interface psyn_*, built STRICT by core/pump/CMakeLists.txt as thunder_pump_synth) behind two things:
//   PumpSynth        an instance that plays MIDI events with a sample offset and renders per block (the interface the live lanes can reuse; PROVISIONAL, the lead's to wire)
//   xeng_psyn        the RENDERED path: PsynCore.render of pump/synth/pump-synth-engine.js (a note into a fresh instance, held, released, rendered until it has died away, folded to mono,
//                    the pad's Level / Drive, the silent tail trimmed, a 3 ms fade), line for line.
// THE JS IS THE SPECIFICATION. What differs from it, and only that: the arithmetic of pump's DSP, which here is a strict build (no -ffast-math) against the web's fast-math wasm; see docs/port/U33-NOTES.md.
// Threads: pump keeps process-global state in pump_synth.cpp (the handle table, the staging area, the two output blocks `psyn_out`): ONE thread at a time may use any of this
// (control thread: make / destroy / xeng_psyn; audio thread: midi / process / out). Not enforced here (as for U18's pump_*).
#ifndef THUNDER_ENGINE_PUMPSYNTH_H
#define THUNDER_ENGINE_PUMPSYNTH_H
#include <cstddef>
#include <cstdint>

namespace thunder {

// ---- the table (generated, pumpsynthtable.inc: from the pinned pump's own parameter tables) ----------------------------------------------------------------------------------------------
constexpr int kPsynTypes = 13;          // psyn_types(): mono poly coil orbit fold gloss tract strike boing x0x life freeze wob (pump_synth.cpp's Type enum order)
constexpr int kPsynMaxParams = 256;     // the most parameters of any instrument is 144 (BOING); a static_assert in pumpsynth.cpp
constexpr int kPsynFlagInt = 2, kPsynFlagBool = 4, kPsynFlagOutput = 8;   // pump::ParamSpec flags the page looks at (rowOf, the HIDE / flags & (8 | 32) filter)
struct PsynParamInfo { const char* sym; float mn, mx, def; int flags; };
struct PsynKind {                       // KINDS[key] of pump-synth-engine.js (what is not in the meta)
  const char* key;                      // the engine key in the page's XENG: "pmono" ...
  const char* name;                     // the plugin's key in the meta: "mono" ...
  int type;                             // the instrument's id in pump-synth.wasm
  double len;                           // the note's default length (seconds)
  double trim;                          // the render's level trim (POLY 1.5)
  bool kit;                             // a drum machine: a pad plays one of the kit's drums
  int drums;                            // a kit's drum count (K.drums, derived by the page from the parameter names: tests/psyn_test checks it against the page)
  bool bpm;                             // WOB: the song's tempo reaches it
};
const PsynKind* psyn_kind(int type);                // nullptr outside 0..kPsynTypes-1
const PsynKind* psyn_kind_by_key(const char* key);  // "pmono" ...; the live twins ("pmonol") are not here: they render as their twin
int psyn_param_count(int type);
const PsynParamInfo* psyn_param(int type, int i);   // nullptr outside the table
int psyn_param_index(int type, const char* sym);    // the plugin's own symbol ("cutoff"); -1 if there is none
// The pad's key for a plugin symbol: the page's `al`: drive, tune and level are the pad's own names, so the plugin's are kept under o_drive, o_tune and o_level.
const char* psyn_pad_key(const char* sym);

// ---- an instance: MIDI in at a sample offset, a block out ---------------------------------------------------------------------------------------------------------------------------------
class PumpSynth {
 public:
  PumpSynth() = default; ~PumpSynth() { destroy(); }
  PumpSynth(const PumpSynth&) = delete; PumpSynth& operator=(const PumpSynth&) = delete;
  // Control thread (allocates): an instance of `type` at `sr` (pump refuses a rate outside 1000..768000 Hz, or a type outside the table: false). `values` are the plugin's values in its
  // parameter order, `n` of them (NaN, or fewer than the instrument has: the default; clamped by pump as a host's setParameterValue is).
  bool make(int type, double sr, const double* values, int n);
  void destroy();
  bool ok() const { return h_ >= 0; }
  int latency() const;                 // what the plugin reports (MONO's oversampler, GLOSS): the output is this many samples late
  void tempo(double bpm);              // the host's tempo (WOB's wobble)
  void file(const float* mono, int n, double sr);   // FREEZE's File source (control thread: copies)
  void set(int i, double v);           // a value of the running instance: written now, applied at the next process()
  // Audio thread: no allocation after make() for every instrument but FREEZE's first note of a built-in source (builtins() warms them) and the event sort of pump's SynthBase::run (see the notes).
  // midi(): status 0x90 note on / 0x80 note off / 0xE0 pitch bend ..., `frame` the sample of the NEXT process() call (0..4095, a later one is clamped by pump); at most 512 events per process(), the rest are dropped by pump.
  void midi(int frame, int status, int d1, int d2);
  void process(int n);                 // n <= 4096 frames (more is cut to 4096 by pump), into out(0) / out(1)
  static const float* out(int ch);     // pump's process-global output blocks (4096 floats each, valid until the next process() of ANY instance)
  static void builtins();              // control thread: builds FREEZE's seven built-in spectra now (pump caches them on first use), so no later note allocates
 private:
  int h_ = -1, type_ = -1;
};
constexpr int kPsynBlock = 4096;       // BLK: the render's block, and pump's most frames per call

// ---- the rendered path -----------------------------------------------------------------------------------------------------------------------------------------------------------------------
struct PsynPad {                        // `P` of PsynCore.render: the pad's values (the plugin's parameters in order) and its own tune / drive / level / vel / kd
  int type = 0;
  double v[kPsynMaxParams];             // the plugin's parameters by index; NaN or any non-finite value: the plugin's default (the JS: typeof v === 'number' && isFinite(v) ? v : specs[i].def)
  double tune = 0, drive = 0, level = 0;   // P.tune (+P.tune || 0), P.drive, P.level (the pad's, not the plugin's o_*)
  double vel = 100;                     // P.vel (Hit): `P.vel || 100`, rounded, 1..127
  double kd = 0;                        // P.kd (a kit's Drum): `P.kd || 0`, rounded, clamped to the kit
  PsynPad();                            // every parameter NaN (its default)
  void setDefaults();                   // every parameter at the table's default (the page's `d()`)
};
struct PsynFile { const float* data = nullptr; double len = 0, sr = 0; };   // FREEZE's File: V.layers[V.frzL].sample (.data, .sr); data == nullptr: none
// One note rendered: `hold` (`hold || V.len || K.len`: pass V.len as `vlen`, 0 for none), `bpm` (> 0: WOB's tempo), `file` (FREEZE only). out == nullptr: only the size the caller's buffer must have (an upper bound
// of the work area, at least 1). Otherwise the finished sound goes into out and its length is returned; 0 if `cap` is too small, 1 (a zero sample) where the JS returns Float32Array(1): an unknown type, or a
// rate pump refuses. No allocation except what PumpSynth::make does (control thread, offline).
size_t xeng_psyn(const PsynPad& pad, double sr, double hold, double vlen, double bpm, const PsynFile* file, float* out, size_t cap);
// PsynCore.latency(k, sr): the latency the instrument reports at `sr` (48000 when sr is 0 / NaN), 0 when pump refuses the instance.
int xeng_psyn_latency(int type, double sr);

}  // namespace thunder
#endif
