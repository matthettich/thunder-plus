#!/bin/sh
# Rebuilds tfx.wasm, the track-effects module embedded in index.html (base64, in the tfxWasm script tag).
# It is separate from dsp.wasm (the synth's module) because each tracker track runs its own copy in an
# AudioWorklet, so it holds only the effects.
#
# Sources (all MIT licensed):
#   git clone https://github.com/pichenettes/eurorack && git -C eurorack submodule update --init stmlib
#   git clone https://github.com/electro-smith/DaisySP
#   git clone --filter=blob:none --sparse https://github.com/airwindows/airwindows && \
#     git -C airwindows sparse-checkout set plugins/LinuxVST/src/{Density,ToTape6,EQ,Pressure4,Galactic,TapeDelay2,DeRez2,PurestDrive,Air3,Verbity2,ToVinyl4,Dubly,Cabs,Tube2}
#   git clone https://github.com/GhostNoteAudio/CloudSeedCore   (C = its folder)
# Toolchain: wasi-sdk 25 (https://github.com/WebAssembly/wasi-sdk/releases), WASI_SDK = its folder.
# The Clouds window.h patch from ../build.sh is applied here too. Airwindows needs the same
# `default: throw;` → `default: break;` and createEffectInstance removal.
#
# Usage: E=eurorack D=DaisySP A=airwindows/plugins/LinuxVST/src C=CloudSeedCore WASI_SDK=... sh build.sh
set -e
H=$(cd "$(dirname "$0")" && pwd)
W=$(mktemp -d)
cp -r "$E" "$W/eurorack"; E="$W/eurorack"
python3 - "$E/clouds/dsp/window.h" <<'PY'
import sys; p=sys.argv[1]; s=open(p).read()
s=s.replace("    phase_ = 0;\n    regenerated_ = false;\n    envelope_phase_increment_","    phase_ = 0;\n    regenerated_ = false;\n    done_ = false;\n    half_ = false;\n    envelope_phase_increment_",1)
open(p,'w').write(s)
PY
AW="Density ToTape6 EQ Pressure4 Galactic TapeDelay2 DeRez2 PurestDrive Air3 Verbity2 ToVinyl4 Dubly Cabs Tube2"
# Cloud Seed's delay buffers are sized for 192 kHz (~67 MB an instance); shrink them for 48 kHz phones.
# fx_cloudseed.cc keeps its ranges inside these sizes.
cp -r "$C" "$W/cs"; C="$W/cs"
sed -i 's/DelayBufferSize = 192000 \* 2;/DelayBufferSize = 32768;/' "$C/DSP/ModulatedDelay.h" "$C/DSP/MultitapDelay.h"
sed -i 's/DelayBufferSize = 19200;/DelayBufferSize = 2400;/' "$C/DSP/ModulatedAllpass.h"
grep -q 'DelayBufferSize = 32768' "$C/DSP/ModulatedDelay.h" && grep -q 'DelayBufferSize = 2400' "$C/DSP/ModulatedAllpass.h" || { echo "Cloud Seed patch failed"; exit 1; }
for p in $AW; do mkdir -p "$W/aw/$p"; for f in "$A/$p"/*; do
  sed 's/default: throw;/default: break;/; s/^AudioEffect\* createEffectInstance.*$//' "$f" > "$W/aw/$p/$(basename "$f")"; done; done
CXX="$WASI_SDK/bin/clang++ --target=wasm32-wasip1 --sysroot=$WASI_SDK/share/wasi-sysroot -O3 -ffast-math -msimd128 -fno-exceptions -fno-rtti -std=c++14 -Wno-everything"
O="$W/obj"; mkdir -p "$O"
cc() { inc=$1; shift; for f in "$@"; do $CXX $inc -c "$f" -o "$O/$(echo "$f" | sed 's#[/.]#_#g').o"; done; }
cc "-I$H" "$H/tfx.cc"
cc "-I$H" "$H/fx_extra.cc"   # Thunder's own effects (original code)
cc "-DTEST -include cstdio -I$H -I$E" "$H/fx_mi.cc" \
  $E/rings/dsp/part.cc $E/rings/dsp/resonator.cc $E/rings/dsp/string.cc $E/rings/dsp/fm_voice.cc $E/rings/resources.cc \
  $E/clouds/dsp/granular_processor.cc $E/clouds/dsp/correlator.cc $E/clouds/dsp/mu_law.cc $E/clouds/resources.cc \
  $E/clouds/dsp/pvoc/frame_transformation.cc $E/clouds/dsp/pvoc/phase_vocoder.cc $E/clouds/dsp/pvoc/stft.cc \
  $E/stmlib/dsp/atan.cc $E/stmlib/dsp/units.cc $E/stmlib/utils/random.cc
# Mutable Instruments Beads (MIT): the texture synthesizer, ported as a track effect (see fx_beads.cc).
cc "-DTEST -include cstdio -I$H -I$E" "$H/fx_beads.cc" \
  $E/beads/dsp/delay_clock.cc $E/beads/dsp/delay_controller.cc $E/beads/dsp/mu_law.cc \
  $E/beads/dsp/processor.cc $E/beads/dsp/trigger_generator.cc $E/beads/resources.cc
cc "-I$H -I$D/Source -I$D/Source/Effects -I$D/Source/Utility" "$H/fx_ds.cc" $D/Source/Effects/chorus.cpp $D/Source/Effects/decimator.cpp
cc "-I$H -I$D/Source -I$D/Source/Effects -I$D/Source/Utility" "$H/fx_ds2.cc" $D/Source/Effects/flanger.cpp $D/Source/Effects/phaser.cpp \
  $D/Source/Effects/tremolo.cpp $D/Source/Effects/overdrive.cpp $D/Source/Effects/wavefolder.cpp $D/Source/Effects/autowah.cpp \
  $D/Source/Filters/ladder.cpp $D/Source/Synthesis/oscillator.cpp $D/Source/Control/phasor.cpp
# Mutable Instruments Warps (MIT)
cc "-DTEST -include cstdio -I$H -I$E" "$H/fx_warps.cc" $E/warps/dsp/modulator.cc $E/warps/dsp/oscillator.cc $E/warps/dsp/vocoder.cc \
  $E/warps/dsp/filter_bank.cc $E/warps/resources.cc
# Cloud Seed (MIT)
cc "-I$H -I$C -DBUFFER_SIZE=128 -DMAX_STR_SIZE=32 -Dstrcpy_s(a,b,c)=strncpy(a,c,b)" "$H/fx_cloudseed.cc" $C/DSP/RandomBuffer.cpp $C/DSP/Biquad.cpp
for p in $AW; do cc "-I$H -I$H/../stub -I$W/aw -include stdlib.h" "$H/aw_$p.cc" "$W/aw/$p/$p.cpp" "$W/aw/$p/${p}Proc.cpp"; done
$CXX -mexec-model=reactor -Wl,--export=_initialize -Wl,--export=malloc -Wl,--strip-all -Wl,-z,stack-size=262144 "$O"/*.o -o tfx.wasm
echo "Built tfx.wasm. Paste its base64 into the tfxWasm script tag in index.html."
