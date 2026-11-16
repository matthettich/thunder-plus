# U16: Beat grains, Beat scramble, Glitch sequencer, Vintage sampler

Slot kinds 100 to 103 of the native track-effect host (`FxHost`), done the way U15 did Flex / Step gate / Retrigger.

## What was ported, and from where

All four are plain JS in `index.html` (the same code is generated into `chains/chains-worklet.js`), run by the ThunderFx worklet (`TFX_SRC`):

| id | key | JS | C++ |
|---|---|---|---|
| 100 | `grains` | `GrainsCore`, `beatLock`, `bkHash` (about line 18563 to 18613) | `GrainsCore` in `core/src/fx/idmcores.cpp` |
| 101 | `scramble` | `ScrambleCore` | `ScrambleCore` |
| 102 | `glitch` | `GlitchCore` | `GlitchCore` |
| 103 | `vsamp` | `VSampCore` | `VSampCore` |

Host side (`fxhost.cpp`): `fx_is_idm` / `fx_is_core`, `ClkFx` instances for the four, the worklet's `s.dp` objects (`setClkDp`), `runClk`, the node's sleep (`ck`): Grains, Scramble and Glitch are `ck` 3 when on (never sleep, run while the clock is stopped), no `feed` (a switched-off one is not fed, as the JS); the Vintage sampler is `ck` 0 (it is not in the worklet's `clocked` set) and is never napped (`slotRun` always). `fxmap.cpp` has the four `map()` functions; `fxtable.inc` (generated, `fx-table.mjs`) the controls; the page's `TFX` is the source.

Rules kept as in U15: doubles in the JS's arithmetic order, float32 only where the JS writes a Float32Array, `-ffp-contract=off`, js_exp / js_pow / js_cos / js_sin / js_log from fdlibm, Math.round = `js_round`, Math.min / max keep NaN, reads past a ring's end are NaN as `undefined` is. Randomness: Grains' xorshift32 (`rnd`, seed 1), the Vintage sampler's xorshift32 noise (seed `0x1234567`) and `bkHash` are integer arithmetic reproduced as written; nothing uses a platform random number.

New libm: `Math.tanh` (the MPC60 converter model) is `js_tanh` = V8 12.4's `ieee754::tanh` and `expm1` (text from tag 12.4.254, in `rt/fdlibm.cpp`). `libm_golden.inc` now holds five functions (the first four columns are unchanged: checked); ctest `libm` proves `js_tanh` on 400000 arguments against Node 22 on x86-64 Linux.

Allocation: the rings are made in the constructors (the JS makes Scramble's and Glitch's rings on their first block; the content is zeros either way); Grains' voice list is a fixed array of 24 with `shift` / `splice` done as moves. `rt_harness` has `run_fxidm` (all four, odd block sizes, a stopped and jumping clock, a chain swap that retires three of them) under the allocation guard (ctest `rt_core`).

`Math.sin(...) ** 2` in Grains is `js_pow(x, 2)`; fdlibm's pow returns `x * x` for y = 2, so a JIT that folds `** 2` into a multiply gives the same bits.

## Reference and gate

`native/tools/fx-idm-dump.mjs` runs the real worklet (cores cut out of `index.html` by `fx-src.mjs`, as U15) in Node. It reuses the clock, signals and `runCase` of `fx-clk-dump.mjs` (which now exports them; its golden is unchanged, checked). `test_fxidm` compares every case sample by sample (bit-exact is the gate) and with the committed FNV hash `tests/golden/fxidm_golden.inc`. `parity-u16.sh` runs the gate; ctest `fxidm_ref`, `fxidm`, `fxidm_golden_current` (Linux), plus `fx_table_current`.

The committed golden hashes were made on x86-64 Linux, Node 22: the cores call Math.exp / pow / sin / cos / log / tanh in the audio loop, which Node on macOS arm64 may round differently (the U15 lesson). The C++ is compared with the reference made by the local Node and with the committed hash, so another platform's Node that rounds differently fails loudly rather than passing. (The libm itself is gated against its own committed golden on every platform.)

## Cases (242, all bit-exact)

Beat grains 46 (every RATE with the second start, random pitch 0 to 3, REVERSE, SHAPE ends, SIZE ends with the 24-voice cap, SPRAY, POSITION past the ring at 30 BPM, density 0 / mid, mix 0, FREEZE and toggling it, 12 random parameter sets, clock stopped / stopping / tempo changes / jump / const / absent, FXO). Beat scramble 40 (SLICE x3, VARY x3 over three wraps of the clock, KEEP, CHANCE 0 / 255, REVERSE 0 / 255, seeds, SMOOTH ends, mix, 12 random, stopped, stops, tempo, jump, wrap, const, absent, FXO). Glitch 55 (STEP x3, VARY x3, each action alone at each STEP (6 x 3), CHANCE 0, seeds, mix, zero weights, 12 random, clock cases, FXO). Vintage sampler 82 (every model at default, raw filter, loud (clipping and DRIVE), quiet (the hiss); DRIVE, FILTER, RATE, NOISE, MIX at their ends on three models; 16 random; settings changing while it runs; FXV; FXO). Host: block sizes 1 / 37 / 128 / 480 / 1024 and a mixed list with the four in series, series with every command, chain sets, FXV, a resting node, the sleep rules (18 s) for each kind on / stopped / off. Plus the `map()` of the four kinds on 200 value sets each (missing, null, strings, out of range) as doubles, and two hosts side by side on nine cases.

Result: all 242 bit-exact with g++ 13 and with clang++ 18 (Linux x86-64, Release); full ctest 80 of 80 on each.

## Quirks kept (they are the JS)

- Beat grains ring is 6 s; Scramble / Glitch 13 s. A clock param that is constant over the block (`bp.length` 1) is no clock for `beatLock` (pass-through for Scramble / Glitch; Grains free-runs).
- Grains: `rnd()` is consumed even when the grain is then not started (POSITION beyond the ring); `v.shift()` happens before that check.
- Vintage sampler: the left channel's hum phase advances per sample in the left pass only, so the right pass hears the end-of-block phase for every sample; the phase wraps once a block.
- Glitch's `act` 6 (CRUSH) holds 6 samples; its `sub` list is picked even for acts that do not use it.

## Not sure / left

- `Math.tanh` and `Math.log` behaviour inside V8's optimising compiler (TurboFan) is assumed to be the same `ieee754` functions as the interpreter's (it is for Node 22); real Chrome (a different V8 build) is the step-11 check, as for U15's libm.
- Nothing is left unported from the unit. Degrade, Resonator, Pitch delay / reverb are U17 and are still left out of the host (the sound passes).
