#!/bin/sh
# Rebuilds dsp.wasm, the module embedded in index.html (base64, in the plaitsWasm script tag).
#
# Sources (all MIT licensed):
#   git clone https://github.com/pichenettes/eurorack && git -C eurorack submodule update --init stmlib
#   git clone https://github.com/electro-smith/DaisySP
#   git clone --filter=blob:none --sparse https://github.com/airwindows/airwindows && \
#     git -C airwindows sparse-checkout set plugins/LinuxVST/src/Density plugins/LinuxVST/src/ToTape6
# Toolchain: clang 18 + wasm-ld, plus the WASI sysroot and libclang_rt.builtins for wasm32
# from https://github.com/WebAssembly/wasi-sdk/releases (RESDIR is a clang resource dir holding
# include/ and lib/wasi/libclang_rt.builtins-wasm32.a).
#
# Patches applied below:
#   - clouds/dsp/window.h: Window::Start() also clears done_/half_, otherwise Stretch mode
#     goes silent after its first windows.
#   - Airwindows: `default: throw;` becomes `default: break;` and createEffectInstance is
#     removed, since there is no VST host here.
#
# Usage: E=eurorack D=DaisySP A=airwindows/plugins/LinuxVST/src SYSROOT=... RESDIR=... sh build.sh
set -e
W=$(mktemp -d)
cp -r "$E" "$W/eurorack"; E="$W/eurorack"
python3 - "$E/clouds/dsp/window.h" <<'PY'
import sys; p=sys.argv[1]; s=open(p).read()
s=s.replace("    phase_ = 0;\n    regenerated_ = false;\n    envelope_phase_increment_","    phase_ = 0;\n    regenerated_ = false;\n    done_ = false;\n    half_ = false;\n    envelope_phase_increment_",1)
open(p,'w').write(s)
PY
mkdir -p "$W/aw/Density" "$W/aw/ToTape6"
for p in Density ToTape6; do for f in "$A/$p"/*; do
  sed 's/default: throw;/default: break;/; s/^AudioEffect\* createEffectInstance.*$//' "$f" > "$W/aw/$p/$(basename "$f")"; done; done
CXX="clang++ --target=wasm32-wasi --sysroot=$SYSROOT -resource-dir=$RESDIR -O3 -ffast-math -msimd128 -fno-exceptions -fno-rtti -std=c++14 -Wno-everything"
O="$W/obj"; mkdir -p "$O"
cc() { tag=$1; shift; inc=$1; shift; for f in "$@"; do $CXX $inc -c "$f" -o "$O/${tag}_$(echo "$f" | sed 's#[/.]#_#g').o"; done; }
pl=""; for f in algorithms additive_engine bass_drum_engine chiptune_engine chord_bank chord_engine dx_units fm_engine grain_engine hi_hat_engine lpc_speech_synth lpc_speech_synth_controller lpc_speech_synth_phonemes lpc_speech_synth_words modal_engine modal_voice naive_speech_synth noise_engine particle_engine phase_distortion_engine resonator sam_speech_synth six_op_engine snare_drum_engine speech_engine string string_engine string_machine_engine string_voice swarm_engine virtual_analog_engine virtual_analog_vcf_engine voice waveshaping_engine wavetable_engine wave_terrain_engine; do pl="$pl $(find "$E/plaits" -name "$f.cc" | head -1)"; done
MI="-DTEST -include cstdio -I$E"
cc pl "$MI" w_plaits.cc $pl $E/stmlib/dsp/units.cc $E/stmlib/utils/random.cc $E/plaits/resources.cc
cc rg "$MI" w_rings.cc $E/rings/dsp/part.cc $E/rings/dsp/resonator.cc $E/rings/dsp/string.cc $E/rings/dsp/fm_voice.cc $E/rings/resources.cc
cc cd "$MI" w_clouds.cc $E/clouds/dsp/granular_processor.cc $E/clouds/dsp/correlator.cc $E/clouds/dsp/mu_law.cc $E/clouds/resources.cc \
  $E/clouds/dsp/pvoc/frame_transformation.cc $E/clouds/dsp/pvoc/phase_vocoder.cc $E/clouds/dsp/pvoc/stft.cc $E/stmlib/dsp/atan.cc
cc el "$MI" w_elements.cc $E/elements/dsp/exciter.cc $E/elements/dsp/multistage_envelope.cc $E/elements/dsp/ominous_voice.cc $E/elements/dsp/part.cc \
  $E/elements/dsp/resonator.cc $E/elements/dsp/string.cc $E/elements/dsp/tube.cc $E/elements/dsp/voice.cc $E/elements/resources.cc
cc fx "-Istub -I$W/aw -I$D/Source -I$D/Source/Effects -I$D/Source/Utility -include stdlib.h" w_fx.cc w_density.cc w_totape.cc \
  $W/aw/Density/Density.cpp $W/aw/Density/DensityProc.cpp $W/aw/ToTape6/ToTape6.cpp $W/aw/ToTape6/ToTape6Proc.cpp \
  $D/Source/Effects/chorus.cpp $D/Source/Effects/decimator.cpp
$CXX -mexec-model=reactor -Wl,--export=_initialize -Wl,--strip-all "$O"/*.o -o dsp.wasm
echo "Built dsp.wasm. Paste its base64 into the plaitsWasm script tag in index.html."
