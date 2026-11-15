# Upstream pins (U02)

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

## Parity
`native/tools/parity.sh u02` compares the CMake-built wasm with (a) the committed files and (b) the output of the three `build.sh` scripts
on these same pins. Findings of the first local run: with HEAD pins the original `build.sh` already does not reproduce the committed files
(tfx 674311 B vs 674341 B committed), so some drift in upstream (or a different path / locale for the link) is already in the committed
bytes. The scripts link `"$O"/*.o`, a shell glob of object names made from absolute temp paths: the wasm bytes depend on the checkout path and
locale. The CMake link order is not that order, so (b) differs by a few hundred bytes (order only: same sources and flags).
