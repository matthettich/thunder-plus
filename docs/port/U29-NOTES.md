# U29: rendered-only engines set 2 (XENG): table (+ a loaded wavetable V.wtData), vector, scanned, resynth (+ the analysis V.rsData, SpecUtil), bytebeat (BBExpr reused)

Branch `n3/u29` (from `native`). Gate command: `native/tools/parity.sh u29` (PASS locally: libm, engine_xeng2_ref / engine_xeng2 / golden_current / fuzz, rt_core).

## State (work in progress, first push)
- All five engines ported line by line: `native/core/src/engine/xeng_{table,vector,scanned,resynth,bytebeat}.cpp`; SpecUtil (fft with the twiddle cache, unb64) and the scratch arena are in `xeng_util.{h,cpp}`.
- `ctest engine_xeng2` (local, Release GCC): **333 of 333 factory voices** (bytebeat 73, resynth 51, scanned 69, table 67, vector 73) equal beta's `tests/golden/engines.json` hash; **262 of 262 direct cases** equal the committed Linux hash and the local Node.
- Done: six mutations (scanned shuffle, vector gain, resynth 1.38, table formant, bytebeat DC, FFT twiddle) each fail 1 to 417 cases; `engine_xeng2_fuzz` (1891 renders, 72 refused, 0 failed: every parameter at -1e308..1e308, NaN in every field, hostile wtData / rsData / bbExpr); ASan + UBSan (`-DTHUNDER_SANITIZE=ON`) clean on engine_xeng2, its fuzz, U28's engine_xeng_fuzz and rt_core; `rt_engine_xeng2` guard case (fails without its warm-up: the guard works); U28's `engine_xeng` still passes.
- No golden output contains NaN or infinity (checked over all 595 reference renders), so a NaN sign bit never decides a hash.
- Not run here: clang, macOS, Windows (CI).

## What is where
- Fixture and reference: `native/tools/engine-xeng2-dump.mjs` (own vm of the ENGINE block; `--fixture <beta>` makes `tests/golden/engine_xeng2_voices.json`, `engine_xeng2_voices_golden.inc` (beta's rows, copied) and `xeng2_ranges.json`; `--golden` writes `engine_xeng2_golden.inc`; `--check` compares the local Node with the committed hashes). A Resynth voice's analysis is stored once in the fixture's `blobs` and named by `{"$blob": n}` (the test hands it to `xeng_rs_from_json`).
- Test: `native/tests/test_engine_xeng2.cpp`; ctest `engine_xeng2_ref` (Node makes the reference, about 1 min), `engine_xeng2`, `engine_xeng2_golden_current` (Linux).
- U28's `XKind::Other` for table / vector / scanned / resynth / bytebeat is gone (they are `XKind::Table` ... `Bytebeat`); U28's test now uses `sf` as its "engine of a later unit" example. Nothing else of U28 changed.

## Decisions
- **Scratch arena** (`XArena`, `xeng_util.h`): the four engines other than bytebeat take every array from a per-thread arena of zeroed blocks that stay allocated; after one warm-up render of the same size nothing is allocated (the `rt_harness` case does exactly that, under the guard). Bytebeat allocates (the compiled formula is `std::vector`s in U21's `BBProg`).
- **Bytebeat** reuses `modfx::BBProg` (U21), not copied. The JS's `catch` returns `new Float32Array(1)`: a formula that does not compile gives one sample of silence (not run through `finish`), as here. `String.prototype.trim()` emptiness (ASCII and the Unicode spaces) is reproduced for the "uses the default formula" test. NOTE: index.html's `BBExpr` and `chains-worklet.js`'s differ only in two error message strings (the compile succeeds or fails identically).
- **Vector** takes the song tempo as `EngRenderOpts::bpm` (the JS's `songBpm()`, 120 when there is no song).
- **Loaded wavetable**: `frames x partials <= 2^21`, each at most 4096, both whole numbers, else `kEngUnsupported` (the JS allocates whatever the data says; the app writes at most 64 frames of 256 partials).
- **Resynth**: `ResynthCore.analyse` is NOT ported (it runs on the page when a sample is loaded; the pad only stores its result). Bounds: parts <= 4096, frames x parts <= 2^22; a negative count (the JS's RangeError), zero frames with parts, and `rat` entries that are missing or not finite (the JS plays NaN) are `kEngUnsupported`.
- A table position that is NaN reaches `cyc[NaN]` in the JS and throws: refused. Reads past a cycle table (a phase of 1 or more, from a huge note) are NaN as in the JS (`undefined`).
