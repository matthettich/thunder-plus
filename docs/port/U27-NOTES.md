# U27: the live engines Acid and Machine (rendered through renderLiveCore) and the Blip drum (renderBlip)

Branch `n3/u27` (from `native`). Gate command: `native/tools/parity.sh u27` (PASS locally: libm, engine_xeng3_ref / engine_xeng3 / golden_current / fuzz, rt_core).

## State
- Ported line by line: `native/core/src/engine/xeng_live.{h,cpp}` (AcidCore, MachCore, renderLiveCore) and `xeng_blip.cpp` (BlipVoice + renderBlip: index.html's copy, NOT the chains copy that `modfx/mod_misc.cpp` has, which differs, e.g. `Math.max(2, decay)`).
- `ctest engine_xeng3` (local, Release GCC): **185 of 185 factory voices** of beta's `tests/golden/engines.json` (acid 96, mach 34, blip 55) equal beta's hash; **461 of 461 direct cases and core scripts** (acid 128, mach 190, blip 143) equal the committed Linux hash and the local Node.
  The set: counted by beta's own listing tool (`tools/engine-golden.js` `listVoices`, beta at adda1713, 2142 listed voices): `--fixture` fails if the set it writes differs from a second count of the listing.
- Not run here: macOS, Windows (CI). clang: `-fsyntax-only -Wall -Wextra` clean on the new files.
- No golden output contains NaN or infinity (the dump tool refuses to write one), so a NaN sign bit never decides a hash.

## What is where
- Reference / fixture tool `native/tools/engine-xeng3-dump.mjs` (`--fixture <beta>` writes `engine_xeng3_voices.json`, `engine_xeng3_voices_golden.inc` (beta's rows, copied) and `xeng3_ranges.json` (XUI for acid / mach, BLU for blip); `--golden` writes `engine_xeng3_golden.inc`; `--check`; plain = the reference for ctest). Own vm of the ENGINE block, as U28 / U29.
- Test `native/tests/test_engine_xeng3.cpp` (ctest `engine_xeng3_ref`, `engine_xeng3`, `engine_xeng3_golden_current`), fuzz `test_engine_xeng3_fuzz.cpp` (`engine_xeng3_fuzz`), `rt_engine_xeng3` in `rt_harness.cpp`.
- Shared files touched with the smallest edit: `xeng.h` (three parameter lists, `XKind::Acid / Mach / Blip`, three entry points), `xeng_voice.cpp` (names, `runEngine` cases, a template flag in `fill` because a Blip keeps its own `drive`, `anyFxOn` is false for acid / mach), `core/CMakeLists.txt`, `tests/CMakeLists.txt`, `tools/parity.sh`, `tests/rt_harness.cpp`.
- Case families: factory voices (185); random parameters from the cards' ranges (36 acid, 42 mach, 34 blip) at 22050 to 96000 Hz with Level / Tune / Drive / len / gate / hold; every wave, pulse width, resonance, cutoff, machine (also out-of-range values), A / B / C ends, LFO destination (-1 to 4), glide, filters, envelopes; every blip filter / grain / FM / ring / kick / noise / click / drive value, the pad effects on Blip; holds that clamp (40 s at 4 kHz), fractional sample rates, rates 1 and 100; and **core scripts** (below).

## Decisions
- **What a pad render reaches.** `renderX` plays ONE plain note: `on(false, false)` for Acid (accent 0, no legato / slide), `on(false)` for Machine. So the accent, slide and legato code of AcidCore / MachCore is never run by a factory voice. The unit still ports it (the live path needs it) and gates it with **core scripts**: the dump tool drives the JS cores directly (note on with legato and accent, several notes, a new rate every block, `set()` between notes, blocks that are not multiples of 16, fractional block bounds, `renderLiveCore` with accent) and the test drives the C++ cores the same way; R must equal L bit for bit.
- **Live-path reuse (interface provisional, U26 / the lead decide).** `AcidCore` and `MachCore` (`xeng_live.h`) are plain objects with explicit state: no allocation, no locks, no I/O, `set / on / off / render(L, R, len, a, b, rate)` as in the JS (blocks of 16, `R` may be null). `len` is the length of L: the JS writes a Float32Array out of range into the void, here the write is skipped. `xeng_acid / xeng_mach / xeng_blip` are the rendered entry points. Nothing about lanes or node shells is designed here.
- **Pad effects**: Blip runs them (renderVoice: `padFx(V, renderBlip(V, sr))`), Acid and Machine do not (`engFx` is false for a live engine), as the JS.
- **Typed-array semantics** that `renderLiveCore` / `renderBlip` depend on are kept: `new Float32Array(N)` truncates N (NaN gives 0), a fractional N makes a last block of fractional length and ignores the writes at fractional indices, `L[N - 1]` of a fractional N reads `undefined` so the trim loop stops at once.
- **`set(o)`** (non-finite becomes the default) is in `AcidCore::set` / `MachCore::set`; a NaN put into a parameter struct by an API user behaves as the JS's set() makes it.
- `Math.max / min` keep NaN (`l23::mx / mn`); every `Math.*` is a `js_*` function; no new helper was needed.
- The mach Swarm's `this.ph` is a `Float64Array(10)`: a voice index of 10 or more reads `undefined` (NaN, so the whole output is NaN, as in the JS) and writes nothing. The loop therefore runs only to 11 voices (the rest could only add NaN); more than 1e4 voices is refused (the JS never finishes: loops that often per sample).

## Unsupported (kEngUnsupported; the bound call gives 0)
- The JS throws: `new Float32Array(negative)` (a negative Release long enough: `renderLiveCore`'s N; a Blip of negative length), Machine Ensemble with A = NaN (`CHORDS[NaN].length`: reachable only if the LFO phase runs to infinity, e.g. LFO rate 1e308 at 1 Hz).
- The JS returns an empty sound (N between -1 and 1 after truncation, Blip likewise): refused as well, not rendered as zero samples.
- A render longer than 2^25 floats (a 12-minute render at 48 kHz): the JS would allocate whatever hold and rate say; here it is bounded. A Machine Swarm of more than 1e4 voices (above).
- sample rate not in (0, 1e7] (as U28 / U29).

## Mutation checks (one constant at a time, then restored; counts are failing items of 185 voices / 461 cases at the time)
acid DC filter 0.997 to 0.996: 96 voices, 125 cases. mach amp 0.35 to 0.36: 34 voices, 174 cases. blip repeat gain 0.72 to 0.73: 7 voices, 38 cases. blip click window 0.0015 to 0.0016: 53 voices, 133 cases. renderLiveCore tail 1.6 to 1.7: 11 voices, 56 cases. mach noise colour 0.95 to 0.96: 2 voices, 14 cases.
Found by direct cases only (a factory voice cannot reach them): acid accent sweep 3.2 to 3.3: 33 cases; mach chord 9 (0,5,10,15) to (0,5,10,16): 4 cases (now ten `mach_chord<n>` cases); acid slide threshold 0.5 to 0.6: 3 cases.
Two nothing caught, so cases were added (`mach_glide_edge*`, `mach_glide_retrig*`, `acid_slide_edge*`): Mach's non-legato glide threshold (now 1 case fails). Mach's legato branch threshold (`glide > 0.5 && leg`) is an **equivalent mutant**: the JS has two `else if` branches with identical bodies, so raising only the first changes nothing.

## Fuzz
`engine_xeng3_fuzz`: every parameter of the three engines at 16 extreme values (-1e308, -1e9, -1000, -1, -0.5, 0, 1e-300, 0.5, 1, 2.04, 100, 1e9, 1e308, NaN, +inf, -inf) alone, all fields extreme at once, the voice's tune / drive / level / len / hold, sample rates -1 to 1e9 / NaN, and a combined-fields mode (2 to 7 fields at once, plus machine / LFO switches, random sample rate 1 to 96000 and hold from {NaN, 0, -1, 0.001 ... 1e9, inf, 1e308}, a 20 s limit per render). ctest: 300 combined, 3208 renders, about 11 s. Under ASan + UBSan (`-DTHUNDER_SANITIZE=ON`) with `FUZZ_COMBINED=6000`: 9444 renders (6000 combined, 593 refused, 0 failed); `engine_xeng3` and `rt_harness` also clean there. The sanitizer build directory is deleted.

## Least sure
- Platforms other than Linux x86-64 were not run here. Math goes through the `js_*` helpers and `-ffp-contract=off` is the core's setting, as in U28 / U29; CI is the check. A platform that differs from the committed Linux hash is a finding to explain, not a hash to change.
- Fractional sample rates are exercised by 6 cases (they make N fractional only when `sr * 32` or `sr * 12` clamps the length).
- The NaN-valued renders (Machine Swarm with 11 or more voices, B above 1.8) are not hashed (a NaN's sign is the CPU's); they are in the fuzz (no crash, bound respected) and one asserts the output is NaN-valued like the JS's.
- The live interface (`xeng_live.h`) is provisional.
