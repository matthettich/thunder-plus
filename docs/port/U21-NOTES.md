# U21 notes: MODFX modules C

Branch `n1/u21` (on `n1/u19`). Framework: U19's (`native/core/src/modfx/`, `native/tests/test_modfx.cpp`, `native/tools/modfx-dump.mjs`).
Gate: `native/tools/parity.sh u21` (`parity-u21.sh`).

## Done (17 modules, bit-exact with the real JS; g++-13 and clang++-18)
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

## Not done: Marbles (Random generator) and Tides (Function generator)
The wrapper is written (`native/core/src/modfx/mod_mods.cpp`: parameter block, modsClock, input/output copies, mods_process sanitising, "bad value: start afresh"), hosting `thunder_dsp_mods` directly (`make_marbles` / `make_tides`). It is built only with `-DTHUNDER_MODFX_MODS=ON` (default OFF; not registered, not in any gate), because it is NOT bit-exact against the golden, which comes from mods.wasm:
- Measured with the defaults (`-O2 -ffast-math`): of the Marbles/Tides cases, 5 of 6 engine renders differ (Marbles @default matches) and 32 of 36 direct cases differ; the differences are float-ULP scale (first differing samples agree to about 7 digits, e.g. 0.0696506053 vs 0.0696506128). Marbles' random sequences stay in step, so the structure is right.
- With `-fno-fast-math -ffp-contract=off` on the mods library (the U14 treatment of tfx): no better (same 5 renders).
- Cause (not proven sample by sample): the golden is wasm compiled by clang with `-ffast-math` (reassociation, reciprocal, its own libm), which a native g++/clang build cannot reproduce bit for bit; matching it would need the wasm itself run (a wasm runtime in the core), or a hand-ordered port of the Marbles/Tides cores. Left to the lead.
- Also: `Mod::operator new` is malloc, so the "bad value: object made again" path allocates inside process (the allocation guard would flag it); the JS's 64-object instance limit and 375-block wait are not carried over; the Marbles/Tides outputs would need the same Node reference (add `'marbles', 'tides'` to `MODS21` and regenerate the golden) if a bit-exact route is found. The mods.wasm sample rate/seed handling follow the JS (Tides seed 0, Marbles `nextSeed()`).

## Shared files (conflicts with U20 to expect, all small)
`native/core/CMakeLists.txt` (source list after `mod_clk.cpp`, the THUNDER_MODFX_MODS block), `native/core/src/modfx/modfx.cpp` (registry line), `mods.h` (add* declarations), `native/tools/modfx-dump.mjs` (`MODS21` list, `mi()` seed offsets 100+, the `key`/`voiced` cases, `bbCheck`), `native/tests/golden/modfx_golden.inc` (regenerate with `node native/tools/modfx-dump.mjs --golden` after merging), `native/tests/test_modfx.cpp` (`keyEngine`, `bbTest`), `native/tools/parity.sh`.

## Least sure of
Pitch correct (the detector depends on long state chains; mutation of the threshold is caught but only the signal kinds in the fixtures exercise it) and Bytebeat formulas outside the 16 built-ins (146 compared, the grammar is larger).
