# U28: rendered-only engines set 1 (XENG): tone, modal, swarm, drift, piano, ep, organ, breath, strings, fmdrum, wind, phase, partials, skin, chip, and KeysUtil

Status at the first push (work in progress, see "Progress" at the bottom for the latest): all 15 engines ported; **1068 of 1068 factory voices** of beta's `tests/golden/engines.json` whose engine is one of them give beta's hash (bit-exact),
**313 of 313 direct cases** against the real JS. Gate command: `native/tools/parity.sh u28`.

## What is where
- `native/core/src/engine/xeng.h`: the parameter structs (X-macro lists = the JS `defaults()` objects), `XengVoice`, `xeng_voice_from_json`, `xeng_render_voice` / `xeng_render_bound` (renderX + padFx), one `xeng_<name>(...)` per engine.
- One source file per engine, `xeng_<name>.cpp`; `xeng_util.{h,cpp}` is KeysUtil (`KeysBank` = bank / add, `KeysRnd` = rnd, `finish`) plus the tail every engine shares (`trim_fade`, `level_drive`); `xeng_voice.cpp` is `renderX`, `normX` / `xParams`, `hold || Math.min(V.len || k, m)`, the pad effects.
- Shared files touched, each with the smallest edit: `core/CMakeLists.txt` (one `target_sources` line), `rt/jsmath.h` + `rt/fdlibm.cpp` (`js_hypot2`: the existing copy of V8's `Math.hypot` builtin, called with two values), `tests/CMakeLists.txt` (a block at the end), `tests/rt_harness.cpp` (`run_engine_xeng` + one line in `main`), `tools/parity.sh`.
- Test and reference: `native/tests/test_engine_xeng.cpp`, `native/tools/engine-xeng-dump.mjs` (own vm of the ENGINE block), goldens `native/tests/golden/engine_xeng_voices.json` (the 1068 voices), `engine_xeng_voices_golden.inc` (beta's hashes, copied by `--fixture`), `engine_xeng_golden.inc` (the 313 direct cases' hashes, Node 22 x86-64 Linux), `xeng_ranges.json` (min / max / curve / default of every parameter, from XUI: the direct cases draw from it).
- ctest: `engine_xeng_ref` (Node makes the reference: about 4 minutes), `engine_xeng`, `engine_xeng_golden_current` (Linux), `rt_core` runs `rt_engine_xeng`.

## Decisions
- **Output goes into the caller's buffer** (`xeng_render_bound` floats at least), as U22 / U23. `xeng_<name>(p, sr, ..., nullptr, 0)` returns the render length N without rendering (the bound).
- **padFx after the engine** (`engFx`: every non-live XENG engine): ext buffer of `N + round(tail * sr)`, the pad effects, **then padFx's own trim and 2 ms fade** (only when an effect is on and ready; with none the engine's render is returned as it is). Effects other than the four JS ones pass the sound through, as the golden's Node does (U22's decision). Voices with pad effects allocate (U17's `std::vector`).
- **Allocation**: none in `tone, modal, swarm, drift, piano, ep, organ, breath, fmdrum, phase, partials, skin, chip` (fixed arrays for modes / voices / partials; drift's chorus line is a 32 KB stack buffer when `ceil(sr * 0.03) + 4 <= 8192`, a `std::vector` above). `strings` and `wind` allocate (Float32Arrays sized from the pitch and the sample rate: `std::vector`; wind also its quiet run and correlation table): offline render.
- `Math.hypot(a, b)` (the phasor renormalisation of piano / ep / organ): `js_hypot2` = V8's builtin as already copied for `js_hypot3`, with two values. No new maths was written.
- Out of bounds: wind's correlation read `tst[s0 + i + L]` past the quiet run gives NaN (the JS's `undefined`) through a guard; every other index is inside its array by construction (checked), and the chorus read / string loops wrap with a guard.
- `modal`'s hold is `hold || (V.gate || 400) / 1000 * 2` (renderX's last line: modal has no case of its own there); the voice's `gate` is read for that.

## Not here (U29 / later)
table, vector, scanned, resynth, bytebeat, sf, wavesl, acid, mach, lx*, p*: `xeng_voice_from_json` returns false with `V.kind == XKind::Other` and `xeng_render_voice` returns `kEngUnsupported` for them (never silence).

## Progress
- 16 Nov: all 15 engines done; PR #195. Merged origin/native (after #191, #192) cleanly; engine_xeng passes. Local test times (2 cores): `engine_xeng_ref` about 3.5 min, `engine_xeng` 35 s, `engine_xeng_golden_current` 3.3 min. Checks done: GCC and clang Release, GCC ASan + UBSan clean, gcov: every line of xeng_*.cpp executed; mutations (tone dist, piano hypot, strings comb) fail 31 / 25 / 58 voices.
- Left: nothing in this unit. If U24 merges first, merge native again (conflicts expected only in core/CMakeLists.txt, tests/CMakeLists.txt, rt_harness.cpp: keep both sides).
