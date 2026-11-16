# U20: MODFX modules B (Spectral, Extreme stretch, Convolution reverb, the shared FFT)

Port of the part of the shared MODFX block of `chains/chains-worklet.js` between "Spectral, Extreme stretch and Convolution reverb" (line 2989) and "Pitch correct, Vowel filter ..." (line 3990): `FFTR` with its `FFTR_CACHE`, `Spectral` (`spectral`), `XStretch` (`stretch`), `Convolve` (`convolve`) with the IR maker, on the module interface of U19 (`modfx.h`). The block is identical in `waves/waves-worklet.js` (`modfx-dump.mjs` checks that on every run).

## Files

| file | what |
|---|---|
| `native/core/src/modfx/fftr.h`, `fftr.cpp` | `FFTR`: tables (bit reversal, twiddles, the split's) and the cache `FFTR::get(n)`, `FFTR::hann(n)`; the passes are templates over the element type of the arrays (Float32Array rounds on a store, Float64Array does not) |
| `native/core/src/modfx/mod_spectral.cpp` | Spectral, Extreme stretch, and the helpers they share (`fxSafe`, `sinR` / `cosR`, `wrapPi`, `tiltGain`) |
| `native/core/src/modfx/mod_convolve.cpp` | Convolution reverb: the three-stage partitioned convolution, the job scheduler, the IR maker (rooms by the image method, plate, spring, gated, reverse, cab, telephone, song IR) |
| `native/core/src/modfx/mods_b.h` | the two `add...Mods` of this unit (a header of its own: `mods.h` is unchanged) |
| `native/core/src/rt/fdlibm.cpp`, `jsmath.h` | `js_atan2` (V8 12.4 `ieee754::atan2` and `atan`) and `js_hypot3` (V8's `Math.hypot` builtin: scaled, Kahan-summed) |
| `native/tools/modfx-b-cases.mjs` | the direct cases of the three modules (own file, hooked into `modfx-dump.mjs`) and the song-IR maker |
| `native/tools/parity-u20.sh` (+ `parity.sh u20`) | the gate |

## Gate and results

1. Engine run: the committed `tests/golden/chains-modules.json` hashes of `spectral`, `stretch`, `convolve` (default / stressed / switches-on): 9 of 9 reproduced.
2. Direct cases (see `modfx-b-cases.mjs`): Spectral 26 (every mode with Freeze on and off, Blur to 12 s, the feedback spiral of Shift, NaN / infinity / |x| >= 8 inputs, partial blocks, 44.1 / 96 kHz), Extreme stretch 23 (every window size, freeze, jumps of the read head with the 1 s crossfade at 8 kHz, restarts of the song, silences, partial blocks), Convolution reverb 50 (every one of the 14 types, corners of Size / Decay / Tone / Early, a change of control while it plays and the crossfade to the new IR, the song's IRs: mono, stereo, 96 kHz (the anti-alias path), 8 kHz, 22.05 kHz, longer than 6 s, with a NaN in it, an empty slot, swapped while it plays, removed; partial blocks; 8 / 44.1 / 96 kHz). All 99 bit-exact against the real worklet, g++-13 and clang++-18 (Release). The "convolution of a test IR within -80 dB" gate is therefore met with margin: the song-IR cases (`convolve_t10..t13`, `c10..c13`, `irswap`) are bit-exact, not just under -80 dB.
3. The committed FNV-64 of every case is in `tests/golden/modfx_golden.inc` (regenerate with `node native/tools/modfx-dump.mjs --golden`); ctest `modfx_golden_current` (Linux) proves it is what the JS gives now. The sample-by-sample comparison with the local Node is enforced on x86-64 Linux only (as U19).
4. libm: `js_atan2` and `js_hypot3` are in the golden of ctest `libm` (now eight columns; the first six are unchanged: checked) on 400000 arguments plus a table of special arguments (zeros, infinities, NaN, 2^70, 1e-310 ...): bit-exact.
5. ctest `rt_core` (`rt_modfx`): every module of the registry, the three included, 3 sample rates x 1500 blocks under the allocation guard.

Mutation checks (a changed constant makes the named cases fail): the crossfade length of the IR swap (`convolve_chg0/chg1`), the spring's all-pass coefficient (`t5_spring`), the Song IR anti-alias filter (`t12_song3`, `c10`), the jump's crossfade (`stretch_jump0`), the restart jump (`stretch_jump1`), the feedback amount of Shift (`spectral_mode5`).

## Decisions

- **Song IRs: `Ctx::irs`** (`const SongIR* const*`, four entries, a null entry is an empty slot, null for no IRs at all) is the JS's `globalThis.__thunderIR`. `SongIR` is `{L, nL, R, nR, sr, id}` (read-only views; `id` is the JS's id string as a number, equal strings giving equal numbers). The module compares the pointer and the id as the JS compares the object and the id: when the song's IRs change, the host must hand a NEW `SongIR` object (the JS does: `irsLive()` makes new objects), and it must keep the old buffers alive and unchanged while a module may still read them (the JS's garbage collector does that there; U31: a retire list, or a copy made by the module when an IR is taken: the module keeps raw pointers `srcL / srcR` while it makes the IR, about 100 blocks).
- **The FFT cache.** `FFTR::get(n)` / `FFTR::hann(n)` make a size's tables once, process-wide, under a lock, and keep them for ever; the tables are immutable, so any thread may read them. They allocate, so they are called only from module constructors (the JS does the same in `create()`: "made now, not on the audio thread later") and a module keeps the pointers; nothing calls `get()` in `process()`. **Where the allocation happens: module construction (the three constructors), outside the allocation guard.** U31 must construct the modules (`Def::make`) off the audio thread too; U19's modules already allocate their rings there.
- **The JS's `FFTR` owns one job** for its convenience `forward()` / `inverse()`, shared by everything that uses that size. A job is scratch that `fwdStart` / `invStart` overwrite completely, so the result does not depend on whose job it is; here every module instance passes its own `FFTR::Job` (so two instances on two threads cannot meet). Bit-exactness is unaffected (proven by the cases).
- The JS allocates nothing in `process()` for these three modules (checked line by line: `FFTR.get(N)` in `xstHop` / `xstUnit` / `cnvFrame` is always a cache hit on a size made in `create()`), so there is nothing to mirror outside the guard.
- float32 rounding follows the JS Float32Arrays exactly: the spectra (`re`, `im`, `pw`, `Spectral`'s state, `Convolve`'s partitions `H` / `X`, the history rings, the IR itself) are `F32` (rounds on a store); the FFT job, `Y`, `x` of Convolve and the IR maker's state are doubles. Values such as `const ph = QS[j] = wrapPi(...)` keep the unrounded double for the expression, as the JS does.
- xorshift32 is a `uint32_t` (the JS keeps an int32 and reads `r >>> 0`: same bits). `Math.hypot` with three arguments (the IR's mirror-image distances) is V8's builtin, not fdlibm: `js_hypot3`.

## Merge notes (U21 is in parallel)

- Registry: `modfx.cpp` gets one new function `regAll()` (the families of `reg()`, then mine) and `find` / `count` / `at` read it; U21's edit of the `reg()` lambda line does not touch those.
- `modfx-dump.mjs`: the hooks are one import, `MODS.push(...MODS_B)`, one line in `casesFor` (`casesB`), and in `runDirect` four small ones (parameter schedule `c.pb`, own signal maker `c.mkSig`, the IR table per block, `irs` in the meta). `test_modfx.cpp`: one block (`irMake`, the IR schedule in `runCase`).
- `tests/golden/modfx_golden.inc` is generated: after merging U21, re-run `node native/tools/modfx-dump.mjs --golden` (the order of `MODS` decides the seeds of the direct cases, so both units' hashes move when the order changes).
- `native/core/CMakeLists.txt`: one `target_sources` line.
- `FFTR` is in `fftr.h` for the Morph module (line 5623 of the chains worklet, outside the shared block) and anything else that wants the transform.

## Fixture

The three modules add 51 MB to `modfx-ref` (Convolve 27, Extreme stretch 16, Spectral 8; U19's part is 138 MB), making about 189 MB, and about 15 s of Node time. If U21 adds as much again the directory goes over 200 MB: the next step then is to stop writing the input files (`*.in.f32`) for the long cases and make the inputs in C++ (they come from `signal()`, which uses `Math.sin`: it would be `js_sin`, exact on every platform).

## Left out

- `show()` of the three modules (none of them has one; Convolve's `latency: 0` and Spectral's `latency: SPX_N` fields of the JS module object are table data for U31, not ported here: the registry does not carry `latency`).
- The Morph module that also uses `FFTR` (outside the shared block) is not part of this unit.
