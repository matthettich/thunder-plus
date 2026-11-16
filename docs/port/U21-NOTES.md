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

## Marbles / Tides against the fast-math wasm: where the first differing sample comes from (D2, 16 Nov)
Answer in one line: **the cause is `-ffast-math` in BOTH builds (the golden's wasm and the native one), each compiler making its own value-changing choices; with no fast-math the native build and the wasm are bit-identical on every case run; no libm, no state, no ordering difference of the C++ was found.** The wasm's fast-math is not the only cause in the sense that the native `-ffast-math` build deviates from strict IEEE on its own, at different samples. Marbles and Tides stay switched off; nothing in the repo is changed except this text.

### Method (same machine, 2 cores; nothing committed but this section)
Driver on the C++ API of `dsp/mods` (`make_marbles` / `make_tides`, 128-sample blocks, 48000 frames, parameters and inputs written by one Node script into a file both sides read), run (a) natively, the objects built from the same sources and flags as `thunder_dsp_mods` (GCC 13.3, `-O2 -ffast-math`), (b) in Node on the `mods.wasm` that `build-wasm.sh` / CMake makes (wasi-sdk 25 clang, same flags). Eight cases made by me, not the golden's own parameter sets: Marbles default / jitter+dejavu / spread+steps+scale / another model, Tides AD, looping, free-running (default), clocked, audio range, trigged, AR. Variants: the same objects rebuilt with other flags, or with `#pragma clang fp reassociate(off)` in one function of the upstream sources (a scratch copy; the repo and the pin are not touched). In-source printing of intermediates was tried and DROPPED: it changes what GCC does (with prints in `RenderInternal` the native build gave the wasm's bits), so the proof is by flags and pragmas.

### What is the same
* Native strict (`-fno-fast-math -ffp-contract=off`) against wasm strict (same flags in clang): **bit-identical on all 8 cases**, 48000 frames x 8 outputs. So glibc against wasi-libc maths, the tables, the structure and the wrapper agree on these inputs. (Not shown: every possible input of `powf` / `expf` paths; those cases do not reach libm differences.)

### Tides (the golden's looping, free-running default, `tides_def`): the first differing sample is sample 0
* Output 0, sample 0 (first block): native `0xbd28b79c` (-0.04119072853), wasm `0xbd28b79f` (-0.04119073972): 3 ulp. 47060 of 48000 samples of output 0 differ. Before the x0.2 scaling the generator's channel 0 is `-0x1.a5cb06p-3` native against `-0x1.a5cb0cp-3` wasm. The wasm's value is also the strict value.
* So at sample 0 the WASM equals strict and the NATIVE deviates. On the native side `-ffast-math` contains `-ffinite-math-only`: compiling only `dsp/mods/mod_tides.cc` (which holds the inlined `PolySlopeGenerator::RenderInternal`, `Fold`, the waveshaper and the shaper) with `-ffast-math -fno-finite-math-only` gives the strict bits at sample 0 (2 differing samples left, first at 5). `-fno-associative-math`, `-fno-reciprocal-math`, `-fno-signed-zeros`, `-ffp-contract=off` alone do not change sample 0. The exact expression GCC rewrites under finite-math-only was NOT isolated (GCC's fast-math flags are per function, so the pragma bisect used on clang below does not work there).
* The wasm deviates from strict later: first at sample 2 of output 0 (`0xbe3c98e0` against strict `0xbe3c98df`, 1 ulp; 26 samples of output 0 in all). Cause: reassociation by clang. `-ffast-math -fno-associative-math` (all else kept) makes the wasm bit-identical to strict; `-fno-reciprocal-math`, `-fno-approx-func`, `-fno-signed-zeros`, `-fno-finite-math-only` do not. With `#pragma clang fp reassociate(off)` in a scratch copy: sample 2 stays wrong with it on `Fold` alone, `RenderInternal` alone, `Scale`, `Tame`, `Filter::Process`, the ramp generator, the parameter interpolator, `units.h`, or the 1st wasm TU `poly_slope_generator.cc`. It goes (bit-identical) with it on `RampWaveshaper::Shape` (the `x + (y - x) * t` interpolations) + `RampShaper::SkewedRamp` (`phase * (0.5f / pw)`) + `PolySlopeGenerator::Fold` (`5 * (bipolar + (folded - bipolar) * fold)`, with `fold` 0 here and a different `shift` every sample while the 8-sample parameter ramp runs). Three sites, each of which has to be kept in strict order for the first 26 samples to agree; `Shape` alone brings 26 differing samples down to 3.

### Marbles
* `marbles_def` (all defaults): bit-identical native fast against wasm fast, and both against strict (this is the one the notes mention as matching).
* `marbles_jit` (jitter .5, dejavu .6, model 1): wasm fast == strict; NATIVE deviates: first sample 17803 of output 6 (Y), `0x3ea74155` against `0x3ea74153`, 21010 samples of that output. Making only `random/x_y_generator.cc` strict, or only `-fno-associative-math` for that file, moves the first differing sample to 42162; the rest comes from `random/t_generator.cc`. Reassociation (GCC), not finite-math, not reciprocal, not signed zeros.
* `marbles_x` (spread .9, steps .4, scale 2, xrange 1, model 3): native fast == strict; the WASM deviates: first sample 6084 of output 3 (X1), `0x3edda310` (strict) against `0x3edda311` (wasm), 168 + 1137 samples of outputs 3 and 4. Strict `random/t_generator.cc` removes the output-3 differences; `-fno-associative-math` makes the wasm identical to strict.
So one compiler deviates in one case and the other compiler in another: nothing is wrong with either; neither is the reference once `-ffast-math` is in.

### Which build would be bit-identical (measured on the 8 cases, nothing applied)
| native flags (all mods objects) | wasm flags | result |
|---|---|---|
| `-O2 -ffast-math` (today) | `-O2 -ffast-math` (today, the golden) | 5 of 7 non-default cases differ from sample 0 to 6084 / 17803 |
| `-ffast-math -fno-associative-math -fno-finite-math-only` | `-ffast-math -fno-associative-math` | both bit-identical to strict, so to each other, on all 8 cases |
| strict (`-fno-fast-math -ffp-contract=off`) | strict | bit-identical |
GCC needs `-fno-finite-math-only` as well (Tides sample 0); with `-fno-associative-math` alone native Tides still differs from sample 0 (`tides_def` 47437 samples; Marbles is exact). So to make the native build match the golden bit for bit the golden would have to be regenerated from a `mods.wasm` built with `-fno-associative-math` (or strict), i.e. the web app's `mods.wasm` (embedded in `chains-worklet.js`) rebuilt and its output changes at ULP scale for every user project that uses Marbles / Tides. That is a web-app change and a golden change: not mine, not done; Fable's decision. The other routes in the text above (run the wasm, or a hand-ordered port) stay as they were.

### Not done / least sure of
* The eight cases are mine; the golden's own 6 engine renders and 36 direct cases were not re-run, only the same kind of difference shows. Confident the picture (ULP, reassociation) is the same; not claimed case for case.
* The GCC expression behind Tides sample 0 under `-ffinite-math-only` is not found. The clang side is pinned to three source functions by pragma, not to a single instruction.
* The re-creation allocation in `process` (above) is untouched.
