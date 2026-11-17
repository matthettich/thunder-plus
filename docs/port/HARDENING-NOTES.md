# Hardening, 17 Nov: the small items of REVIEW-16NOV.md (B1, B3) and TODAY.md ("Evening, 16 Nov", 5)

Branch `n1/hardening`. Every item is C++ undefined behaviour or an unchecked assumption where the JS gives a defined result. The tests are in `native/tests/test_hardening.cpp` (ctest `hardening`, one named
check per item; built with the sanitizers in CI). No golden row was added, changed or regenerated: every existing golden test passes unchanged (see "Results").

## 1. B1-2: `FlexCore` 5-tuplet, `kTup[(int)j]` with j = -1
- File: `native/core/src/fx/clkcores.cpp` (PK_tup, line 132 before; now `flex_tup_slot`, declared in `clkcores.h`). JS: `index.html` `FlexCore.pat` `case 'tup'`, `sj = o.q === 5 ? FlexCore.K.tup[j] : j`.
- JS: `K.tup = [0, 0, 1, 2, 3]`; `j = Math.min(q - 1, Math.floor(u * q + 1e-9))` is -1 when `u < -2e-10` (the position a hair below a beat boundary: `cs()` rounds the boundary up by 1e-9). `tup[-1]` is `undefined`, `sj / 4` is NaN, the read position of that one sample is NaN.
- C++ did: `kTup[(int)-1]`, an out-of-bounds read of whatever precedes the array (also `(int)NaN` for a NaN j).
- Now: `flex_tup_slot(j)` returns the table entry for an integer j in 0..4 (and -0, which is the JS key "0") and NaN for anything else, so that sample is NaN as in the JS. No read outside the table.
- Test: `B1-2`: the table, -0, ten indices outside it (-1, -2, 5, 6, +-1e9, 0.5, NaN, +-Infinity) give NaN; the j the position `u = -3e-10` makes is -1 and the read position comes out NaN. (Through `FlexCore::process` the state is only reachable with a double position in [nC - 1e-9, nC - 2e-10 C), which the golden cases do not produce; the helper is tested directly.)

## 2. B1-5: `sinT` with `p - floor(p) == 1`
- File: `native/core/src/modfx/util.h` `sinT`. JS: `chains/chains-worklet.js:36`.
- JS: a tiny negative `p` makes `p - Math.floor(p)` exactly 1 (`-1e-20 - -1`), `x = 4096`, `i = 4096`, `SIN[4097]` is `undefined` (the table has 4097 entries, 0..4096): `SIN[4096] + (undefined - SIN[4096]) * 0` is NaN.
- C++ did: `kSin[i + 1]` = `kSin[4097]`, one past the table.
- Now: `if (i >= kTS) return kNaN;` (i can only be 4096 when `p - floor(p) == 1`). NaN, +-Infinity and all other phases are as before (NaN input gives i = 0 and a NaN sum, as the JS).
- Test: `B1-5`: -1e-20 and -1e-17 give NaN; 0, 1, 1/4, -1/4, the largest phase below 1 and NaN / Infinity are what they were.

## 3. B1-7: NaN-to-int casts and the `miss` counter
- Bytebeat, `mod_gen.cpp` (`(int32_t)clamp(js_round(p[2..4]), 0, 255)`). JS: `clamp(Math.round(p.a), 0, 255)` stays NaN and is passed to `s8(t, a, b, c, mode)`, whose `exec` stores it in an `Int32Array` (`St[++s] = a`): NaN is stored as 0. C++ did `(int32_t)NaN` (UB). Now `i32(...)` (`x | 0`: NaN is 0, a value already clamped to 0..255 is itself). Test: all 16 formulas x 3 modes with a, b, c NaN render bit for bit as with 0 (and the renders are not silent).
- PitchTrk, `mod_cv.cpp` (`(int)jmod(w_ - W - tMax - 1 + N * 2, N)`). JS: `tMax = Math.min(d.length - 2, Math.ceil(rs / Math.max(20, P.min)))` is NaN when `min` is NaN; `o` is NaN but `for (t = 1; t <= tMax; ...)` never runs, so `o` is never read. C++ did `(int)NaN` (UB, harmless value). Now `i32(...)`. Test: a NaN `min` for 120 blocks (the detector branch runs), finite outputs; under the sanitizers the cast is itself the failure.
- PitchCorrect `++miss`, `mod_voice.cpp`. JS: `else if (++st.miss > 2) st.voiced = 0`, `miss` a Number that keeps counting (exact to 2^53; only `> 2` and `miss = 0` ever read it): it just keeps being > 2. C++ `++miss` on an `int` overflows (UB) after 2^31 control blocks, about 66 days of continuous silence. Now `miss = missInc(miss)` (util.h): counts up to `kMissCap` (2^20) and stays there; `voiced` comes out the same because `miss` has no other reader. Test: `missInc` counts, saturates at the cap and at INT_MAX, and stays above 2 after 1.5 x the cap steps (a run to 2^31 cannot be done in CI time; the primitive is what is tested).
- Not changed, said here: the same `(int)clamp(js_round(p[k]), ...)` shape is in `mod_basic.cpp` (lines 58, 101, 280), `mod_voice.cpp` (214, 386) and `mod_cv.cpp` (309: `(int)clamp(js_round(io.p[0] * c.sr), 0, M - 1)`). The JS there uses the value as a table index / switch where NaN gives `undefined` / a different branch, so `i32` (0) would not be the JS's answer; they need their own reading of the JS and were not in the list.

## 4. B1-4: `pad_pitchfx` with a saved null
- Files: `native/core/src/fx/u17cores.{h,cpp}` (`PadPitchFx::sizeNull`, the tail), `engine/engine.h` (`EngFx::sizeNull`), `engine/engine_json.cpp` (the loader: where the null is told from an absent key), `engine/engine.cpp` and `engine/xeng_voice.cpp` (`pitchTail`, the two copies of the tail formula for the render bound). JS: `index.html` `applyFx`, `tail = Math.round(sr * (rv ? 3 + f.size * 4 : Math.min(8, f.time / 1000 * (1 + 6 * f.fb))))`.
- JS: `JSON.stringify` writes null for a NaN, so a saved voice can hold `size: null`. `null` is 0 in a sum: `3 + null * 4 = 3`, a valid 3 s tail; a key that is absent is `undefined`, the tail is NaN, `new Float32Array(NaN)` is empty and `L.set(a)` throws a RangeError (the voice is refused). The core itself reads `f.size != null ? f.size : 0.6`, so null and undefined both give 0.6 there.
- C++ did: the loader read a JSON null and an absent key alike (NaN), so a null `size` made the tail NaN and an empty vector (a refusal) where the JS renders; same for `time` / `fb` of a Pitch delay (JS tail 0 and a sound).
- Now: `eng_fx_from_json` records `sizeNull` (the key is present and JSON null) and, for the two pitch effects only, turns a JSON null `time`, `fb`, `tone`, `mix`, `mode` into 0 (in PitchFxCore they are only summed, multiplied, compared with `<` / `>` or `| 0`'d, where null is exactly 0). `size` stays NaN (the core's 0.6) but the three tail formulas use `sizeNull ? 0 : size`. A key that is absent, and a real NaN handed straight to `pad_pitchfx` (no flag), behave as before (refused). `pitch` is not converted: Time-Reverse mode tests `p.pitch !== 0`, where null is not 0; a null `pitch` stays NaN (still different from the JS there; not in the list).
- Test: `B1-4`: preverb `size: null` gives n + 3 s and the same sound as `size: 0.6` cut to that length; `size` absent, and a NaN without the flag, are refused as before; pdelay `time: null` gives tail 0 and the sound of `time: 0`; `time` absent is refused; `fb: null` sounds as `fb: 0`; the voice render bound (both copies) treats `size: null` as `size: 0` and `size` absent as no tail.

## 5. B3-3: nothing checked `ctx.block <= kBlock` (128)
- File: `native/core/src/modfx/modfx.h` (`Module`). Reading first: there is no production host for MODFX in `native` yet (`fxhost.h`: MODFX slots are not there); the only callers of `Module::process` were `test_modfx.cpp` and `rt_harness.cpp`. In the JS `Engine.process(frames = BLOCK)` is called by the worklet with one render quantum (at most 128) and sets `ctx.block = frames`; a longer `frames` is a bug there.
- C++ did: `cp(port, src, n)`, the fixed-size state arrays and the 128-float port buffers all assume `block <= 128`; a longer block overran them.
- Now: `Module::run(io, ctx)` is the one way in: a block above `kBlock` (or negative) is REFUSED: nothing is called, nothing is written, `run` returns false and `refused()` counts it. `process()` became protected in the base class, so no caller holding a `Module*` can bypass it (a compile error, tested with a `static_assert`). Both callers use `run`.
- Refuse, not split, because the existing contracts differ: `FxHost::process` splits a longer call into 128s (tcChunks) because its buffers are as long as the call and its lanes and sources are cut with it, and it zeroes the output above `kFxMaxCall`; a MODFX module's ports are `kBlock` floats long by contract, `ctx.key(sck)` is a fixed 128-float buffer with no offset, and the JS never hands more than a quantum. The host that will feed MODFX has to cut its device buffer into chunks of `kBlock` before this, as the worklet gets them. `fill(kBlock)` already writes the whole buffer whatever `ctx.block` is.
- Test: `B3-3`: 0, 1, 37, 127, 128 run; 129, 130, 256, 4096, 2^20, INT_MAX, -1, -128, INT_MIN are refused and never reach the module; every one of the registry's modules (canary-filled outputs) refuses 129, 256, 4096 and -1, writes nothing, and still runs a 128 block.

## 6. `xeng_swarm.cpp:48` and `xeng_drift.cpp:51`: the runaway flag tested one control block late
- JS: a path faster than 1e4 wraps per control step makes `while (q.ph >= 1)` not end within any time a render can take; the C++ refuses (`kRefuse`) instead.
- C++ did: `step()` sets `bad`, but `if (bad) return kRefuse` was the first line of the NEXT control block. A voice of one control block (N <= 32 samples) never reached it and was rendered. (A longer voice was refused one block late, same answer: the path rate and `dt` are constants, so a bad path is bad in the first block.)
- Now: the test is right after the block's steps, before anything is rendered: exact for every N. (The check at the top of the loop is gone: it can no longer be true there.)
- Test: at sr = 200 (N = 15 for swarm, 20 for drift, both one control block) a runaway rate is refused; at 48 kHz it is refused as before; ordinary voices still render at both rates; a swarm just below the limit renders. Checked on the old code: the two one-block refusal checks fail without the change.

## 7. The three low findings from the U29 verification
Skipped, as the task says (not required).

## Results (GCC 13, Release; sanitizer build)
See the PR description for the run of `ctest` on GCC and with `-DTHUNDER_SANITIZE=ON`, and the CI jobs. Golden rows added: none; changed: none.
