#!/bin/sh
# Builds tfx.wasm, thunder-dsp.wasm, mods.wasm (U02) and pump.wasm, pump-synth.wasm (U03, from the pinned matthettich/pump) from CMake with wasi-sdk (the same sources, upstream pins and flags as
# dsp/build.sh, dsp/tfx/build.sh, dsp/mods/build.sh; pump's wasm/build.sh). Usage: native/tools/build-wasm.sh [OUTDIR]   (default /tmp/u02-wasm)
# WASI_SDK=<folder of wasi-sdk 25> is used if set; otherwise wasi-sdk 25 is downloaded once into $OUTDIR/../u02-wasi-sdk.
set -e
H=$(cd "$(dirname "$0")/.." && pwd)
OUT=${1:-/tmp/u02-wasm}
mkdir -p "$OUT"
if [ -z "$WASI_SDK" ]; then
  WASI_SDK=$(dirname "$OUT")/u02-wasi-sdk
  if [ ! -x "$WASI_SDK/bin/clang++" ]; then
    mkdir -p "$WASI_SDK"
    curl -fsSL "https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-25/wasi-sdk-25.0-x86_64-linux.tar.gz" | tar -xz -C "$WASI_SDK" --strip-components=1
  fi
fi
cmake -S "$H" -B "$OUT/build" -DCMAKE_TOOLCHAIN_FILE="$WASI_SDK/share/cmake/wasi-sdk-p1.cmake" -DWASI_SDK_PREFIX="$WASI_SDK" \
  -DTHUNDER_BUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=
cmake --build "$OUT/build" -j --target thunder_wasm_tfx thunder_wasm_w thunder_wasm_mods thunder_wasm_pump thunder_wasm_pump_synth
for f in tfx thunder-dsp mods; do cp "$OUT/build/core/dsp/$f.wasm" "$OUT/$f.wasm"; done
for f in pump pump-synth; do cp "$OUT/build/core/pump/$f.wasm" "$OUT/$f.wasm"; done
ls -l "$OUT"/*.wasm
