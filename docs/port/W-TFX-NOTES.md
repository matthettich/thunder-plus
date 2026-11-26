# W-TFX notes: the effects library on Windows through clang-cl (builder log, Sonnet, 26 Nov 2026)

Plan: `W-TFX-PLAN.md` (section 7 overrides). Branch `n4/w-tfx` from the plan branch (native 4b646448). PICKUP: first CI round pending.

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
