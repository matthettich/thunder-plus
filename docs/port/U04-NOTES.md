# U04 notes: the LXR drum-synth engine as a separable (GPL) module

Branch `n1/u04` (on `native`), 17 Nov 2026. Gate: `native/tools/parity.sh u04` (or ctest `lxr*`, `rt_core`). Licence facts: `native/lxr/LICENSES.md`, `native/core/third_party/PINS.md`.

## The seam and the two configurations
- Core: `native/core/include/thunder/lxr_seam.h` (C struct of function pointers `ThunderLxrApi`: describe, render_bound, render_voice, live_open, live_process; abi + size checked) and
  `native/core/src/lxr/lxr_seam.cpp` (a registry of one pointer). `XKind::Lxr` in `xeng`: names lxdrum / lxsnare / lxcym / lxhat. With no module registered a voice is refused
  (`kEngUnsupported`) and `xeng_voice_from_json` says false. The core names no firmware symbol or header (`native/tools/lxr-boundary.sh`, ctest `lxr_boundary`: sources + `nm` of
  `libthunder_core.a`, and of every test binary in an OFF build).
- `THUNDER_WITH_LXR` (default ON for the owner's builds, not on MSVC): `native/lxr` builds `thunder_lxr`, which links the core (never the reverse); `thunder_lxr_module_register()` at start-up.
  OFF: nothing is configured, built or linked. CI (`native.yml`, job Core Linux GCC) adds a "Closed build" step: `-DTHUNDER_WITH_LXR=OFF -DTHUNDER_WITH_DSP=OFF -DTHUNDER_WITH_PUMP=OFF`,
  target `test_lxr_seam` (refusal + another engine unchanged) and `lxr_boundary`. The ON configuration is the normal build (test_lxr, lxr_fuzz, lxr_seam, rt_core).
- Host (`native/lxr/src/lxr_module.cpp`): the page's sequence (power-on state, `lxr_init`, parameter writes `clamp(round(v)||0,0,255)`, vdst/ldst target lists, bpm, `lxr_load`, 20 settle blocks,
  trigger, blocks of 16 to quiet / maxS, 32-tap Hann sinc resample, Level / Drive / trim / fade) with the core's `js_*` maths; static buffers, no allocation after construction (rt_harness
  `rt_lxr`: 30 pad + live renders under the guard). Live (PROVISIONAL, not wired to sequencer or mixer): 64-frame blocks per voice, triggers act at the first block boundary at or after
  their integer offset, a pending list of 64 per voice, a busy flag refuses concurrent use.
- Power-on state: the page restores the wasm's whole memory; natively the module snapshots (first use) and restores voices, velocity modulators, FX bus, FX_BUFFER and the mixer globals.
  Result: every case is bit-identical in any order and after another render (818 cases reversed, each after a hostile render: 0 differ).

## The reference and the wasm
`native/tools/lxr-dump.mjs` runs the UNMODIFIED `lxr/lxr-engine.js` `LxrCore.render` + the shipped `lxr/lxr.wasm` in Node (a recording wrapper takes the parameter bytes, bpm, trigger and the raw 44.14 kHz
blocks) and writes samples for 818 cases: every factory preset (pre_*), the init kit, the ELECTRO song sounds, direct cases for each voice type (lo/mid/hi/max/rnd), every knob at 0 and 127,
vdst/ldst targets, tune, velocities, level, drive, hold, length, bpm, sample rates 8000..192000, the open hat. Files: `golden/lxr_cases.inc` (names; ctest `lxr_cases_current`).
- lxr.wasm flags (`lxr/build.sh`): clang `--target=wasm32-wasi -O3 -ffast-math -funsigned-char -std=gnu11`. Reproducible from the repo source: YES (wasi-sdk 25, clang 19.1.5: sha256
  `ec6ab14cb540e56156814f13fe3d7a245dd9815e65f8c30331a9cfe4b360f637`, identical to the shipped file), so there is no second pin-built run.
- Native: strict (`-ffp-contract=off`, no fast-math), `-funsigned-char -fno-strict-aliasing -std=gnu11`.

## The gate and what it found (HONEST NUMBERS)
- The parameter bytes the native loads equal the page's in all 818 cases (exact).
- 76 cases are bit-exact on Linux x86-64 GCC (`golden/lxr_gate.inc`, enforced there only).
- **No case is within -100 dB except the exact ones; 742 are left out BY NAME in `golden/lxr_leftout.inc`**, each with finalDb, rawDb, a length bound and an RMS bound (written by
  `test_lxr --write-leftout`, never hand-edited). They get the structural check (finite, RMS within bound, length within bound; 726 have the SAME length) on every platform, and where the
  exact list holds finalDb may not worsen by more than 1 dB. Distribution (max |diff| / reference peak): 573 at <= -70 dB, 138 at -70..-60, 9 at -60..-50, 6 at -50..-30, 16 above -30.
- Why -100 dB is out of reach: the wasm is `-ffast-math` (reassociation, contraction, reciprocals), strict native is a few ulp off, and the firmware's voice output is int16, so one flipped LSB is
  already about -90 dB of full scale; the IIR / FM / noise paths then carry the ulp. Quiet cases are 3-5 LSB apart (about -75 dB); the 16 above -30 dB are the chaotic ones: a sample-timing or
  branch flip in decimation / threshold paths (pre_lxcym_4, vdst_lxcym_2, ldst_lxcym_18, open_lxhat_long) or random knobs (rnd*, k127_lxcym_p201). Needs the lead's decision: accept this tier, or build a
  fast-math native twin (clang `-O3 -ffast-math -fno-vectorize` matched the wasm's post-load state in a probe, not pursued).
- Not done: direct-case extremes use knob 0/127, not each page row's own min/max.

## Findings in the LXR source (host patches, `native/lxr/prepare-lxr.sh`, applied to a copy in the build dir; each fails if its text is not found exactly once)
1. `INDTCM`, `INDTCMZ`, `IN_ITCM_RAM` emptied (ARM sections; also needed for Mach-O).
2. modulationNode.c `modNode_setOriginalValueChanged`: reads `*ptr` of a NULL parameter pointer (lxr_load, every kit): host crash, wasm reads address 0. Guard added.
3. MidiParser.c `midiParser_originalCcValues[0xff]`: stores at index 65536 (parameter 127) and 276 / 383 (NRPN path). Stores outside the table skipped (the table is never read).
4. Out-of-range float -> integer conversions (UB; wasm and ARM saturate, x86 wraps): routed through `lxr_f2u32 / lxr_f2i32 / lxr_f2i16` (`Core/Inc/lxr_host_cvt.h`). Sites: Oscillator.c
   (phase-increment returns, `fast_log2` arg, 5 FM index casts, 2 pwm phases, 10 `buf = x*gain`), lfo.c (2 increments, `incInt`), distortion.c (2), Snare.c (2), ResonantFilter.c
   (`tmp = .. * FILTER_GAIN` x8 incl. tanh x2), transientGenerator.c, CymbalVoice.c, HiHat.c (x2), DrumVoice.c, BufferTools.h (3), MidiParser.c (4 LFO phase offsets).
5. Wavetable reads of `[1024]` on 1024-long rows (26 sites in Oscillator.c): `lxr_tab` reads the next row's first sample (as the ARM / wasm do) and, past the last row of a table, the row's own first sample
   (the wasm reads whatever follows in memory).
6. `calcNextOscSampleBlock` / `...FmBlock` for a waveform number with no oscillator ("sample playback", code commented out / `#if 0`): the block buffer was left unwritten (the wasm outputs stale
   memory, a host uninitialised memory; it made 7 snare cases depend on history). Native writes zeros.
Module-level safety deviations (outside the page's UI ranges): Level limited to +-120 dB, Drive to 10 (1e9 dB / inf gave NaN via 0 * inf). A sample rate outside (0, 1e7] or a render over 2^25 floats is refused.

## Memory safety
`-DTHUNDER_SANITIZE=ON` (also `-fsanitize=float-cast-overflow` for the firmware C, fatal). `test_lxr_fuzz [renders] [seed]` (several fields at once from extremes / NaN / inf / out-of-range, bad sample rates
which must be refused, trigger lists with offsets far out of order, voices -3..9, kits of random bytes and length, blocks of 1..5000 frames, a 30 s limit per render, output finite and within its bound):
ctest `lxr_fuzz` = 150 renders. Local, ASan + UBSan (gcc, DSP / pump OFF for speed): about 19000 renders in total (2 x 5 seeds x 2000 and 6 x 1500 after the last fix), clean; the two late float-cast
sites (ResonantFilter) were found by it and patched. test_lxr (818 cases) and rt_harness are clean under the sanitizers. The ASan global-buffer-overflows found on the way were the `[1024]` reads (item 5).

## Not in this PR
Wiring into the sequencer / mixer / `thunder-render` (the live interface is provisional); MSVC / Windows (module absent, as in a closed build); Android / iOS / Mac builds of the module (not run here);
a second pin-built wasm run (the shipped one is byte-identical to the build); the -100 dB decision above; the pre-existing `#include "rt/dbl.h"#include <cmath>` on one line in `core/src/fx/fxmap.cpp` (a warning, not mine).
