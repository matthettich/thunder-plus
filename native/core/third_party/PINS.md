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

**Which pump commit the app's committed wasm came from.** Not exactly known (no stamp in the wasm, and the thunder-plus history of
`pump/pump.wasm` is squashed). Evidence, measured on 15 Nov by `parity.sh u03`:
- `pump/synth/pump-synth.wasm`: **byte-identical** to the CMake build at this pin (and to wasm/build.sh's at 7b8cb1d / 6f1ec29).
- `pump/synth/pump-synth-meta.js` sha1 stamp equals the one generated from this pin.
- `pump/pump.wasm` (1,599,108 B) is **not** byte-equal to any build tried: pin 1,601,290 B (CMake) ; wasm/build.sh at 6f1ec29, 3d53287 and 7b8cb1d
  1,599,184 B (identical to each other); at 4a1533b 1,599,823 B; at ff046ce 1,599,488 B. wasm/build.sh and the CMake build agree on flags
  and order, so the difference is source, not toolchain.
- `pump/pump-meta.js` stamp (`30b389d8...`) is what pump produces from 6f1ec29 (Nov 2, STRIP) up to 4a1533b / ff046ce (Nov 14, MASTER alt-solo), and
  **not** what the pin produces (`72cc7e55...`): the pin includes 30a3e95 "pump MASTER: resonance stage" (14 Nov, after the shipped build).
  So the shipped pump.wasm was built from a commit between 6f1ec29 and ff046ce (possibly with uncommitted edits), and the pin's MASTER (type 23) is newer than the app's.
- Despite that, every golden and test that renders through pump passes at the pin (see Parity), so the pin reproduces today's audio hashes.
Consequence for U18: the pin's MASTER has parameters the app's `pump-meta.js` does not list. Regenerate `pump-meta.js` from the pin when the
host glue lands, or pin 7b8cb1d (8 Nov, the newest commit whose meta stamp equals the shipped one) instead; the owner decides.

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
