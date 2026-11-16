# U17: Pitch reverb, Pitch delay, Resonator, Degrade

Slot kinds 96 to 99 of the native track-effect host (`FxHost`), done the way U15 and U16 did their JS cores, and the pads' use of the same cores (`applyFx`).

## What was ported, and from where

Plain JS in `index.html` (the same classes are generated into the chains / waves worklets), run by the ThunderFx worklet (`TFX_SRC`):

| id | key | JS | C++ (`core/src/fx/u17cores.*`) |
|---|---|---|---|
| 96 | `preverb` | `PitchFxCore` with `kind` 1 (`rvMix`) | `PitchFxCore` |
| 97 | `pdelay` | `PitchFxCore` with `kind` 0 | `PitchFxCore` |
| 98 | `reson` | `ResonCore` | `ResonCore` |
| 99 | `degrade` | `DegradeCore` | `DegradeCore` |

Host side (`fxhost.cpp`): `fx_is_u17` joins `fx_is_core`; `PitchFx` / `ResonFx` / `DegradeFx` are `ClkFx` holders (no clock is read); `setClkDp` has the worklet's `s.dp` objects for them. The sleep rules are the worklet's: none of the four asks anything of the node's `ck`; **Degrade always runs** (it makes its own noise), **Pitch reverb, Pitch delay and the Resonator nap like a wasm effect** (`slotNap`: asleep after 30 s of silence in and out) because the worklet's line is `s.js && !(s.jt === 96 || 97 || 98)` for "always run". `fxmap.cpp` has the four `map()` functions, `fxtable.inc` (generated, `fx-table.mjs`) the controls.

Rules as U15 / U16: doubles in the JS's arithmetic order, float32 only where the JS writes a Float32Array (block L / R, the delay lines, comb and all-pass buffers, the shifters' buffers, the resonator strings), `-ffp-contract=off`, `js_exp / js_pow / js_cos / js_sin / js_tanh` from fdlibm (no new libm function was needed: `libm_golden.inc` is unchanged), Math.round = `js_round`, Math.min / max keep NaN, `x | 0` = `js_to_int32`, a read past a Float32Array's end is NaN and a write there is dropped (as typed arrays do). `(i + N) & (N - 1)` in the shifter keeps ToInt32 (NaN reads index 0, not undefined). Randomness: Degrade's xorshift32 (seed `0x2545f491`, two draws per sample only while the noise level is above 0) is integer arithmetic as written. No allocation after construction: all buffers are made in the constructors.

## Pad use (index.html `applyFx`)

The pads render a whole sound offline: `applyFx(f, a, sr)` for `pdelay` / `preverb` (room for a tail: `3 + size * 4` s, or `min(8, time / 1000 * (1 + 6 * fb))` s, a core run in 128-frame blocks on L and R copies, the result `(L + R) * 0.5`), and for `degrade` / `reson` (same length, L back). Ported as free functions in `u17cores.cpp`: `pad_degrade`, `pad_reson`, `pad_pitchfx` (values are the page's own, not the mapped slot values; an absent value is NaN; pitch2 / size / wow default as `!= null` does). Tested against the REAL `applyFx` cut out of `index.html` with its `FX` table, `rvMix` and the cores, 34 cases at sample rates 22050 to 96000.

NOT done: the pad chain around it (`for (const f of V.fx) if (f.on && FX[f.type]) buf = applyFx(...)`, index.html about lines 4972 / 5014) needs the pad voice render (U22: `renderVoice`, the pad's `V.fx` list, stage peaks); the chain loop itself is four lines and calls these functions in order. The other `applyFx` branches (wasm effects, MODFX `modFxRun`, `txRun`) are other units. A `preverb` with no `size` makes a JS exception (`Float32Array.set` into a length-NaN array); `pad_pitchfx` returns an empty vector, and the case is in the test.

## Reference and gate

`native/tools/fx-u17-dump.mjs` runs the real worklet (cores cut out of `index.html` by `fx-src.mjs`, as U15 / U16) and the real `applyFx` in Node. It reuses the signals and `runCase` of `fx-clk-dump.mjs`. `test_fxu17` compares every case sample by sample (bit-exact is the gate) and with the committed FNV hash `tests/golden/fxu17_golden.inc`. `parity-u17.sh` runs the gate; ctest `fxu17_ref`, `fxu17`, `fxu17_golden_current` (Linux), plus `fx_table_current`.

The committed golden hashes were made on x86-64 Linux, Node 22 (the cores call Math.exp / pow / sin / cos / tanh). The gate is the committed hash on every platform; the sample-by-sample comparison with the LOCAL Node run must also hold on x86-64 Linux and is reported elsewhere (macOS arm64 Node rounds some of those differently): the rule the lead set after U16.

Fixture: 233 cases, 156 MB, about 21 s of Node (U16: 800 MB, 145 s). The clock-free cases write no `.bp.f32`.

## Cases (233, all bit-exact)

Degrade 45 (BITS x8, RATE x7 including 255 = off, NOISE x FOLLOW on a quiet signal, COLOR, DRY / WET, loud, tail, 12 random). Resonator 42 (CHORD 0 to 11, NOTE 24 to 96, STRING / TUBE, DECAY ends including a 8 s ring, BRIGHT, SPREAD, MIX, loud clipping of the loop, quiet, 12 random). Pitch delay 39 (every MODE at default and at FEEDBACK max, PITCH 0 / 48 per mode, TIME 0 / 1 / 255 with the full 2 s line, Arp and Reverse at long times, TONE, WOBBLE, MIX, loud, 12 random). Pitch reverb 40 (every MODE x SIZE 0 / 128 / 255, SHIMMER 0 / 255, PITCH, PITCH 2 in Dual, TONE, MIX, two 2.5 s runs at maximum SIZE and SHIMMER for the slow limiter, 12 random). Host 33: block sizes 1 / 37 / 128 / 480 / 1024 and a mixed list with the four in series, series with every command, FXO off / on, chain sets, FXV (`pset`), a resting node, a chord change while it rings, the four beside Grains / VSamp / Retrigger on the beat clock, the node's sleep (3 s) for each kind on / off, and the slot nap (a 31.5 s case with a Retrigger holding the node awake so the 30 s nap is reached for the three that nap). Pads 34. Plus the `map()` of the four kinds on 200 value sets each (missing, null, strings, out of range) as doubles, and two hosts side by side on nine cases. A deliberate error (the shifter's 0.75 changed in the last digit) makes 100 cases fail: the gate bites.

Result: all 233 bit-exact with g++ 13 and with clang++ 18 (Linux x86-64, Release); full ctest 83 of 83 on each.

## Quirks kept (they are the JS)

- `PitchFxCore.process` computes `rvMix` (a sin and a cos) every block even for the delay; `fenv` (the shimmer limiter's envelope) starts at 0 and is only touched in reverb mode; `mode` 3 or more in the reverb does nothing to the loop (`f = 0`).
- Pitch delay: one `step` / `cnt` / `wph` / `rev` for both channels, counted once per sample after the channel pair; the right channel's read is 0.7 ms earlier; `tsm` snaps to the target while below 1 sample.
- Resonator: only the voices of the current chord are re-tuned each block; the others keep their ring state until the chord grows again (the "chord change" case).
- Degrade: the noise filters (`nl`, `nr`) and the generator only run while the noise level is above 0; the hold clock (`ph`) is not reset when RATE goes to OFF.

## Not sure / left

- Pad chain / `V.fx` loop (U22), above.
- `Math.tanh` / `sin` / `cos` inside V8's optimising compiler (TurboFan) assumed the same fdlibm functions as the interpreter (true for Node 22); real Chrome is step 11, as for U15 / U16.
- The worklet's `tr` (transport) argument is passed to these cores in the JS and ignored by them; not passed here.
- Pad values that are NaN in the JS because they are `undefined` and values that are NaN by arithmetic are the same thing here; the page cannot make a literal NaN (JSON has none).
