# W-TFX notes: the effects library on Windows through clang-cl (builder log, Sonnet, 26 Nov 2026)

Plan: `W-TFX-PLAN.md` (section 7 overrides). Branch `n4/w-tfx` from the plan branch (native 4b646448). PICKUP: clang-cl core job green; desktop shell on fallback (a) in CI (see "Lead's decisions"); W-PUMP item: live_meters2 must pass (3 checks listed under "For W-PUMP") and its exclusion in `windows-clangcl` be dropped.

## Done (before the first CI round)
- Step 2: `native/core/dsp/CMakeLists.txt` gate is `MSVC AND NOT Clang` (cl.exe configures skip as before, only the message changed).
- Step 3: flag translation inside `dsp_group()` for clang-cl only (`DSP_CLANGCL`): `-O2/-O3`, `-ffast-math`, `-fno-fast-math`, `-ffp-contract=off` -> `/clang:`; `-fno-rtti` -> `/GR-`; `-ffunction-sections` -> `/Gy`; `-fdata-sections` -> `/Gw`; `-w` -> `/w`; `-include` -> `/FI` (same order); `_USE_MATH_DEFINES` on every dsp object. An unknown flag is a configure error (no silent drop).
  `-fno-exceptions` is NOT passed to any group on clang-cl (plan section 3), so mods and pe are built WITH exceptions on Windows where Linux builds them without: no sample difference, only unwind tables. (Plan section 7.4 says "as on Linux unless clang-cl needs otherwise"; section 3 is the more specific rule and avoids `/EHsc` vs `-fno-exceptions` ordering. The lead can ask for `/clang:-fno-exceptions` on mods/pe later.)
- Step 4: CloudSeed `strcpy_s`: `CS_OPTS` empty on clang-cl; `host_std_shim.h` defines `strcpy_s(a,b,c) -> strncpy(a,c,b)` under `_MSC_VER`, after `<string.h>`.
- Step 6: `native/core/CMakeLists.txt`: `/clang:-ffp-contract=off` next to `/fp:precise` when the compiler is Clang (cl.exe unchanged).
- Step 7: CI job `windows-clangcl` (`-A x64 -T ClangCL`, default generator: the runner has Visual Studio 18, not 2022; 90 min), with the probe (step 1) as its first step, a check of `/MD` / exception settings in the generated vcxproj files, the full ctest, and a strict subset step (`fx fxzero padfx live_meters2 live_chlvl live_masterm fxsong modfx dsp_layout`: no `***Skipped` / `Not Run`, `TFX: yes` printed by the three live tests). Artifact `thunder_core-windows-clangcl-x64`. The cl.exe `windows` job is untouched. Not done yet: `native-desktop.yml` on `-T ClangCL` (needs this job green first).
- Step 5 / B2 (Beads 12-bit items): a hosted path DOES use them: `fx_beads.cc` `setup()` configures `Processor::Media` 0..3, and media 1 (Vintage digital) and 2 (Clean tape) are `RESOLUTION_12_BIT` (`processor.h`), and `padfx_mi.cc` does not host Beads. Under the MS ABI the two packed bit-field structs are expected to be 4 bytes (the probe step prints the unpatched `sizeof` under clang-cl). So, per the lead's correction 2, `prepare.sh` now packs the 3 bytes explicitly on every non-wasm host build (the wasm keeps the original bit-fields through `#ifdef __wasm__`, so the wasm bytes do not change). Verified on Linux with a differential harness (original bit-field vs the patch, g++, 20000 random stores each, both structs): identical raw bytes and values, size 3 both. `tests/test_dsp_layout.cpp` (`dsp_layout`) has `static_assert(sizeof == 3)` and checks the layout against an independent reference.
- Step 8: pe and mods come with the same directory; nothing extra needed in CMake. `thunder_dsp_w` builds in ALL on host (not measured yet; make it `EXCLUDE_FROM_ALL` if the CI time says so).

## Probe result (step 1)
(pending the first CI run)

## Windows figures: fx / padfx / modfx beside Linux and Mac
(pending)

## Flags clang-cl accepted / ignored
(pending: read the first build log for "unknown argument" / "argument unused")

## Pump (next unit, W-PUMP)
`pump/CMakeLists.txt` has the same `if(MSVC) return()`; `live_fxvalues` and `live_fxvalues_alloc` keep skipping on Windows until it follows.

## CI round 1 (44abc006): probe output
- Runner: Visual Studio 18 Enterprise (MSVC 14.52.37025), so `-G "Visual Studio 17 2022"` fails ("could not find any instance"): the job now uses the default generator. cmake 4.5.0, GNU sed 4.9, Python 3.12.10, `sh` and git on PATH; node 22.
- `C:\Program Files\LLVM\bin\clang-cl` is 20.1.8 and the VS 18 STL rejects it (STL1000 "expected Clang 22 or newer"); the VS-bundled `VC/Tools/Llvm/x64/bin/clang-cl.exe` is the one `-T ClangCL` uses (the probe now prints its version and compiles the layout probe with it).
- fetch.sh + prepare.sh with forward-slash drive paths (`D:/a/_temp/...`): rc=0 (B10 needs no cygpath).

## CI round 2 (00d7bb42): first clang-cl build
- Configure with `-A x64 -T ClangCL` works: "Clang 22.1.3 with MSVC-like command-line" (VS 18's bundled LLVM). dsp object command line as intended: `/EHsc /MD /GR- /std:c++14 /FIcstdio /clang:-O3 /clang:-fno-fast-math /clang:-ffp-contract=off /w /D TEST /D _USE_MATH_DEFINES`, `--target=amd64-pc-windows-msvc`; `/MD` present (the blanked CMAKE_CXX_FLAGS_<cfg> did not lose it), `/EHsc` present. No "unknown argument" warnings (the build log has 0 warnings: `/w` silences the dsp groups, so unknown-argument warnings would show only for the core).
- The whole build (229 projects) ran in 59 s of msbuild time and failed only in `thunder_dsp_w_fx_1/2` (w_density.cc, w_totape.cc: the web's sources do `#define private public`, which the MSVC STL's xkeycheck.h forbids). thunder_dsp_w is wasm-only (no host target links it), so it is not built on clang-cl now. Every tfx / mods / pe object compiled with clang-cl at that point.
- Probe: the `/nologo` of the layout probe was turned into a path by Git Bash (MSYS_NO_PATHCONV now set).

## CI round 3 (80a138cd): the rest of the tree under clang-cl (the job builds ALL)
- tfx / mods / pe: all objects and libraries built (nothing failed there). The failures were outside the dsp libraries:
  1. `dsp_smoke_w` links `thunder_dsp_w`, which is not made on clang-cl now: the smoke test is built only `if(TARGET thunder_dsp_w)`; `thunder_dsp_w` is not made on clang-cl at all (wasm-only; w_density.cc / w_totape.cc `#define private public`, forbidden by the MSVC STL).
  2. `native/desktop/shared/posix_shim.h` (H6, force-included into the Android shell's files) did `#define __attribute__(x)` for cl.exe; under clang-cl that macro breaks clang's own `<mmintrin.h>` (38 errors in `thunder_desktop_shared`). Guarded with `!defined(__clang__)` (cl.exe unchanged). Finding for plan step 7: the H6 shell's shared library needed only this one fix to compile under clang-cl; its WebView2 half is built by `native-desktop.yml`, not by this job, so that is still untested.
  3. `test_live_c`, `test_midiport_c`, `test_render_c` (C files with -Werror): clang-cl warns "overriding -ffp-model=precise with -ffp-contract=off" for every core consumer (518 warnings): the explicit contract=off wins as intended; `/clang:-Wno-overriding-option` did NOT silence it (round 4: the C tests, /W4 /WX, still failed), so on clang-cl the core now passes only `/clang:-ffp-contract=off` and not `/fp:precise` (precise is clang-cl's default model anyway).
- Probe result (round 4, B2): **the unpatched upstream bit-field structs are `sizeof A=4 B=4` under clang-cl** (3 on Linux/Mac/wasm): the lead's expectation held; the prepare.sh explicit 3-byte layout is therefore needed, and a hosted path uses it (Beads media 1 and 2).

## CI round 4 (81632868): first full ctest on clang-cl (176 tests, 4 failed, 31 min of ctest)
- Build: green, 4.5 min of msbuild for everything. Object command lines carry `/EHsc /MD /GR- /std:c++14` and the `/clang:` flags; no LNK2038.
- RAN and passed: fxzero, dsp_layout (sizeof 3), modfx (Marbles / Tides within their -100 dB gate and committed hashes), fxsong, live_fx, live_capi, live_chlvl, live_masterm (TFX: yes), render_api_oom, dsp_smoke_tfx, dsp_smoke_mods, the pe layer tests. Skipped (not in the strict list): padfx_oom ("no allocation hooks on this platform"), the pump tests, chains_mem / chains_alloc and the `_alloc` live tests.
- FAILED: `fx`, `padfx`, `chains` (one case): all one cause, the **space** effect (Cloud Seed): e_space_def -13.3 dB, e_space_alt +3.1 dB, fx_space_def -13.7, fx_space_alt -5.7, chains mod/space/stressed RMS -3.16 dB. Every other case of the 34 matches (list in the next section). Cause found by reading the sources, not an argument-order effect: `ModulatedAllpass.h` / `ModulatedDelay.h` compute `0.01 + 0.98 * std::rand() / (float)RAND_MAX`; UCRT's RAND_MAX is 32767 while wasi-libc's / glibc's is 0x7fffffff and `thunder_wasi_rand` returns 31-bit values, so every modulation phase was far above 1. Fix: `wasi_rand.h` (force-included into the cloudseed and airwindows groups) sets `RAND_MAX` to 0x7fffffff under `_MSC_VER`.
- FAILED: `live_meters2` (3 checks, parts 3 to 7): all about the pump: "slots mask is 0b111 for a chain of 3" (the pump slot is not hosted), "both dynamics slots carry type id 7 and 201", "the pump COMP slot shows gain reduction (deepest 0.00 dB)". It cannot pass before W-PUMP; the test is not changed. The job now runs it as its own step (continue-on-error) and excludes it from the main ctest run; **the lead decides** whether it stays that way until W-PUMP.

## CI round 5 (269de6ad): clang-cl job green
- `windows-clangcl`: build + full ctest (176 tests with `desktop_tfx`, `live_meters2` excluded from the main run and gated by its own step) 100% passed; the strict subset (fx, fxzero, padfx, live_chlvl, live_masterm, fxsong, modfx, dsp_layout, 11 with fixtures): no skip, `TFX: yes` twice. The `space` effect is inside its gate after the RAND_MAX fix. `live_meters2` alone: 615 checks, 3 failed, all pump (W-PUMP).
- Linux clang, Linux GCC, macOS, wasm (parity u02/u03/u18/u33: the Beads patch is `#ifndef __wasm__` so the wasm bytes are unchanged) and cl.exe passed on the same code, except the cl.exe job's `desktop_input_capture` ("meters: one peak per armed channel", a timing-dependent desktop test that does not touch anything changed here; it had passed one push earlier): to be seen again on the next run.
- Android debug APK failed once on Maven 429 (Too Many Requests): infrastructure, not this change.
- Added `native-desktop.yml` job `windows-clangcl` (a parallel copy of `windows` with `-T ClangCL`, no sweep, not replacing the cl.exe job: that is the lead's call). Owner's PC: Visual Studio Installer > C++ Clang tools for Windows is needed for `-T ClangCL` (HANDOVER line to be added when the desktop job moves).
- The temporary workflow `w-tfx-figures.yml` that printed the Linux and macOS figures is deleted; its logs (run 17d5ef06) expire with the run retention, the table below keeps the figures.

## Windows (clang-cl) figures beside Linux (GCC) and macOS (arm64), run 17d5ef06 (dB vs the wasm / worklet reference)
Summary lines (Linux / Mac / Windows): fx worst -68.526 dB (e_pshift_def) on all three, 0 failed; bit-exact cases 47 / 46 / 48 of 91; padfx worst -57.245 dB (rv_noseg_beads_media) on all three, bit-exact 130 / ? / 129 of 165; modfx 1326 of 1362 bit-exact (committed hashes) on Linux, Mac and Windows, Marbles / Tides 42 of 42 inside -100 dB, worst of the rest -106.798 dB (tides_mod1) on all three. Argument-order (B9) produced no Windows-only miss. The Beads cases (media 1 and 2 use the 12-bit items): e_beads_def/alt Windows -133.7 / -102.4 dB = Linux.
Cases where the three differ (all other 246 cases print the same figure on all three):

| case | Linux | Mac | Windows |
|---|---|---|---|
| e_tape_def | -210.8 | -198.8 | -204.8 |
| e_tgate_def | exact | -109.6 | exact |
| e_fshift_def | -140.1 | -146.2 | -140.1 |
| e_fshift_alt | -117.9 | -139.9 | -118.1 |
| h_series2_drums | -193.6 | -193.6 | exact |
| h_rest | -286.4 | -286.4 | -288.0 |
| fx_tremolo_def | -168.2 | -168.2 | -169.2 |
| fx_space_def | -127.9 | -127.9 | -127.7 |
| fx_transient_def | exact | -159.6 | -159.6 |
| fx_fshift_def | -160.6 | -180.5 | -168.3 |
| fx_fshift_alt | -119.0 | -274.3 | -118.4 |
| fx_tape_def | exact | -244.3 | exact |
| fx_tape_alt | -231.1 | -230.2 | -231.1 |
| rv_noseg_tremolo_shape | -170.3 | -170.4 | -170.4 |

(14 rows. Full per-effect list: the `windows-clangcl` job log, step 'The effects must RUN here', and the `W-TFX figures` job logs of the PR.)

## Lead's decisions (26 Nov), work log
1. Desktop shell, fallback (a): `native-desktop.yml` `windows` (cl.exe, WebView2) first builds `thunder_dsp_tfx/_pe/_mods` with clang-cl into `build-clang`, then configures the shell with `-DTHUNDER_DSP_PREBUILT_DIR=...\build-clang\core\dsp\Release`; `core/dsp/CMakeLists.txt` then makes IMPORTED static libraries instead of building (cl.exe only). New `desktop_tfx` test (native/desktop/tests/test_tfx.cpp): prints `TFX: yes`, checks the live caps word and runs all 34 effects through the linked libraries; `THUNDER_EXPECT_TFX=1` is set for it when the libraries are prebuilt, so a cl.exe build without effects fails there. The parallel clang-cl shell job I had added was removed (it could not compile WebView2). Results: see "Fallback (a) rounds" below.
2. For W-PUMP: live_meters2 (non-gating step until W-PUMP) fails these 3 checks without the pump library: (a) `test_live_meters2.cpp:167 badSlots == 0: slots mask is 0b111 for a chain of 3 (50 records differ)`; (b) `:185 ntype == 2 * dy.size(): both dynamics slots carry their core type id (7 and 201): 50 of 100`; (c) `:186 nred > 0 && minRed < -3: the pump COMP slot shows gain reduction (deepest 0.00 dB, 0 posts below -3)`. W-PUMP must make them pass and then drop the `-E "^live_meters2$"` in `windows-clangcl`.

## Fallback (a) rounds and the input_capture flake
- Round 1 of fallback (a) (270a2d07): green on the first try. `Desktop shell (Windows x64, WebView2)` (cl.exe): clang-cl built `thunder_dsp_tfx.lib` 2,005,526 bytes, `thunder_dsp_pe.lib` 1,041,608, `thunder_dsp_mods.lib` 330,802; configure printed "thunder_dsp: prebuilt clang-cl libraries from ...\build-clang\core\dsp\Release"; the shell (WebView2, cl.exe) linked with them: no LNK errors; the 12 desktop_ tests passed, including `desktop_tfx` (34 of 34 effects ran through the linked libraries; with THUNDER_EXPECT_TFX=1 a build without THUNDER_HAVE_TFX would have failed it); selftest and the menus sweep passed. (The log line `TFX: yes` itself is not printed in that job: the -V step I meant to add was not applied; the pass of desktop_tfx under THUNDER_EXPECT_TFX=1 is the proof.) CRT/STL: both sides `/MD`, Release, `_ITERATOR_DEBUG_LEVEL` 0, no `/GL` anywhere (a mismatch would be LNK2038 / LNK2019: none).
- `desktop_input_capture` ("test_input_capture.cpp:47 meters: one peak per armed channel", the line reading `readPeaks()` after `stop`, which depends on thread timing) : cl.exe core job failed on 269de6ad and 17d5ef06 (2.64 s / 2.61 s), passed on 81632868 and on 270a2d07; the clang-cl core job passed on 269de6ad and failed on 270a2d07 (2.73 s). Runs with no change near that code: a timing flake, no gate or code change made. (Rerun of the failed clang-cl job requested once the run finishes.)

## Follow-ups from the verifier (W-TFX-VERIFY.md)
- M1: the Windows `live_meters2` step now GATES: it fails unless TFX: yes, exactly three FAIL lines, those three matched by their check text (slots mask, type ids 7 and 201, pump COMP reduction) and "3 failed"; no continue-on-error. A fourth failure, or a pump check that starts passing (W-PUMP), turns it red: W-PUMP then deletes the step and the `-E`.
- M3: `THUNDER_DSP_PREBUILT_DIR` stops the configure (FATAL_ERROR with the reason) for a non-x64 target or any configuration but Release; the desktop job now configures with `-DCMAKE_CONFIGURATION_TYPES=Release`.
- M2: `native/desktop/tests/test_input_capture.cpp` now polls `readPeaks()` over the feeding window (and the drain wait) and keeps the per-channel maximum, instead of one read after `stop` that could land just after the drain thread's periodic meter event had reset the peaks. This changes how the test reads, not what it requires: the same `pk.size() == 3 && pk[0] > 0 && pk[2] > 26.9` check, same thresholds. Test-only; approved by the lead.
- L3: stale text above corrected (176 tests; figures workflow gone).
