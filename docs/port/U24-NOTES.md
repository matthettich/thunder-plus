# U24: layer types FM family, additive family, membrane / bell / bar / karplus / vosim / formant

STATUS: DONE locally. 110 of 110 factory voices and 383 of 383 direct cases bit-exact (local Node reference differs for 0). GCC Release: full ctest 92 of 92; clang 18 Release and GCC ASan + UBSan: engine_u24, rt_core pass. Gate command: `native/tools/parity.sh u24`. Mutation checks: Karplus DC constant, VOSIM DC constant, an FM algorithm's modulator list, the additive normaliser, the FM index floor: each fails (the first three at 2 to 5 cases).
Rebased onto native after U23 (#192) merged: conflicts in LType / Osc / renderLayer / CMake / rt_harness / parity.sh resolved (U23's `x.blk(inc)` and my `u24.blk(t, env, inc)` are both called from `Osc::blk`; one `osc.blk(t, e0, inc)` call). The fixture now allows U22, U23 and U24 types: 110 voices (97 with U22 + U24 types, 13 that also mix in U23's click / wavetable / pd / chip / supersaw). `test_engine_u23.cpp` line 136 asserted `fm2` is not a type yet: it now asserts `plaits` (still not ported); nothing else of U23 changed.

## Types
fm2 fmfb fmmetal fmcross fm4 fm6 genesis sb arcade, addsaw addodd membrane bell bar chord, formant karplus vosim. Every other type stays `kEngUnsupported`.

## Where
- `native/core/src/engine/layer_u24.h` (interface), `layer_u24.cpp` (dispatch, `u24_layer_ok`), `layer_fm.cpp`, `layer_add.cpp`, `layer_karplus.cpp`, `layer_formant.cpp` (formant + vosim).
- Shared files touched (smallest edits): `engine.h` (18 `LType` values on their own line), `engine.cpp` (include; `eng_layer_type` first line; `Osc` member `u24`, `init` default, new `blk`, `s` default; `osc.blk(t, e0, inc)` call in `renderLayer`; the refusal test adds `u24_layer_ok`), `core/CMakeLists.txt` and `tests/CMakeLists.txt` (own lines).
- Own files: `tools/engine-u24-dump.mjs`, `tests/test_engine_u24.cpp`, `tests/golden/engine_u24_voices.json`, `engine_u24_voices_golden.inc` (beta's rows, copied), `engine_u24_golden.inc` (direct cases).
- (Merge notes, done) U23 also edited `LType`, `Osc::init/s`, `eng_layer_type` and adds the `osc.blk` call (needed by some of its types too): keep one copy of the `blk` call and one `blk` method (add its types to the same method). Keep my 18 enum values contiguous (`u24_is` is a range test).

## Decisions
- Voices: `engine_u24_voices.json` holds the factory voices whose layers are all U22's, U23's or U24's types with at least one of U24's (110). U22's own voices stay in `engine_voices.json`. With U23's fixture (`engine_u23_voices.json`, voices of U22 + U23 types) and U22's, every factory voice of the three unit's types is gated exactly once.
- Direct cases use a copy of the U22 helpers (slim / build / defaultsOf) in `engine-u24-dump.mjs`; `engineOf` and `ROOT` are imported from `engine-dump.mjs`.
- `blk(t, env, inc)`: the JS calls it every 16 samples with the envelope's value at the block start; FM / additive types use it, the others ignore it.
- Layers the JS throws on (chord shape index outside 0..13 after rounding, fm4 / fm6 / genesis / sb / arcade algorithm that is NaN, addsaw / addodd with more than 64 partials) return `kEngUnsupported`, never silence. fm2 / fmcross with a ratio index outside the table give NaN, like the JS's `undefined`.
- Karplus: the delay line and the excitation are float32 (as the JS Float32Arrays); a NaN period leaves the line untouched and the output NaN (the JS's undefined reads). The two 16 KB excitation buffers are on the stack (no heap).

## Not sure
- Nothing in the factory voices uses a knob outside 0..1; the `edge_over_*` / `edge_under_*` cases cover the JS behaviour there (slice with a negative end, algorithm clamps).

## CI time (Linux job, about 29 min on a 191 run: GCC build+ctest 8.5 min, clang 16 min with build, sanitizers 11.5 min)
The three ctest passes each run every `*_ref` (Node makes the reference; about 266 s per pass locally: engine_ref 75, fxidm 50, modfx 38, fxclk 28, fxu17 22, master 22) and every `*_golden_current` (about 264 s: it proves the committed hashes against Node, independent of the C++ compiler). A cheap cut that changes no gate: run `*_golden_current` only in the GCC pass (`ctest -E _golden_current` in the clang and sanitizer passes), saving about 4 min each. Not done here: `.github/` is outside `native/` and `docs/port/`.
