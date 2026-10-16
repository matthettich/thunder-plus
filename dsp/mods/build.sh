#!/bin/sh
# Rebuilds mods.wasm, the modular modules' WebAssembly (the Random generator, based on Mutable Instruments Marbles, and
# the Function generator, based on Mutable Instruments Tides 2). It is embedded as base64 in the SHARED MODULES block
# of chains/chains-worklet.js (MODS_WASM_B64), so …chains and …waves carry it with no extra file. One copy is compiled
# per audio thread or worker; every module is an object inside it (see mods.cc).
#
# Sources (MIT licensed):
#   git clone --depth 1 https://github.com/pichenettes/eurorack && git -C eurorack submodule update --init stmlib
# Toolchain: wasi-sdk 25 (https://github.com/WebAssembly/wasi-sdk/releases), WASI_SDK = its folder.
#
# Patches applied below:
#   - Marbles' ramp extractor counts its time constants (the 100 Hz audio-rate threshold and the 3 s clock-lost reset)
#     in 32 kHz samples; they are made to follow the host's sample rate (marbles_sample_rate_hz). The clock-lost
#     timeout also becomes at least 4 clock periods: with a clock slower than 3 s (2 bars at 120 BPM) every other
#     pulse counted as a lost clock and reset the déjà vu loop.
#   - Tides' ramp extractor gets Restart(), so a clocked Function generator starts its (divided) cycle on the next
#     clock pulse when the song starts, as Marbles' does with its reset.
#
# Usage: E=eurorack WASI_SDK=... sh build.sh        (writes mods.wasm and mods.b64 here)
# Then paste mods.b64 into MODS_WASM_B64 in chains/chains-worklet.js and run tools/sync-shared.py.
set -e
H=$(cd "$(dirname "$0")" && pwd)
W=$(mktemp -d)
mkdir -p "$W/eurorack"; cp -r "$E/marbles" "$E/tides2" "$E/stmlib" "$W/eurorack/"; E="$W/eurorack"
R="$E/marbles/ramp/ramp_extractor.cc"
sed -i 's#^namespace marbles {#extern float marbles_sample_rate_hz;\nnamespace marbles {#' "$R"
sed -i 's#1.0f / (100.0f / 32000.0f)#1.0f / (100.0f / marbles_sample_rate_hz)#; s#reset_interval_ = 32000 \* 3;#reset_interval_ = uint32_t(marbles_sample_rate_hz * 3.0f);#; s#std::max(4.0f / target_frequency_, 32000 \* 3.0f)#std::max(std::max(4.0f / target_frequency_, 4.0f * period), marbles_sample_rate_hz * 3.0f)#' "$R"
[ "$(grep -c marbles_sample_rate_hz "$R")" = 4 ] || { echo "Marbles ramp extractor patch failed"; exit 1; }
T="$E/tides2/ramp/ramp_extractor.h"
sed -i 's#^  void Reset();#  void Reset();\n  void Restart() { reset_counter_ = 1; }#' "$T"
grep -q 'void Restart()' "$T" || { echo "Tides ramp extractor patch failed"; exit 1; }
CXX="$WASI_SDK/bin/clang++ --target=wasm32-wasip1 --sysroot=$WASI_SDK/share/wasi-sysroot -O2 -ffast-math -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -std=c++14 -Wno-everything"
O="$W/obj"; mkdir -p "$O"
cc() { inc=$1; shift; for f in "$@"; do $CXX $inc -c "$f" -o "$O/$(echo "$f" | sed 's#[/.]#_#g').o"; done; }
MI="-DTEST -include cstdio -I$H -I$E"
cc "$MI" "$H/mods.cc" "$E/stmlib/dsp/units.cc"
# Mutable Instruments Marbles (MIT)
cc "$MI" "$H/mod_marbles.cc" $E/marbles/random/t_generator.cc $E/marbles/random/x_y_generator.cc $E/marbles/random/output_channel.cc \
  $E/marbles/random/quantizer.cc $E/marbles/random/lag_processor.cc $E/marbles/ramp/ramp_extractor.cc $E/marbles/resources.cc
# Mutable Instruments Tides 2 (MIT)
cc "$MI" "$H/mod_tides.cc" $E/tides2/poly_slope_generator.cc $E/tides2/ramp/ramp_extractor.cc $E/tides2/resources.cc
$CXX -mexec-model=reactor -Wl,--export=_initialize -Wl,--strip-all -Wl,--gc-sections -Wl,-z,stack-size=65536 "$O"/*.o -o "$H/mods.wasm"
base64 -w0 "$H/mods.wasm" > "$H/mods.b64"
rm -rf "$W"
echo "Built mods.wasm ($(wc -c < "$H/mods.wasm") bytes). Paste mods.b64 into MODS_WASM_B64 in chains/chains-worklet.js."
