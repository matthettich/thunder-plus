# U25 notes: Plaits / Elements layer types (17 Nov 2026)

Branch `n3/u25` (on `native`). Gate: `native/tools/parity.sh u25` (builds the reference with Node + the web's `waves/dsp/thunder-dsp.wasm`, then runs `test_engine_u25`).

## What is hosted
- Layer types `plaits` (Macro osc) and `elements` (Resonator), natively, through a strict library `thunder_dsp_pe` (`-O3 -fno-fast-math -ffp-contract=off`,
  `native/core/dsp/pe_host.{h,cc}` + the CMake group `pe`). Engine side: `native/core/src/engine/layer_pe.{h,cpp}`, `layer_pe_params.h`, edits in `engine.{h,cpp}`, `engine_json.cpp`.
- Web behaviour copied: the layer is pre-rendered whole through the wasm (Plaits 48 kHz in 48-sample chunks, Elements 32 kHz in 32-sample chunks, note follows the layer pitch
  envelope), then played back with linear interpolation, 0 after the end. The page keeps ONE wasm instance, so `stmlib::Random` (0x21), Plaits' shared RAM and Elements' dither
  persist across a voice's layers: natively one zeroed `Session` per `eng_render_voice`. `thunder_dsp_pe` and `thunder_dsp_w` must not be linked into one program (duplicate upstream symbols).
- No beta factory voices use these types, so 56 "factory-style" voices were defined (all 24 Plaits models, 3 Elements models, stacks, pitch envelopes) plus direct edge cases: 317 cases.
- MSVC builds no DSP libraries: on Windows the two types are unknown and refused (the test checks only that). Not built or run on macOS / Windows / Android yet.

## Refusals
Knobs outside the sliders' range return `kEngUnsupported` (Elements' exciter crashes natively on negative values and the wasm reads garbage memory there). Ranges: Plaits pHarm, pTim, pMor,
pDecay, pLpg 0..1; pFm, pTimEnv, pMorEnv -1..1; Elements knobs 0..1, model 0..2. 4 edge cases (`edge_el_over/under` kinds) are refused, not compared.

## Gate numbers (Linux x86-64, GCC and clang, plain and ASan + UBSan)
- Plumbing: the wasm's own pre-renders are injected (`PeTest` hook): 313 of 313 rendered cases bit-exact. So every remaining difference is the library build (strict vs fast-math) only.
- Tolerance: 317 cases: 73 inside -100 dB, of which 27 bit-exact (listed in the test output and must stay exact); 10 of the 56 factory-style voices inside. Worst inside: -100.29 dB (`u25case141`).
- 240 cases are left out of the tolerance gate BY NAME (committed `kPeOut` table in `tests/golden/engine_u25_native.inc`, no wildcard), by class, each with its printed dB, first differing sample, size:
  - Q 160: raw Plaits pre-render differs by at most one int16 output step (about 3e-5 x layer gain, about -80 dB of a 0.4 peak: the line cannot hold for any flipped sample).
  - F 33: an Elements pre-render is itself beyond the line (float arithmetic, fast-math vs strict).
  - D 45: a Plaits raw step of more than 1 LSB (the model's trajectory parts ways; chaotic).
  - N 2: NaN or non-finite on one side only.
- The test fails on: a case beyond the line and not listed, a stale list entry, a changed class, worse than recorded by more than 1 dB, a plumbing failure, or a native hash differing from the committed one.
- All 313 rendered cases have a committed native hash (strict render made on Linux x86-64), checked on every platform. Not yet seen on macOS, Windows, Android: if a platform differs, report the cases.
- Proposal for the lead: a "one-LSB rule" for int16-output models: allow max difference <= 1 output step instead of -100 dB.

## Third-party patches (`native/core/third_party/prepare.sh`, rows in `PINS.md`)
Found by ASan. Both Elements, one float past / beyond a 257-entry table:
1. `elements/dsp/string.cc:128` `lut_svf_shift` (damping_cutoff >= 256).
2. `elements/dsp/resonator.cc:67` `lut_stiffness` (geometry_ >= 1).
Applied to host builds only (`#ifdef __wasm__` keeps the original read), because the web's wasm is what the existing waves goldens were made with (the first CI run failed the u02 wasm parity job when the patch was in the wasm too). Natively they change output for exactly two cases, `edge_el1_100` and `edge_el2_100` (knobs at 1.0): upstream read bytes beyond the table (garbage that differs between the wasm and a native build), the patch returns the last table entry. Their
committed native hashes were regenerated with the patch (they are new in this PR, no existing golden changed). Plaits needed no patch for the committed cases.

## Allocation in the audio path
`pe_prepare()` (called from `eng_init`) makes a thread-local workspace (Session + 384000-float pre buffer, about 1.8 MB). After that a render allocates nothing. A thread that did not call it allocates on first use.
`rt_harness` has an allocation-guard case (`run_engine_u25`) that renders both types and fails on any allocation.

## Known upstream Plaits reads outside tables (NOT patched; top risk)
Found by a library-level ASan fuzz, may crash natively on VALID slider values. `test_engine_u25_fuzz` is fenced to the clean region; the fence is commented in the test:
- Model 15 (speech): reads one `Frame` past the 210-byte `phonemes_` table (`lpc_speech_synth.cc:136`) at every note. Not fuzzed.
- Models 5 and 9 with pFm != 0: out-of-range table read; SEGV seen in `WaveshapingEngine::Render` (waveshaping_engine.cc:126, `lut_fold` index).
- Model 9 below note -24 (UI tune -48 plus a pitch envelope): same `lut_fold` index.
- Model 13 (wavetable): NULL `wave_map_` entry SEGV at high notes (60+), or with any modulation, or harmonics / timbre / morph above 0.9. Cause not found (`wave_map_` 64 entries vs 256 filled?).
The committed 313 cases are ASan-clean. Fuzz: 360 renders, 0 failed. Decision for the lead: patch these upstream reads (changes output only where memory is garbage) or refuse those regions.

## Least sure
1. The Plaits risk list above (possible native crashes on valid input).
2. Hashes on other platforms (libm differences in Elements; float strictness) not seen.
3. Whether the lead accepts class Q/D left-outs (240 of 317) or wants the one-LSB rule.
4. Reference uses a fresh wasm instance per voice; the page shares one across voices (statics persist), native resets per voice.
