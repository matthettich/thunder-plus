#!/bin/sh
# Fetches the upstream sources dsp/build.sh, dsp/tfx/build.sh and dsp/mods/build.sh clone, at the exact commits in PINS.md.
# Usage: fetch.sh [DEST]   (default: _deps next to this script). Idempotent: a folder already at its pin is left alone.
# Needs git and network access to github.com. Nothing here is patched: patches are applied by prepare.sh on a copy.
set -e
H=$(cd "$(dirname "$0")" && pwd)
D=${1:-$H/_deps}
mkdir -p "$D"
# name url commit [sparse paths...]
get() {
  name=$1; url=$2; sha=$3; shift 3
  if [ -d "$D/$name/.git" ] && [ "$(git -C "$D/$name" rev-parse HEAD 2>/dev/null)" = "$sha" ]; then return 0; fi
  rm -rf "$D/$name"; git init -q "$D/$name"; git -C "$D/$name" remote add origin "$url"
  if [ $# -gt 0 ]; then
    git -C "$D/$name" sparse-checkout init --no-cone
    git -C "$D/$name" sparse-checkout set "$@"
    git -C "$D/$name" fetch -q --depth 1 --filter=blob:none origin "$sha"
  else
    git -C "$D/$name" fetch -q --depth 1 origin "$sha"
  fi
  git -C "$D/$name" checkout -q FETCH_HEAD
  [ "$(git -C "$D/$name" rev-parse HEAD)" = "$sha" ] || { echo "pin mismatch for $name" >&2; exit 1; }
}
# stb_vorbis (U32, the SoundFont player's SF3 decoder): one file of nothings/stb at an exact commit. THUNDER_FETCH_STB_ONLY=1 fetches only this (the closed build has no DSP, so it must not need the other upstreams).
get stb https://github.com/nothings/stb 9600522de53973336fa033a289858a4354cbb04c /stb_vorbis.c /LICENSE
[ -z "$THUNDER_FETCH_STB_ONLY" ] || { echo "third_party: stb fetched into $D"; exit 0; }
get eurorack  https://github.com/pichenettes/eurorack    f4cfa7697095a28d292bff758beb51deb49b5a86
# stmlib is eurorack's submodule: the commit eurorack itself records (git ls-tree HEAD stmlib)
[ "$(git -C "$D/eurorack" ls-tree HEAD stmlib | awk '{print $3}')" = e3bd7c9cc00e4364166f9905c0509b6ffd0535ec ] || { echo "stmlib pin differs from eurorack's" >&2; exit 1; }
get stmlib    https://github.com/pichenettes/stmlib      e3bd7c9cc00e4364166f9905c0509b6ffd0535ec
get DaisySP   https://github.com/electro-smith/DaisySP   2c72eaf9eac5fc0dca1919d65d606da907832618
get CloudSeedCore https://github.com/GhostNoteAudio/CloudSeedCore deb21ded9eb7dad9b3ff94ce1ba96a963716594e
get airwindows https://github.com/airwindows/airwindows  143a86df3642881e0be4bf827815050721063b41 \
  /LICENSE /plugins/LinuxVST/src/Density /plugins/LinuxVST/src/ToTape6 /plugins/LinuxVST/src/EQ /plugins/LinuxVST/src/Pressure4 \
  /plugins/LinuxVST/src/Galactic /plugins/LinuxVST/src/TapeDelay2 /plugins/LinuxVST/src/DeRez2 /plugins/LinuxVST/src/PurestDrive \
  /plugins/LinuxVST/src/Air3 /plugins/LinuxVST/src/Verbity2 /plugins/LinuxVST/src/ToVinyl4 /plugins/LinuxVST/src/Dubly \
  /plugins/LinuxVST/src/Cabs /plugins/LinuxVST/src/Tube2
# pump: the owner's own repo (U03). Sparse checkout WITHOUT plugins/machine (GPL-3.0-only LXR firmware): it is never even on disk.
# tests/data/mix.f32 is the input of pump's own wasm/compare.mjs.
get pump https://github.com/matthettich/pump deb07d331b4f6a058abc847106be66bfc498faa3 \
  /LICENSE /THIRD_PARTY_NOTICES.md /common/ /wasm/ /plugins/ '!/plugins/machine/' /tools/check-licence-boundary.sh /tests/data/mix.f32
echo "third_party: all pins fetched into $D"
