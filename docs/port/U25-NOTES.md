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

## Refusals (`pe_layer_ok`, `PeOsc::init`)
Knobs outside the sliders' range (or non-finite) return `kEngUnsupported` (Elements' exciter crashes natively on negative values and the wasm reads garbage memory there). Ranges: Plaits pHarm, pTim, pMor,
pDecay, pLpg 0..1; pFm, pTimEnv, pMorEnv -1..1; Elements knobs 0..1, model 0..2. 4 edge cases (`edge_el_over/under` kinds) are refused, not compared.
A layer whose NOTE (36 + tune + voice tune + fine/100 + pitch envelope) is NaN, infinite or beyond +-1e6 semitones is refused too (the upstream code indexes pitch tables with it). The page's reach is about -100 .. 250.
No other region is refused: every Plaits model and every Elements model is memory-safe over the whole of its ranges (next section). Regions refused for lack of a fix: NONE.

## Memory safety (17 Nov, the lead's decision 1): all fences removed
`test_engine_u25_fuzz` has no fenced region: all 24 Plaits models and 3 Elements models over the full slider ranges (knobs at 0, 1, 0.5, 0.9, 0.99, 1e-6, 0.999999 and random, FM / envelope amounts -1..1), notes from far below to far above the page's reach
(layer tune +-96, fine +-100, pitch amount +-96, voice tune), every sample rate, NaN / infinite / huge values in tune, fine, pitch amount, decay, gate and length, and one voice in 40 with a knob outside its range (must be refused). Under ASan + UBSan
(`-DTHUNDER_SANITIZE=ON`) each render has a time limit (`FUZZ_LIMIT_S`, default 60 s; the slowest render took 0.2 s). Result: 40000 renders (seed 25251) and 40000 (`FUZZ_SEED=777`), 0 failures, every model at least 2000 times. The default run (ctest) is 2400 cases.
The five upstream defects found, each patched in `third_party/prepare.sh` for HOST builds only (`#ifdef __wasm__` keeps the original: the web's wasm is what the existing goldens were made with), with file, line, table size and trigger in the comment there:
1. `waveshaping_engine.cc` (models 5 and 9 of the old list; really model 9): `lut_fold` index. Trigger: the slope oscillator at a tiny f0 (note below about -24; the engine clamps at -119) returns 1e8 and more, ws_index leaves int, mix * gain + 0.5 is far outside the table. ws_index clamped to +-2e9, index to the range whose table index is 0..512 (the table has 516 floats).
2. `lpc_speech_synth.h` (model 15): `PlayFrame(frames, i, interpolate = false)` reads `frames[i + 1]` too (weight 0); `phonemes_` is 15 x 14 = 210 bytes, consonant 14 reads frame 15. Without interpolation the second frame is the first (the same value).
3. `wavetable_engine.cc` (model 13), THE CAUSE OF THE NULL `wave_map_` CRASH: not the hosting. The wasm wrapper (`dsp/w_plaits.cc`) and `pe_host.cc` make the same calls with the same 16384-byte allocator. `wave_map_` holds 4 banks x 64 = 256 entries (upstream allocates 64 and writes 256: harmless inside the shared buffer, now 256).
   With a quantised position (harmonics above 4/7) x, y or z reaches EXACTLY 7.0, so the next corner is 8 (z: -1 after the fold): index 256 and up (seen: x=0 y=8 z=3 idx 256; harmonics 0.999999, morph 0.99, fm 1) is read with a weight of 0. The slot is NULL (zero memory); the wasm reads its low linear memory there, harmlessly. x, y to 0..7 and z to 0..3 at the read: no output changes.
4. `wave_terrain_engine.cc` (model 5): `aux = Sine(1 + 0.25 * aux_s)`; the path oscillator overshoots 1 by 0.1 % (y = -1.001), so the phase is about -0.002 and below and the index is -1 (4 bytes before `lut_sine`). A phase at or below -1/512 (and NaN) becomes 0.
5. `sine_oscillator.h` `SineNoWrap` (model 21 bass drum in sustain mode): `lut_sine` has 641 floats; a NaN phase (the resonator at about note -30 and below) read 0 bytes after it. NaN / at or below -1/512 become 0, 1.25 and above become the largest float below it.
Each clamp leaves every index inside its table as it was: the committed native hashes of all 313 cases did not change with patches 1 to 5 (checked: `--write-native` gives the same 313 hashes). Patches 3 to 5 (and the two Elements ones) change nothing a render could depend on except where memory was outside a table.

## The gate (17 Nov; the lead's decision 2)
- Plumbing: the wasm's own pre-renders are injected (`PeTest` hook): 313 of 313 rendered cases bit-exact, so the only difference left is the library build (strict vs fast-math). Said in the test header.
- 317 cases: 73 inside -100 dB (27 bit-exact, must stay exact; worst inside -100.29 dB, `u25case141`; 10 of the 56 factory-style voices inside).
- THE ONE-STEP RULE (named rule for the class "Plaits int16 output", in the header of `test_engine_u25.cpp` with the reason: the strict and fast-math builds round a float differently at the int16 conversion): a case passes when every sample of the RAW int16 stage
  (the Plaits pre-render, scaled by 1/32768, before any resampling or filter) is within one step of the wasm's, its Elements layers are within the line and there is no NaN; the count of raw samples that differ is printed. 160 cases are GATED BY THE RULE
  (35067 raw int16 samples differ in all, at most 4343 in one case). These were group Q.
- Left out by name (committed `kPeOut`, 80 cases, each printed): D 45 (a raw Plaits step above one step: numbers printed), F 33, N 2.
- F (33): an Elements pre-render itself beyond the line. The largest is `edge_el2_1010` (String model, -1.4 dB of the pre-render's peak: a blow-up, chaotic); the rest lie between -62.8 and -99.7 dB (median -85.7 dB). In EVERY case the first differing pre-render sample is in block 0
  (sample 0 to 5, a tiny float difference), and the error then GROWS with time (the figure over the first 3200 samples is lower than the worst in 25 of 33 cases; equal in 8). Each F case prints its worst dB, first differing sample and early figure.
- N (2): `edge_el1_100` and `edge_el2_100`: the NaN is on the WASM side (the wasm pre-render is NaN from sample 420 / 418), none native. These are exactly the two cases where the Elements table-read patch (`lut_svf_shift[257]`, `lut_stiffness[257]`) changes output: the wasm reads bytes past the table
  (here values that blow the String resonator up), native reads the last table entry. So not a native bug: the wasm's out-of-table read. They stay out by name.
- The test fails on: a case beyond the line, outside the rule and not listed; a stale list entry; a changed class; worse than recorded by more than 1 dB; a plumbing failure; a native hash differing from the committed one; a rule-gated case with a raw step above one.
- All 313 rendered cases have a committed native hash (strict render made on Linux x86-64), checked on every platform. Not yet seen on macOS, Windows, Android.

## Statics (the lead's decision 3: native resets per voice, the page shares ONE wasm instance across renders)
Statics of the wasm module that these types use: `w_plaits.cc` (shared_buffer, voice, patch, mods, frames, out_buf, aux_buf), `w_elements.cc` (reverb_buffer, part, ps, zeros, `rng_state` for the dither), and `stmlib::Random::rng_state_` (0x21 at the start; Plaits noise, particles, swarm, hi-hat, snare, string, Elements exciters).
`pl_init` / `el_init` re-initialise the voice / part but NOT the Random state or the dither state. MEASURED with the web's thunder-dsp.wasm in Node (one instance, same layer rendered twice; `/tmp`-script, 400 blocks, strike on): a SECOND render in one instance differs from the first
for Plaits models 16 (swarm), 17 (noise), 18 (particle), 19 (string), 22 (snare), 23 (hi-hat) (about 19000 of 19200 samples, max difference 1.2) and for all three Elements models (about 12700 of 12800, max 0.02 to 0.03); every other Plaits model (0 to 15, 20, 21) is identical.
The first render equals a fresh instance. A render after ANOTHER model in the same instance differs for 16 and 19 (after 23 / 3). Consequence in the page (finding, nothing changed there): a layer of those types sounds different depending on what rendered before it in the session
(preview vs. export vs. a second hit of the same voice), because the noise generator continues. Natively, and in the reference used for the gate, each voice starts from the fresh state, so native matches the page only for the first noise-using render of a page session.
If exact page behaviour is wanted natively, the Session would have to persist across voices (a decision for the lead; it makes renders order-dependent).

## Allocation in the audio path
`pe_prepare()` (called from `eng_init`) makes a thread-local workspace (Session + 384000-float pre buffer, about 1.8 MB). After that a render allocates nothing. A thread that did not call it allocates on first use.
`rt_harness` has an allocation-guard case (`run_engine_u25`) that renders both types and fails on any allocation.

## Third-party patches (`native/core/third_party/prepare.sh`, rows in `PINS.md`)
Host builds only. Elements (found earlier): `elements/dsp/string.cc:128` `lut_svf_shift` (damping_cutoff >= 256), `elements/dsp/resonator.cc:67` `lut_stiffness` (geometry_ >= 1): they change output for `edge_el1_100`, `edge_el2_100` only. Plaits: the five above.

## Least sure
1. Hashes on other platforms (libm differences in Elements; float strictness) not seen.
2. The fuzz proves no crash / no out-of-table read for the inputs it draws (80000 renders, two seeds); it is not a proof over all floats. The five clamps are the narrow fixes of what it found.
3. Rule-gated cases are checked at the raw stage only (the plumbing is proven bit-exact with injected pre-renders, but only on the 313 cases).
