#!/bin/sh
# Rebuilds tfx.wasm, the track-effects module embedded in index.html (base64, in the tfxWasm script tag).
# It is separate from dsp.wasm (the synth's module) because each tracker track runs its own copy in an
# AudioWorklet, so it holds only the effects.
#
# Sources (all MIT licensed):
#   git clone https://github.com/pichenettes/eurorack && git -C eurorack submodule update --init stmlib
#   git clone https://github.com/electro-smith/DaisySP
#   git clone --filter=blob:none --sparse https://github.com/airwindows/airwindows && \
#     git -C airwindows sparse-checkout set plugins/LinuxVST/src/{Density,ToTape6,EQ,Pressure4,Galactic,TapeDelay2}
# Toolchain: wasi-sdk 25 (https://github.com/WebAssembly/wasi-sdk/releases), WASI_SDK = its folder.
# The Clouds window.h patch from ../build.sh is applied here too. Airwindows needs the same
# `default: throw;` → `default: break;` and createEffectInstance removal.
#
# Usage: E=eurorack D=DaisySP A=airwindows/plugins/LinuxVST/src WASI_SDK=... sh build.sh
set -e
H=$(cd "$(dirname "$0")" && pwd)
W=$(mktemp -d)
cp -r "$E" "$W/eurorack"; E="$W/eurorack"
python3 - "$E/clouds/dsp/window.h" <<'PY'
import sys; p=sys.argv[1]; s=open(p).read()
s=s.replace("    phase_ = 0;\n    regenerated_ = false;\n    envelope_phase_increment_","    phase_ = 0;\n    regenerated_ = false;\n    done_ = false;\n    half_ = false;\n    envelope_phase_increment_",1)
open(p,'w').write(s)
PY
AW="Density ToTape6 EQ Pressure4 Galactic TapeDelay2"
for p in $AW; do mkdir -p "$W/aw/$p"; for f in "$A/$p"/*; do
  sed 's/default: throw;/default: break;/; s/^AudioEffect\* createEffectInstance.*$//' "$f" > "$W/aw/$p/$(basename "$f")"; done; done
CXX="$WASI_SDK/bin/clang++ --target=wasm32-wasip1 --sysroot=$WASI_SDK/share/wasi-sysroot -O3 -ffast-math -msimd128 -fno-exceptions -fno-rtti -std=c++14 -Wno-everything"
O="$W/obj"; mkdir -p "$O"
cc() { inc=$1; shift; for f in "$@"; do $CXX $inc -c "$f" -o "$O/$(echo "$f" | sed 's#[/.]#_#g').o"; done; }
cc "-I$H" "$H/tfx.cc"
cc "-DTEST -include cstdio -I$H -I$E" "$H/fx_mi.cc" \
  $E/rings/dsp/part.cc $E/rings/dsp/resonator.cc $E/rings/dsp/string.cc $E/rings/dsp/fm_voice.cc $E/rings/resources.cc \
  $E/clouds/dsp/granular_processor.cc $E/clouds/dsp/correlator.cc $E/clouds/dsp/mu_law.cc $E/clouds/resources.cc \
  $E/clouds/dsp/pvoc/frame_transformation.cc $E/clouds/dsp/pvoc/phase_vocoder.cc $E/clouds/dsp/pvoc/stft.cc \
  $E/stmlib/dsp/atan.cc $E/stmlib/dsp/units.cc $E/stmlib/utils/random.cc
cc "-I$H -I$D/Source -I$D/Source/Effects -I$D/Source/Utility" "$H/fx_ds.cc" $D/Source/Effects/chorus.cpp $D/Source/Effects/decimator.cpp
for p in $AW; do cc "-I$H -I$H/../stub -I$W/aw -include stdlib.h" "$H/aw_$p.cc" "$W/aw/$p/$p.cpp" "$W/aw/$p/${p}Proc.cpp"; done
$CXX -mexec-model=reactor -Wl,--export=_initialize -Wl,--export=malloc -Wl,--strip-all -Wl,-z,stack-size=262144 "$O"/*.o -o tfx.wasm
echo "Built tfx.wasm. Paste its base64 into the tfxWasm script tag in index.html."
