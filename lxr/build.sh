#!/bin/sh
# Rebuilds lxr/lxr.wasm and lxr/lxr-engine.js from lxr/src. GPL-3.0-only (see lxr/LICENSE and lxr/NOTICE.md).
#
# Sources: lxr/src/firmware holds the LXR firmware files the build reads, unchanged, from
# https://github.com/erica-synths/lxr-desktop-app at commit a43cd4fcda23ec3d577dc425261c3eb28bd5abff (23 Oct 2026),
# with the firmware's LICENSE and NOTICE.md. lxr/src/lxr_wasm.c is the interface Thunder Plus calls; lxr/src/stub
# stands in for the STM32 HAL, the Cortex-M DSP instructions (written out in C) and ST's USB header.
#
# Toolchain: clang 18 + wasm-ld, with the WASI sysroot and libclang_rt.builtins-wasm32 from wasi-sdk 25
# (https://github.com/WebAssembly/wasi-sdk/releases: wasi-sysroot-25.0.tar.gz, libclang_rt.builtins-wasm32-wasi-25.0.tar.gz).
# RESDIR is a clang resource dir holding include/ (clang's own headers) and lib/wasi/libclang_rt.builtins-wasm32.a.
# Python 3 and a host clang write the engine's knob pages from the firmware's menu tables (src/tools/lxr_meta.py).
#
# Flags as the firmware's (-O3 -ffast-math), and -funsigned-char because ARM's char is unsigned.
#
# Usage: SYSROOT=/path/wasi-sysroot-25.0 RESDIR=/path/resdir sh lxr/build.sh
set -e
D=$(cd "$(dirname "$0")" && pwd)
F="$D/src/firmware/Core"
O=$(mktemp -d)
CC="clang --target=wasm32-wasi --sysroot=$SYSROOT -resource-dir=$RESDIR -O3 -ffast-math -funsigned-char -std=gnu11 -Wno-everything -I$D/src/stub -I$F/Inc"
for f in "$F"/Src/Soundengine/DSPAudio/*.c "$F"/Src/Soundengine/DSPAudio/FX/*.c "$F"/Src/Soundengine/DSPAudio/FX/VerbTools/*.c \
         "$F"/Src/Soundengine/MIDI/MidiParser.c "$F"/Src/Soundengine/MIDI/ParameterArray.c "$F"/Src/Soundengine/FxBus.c \
         "$F"/Src/Parameters.c "$F"/Src/menu/Cc2Text.c "$F"/Src/Preset/initKit.c "$D/src/lxr_wasm.c"; do
  $CC -c "$f" -o "$O/$(basename "$f" .c).o"
done
clang --target=wasm32-wasi --sysroot="$SYSROOT" -resource-dir="$RESDIR" -mexec-model=reactor -Wl,--export=_initialize -Wl,--strip-all "$O"/*.o -o "$D/lxr.wasm"
python3 "$D/src/tools/lxr_meta.py" "$F" "$D/src/stub" "$O/meta.json"
python3 - "$D/src/lxr-engine.src.js" "$O/meta.json" "$D/lxr-engine.js" <<'PY'
import sys
src, meta, out = sys.argv[1:4]
s = open(src, encoding='utf-8').read(); m = open(meta, encoding='utf-8').read().strip()
key = '/*@META*/null/*@END*/'
assert s.count(key) == 1
open(out, 'w', encoding='utf-8').write(s.replace(key, m))
PY
rm -rf "$O"
echo "Built lxr/lxr.wasm and lxr/lxr-engine.js."
