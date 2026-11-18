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
4. `wave_terrain_engine.cc` (model 5): `aux = Sine(1 + 0.25 * aux_s)`; the path oscillator overshoots 1 by 0.1 % (y = -1.001), so the phase is about -0.002 and below and the index is -1 (24 bytes before `lut_sine`, ASan; an earlier text said 4). A phase at or below -1/512 (and NaN) becomes 0.
5. `sine_oscillator.h` `SineNoWrap` (model 21 bass drum in sustain mode): `lut_sine` has 641 floats; a phase of 1.25 or more or a NaN (the resonator at about note -30 and below) read 60 bytes before `syx_bank_2` (ASan: past the end of `lut_sine`). NaN / at or below -1/512 become 0, 1.25 and above become the largest float below it.
Each clamp leaves every index inside its table as it was: the committed native hashes of all 313 cases did not change with patches 1 to 5 (checked: `--write-native` gives the same 313 hashes). Counters in the clamps (the verifier's) show that on the 313 cases patches 1, 3, 4 and 5 NEVER fire, so for them "the hashes did not change" is vacuous: the clamp results are checked by the clamp cases below.
### The clamp cases (follow-up, 17 Nov)
One native-only case per patched Plaits read (`kClamp` in `test_engine_u25.cpp`, SHA-256 in `golden/engine_u25_clamp.inc`, made by `test_engine_u25 <ref> --write-clamp FILE`): the render must be finite, not trivially short and equal the committed native render. Each voice was found with a counter in the clamp to REACH it: P1 model 9 (ws_index beyond int: `clamp_p1_waveshaping_index`), P2 model 15 consonant 14 (`clamp_p2_speech_consonant`), P3 model 13, harmonics 0.999999 (`clamp_p3_wavetable_corner`), P4 model 5 with the AUX output (`clamp_p4_wave_terrain_aux`), P5 model 21 (`clamp_p5_bass_drum_sine`: a phase of 1.25 or more). Mutations of the prepared copy, each reverted: P1 `2.0e9f` -> `1.0e5f` fails the P1 case; P3 `z > 3 ? 3` -> `z > 2 ? 2` fails the P3 case; P4 `aux_phase = 0` -> `0.01` fails the P4 case; P5 `1.2499999f` -> `1.2f` fails the P5 case. Two things cannot be shown by an output: (a) P2's clamp reads a frame that is multiplied by a weight of 0, so any finite value gives the same sound (the case catches a non-finite one); (b) the second clamp of P1 (`index`, 516-entry `lut_fold`) and the NaN / negative side of P5 (and P4's NaN) were NOT reached by several tens of thousands of targeted and random renders (after the ws_index clamp `index` stays inside 0..1 in practice: x, y within -1..1, gain at most 0.49): they are defensive, and stay covered by the memory-safety fuzz only. (An early P1 mutation, 2e9 -> 1e9, is an equivalent mutant: both are multiples of 256, which the `& 255` removes.)

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
`pe_prepare()` (called from `eng_init`) makes a thread-local workspace (Session + 384000-float pre buffer, about 1.8 MB). After that a render on THAT thread allocates nothing. The workspace is per thread: a thread that did not call it allocates on its first render (a calloc and a malloc, about 1.8 MB).
`rt_harness` has an allocation-guard case (`run_engine_u25`) that renders both types and fails on any allocation, on a thread that called `eng_init()`. It now also covers the other thread (follow-up): a fresh thread that renders WITHOUT preparing (`pe_thread_ready()` false, the first render makes 2 allocations, the second none) and a fresh thread that calls `pe_prepare()` first and then renders under the guard (passes). `pe_thread_ready()` (layer_pe.h) says whether the calling thread has its workspace.

## Conditions before this is wired (the audio thread)
Nothing renders Plaits / Elements outside the engine yet. When a live lane or a render worker is wired:
1. `eng_init()` once at start-up (tables), AND `pe_prepare()` on EVERY thread that will render a Plaits / Elements layer under an allocation rule (an audio callback; a pool of render workers that must not allocate), off the audio thread, before its first render: the workspace is `thread_local`. A thread that has not will allocate about 1.8 MB inside its first render.
2. The wiring should assert it where a thread starts rendering: `assert(pe_thread_ready())` (or refuse and prepare), so a missing call fails in a debug build instead of in the audio callback. No assertion is built into `PeOsc::init` itself because the offline renders on the control thread legitimately make the workspace on first use.
3. A thread must not be handed to another after its first render without the same call on the new one; the Session is not shared between threads (and a voice is rendered start to finish on one thread).

## Third-party patches (`native/core/third_party/prepare.sh`, rows in `PINS.md`)
Host builds only. Elements (found earlier): `elements/dsp/string.cc:128` `lut_svf_shift` (damping_cutoff >= 256), `elements/dsp/resonator.cc:67` `lut_stiffness` (geometry_ >= 1). ONLY the `string.cc` patch changes any output, in the two cases `edge_el1_100` and `edge_el2_100` (the verifier's check, removing each patch alone: with `string.cc` unpatched exactly those two change, with `resonator.cc` unpatched none: its table[256] equals table[255], 2.0). The wasm job output is unaffected (`#ifdef __wasm__`). Plaits: the five above.

## Least sure
1. Hashes on other platforms (libm differences in Elements; float strictness) not seen.
2. The fuzz proves no crash / no out-of-table read for the inputs it draws (80000 renders, two seeds); it is not a proof over all floats. The five clamps are the narrow fixes of what it found; their results are checked by the clamp cases (two sub-clamps are unreachable by any input found, see above).
3. Rule-gated cases are checked at the raw stage only (the plumbing is proven bit-exact with injected pre-renders, but only on the 313 cases).

## Follow-ups of the verifier (17 Nov, `n1/followups`)
- The no-library branch of `test_engine_u25` now asserts an Elements layer is refused as well as a Plaits one.
- The gate is a CLASS rule, not a list: 160 cases pass the one-step rule without being named and the number of flips is not capped (the first plan said by name, bounded). Near-silent rule-gated cases show poor dB (`u25case126`: -38 dB, its peak is about one int16 step); the plumbing is bit-exact for every case.
