# Upstream pins (U02, U03)

`fetch.sh` fetches exactly these commits (`git fetch --depth 1 origin <sha>`; it fails if the checkout is not that commit). No branch is
ever followed. `prepare.sh` then applies, on a copy, the same patches `dsp/build.sh`, `dsp/tfx/build.sh` and `dsp/mods/build.sh` apply.
CMake runs both at configure time (`native/core/dsp/CMakeLists.txt`); `native/tools/build-wasm.sh` and `parity.sh` use them too.
Pinned on 15 Nov 2026 at each repository's default-branch HEAD of that day: **the build scripts name no commits, so the commits the
committed wasm files were built from are not known** (see "Parity" below).

| Upstream | Commit (full) | Licence | Used for | In `thunder_core`? |
|---|---|---|---|---|
| pichenettes/eurorack | `f4cfa7697095a28d292bff758beb51deb49b5a86` | MIT (STM32F projects: plaits, rings, clouds, elements, beads, warps, marbles, tides2; the AVR projects are GPL-3.0 and are not used) | Plaits, Rings, Clouds, Elements, Beads, Warps, Marbles, Tides 2 | yes (permissive) |
| pichenettes/stmlib (eurorack's submodule) | `e3bd7c9cc00e4364166f9905c0509b6ffd0535ec` (the commit eurorack records) | MIT | shared DSP / utils | yes |
| electro-smith/DaisySP | `2c72eaf9eac5fc0dca1919d65d606da907832618` | MIT | chorus, decimator, flanger, phaser, tremolo, overdrive, wavefolder, autowah, ladder, oscillator, phasor | yes |
| airwindows/airwindows (sparse: 14 `plugins/LinuxVST/src/*` folders) | `143a86df3642881e0be4bf827815050721063b41` | MIT | Density, ToTape6, EQ, Pressure4, Galactic, TapeDelay2, DeRez2, PurestDrive, Air3, Verbity2, ToVinyl4, Dubly, Cabs, Tube2 | yes |
| GhostNoteAudio/CloudSeedCore | `deb21ded9eb7dad9b3ff94ce1ba96a963716594e` | MIT | Cloud Seed reverb | yes |

Licences were read from each repository's LICENSE file at the pin (eurorack: its README, "Code (STM32F projects): MIT license"; the
per-file headers of the files used are MIT). Nothing non-permissive is fetched. Toolchain for wasm: wasi-sdk 25 (Apache-2.0 with LLVM
exceptions), downloaded in CI, never vendored.

## Patches `prepare.sh` adds beyond the build scripts' own (U14 diagnosis, 15 Nov 2026)
Each is explained at its place in `prepare.sh`. tfx.wasm built from the patched tree gives the same 91 `fx_golden.inc` hashes as the committed one.
| File | What | Applies to |
|---|---|---|
| `beads/dsp/delay_granular_player.h` | the three random draws of a new grain (time, pitch, shape) sequenced in clang's order: as arguments of one call their order is unspecified, and GCC takes the other one | every build (same audio from clang / wasm) |
| `stmlib/utils/random.h` | `Random`'s state through a per-thread pointer set by the effect host (one state per wasm instance in the web app, one per process natively) | host builds (`#ifndef __wasm__`) |
| DaisySP `Effects/pitchshifter.h` (a patched copy in `ds/Effects/`) | `myrand()`'s seed per effect host, same reason | host builds (the wasm build does not use the copy) |
| `clouds/dsp/grain.h` | no read of `lut_window[4097]` (one past the table, multiplied by 0) | every build (same value) |
| `clouds/dsp/correlator.cc` | shift count masked to 5 bits (was a shift by 32: undefined; wasm takes it modulo 32) | every build (the wasm's result everywhere) |
| `plaits/dsp/engine/waveshaping_engine.cc`, `plaits/dsp/speech/lpc_speech_synth.h`, `plaits/dsp/engine/wavetable_engine.cc`, `plaits/dsp/engine2/wave_terrain_engine.cc`, `plaits/dsp/oscillator/sine_oscillator.h` | indices kept inside their tables (`lut_fold`, `phonemes_`, `wave_map_`, `lut_sine` x2; U25, ASan fuzz; file, size and trigger in `prepare.sh`); no committed output changes | host builds (`#ifndef __wasm__`) |
| `elements/dsp/string.cc`, `elements/dsp/resonator.cc` | no read of `lut_svf_shift[257]` / `lut_stiffness[257]` (one past a 257-entry table, multiplied by 0; U25, found by ASan; wasm unchanged) | host builds (`#ifndef __wasm__`) |

## Parity
The gate is the audio, not the bytes: `native/tools/parity.sh u02` builds the three wasm files with CMake, swaps them (and the
`MODS_WASM_B64` base64 in both worklet files) into a temporary copy of the tree, and runs the golden tests there as `npm test` does
(chains-modules / chains-processor / chains-thunder-node / waves-modules / waves-processor and every other test that loads these files).
Local result at the pins above: every one PASSes, so the pins reproduce today's hashes. mods.wasm is also byte-identical to the committed
one; tfx.wasm and thunder-dsp.wasm differ in bytes (the committed files came from unpinned upstreams) but not in audio.

**Canonical link order.** `dsp/build.sh` links `"$O"/*.o`, a shell glob of names made from absolute temp paths, so its bytes depend on the
checkout path and locale. The CMake wasm link lists every object sorted by its source path relative to `dsp/`, DaisySP or the prepared
upstream tree (`native/core/dsp/CMakeLists.txt`, `dsp_wasm`), so two builds on different machines or paths give the same bytes (checked:
same tfx / thunder-dsp / mods hashes from two different checkout paths). `dsp/build.sh` is unchanged.

## pump (U03): the owner's own repository
| Upstream | Commit (full) | Licence | Used for | In `thunder_core`? |
|---|---|---|---|---|
| matthettich/pump (public; the owner's code) | `deb07d331b4f6a058abc847106be66bfc498faa3` (main, 14 Nov 2026, 13:48 -0700) | All rights reserved, (c) 2026 wake: the owner's own code, compiled into his own app (pump `LICENSE`). Not an open licence: do not redistribute the sources | `thunder_pump` (pump_wasm.cpp + wasm/fx/*.cpp), `thunder_pump_synth` (pump_synth.cpp), `pump.wasm`, `pump-synth.wasm` | no: separate libraries (`native/core/pump`), linked by the hosting glue (U18, U33) |

Fetched by `fetch.sh` (sparse checkout: `common/`, `wasm/`, `plugins/` **without `plugins/machine/`**, the licence files, `tests/data/mix.f32`), the
same mechanism as every other pin. WORKPLAN section 12's default was a git submodule; the fetch script was used instead so there is one
mechanism and one place (this file) that lists every pin, and because a sparse fetch can leave the GPL folder out of the checkout. A submodule
would put all of `plugins/machine/` on disk. No pump source is copied into this repo.

**Which pump commit the app's committed wasm comes from: the pin (since web build 638, 16 Nov; `native` took the files on 17 Nov).** `pump/pump.wasm` (1,601,290 B, SHA-256 `ecbfadc17eb40fc5585e86cd95d33ac2ce977fca36320e077c1fb70cee1830ae`)
is **byte-identical** to the `pump.wasm` that `native/tools/build-wasm.sh` builds from this pin (wasi-sdk 25), `pump/synth/pump-synth.wasm` is byte-identical to the pin's `pump-synth.wasm`, and `pump/pump-meta.js` carries the
sha1 stamp (`72cc7e55...`) the pin generates. MASTER (type 23) has 77 parameters and latency 4454 in the web app and in the native build alike.
History (kept as the record of the skew that 638 removed): until build 637 the shipped `pump/pump.wasm` (1,599,108 B) was not byte-equal to any build tried (pin 1,601,290 B; wasm/build.sh at 6f1ec29, 3d53287 and 7b8cb1d 1,599,184 B; at 4a1533b
1,599,823 B; at ff046ce 1,599,488 B), its `pump-meta.js` stamp was `30b389d8...` (what pump produces from 6f1ec29 up to 4a1533b / ff046ce) and its MASTER was older than the pin's (75 parameters, latency 358: the pin adds 30a3e95
"pump MASTER: resonance stage", 14 Nov). Measured on 15 Nov by `parity.sh u03`; resolved on 16 Nov by the owner shipping the pin in build 638. Every golden and test that renders through pump passed at the pin before and after.

### Everything the pump wasm build compiles in (`wasm/build.sh`; checked by `g++ -MM` over every translation unit)
| Component | Licence | Where | In `thunder_pump*`? |
|---|---|---|---|
| pump's own DSP (`common/dsp`, `common/PumpParams.hpp`, `plugins/<name>/*Dsp.hpp|*Params.hpp|*Engine.hpp`, `wasm/`) | All rights reserved (owner) | pump repo | yes |
| pffft (`common/third_party/pffft`, via `common/pump_pffft.c`) | BSD-style FFTPACK licence (`common/third_party/pffft/LICENSE.txt`: Pommier, Mambro, Ayguen, UCAR) | pump repo | yes, as `thunder_pump_pffft` (shared by both libraries) |
| DPF stand-in (`wasm/shim/DistrhoPlugin.hpp`, replaces DISTRHO's header) | pump's own file; no DPF code is compiled (DPF is ISC and is not fetched) | pump repo | yes |
| wasi-sdk 25 (compiler, libc, libc++) | Apache-2.0 with LLVM exceptions (+ MIT / BSD for wasi-libc) | downloaded in CI, never vendored | toolchain only |
| NeuralAmpModelerCore (MIT) + Eigen (MPL-2.0) + nlohmann/json | listed in pump's THIRD_PARTY_NOTICES for pump AMP | `third_party/` | **no** (not fetched; not reachable from wasm/build.sh) |
| LXR firmware, `plugins/machine/` | **GPL-3.0-only** (+ terms) | pump repo | **no**: left out of the checkout (`fetch.sh` sparse exclusion); CMake aborts if the folder exists; no `wasm/*.cpp` includes it (checked: no `machine/` include in any translation unit's dependency list) |

pump's `tools/check-licence-boundary.sh` needs `plugins/machine/` to exist, so it is not run here; the checks above replace it.

### U03 parity and findings
- `parity.sh u03`: pump-synth golden, every test that loads `pump.wasm` / `pump-synth.wasm` (block-size, chains, eng-targets-pump, fx-rest,
  off-pump-latency, psyn-guard, worklet-memory) and the whole `npm test`, in a temp copy of the tree with the CMake-built files: all PASS.
  Plus pump's own `wasm/compare.cpp` + `compare.mjs`, gated for the nine mastering effects (types 0-8: worst native-vs-wasm difference far below -80 dB).
- pump's `wasm/compare.cpp` is stale for types 9+ (reported, not fixed): its usage comment says "type 0-8", its link line (in its header) omits
  `wasm/fx/*.cpp` (the parity script adds them), and `presetsOf()` falls through to METER's presets for every other type, so for the
  creative effects the native side is not given the wasm side's values (VOX shows latency 0 against 1024). Reported as INFO only.
- Host (native) build, found by the smoke test and FIXED 16 Nov: on Linux x86-64 with GCC 13, STUTTER (type 20) put out NaN at its first sample
  if a SPRING (type 13) instance had run earlier in the same process. Cause: GCC 13 uses an MMX register in `SpringTank::process` and no EMMS, so
  the x87 registers stay tagged in use; STUTTER's `fmod` is an inlined x87 `fprem` under -ffast-math and its load overflows the x87 stack.
  `core/pump/CMakeLists.txt` now passes `-mno-mmx` on x86 host builds, and ctest also runs all 25 types in one process (`dsp_smoke_pump_all`).
  A host process whose OTHER code leaves MMX state behind would hit the same NaN in any x87 code: for the shells, nothing of ours uses MMX.
- `wasm/pump_synth.cpp` defines `struct std::mutex` (no-op) inside `namespace std` when building for wasm (undefined behaviour in
  principle; works with wasi-sdk 25). Reported, not changed.
- MSVC: the pump libraries are skipped (the build needs clang or gcc, as for the U02 DSP). Windows hosting would build pump with clang-cl: later unit.

## fast_float (N1 review C9): the JSON number reader
Vendored as one header, `native/core/third_party/fast_float/fast_float.h` (with `LICENSE-MIT`, `LICENSE-APACHE`, `LICENSE-BOOST` beside it).
Used by `rt/json.cpp` `toDouble()` instead of the platform's `from_chars` / `strtod`, so every platform parses a number to the same bits.

| Upstream | Tag | Commit (full) | File sha256 | Licence |
|---|---|---|---|---|
| fastfloat/fast_float | `v8.3.3` | `dbeabeeb3e6791cb96994c2a7a084c36e62ccfb2` (the commit the tag points at; the tag object is `048df798cab53c26c9ab1a7c15585ecf6fd5ef9c`) | `2a8d07368d7d7280741db8de9dbdc69ea3c2955afbcdd54b8165e67fea02cc04` | MIT OR Apache-2.0 OR BSL-1.0 (any one; the three licence files are copied unchanged from the tag) |

The header is the `fast_float.h` release asset of GitHub release v8.3.3, copied unchanged. C++11, no exceptions. Included as a SYSTEM
include path (`third_party`) in CMake and as `-isystem` in the wasm job of `.github/workflows/native.yml`, plus an MSVC `warning(push, 0)`
around the include. Update: change the tag, re-download the asset, and update this table and the sha256 together.

## LXR firmware (U04): the optional, GPL-3.0-only module (`native/lxr`)
| Upstream | Commit (full) | Licence | Used for | In `thunder_core`? |
|---|---|---|---|---|
| erica-synths/lxr-desktop-app (the files in this repository's `lxr/src/firmware/Core`, `lxr/src/stub`, `lxr/src/lxr_wasm.c`) | `a43cd4fcda23ec3d577dc425261c3eb28bd5abff` (23 Oct 2026) | GPL-3.0-only + the firmware's section 7 additional permission / terms (`lxr/src/firmware/NOTICE.md`) | `thunder_lxr` (`native/lxr`): the four LXR engines (drum, snare, cymbal, hat), offline pad render and a provisional live block | **no**: a separate library behind `THUNDER_WITH_LXR` (default ON for the owner's builds; OFF = nothing of it is configured, built or linked). The core talks to it through `thunder/lxr_seam.h` only. Facts and what a closed build leaves out: `native/lxr/LICENSES.md` |

Reference: the web app's `lxr/lxr.wasm` (sha256 `ec6ab14cb540e56156814f13fe3d7a245dd9815e65f8c30331a9cfe4b360f637`), built by `lxr/build.sh` with clang `--target=wasm32-wasi -O3 -ffast-math -funsigned-char -std=gnu11`.
**Reproducible from the repository's source: yes** (wasi-sdk 25, clang 19.1.5, `lxr/build.sh` as it is: the same sha256, byte for byte).
The native module compiles the same sources STRICT (no fast-math, no contraction) with host-only patches made at configure time into the build directory (`native/lxr/prepare-lxr.sh`; each fails loudly if its text is not found exactly once). The patches are listed in `docs/port/U04-NOTES.md`.

## stb_vorbis (U32): the SoundFont player's decoder for SF3 samples (Ogg Vorbis)
| Upstream | Commit (full) | Licence | Used for | In `thunder_core`? |
|---|---|---|---|---|
| nothings/stb (one file: `stb_vorbis.c`, v1.24 in its own header; `LICENSE` beside it) | `9600522de53973336fa033a289858a4354cbb04c` (master, 12 Nov 2026) | MIT or public domain (Unlicense), the reader's choice | `thunder_stb_vorbis` (a C static library linked into `thunder_core`; the in-memory API `stb_vorbis_open_memory` only: no stdio, no push-data API) | yes (permissive; the SoundFont player stays in the core) |

Chosen by the owner on 17 Nov 2026 (U32). `stb_vorbis.c` at the pin: sha256 `b52cf518355f7b2d181f18b97b812f361ffeb26299d935c8cf29e842401d60a6`, 198,004 bytes.
**Licence, read from the file at the pin.** The text below is the licence block of `stb_vorbis.c` (lines 5664 to 5702, the block just before the closing `*/`), copied byte for byte by script from the fetched file (sha256 above); the file's own header says "Ogg Vorbis audio decoder - v1.24 - public domain". The repository's `LICENSE` opens with: "The software libraries in this repository are in the public domain. Most of them are also available under the MIT license -- choose whichever you prefer.". Nothing is vendored: `fetch.sh` fetches the file at the pin; `THUNDER_FETCH_STB_ONLY=1 fetch.sh DEST` fetches only this (the closed build has no DSP and needs nothing else).

```text
------------------------------------------------------------------------------
This software is available under 2 licenses -- choose whichever you prefer.
------------------------------------------------------------------------------
ALTERNATIVE A - MIT License
Copyright (c) 2017 Sean Barrett
Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
------------------------------------------------------------------------------
ALTERNATIVE B - Public Domain (www.unlicense.org)
This is free and unencumbered software released into the public domain.
Anyone is free to copy, modify, publish, use, compile, sell, or distribute this
software, either in source code form or as a compiled binary, for any purpose,
commercial or non-commercial, and by any means.
In jurisdictions that recognize copyright laws, the author or authors of this
software dedicate any and all copyright interest in the software to the public
domain. We make this dedication for the benefit of the public at large and to
the detriment of our heirs and successors. We intend this dedication to be an
overt act of relinquishment in perpetuity of all present and future rights to
this software under copyright law.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
------------------------------------------------------------------------------
```
`prepare-stb.sh` makes the patched copy the host builds compile (see `docs/port/U32-NOTES.md` for each patch and the input that triggers it; none while the file is empty of patches).
The decoder runs at font load / preparation time, never on the audio thread; the host hands the bytes in (nothing in the core opens a file).
