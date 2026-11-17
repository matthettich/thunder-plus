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
# Beads (U14 diagnosis): DelayGranularPlayer::Render passes three random draws as arguments of one call,
#   new_grain()->Start(delay_line, p.time.value() * ..., +-p.pitch.pitch_value(), ComputeGrainDuration(duration), p.shape.value(), ...)
# (each value() draws from stmlib::Random). C++ leaves the order of argument evaluation unspecified: clang (the web app's tfx.wasm, wasi-sdk)
# evaluates left to right (time, pitch, shape), GCC right to left (shape, pitch, time), so a GCC build gave every grain the other draw for its
# delay time and its shape (e_beads_def silent, e_beads_alt -17.7 dB against the wasm). The draws are now sequenced in the wasm's order.
python3 - "$E/beads/dsp/delay_granular_player.h" <<'PY'
import sys; p=sys.argv[1]; s=open(p).read()
old="""      new_grain()->Start(
          delay_line,
          p.time.value() * float(max_delay) + float(safety),
          (reverse ? -1.0f : 1.0f) * p.pitch.pitch_value(),
          ComputeGrainDuration(duration),
          p.shape.value(),
          amplitude, pan,
          trigger.fractional_sample);
"""
new="""      // Thunder: the three draws in a fixed order (time, pitch, shape: the order clang gives the
      // original argument list, which is what tfx.wasm plays); see native/core/third_party/prepare.sh.
      const float thunder_time = p.time.value();
      const float thunder_pitch = p.pitch.pitch_value();
      const float thunder_shape = p.shape.value();
      new_grain()->Start(
          delay_line,
          thunder_time * float(max_delay) + float(safety),
          (reverse ? -1.0f : 1.0f) * thunder_pitch,
          ComputeGrainDuration(duration),
          thunder_shape,
          amplitude, pan,
          trigger.fractional_sample);
"""
if s.count(old)!=1: sys.exit("Beads delay_granular_player.h patch failed")
open(p,'w').write(s.replace(old,new,1))
PY
# stmlib::Random (U14 diagnosis): its state is one static. In the web app every ThunderFx node has its own wasm instance, so its own state
# (0x21 at start); natively all effect hosts in the process shared one, so a Beads / Clouds heard the draws of every other. Host builds
# (not wasm: that code is unchanged) read the state through a per-thread pointer the effect host sets around its work (FxHost, see
# native/core/dsp/host_fx_state.h); with no pointer set (the synth, mods) it is the same static as before.
python3 - "$E/stmlib/utils/random.h" <<'PY'
import sys; p=sys.argv[1]; s=open(p).read()
def rep(old,new):
    global s
    if s.count(old)!=1: sys.exit("stmlib random.h patch failed: "+old)
    s=s.replace(old,new,1)
rep('namespace stmlib {\n','#ifndef __wasm__\n#include "host_fx_state.h"\n#endif\n\nnamespace stmlib {\n')
rep('  static inline uint32_t state() { return rng_state_; }\n',"#ifdef __wasm__\n  static inline uint32_t state() { return rng_state_; }\n")
rep("    return state();\n  }\n", """    return state();
  }
#else  // Thunder host build: the state of the effect host at work on this thread, else the static
  static inline uint32_t& cur() { uint32_t* p = thunder_host::stmlib_rng_cur(); return p ? *p : rng_state_; }
  static inline uint32_t state() { return cur(); }
  static inline void Seed(uint32_t seed) { cur() = seed; }
  static inline uint32_t GetWord() {
    uint32_t& s = cur();
    s = s * 1664525L + 1013904223L;
    return s;
  }
#endif
""")
open(p,'w').write(s)
PY
# DaisySP PitchShifter (U14 diagnosis): myrand() keeps its xorshift seed in a function static (1 at start): per wasm instance in the web app,
# per process natively, so the "spray" of a native pitch shifter depended on every pitch shifter that ran before it in the process
# (e_pshift_alt -1.5 dB in the full test run, -94 dB alone). A patched COPY of the one header goes to ds/Effects/ (host builds put ds/ before
# DaisySP's Source on the include path; the wasm build does not use the copy): the seed is the effect host's (host_fx_state.h).
mkdir -p "$O/ds/Effects"
python3 - "$D/DaisySP/Source/Effects/pitchshifter.h" "$O/ds/Effects/pitchshifter.h" <<'PY'
import sys; s=open(sys.argv[1]).read()
old="    static uint32_t seed = 1;\n"
new="""#ifdef __wasm__
    static uint32_t seed = 1;
#else  // Thunder host build: the seed of the effect host at work on this thread (1 at start, as a fresh wasm instance's)
    uint32_t &seed = thunder_host::daisy_myrand();
#endif
"""
if s.count(old)!=1: sys.exit("DaisySP pitchshifter.h patch failed")
s=s.replace(old,new,1)
old='#include "Utility/dsp.h"\n'
if s.count(old)!=1: sys.exit("DaisySP pitchshifter.h patch failed (include)")
s=s.replace(old,'#ifndef __wasm__\n#include "host_fx_state.h"\n#endif\n'+old,1)
open(sys.argv[2],'w').write(s)
PY
# Clouds (U14 diagnosis, found by AddressSanitizer / UBSan), two things that are undefined in C++ and only happen to work:
# 1. Grain::RenderEnvelope: stmlib::Interpolate(lut_window, gain, 4096.0f) reads lut_window[i] and [i + 1]; when the envelope phase is exactly
#    1.0, i = 4096 and [4097] is one float past the 4097-entry table. The fraction is then 0, so the result is lut_window[4096] whatever
#    finite number is read (tfx.wasm has zero bytes after the table: checked in the committed file). Now that value is returned without the read.
# 2. Correlator::EvaluateNextCandidate: destination[i + 1] >> (32 - offset_bits) shifts by 32 when the candidate is a multiple of 32.
#    wasm's i32.shr_u (and x86-64's and arm64's scalar shift) take the count modulo 32, so tfx.wasm computes >> 0 there; a vectorised native
#    build, or 32-bit ARM (the module itself), gives 0. The count is now masked explicitly: the wasm's result on every platform.
python3 - "$E/clouds/dsp/grain.h" "$E/clouds/dsp/correlator.cc" <<'PY'
import sys
p=sys.argv[1]; s=open(p).read()
old="          window = stmlib::Interpolate(lut_window, gain, 4096.0f);\n"
new="""          // Thunder: gain == 1 would read lut_window[4097], past the table, times a fraction of 0 (prepare.sh)
          window = gain >= 1.0f
              ? lut_window[4096]
              : stmlib::Interpolate(lut_window, gain, 4096.0f);
"""
if s.count(old)!=1: sys.exit("Clouds grain.h patch failed")
open(p,'w').write(s.replace(old,new,1))
p=sys.argv[2]; s=open(p).read()
old="    destination_bits |= destination[i + 1] >> (32 - offset_bits);\n"
new="""    // Thunder: the count modulo 32, as wasm's shift takes it (a shift by 32 is undefined in C++; prepare.sh)
    destination_bits |= destination[i + 1] >> ((32 - offset_bits) & 31);
"""
if s.count(old)!=1: sys.exit("Clouds correlator.cc patch failed")
open(p,'w').write(s.replace(old,new,1))
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
# Marbles LagProcessor::Process (found by AddressSanitizer in U21, 17 Nov): stmlib::Interpolate(lut_raised_cosine, phase, 256.0f) reads table[i] and table[i + 1]; the table has 257 entries and the phase is
# exactly 1.0 (the wrapper's "every ramp at its top" first sample, and a clock ramp that reaches its top), so i = 256 and [257] is one float past the table. The fraction is then 0, so the result is
# table[256] whatever finite number is read (mods.wasm has finite bytes after it). Now that value is returned without the read. The web's mods.wasm is built by dsp/mods/build.sh, which does not have this patch.
python3 - "$E/marbles/random/lag_processor.cc" <<'PY'
import sys
p=sys.argv[1]; s=open(p).read()
old="  float warped_phase = Interpolate(lut_raised_cosine, phase, 256.0f);\n"
new="""  // Thunder: phase == 1 would read lut_raised_cosine[257], past the 257-entry table, times a fraction of 0 (prepare.sh)
  float warped_phase = phase >= 1.0f
      ? lut_raised_cosine[256]
      : Interpolate(lut_raised_cosine, phase, 256.0f);
"""
if s.count(old)!=1: sys.exit("Marbles lag_processor.cc patch failed")
open(p,'w').write(s.replace(old,new,1))
PY
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
