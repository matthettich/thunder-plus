# U21 notes: MODFX modules C

Branch `n1/u21` (on `n1/u19`). Framework: U19's (`native/core/src/modfx/`, `native/tests/test_modfx.cpp`, `native/tools/modfx-dump.mjs`).
Gate: `native/tools/parity.sh u21` (`parity-u21.sh`).

## Done (17 modules + Marbles / Tides below, bit-exact with the real JS; g++-13 and clang++-18)
- `mod_basic.cpp`: Simple delay, Filter, Low/high cut, Classic comp, Bus comp, Glue comp, Noise gate, Auto-pan, Pump. `show()` meters are not ported (UI only).
- `mod_voice.cpp`: Pitch correct (YIN detector + splicing), Vowel filter, Rotary speaker, Granular delay, Reverse echo, Envelope filter.
- `mod_gen.cpp` + `bbexpr.{h,cpp}`: Bytebeat with the BBExpr formula compiler ported in full (the parser is self-contained in the block, so the U19 `expr` decision did not apply), and Chaos (`ChaosCore`).
- `bfutil.h`: helpers the block shares (`soft`, `fxGuard`, `fxSat`, `bfTempo`, the TPT SVF, Hermite taps, `keyOf`).
- Generated tables: `chaos_tab.inc`, `bb_list.inc` by `native/tools/modfx-gen-chaos.mjs` (`--check` is in the parity script).

Gate numbers: 51 engine renders (17 modules x 3 parameter sets) reproduce `tests/golden/chains-modules.json` (213 of 213 with U19's); 323 new direct cases (1227 with U19's) bit-exact against committed FNV-64 and sample by sample on x86-64 Linux; BBExpr: 146 formulas (16 built-in + extras that hit errors, precedence, and the quirks) equal to the JS in compile result, error text/position and values on a (t,a,b,c) grid; `rt_modfx` runs all 71 modules x 3 sample rates under the allocation guard on both compilers. Mutation checks (compressor key case, buscomp constants, rotary, reverse, gdelay, autotune threshold, chaos derivatives, bbexpr op 77) are all caught.

## Faithfulness points
- `ctx.key(sck)` (Source buffer) was added as `Ctx::key` + `keyUser` (nullptr = Self / undefined). The engine hands back a zero buffer for sck>0; the test's direct `key` case feeds the input reversed in time x .7.
- BBExpr exec cases 77/78 (`&&` / `||` with a constant) step over the constant only when the first side did not decide the result; the C++ keeps that. Exec also keeps typed-array out-of-range semantics (undefined reads as NaN, ignored writes, ToInt32 stores) via bounds-safe double-valued reads. The 16 built-ins never hit it.
- The core builds with `-fno-exceptions`: the compiler returns an `Err` record instead of throwing.
- `Float32Array.fill()` writes the whole 128-sample buffer whatever `ctx.block` is (U19's lesson) is kept where the JS uses it.

## Marbles (Random generator) and Tides (Function generator): the decision (the lead, Fable, 16 Nov evening) and what it did
The web app's `mods.wasm` is compiled with -ffast-math, which lets the compiler reorder float sums; no native build can reproduce that order (D2's section below: the cause is -ffast-math in both builds). Rebuilding the
web wasm with `-fno-associative-math` would make the native and the wasm bit-identical, but it was measured on 16 Nov evening (same machine, Node 22, minimum of 7 runs) to cost CPU in the web app (Marbles / Tides within
about +-8 %; the pump effects up to +108 %), and the owner's standing concern is CPU on the phone, so the web app's wasm flags are NOT changed. Instead:
- (a) The native build of the mods library (`thunder_dsp_mods`, `core/dsp/CMakeLists.txt`, `OPT_MOD`) is STRICT: `-O2 -fno-fast-math -ffp-contract=off` (as tfx in U14); the wasm keeps mods/build.sh's flags.
- (b) Against the fast-math wasm the gate for these two modules is a tolerance: each engine render and each direct case within -100 dB of the reference's peak (max abs difference / the reference's peak), the worst printed;
  every case that is bit-exact today stays bit-exact (`kModsExact`, 6 of the 42). The wasm's reference samples are written by `tools/modfx-dump.mjs` (`MODS_TOL`: `engine_<type>_<set>.f32` for the 6 engine renders; the
  direct cases' `<case>.f32` as before); `'marbles', 'tides'` are now in `MODS21` (golden rows regenerated with `node native/tools/modfx-dump.mjs --golden`: 42 rows ADDED, no existing row changed; their hashes are
  the WASM's, kept as the reference's).
- (c) Cross-platform determinism is gated separately: `golden/modfx_mods_native.inc` holds the SHA-256 of the native strict render of each of the 42 (made on Linux x86-64, GCC 13), checked on every platform. Written by
  `test_modfx <ref dir> --write-native FILE` (never hand-edited; rebuild test_modfx after). Not yet seen on macOS / Android in CI: if a hash differs there, that is the finding (libm).
- Switched on: `THUNDER_MODFX_MODS` defaults to ON, the modules are in the registry (`addModsMods`), in `test_modfx`, `rt_modfx` and the other tests that walk the registry.
- Result (GCC 13.3, Release): 6 engine renders + 36 direct cases: all 42 inside -100 dB, 6 bit-exact, worst `tides_mod1` -106.8 dB (then `tides_stress` / `engine_tides_stressed` -119.0, `tides_none` -119.1, `tides_rand2` -120.9).
  Marbles: worst `marbles_voiced` -132.5 dB. No case is left out (`kTolSkip` in test_modfx.cpp is empty); Marbles' random sequences did not part ways in any case.

CI of the first push (7ff4fc7e): GCC, clang, macOS (native hashes held), Windows (Marbles / Tides reported not run: no mods library there), wasm and Android pass. FAILS: Linux sanitizers, `rt_core` and `modfx`:
`AddressSanitizer: global-buffer-overflow ... READ of size 4 ... in marbles::LagProcessor::Process(float, float, float)` (called from OutputChannel::Process, XYGenerator::Process, MarblesMod::process, rt_modfx / test_modfx), 60 bytes
before a string constant of mod_tides.cc: upstream Marbles reads outside a lookup table for some parameter values the random-parameter cases reach (the strict native build exposes it under ASan; the wasm never checks). Not fixed yet.
Next: find the index (ASan build locally, gdb on the first hit, which parameter), then either clamp the value in our wrapper the way the JS clamps (if the JS wrapper's range allows it) or report the upstream read to the lead.

Known, left as is (as before): `Mod::operator new` is malloc, so the "bad value: object made again" path allocates inside process (an allocation guard would flag it if a case reached it; none of rt_modfx's does);
the JS's 64-object instance limit and 375-block wait are not carried over. The mods.wasm sample rate/seed handling follow the JS (Tides seed 0, Marbles `nextSeed()`).

## Shared files (conflicts with U20 to expect, all small)
`native/core/CMakeLists.txt` (source list after `mod_clk.cpp`, the THUNDER_MODFX_MODS block), `native/core/src/modfx/modfx.cpp` (registry line), `mods.h` (add* declarations), `native/tools/modfx-dump.mjs` (`MODS21` list, `mi()` seed offsets 100+, the `key`/`voiced` cases, `bbCheck`), `native/tests/golden/modfx_golden.inc` (regenerate with `node native/tools/modfx-dump.mjs --golden` after merging), `native/tests/test_modfx.cpp` (`keyEngine`, `bbTest`), `native/tools/parity.sh`.

## Least sure of
Pitch correct (the detector depends on long state chains; mutation of the threshold is caught but only the signal kinds in the fixtures exercise it) and Bytebeat formulas outside the 16 built-ins (146 compared, the grammar is larger).
