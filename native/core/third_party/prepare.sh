#!/bin/sh
# Makes the patched copies of the upstream sources, exactly as dsp/build.sh, dsp/tfx/build.sh and dsp/mods/build.sh patch
# them (the three scripts' patches touch different files, so one patched tree serves all three targets).
# Usage: prepare.sh DEPS OUT      DEPS = fetch.sh's folder; OUT gets eurorack/  cs/  aw/<Effect>/
set -e
D=$1; O=$2
[ -n "$D" ] && [ -n "$O" ] || { echo "usage: prepare.sh DEPS OUT" >&2; exit 2; }
rm -rf "$O"; mkdir -p "$O/aw"
cp -r "$D/eurorack" "$O/eurorack"; rm -rf "$O/eurorack/.git" "$O/eurorack/stmlib"
cp -r "$D/stmlib" "$O/eurorack/stmlib"; rm -rf "$O/eurorack/stmlib/.git"
E="$O/eurorack"
# Clouds: Window::Start() also clears done_/half_ (dsp/build.sh, tfx/build.sh)
python3 - "$E/clouds/dsp/window.h" <<'PY'
import sys; p=sys.argv[1]; s=open(p).read()
n=s.replace("    phase_ = 0;\n    regenerated_ = false;\n    envelope_phase_increment_","    phase_ = 0;\n    regenerated_ = false;\n    done_ = false;\n    half_ = false;\n    envelope_phase_increment_",1)
if n==s: sys.exit("Clouds window.h patch failed")
open(p,'w').write(n)
PY
# Marbles / Tides ramp extractors (mods/build.sh)
R="$E/marbles/ramp/ramp_extractor.cc"
# GNU sed is needed (-i with no suffix, \n in a replacement). macOS ships BSD sed: brew install gnu-sed gives gsed.
SED=sed; if command -v gsed >/dev/null 2>&1; then SED=gsed; fi
if ! $SED --version 2>/dev/null | grep -q GNU; then echo "prepare.sh: GNU sed not found (macOS: brew install gnu-sed)" >&2; exit 1; fi
$SED -i 's#^namespace marbles {#extern float marbles_sample_rate_hz;\nnamespace marbles {#' "$R"
$SED -i 's#1.0f / (100.0f / 32000.0f)#1.0f / (100.0f / marbles_sample_rate_hz)#; s#reset_interval_ = 32000 \* 3;#reset_interval_ = uint32_t(marbles_sample_rate_hz * 3.0f);#; s#std::max(4.0f / target_frequency_, 32000 \* 3.0f)#std::max(std::max(4.0f / target_frequency_, 4.0f * period), marbles_sample_rate_hz * 3.0f)#' "$R"
[ "$(grep -c marbles_sample_rate_hz "$R")" = 4 ] || { echo "Marbles ramp extractor patch failed" >&2; exit 1; }
T="$E/tides2/ramp/ramp_extractor.h"
$SED -i 's#^  void Reset();#  void Reset();\n  void Restart() { reset_counter_ = 1; }#' "$T"
grep -q 'void Restart()' "$T" || { echo "Tides ramp extractor patch failed" >&2; exit 1; }
# Cloud Seed: smaller delay buffers (tfx/build.sh)
cp -r "$D/CloudSeedCore" "$O/cs"; rm -rf "$O/cs/.git"; C="$O/cs"
$SED -i 's/DelayBufferSize = 192000 \* 2;/DelayBufferSize = 32768;/' "$C/DSP/ModulatedDelay.h" "$C/DSP/MultitapDelay.h"
$SED -i 's/DelayBufferSize = 19200;/DelayBufferSize = 2400;/' "$C/DSP/ModulatedAllpass.h"
grep -q 'DelayBufferSize = 32768' "$C/DSP/ModulatedDelay.h" && grep -q 'DelayBufferSize = 2400' "$C/DSP/ModulatedAllpass.h" || { echo "Cloud Seed patch failed" >&2; exit 1; }
# Airwindows: no VST host (default: throw -> break, createEffectInstance removed)
A="$D/airwindows/plugins/LinuxVST/src"
for p in Density ToTape6 EQ Pressure4 Galactic TapeDelay2 DeRez2 PurestDrive Air3 Verbity2 ToVinyl4 Dubly Cabs Tube2; do
  mkdir -p "$O/aw/$p"
  for f in "$A/$p"/*; do sed 's/default: throw;/default: break;/; s/^AudioEffect\* createEffectInstance.*$//' "$f" > "$O/aw/$p/$(basename "$f")"; done
done
echo "prepared $O"
