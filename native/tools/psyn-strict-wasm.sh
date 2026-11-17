#!/bin/sh
# U33 DIAGNOSTIC (not part of any gate): builds pump-synth.wasm from the pinned pump with the web build's flags EXCEPT fast-math (-fno-fast-math -ffp-contract=off, which is what the native host is built with),
# so that "native against wasm" differs only by the libm (glibc / system against wasi-libc), not by float reassociation. Usage: psyn-strict-wasm.sh OUT.wasm [EXTRA CXXFLAGS]   (WASI_SDK = a wasi-sdk 25 folder;
# THUNDER_DEPS_DIR = fetch.sh's folder with the pump pin, default native/core/third_party/_deps). Then: node native/tools/psyn-dump.mjs --wasm OUT.wasm DIR; test_pumpsynth DIR.
set -e
H=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${1:?usage: psyn-strict-wasm.sh OUT.wasm [extra flags]}; shift || true
SDK=${WASI_SDK:?set WASI_SDK to a wasi-sdk 25 folder}
P=${THUNDER_DEPS_DIR:-$H/native/core/third_party/_deps}/pump
T=$(mktemp -d)
DEF="-DPUMP_SYNC_KERNELS=1 -DPFFFT_STATIC_DEFINE -DNDEBUG"
FL="-O3 -fno-fast-math -ffp-contract=off -w -msimd128 $*"
"$SDK/bin/clang" --sysroot="$SDK/share/wasi-sysroot" $FL $DEF -I"$P/common" -c "$P/common/pump_pffft.c" -o "$T/pffft.o"
"$SDK/bin/clang++" --sysroot="$SDK/share/wasi-sysroot" -std=c++17 $FL -fno-exceptions -fno-rtti $DEF -I"$P/common" -c "$P/wasm/pump_synth.cpp" -o "$T/synth.o"
EXPORTS=""
for f in _initialize psyn_types psyn_nparams psyn_stage psyn_new psyn_free psyn_values psyn_commit psyn_midi psyn_out psyn_process psyn_latency psyn_tempo psyn_buf psyn_file; do EXPORTS="$EXPORTS -Wl,--export=$f"; done
"$SDK/bin/clang++" --sysroot="$SDK/share/wasi-sysroot" -mexec-model=reactor $EXPORTS -Wl,--strip-all -Wl,-z,stack-size=1048576 -Wl,--initial-memory=16777216 "$T/synth.o" "$T/pffft.o" -o "$OUT"
rm -rf "$T"; ls -l "$OUT"
