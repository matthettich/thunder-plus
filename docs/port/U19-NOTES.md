# U19: MODFX modules A (the shared module block, C++)

Port of the shared MODFX module block of `chains/chains-worklet.js` (between `===SHARED MODULES START===` and `END===`; my range is lines 719 to about 2078, plus Blip drum, Life, Degrade, Reson and the tempo-locked wrappers) to `native/core/src/modfx/`. 54 modules, every one bit-exact against the real JS.

## Module interface (my decision; it does not guess at U31)

`modfx.h`: a `Module` is made by `Def::make(sr, Seeds&)` (the JS `create(sr)`) and run by `process(const IO&, const Ctx&)` (the JS `process(st, io, ctx)`).
- `IO`: input buffers in the JS declaration order (an unpatched input points at a zero buffer, as the engine does), `linked[]` (the engine's `io.linked`), output buffers, `p` (the parameter values in `params:` order, already modulated by the host).
- `Ctx`: sr, block, quality (`full`), tempo, lpb, ticks, playing, `startN`.
- `Seeds`: the engine's `nextSeed()` (`SeedCounter` is the JS LCG over doubles; a recorded list can replace it).
- `Def` carries the port and parameter tables (names, kinds, defaults, min, max, step); `find(type)`, `count()`, `at(i)` give the registry.
U31 can wrap this or replace it; nothing outside `modfx/` and the tests depends on it. Modules take no per-instance text (`io.inst`), which is why `expr` is not here.

## Rules kept (as U15 / U16)

Doubles in the JS arithmetic order (contraction off), float32 only where the JS writes a Float32Array (`In`/`Out`/`FRef`/`Arr<T>` in `util.h` make a store round and a read give the double), Math.* via `js_*` fdlibm, Math.min/max keep NaN, `x|0` is `js_to_int32`, nothing allocates after construction. Quirk worth knowing: `Float32Array.fill()` in a module writes the whole 128-sample buffer whatever `ctx.block` is; the C++ does the same (`fill(kBlock, v)`; Pitch follow and Pitch track were wrong at partial blocks until the direct cases caught it).

## New libm: Math.log2

`js_log2` is V8 12.4.254's `ieee754::log2` (with `k_log1p`), text unchanged, in `rt/fdlibm.cpp`; `libm_golden.inc` now holds six functions (the first five columns are unchanged: checked); ctest `libm` proves it on 400000 arguments against Node 22 on x86-64 Linux. Used by Pitch follow, Pitch track and `DelayLine::create` (`Math.ceil(Math.log2(sr*2.1+8))`).

## Modules (54), with files

| file | modules |
|---|---|
| `mod_stereo.cpp` | meta, phaser, width, pingpong, meter, limiter, utility, ringmod |
| `mod_reverbs.cpp` | ensemble, spring, plate, lverb |
| `mod_cv.cpp` | envfol, gateex, slew, snh, quant, math, logic, steps, clock, clkmul, chance, counter, stepsw, burst, gatedly, flipflop, rect, scale, compare, switch, xfade, const, divider, euclid, pitchfol, pitchtrk |
| `mod_filters.cpp` | blades, onepole, allpass, dcblock, midside |
| `mod_delays.cpp` | dline, dimension, bbd |
| `mod_misc.cpp` | harm, blipdrum, life, reson, degrade |
| `mod_clk.cpp` | flex, sgate, retrig (the tempo-locked wrappers; they use U15's cores in `fx/clkcores.h`; `RetrigCore::setSpb` added) |

## Gate

1. Engine run: the committed `tests/golden/chains-modules.json` hashes (trackIn -> module -> chainOut, 400 blocks, default / stressed / switches-on, FNV-32). 162 of 162 renders reproduce them, on g++-13 and clang++-18 (Release). This is the gate on every platform.
2. Direct cases (904, 16 to 20 per module): the module's create/process driven with random parameters changing mid-run, linked and unlinked inputs, partial blocks, tempo and start changes, 44.1 / 96 kHz, tails and sleeps; compared sample by sample with the live Node and against the committed FNV-64 (`tests/golden/modfx_golden.inc`). All 904 bit-exact on both compilers. The local-Node sample comparison must hold on x86-64 Linux (elsewhere it is reported only).
3. ctest `modfx_ref` (makes the fixture, about 138 MB, about 20 s), `modfx`, `modfx_golden_current` (Linux: the `.inc` is what the JS gives now), `libm`, `libm_golden_current`, `rt_core` (`rt_modfx`: every module at 3 sample rates for 1500 blocks under the allocation guard, 243000 blocks). `native/tools/parity.sh u19` runs them.
4. Waves: `waves-modules.json` needs the waves engine (U30). The shared block text of `waves-worklet.js` is identical to the chains one; `modfx-dump.mjs` fails if they ever differ.
Mutation checks during the work confirmed the direct cases catch errors the engine run misses.

## Left out

- `expr` (needs the `MathExpr` compiler and per-instance text, `io.inst.text`) and `scope` (a probe with no outputs; its `show()` data): not ported; they want U31's decision on instance text and show().
- `show()` of every module is not ported.
- `pdelay`, `preverb` (U17's PitchFxCore) and the tfx `effect(...)` wrappers (U14): not mine. U20 (Spectral, XStretch, Convolve) and U21 (PitchCorrect ... Pump) not mine.
- Degrade and Reson here duplicate what U17 may also port; reconcile when merging.
