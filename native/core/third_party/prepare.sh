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
# Elements (U25, found by AddressSanitizer): two reads one float past a 257-entry table, each times a fraction of 0. Neither changes any output
# (on the wasm path nothing changes; natively two new U25 cases render differently, see U25-NOTES.md).
# 1. elements/dsp/string.cc line 128, String::Process: Interpolate(lut_svf_shift, damping_cutoff, 1.0f) with damping_cutoff == 256.0f reads [257].
# 2. elements/dsp/resonator.cc line 67, Resonator::Process: Interpolate(lut_stiffness, geometry_, 256.0f) with geometry_ == 1.0f reads [257].
# Host builds only (#ifdef __wasm__ keeps the original): the web's wasm module is what the existing waves goldens were made with, and its read is of fixed bytes.
python3 - "$E/elements/dsp/string.cc" "$E/elements/dsp/resonator.cc" <<'PY'
import sys
for p,old,new in [(sys.argv[1],"1.0f - Interpolate(lut_svf_shift, damping_cutoff, 1.0f),","""#ifdef __wasm__
      1.0f - Interpolate(lut_svf_shift, damping_cutoff, 1.0f),
#else
      1.0f - (damping_cutoff >= 256.0f ? lut_svf_shift[256] : Interpolate(lut_svf_shift, damping_cutoff, 1.0f)),
#endif"""),
                  (sys.argv[2],"float stiffness = Interpolate(lut_stiffness, geometry_, 256.0f);","""#ifdef __wasm__
  float stiffness = Interpolate(lut_stiffness, geometry_, 256.0f);
#else
  float stiffness = geometry_ >= 1.0f ? lut_stiffness[256] : Interpolate(lut_stiffness, geometry_, 256.0f);
#endif""")]:
    s=open(p).read()
    if s.count(old)!=1: sys.exit(p+" Elements patch failed")
    open(p,'w').write(s.replace(old,new,1))
PY
# Plaits (U25, found by the ASan + UBSan fuzz over the whole slider ranges; the lead's order of 17 Nov: clamp each index to its table). Host builds only (#ifdef __wasm__ keeps the original read):
# the web's wasm module is what the existing goldens were made with, and what it reads there is whatever its linear memory holds. A clamp changes no output where the index was inside its table.
# 1. plaits/dsp/engine/waveshaping_engine.cc, WaveshapingEngine::Render: lut_fold / lut_fold_2 are 516 floats read as InterpolateHermite(table + 1, index, 512), so `index` must lie in about 0..1.
#    Trigger: the slope oscillator at a tiny f0 (note below about -24; the engine clamps the note at -119) returns values of 1e8 and more, so ws_index = 127 * out + 128 leaves the int range (a
#    float-to-int overflow) and the shaper table value that follows is garbage, mix * gain + 0.5 is far outside 0..1: a read up to megabytes outside lut_fold (a crash natively). Fixed by clamping
#    ws_index to +-2e9 (inside int; NaN to 0) and `index` to the range whose table index i = trunc(index * 512) is 0 .. 512 (below -1/512 and NaN to 0, 513/512 and above to the last one inside); indices inside that range are untouched.
# 2. plaits/dsp/speech/lpc_speech_synth.h, LPCSpeechSynth::PlayFrame(frames, frame, interpolate): reads frames[i + 1] also when interpolate is false (the blend is then 0, so the second frame
#    is multiplied by 0). Table: phonemes_ = 15 frames x 14 bytes = 210 bytes (lpc_speech_synth_controller.cc:259, playback of a consonant, the last one i = 14 reads frame 15). Trigger: a
#    speech model trigger that picks consonant 14, the last in the table. Fixed: with interpolate false the second frame is the first (blend 0: the same value).
python3 - "$E/plaits/dsp/engine/waveshaping_engine.cc" "$E/plaits/dsp/speech/lpc_speech_synth.h" <<'PY'
import sys
p=sys.argv[1]; s=open(p).read()
old1="    float ws_index = 127.0f * out[i] + 128.0f;\n"
new1="""    float ws_index = 127.0f * out[i] + 128.0f;
#ifndef __wasm__
    if (!(ws_index >= -2.0e9f)) ws_index = ws_index > 0.0f ? 2.0e9f : -2.0e9f;   // Thunder (prepare.sh): the int conversion below needs a value inside int (NaN: -2e9)
    else if (ws_index > 2.0e9f) ws_index = 2.0e9f;
#endif
"""
old2="    float mix = x + (y - x) * shape_fractional;\n    float index = mix * wf_gain_modulation.Next() + 0.5f;\n"
new2=old2+"""#ifndef __wasm__
    if (!(index > -0.001953125f)) index = 0.0f;   // Thunder (prepare.sh): lut_fold has 516 entries, read at [i .. i + 3] + 1 for i = trunc(index * 512): i must be 0 .. 512 (NaN: 0)
    else if (!(index < 1.001953125f)) index = 1.0019529f;
#endif
"""
for o,n in [(old1,new1),(old2,new2)]:
    if s.count(o)!=1: sys.exit("plaits waveshaping patch failed")
    s=s.replace(o,n,1)
open(p,'w').write(s)
p=sys.argv[2]; s=open(p).read()
old="""    PlayFrame(
        frames[frame_integral],
        frames[frame_integral + 1],
        frame_fractional);
"""
new="""#ifdef __wasm__
    PlayFrame(
        frames[frame_integral],
        frames[frame_integral + 1],
        frame_fractional);
#else
    // Thunder (prepare.sh): without interpolation the blend is 0 and the second frame is multiplied by 0; do not read the frame after the table's end
    PlayFrame(
        frames[frame_integral],
        frames[interpolate ? frame_integral + 1 : frame_integral],
        frame_fractional);
#endif
"""
if s.count(old)!=1: sys.exit("plaits speech patch failed")
open(p,'w').write(s.replace(old,new,1))
PY
# 3. plaits/dsp/engine/wavetable_engine.cc, WavetableEngine::ReadWave (model 13, the NULL wave_map_ entry crash, U25 17 Nov): wave_map_ has 4 banks x 64 = 256 entries (LoadUserData fills all of
#    them) but x + y * 8 + z * 64 is computed from x0 / x1 = x_integral (+ 1), y likewise (0..8) and z0 / z1 = z_integral (+ 1) folded by `7 - z` (-1 .. 3). With a quantised parameter (harmonics
#    above 4/7: `quantization` 1) the interpolated position reaches EXACTLY 7.0 (x_fractional + 1 after Clamp()), and then the "next" corner is 8 (or, for z, -1): idx 256 and up (found: x=0 y=8
#    z=3 idx 256; case harmonics 0.999999, morph 0.99, fm 1, note 96) or below 0. The corner is multiplied by a fractional part of 0, but it is READ: the slot past the 256 filled entries is NULL (a
#    zero-filled shared buffer), so the read is NULL + phase: a crash natively (the wasm reads its low linear memory there, harmlessly: a different value times 0). Not a hosting difference: the
#    web wrapper (dsp/w_plaits.cc) and pe_host.cc make the same calls with the same 16384-byte allocator. Fix: x and y to 0..7 and z to 0..3 where the corner is read (the value is multiplied by
#    a fraction of 0 there, so no output changes), and wave_map_ allocated with the 256 entries LoadUserData writes (it allocated 64 and wrote 256: inside the shared buffer, so harmless, but wrong).
python3 - "$E/plaits/dsp/engine/wavetable_engine.cc" <<'PY'
import sys
p=sys.argv[1]; s=open(p).read()
old1="  wave_map_ = allocator->Allocate<const int16_t*>(kNumWavesPerBank);\n"
new1="""#ifdef __wasm__
  wave_map_ = allocator->Allocate<const int16_t*>(kNumWavesPerBank);
#else
  wave_map_ = allocator->Allocate<const int16_t*>(kNumBanks * kNumWavesPerBank);   // Thunder (prepare.sh): LoadUserData fills all 256
#endif
"""
old2="  return InterpolateWaveHermite(\n      wave_map_[x + y * 8 + z * kNumWavesPerBank],"
new2="""#ifndef __wasm__
  // Thunder (prepare.sh): the corner after a coordinate of exactly 7.0 (or z folded to -1) is read with a weight of 0: keep it inside the 4 x 8 x 8 map
  x = x < 0 ? 0 : (x > 7 ? 7 : x);
  y = y < 0 ? 0 : (y > 7 ? 7 : y);
  z = z < 0 ? 0 : (z > 3 ? 3 : z);
#endif
  return InterpolateWaveHermite(
      wave_map_[x + y * 8 + z * kNumWavesPerBank],"""
for o,n in [(old1,new1),(old2,new2)]:
    if s.count(o)!=1: sys.exit("plaits wavetable patch failed")
    s=s.replace(o,n,1)
open(p,'w').write(s)
PY
# 4. plaits/dsp/engine2/wave_terrain_engine.cc, WaveTerrainEngine::Render, `aux[i] = Sine(1.0f + 0.5f * kScale * aux_s)` (model 5): Sine() is InterpolateWrap on lut_sine (512 + 4 floats) and only
#    works for a phase of 0 or more (the file's own comment: "only works for a positive argument"). aux_s is the sum over 2 sub-samples of y + z, each in -1..1 on paper, so the phase is >= 0 on
#    paper; the magic-sine path overshoots 1 by 0.1 % (radius 0.999 with an amplitude drifting above 1: y = -1.001 seen), so with z = -1 the phase is about -0.002 and below and the integral
#    index is -1: a read 4 bytes before lut_sine (ASan; found with pFm -1, strike, timbre 1). Fixed: a phase at or below -1/512 (where the index would be -1; NaN too) becomes 0. Phases
#    above it (index 0 with a negative fraction, inside the table) are left as they were.
python3 - "$E/plaits/dsp/engine2/wave_terrain_engine.cc" <<'PY'
import sys
p=sys.argv[1]; s=open(p).read()
old="    aux[i] = Sine(1.0f + 0.5f * kScale * aux_s);\n"
new="""#ifdef __wasm__
    aux[i] = Sine(1.0f + 0.5f * kScale * aux_s);
#else
    {
      float aux_phase = 1.0f + 0.5f * kScale * aux_s;   // Thunder (prepare.sh): see the note in prepare.sh, patch 4
      if (!(aux_phase > -1.0f / 512.0f)) aux_phase = 0.0f;
      aux[i] = Sine(aux_phase);
    }
#endif
"""
if s.count(old)!=1: sys.exit("plaits wave terrain patch failed")
open(p,'w').write(s.replace(old,new,1))
PY
# 5. plaits/dsp/oscillator/sine_oscillator.h, SineNoWrap(phase) (the header's own comment: "potentially unsafe, if phase >= 1.25"): lut_sine has 641 floats (512 + 128 + 1), Interpolate reads
#    [index], [index + 1] with index = phase * 512. A phase of 1.25 and more, or of -1/512 and less, reads outside it. Trigger: a NaN phase (AnalogBassDrum::Render, model 21, sustain mode: the
#    resonator at a note of about -30 and lower, scale = 0.001 / f0, goes NaN, then f, then phase_; found with tune -57, fm -0.43: a read 0 bytes after lut_sine). Fixed: NaN and phases at or below
#    -1/512 become 0, phases of 1.25 and above become 1.2499999 (the last that reads inside); every phase in between (including the small negative ones, index 0) is untouched.
python3 - "$E/plaits/dsp/oscillator/sine_oscillator.h" <<'PY'
import sys
p=sys.argv[1]; s=open(p).read()
old="inline float SineNoWrap(float phase) {\n  return stmlib::Interpolate(lut_sine, phase, kSineLUTSize);\n}"
new="""inline float SineNoWrap(float phase) {
#ifndef __wasm__
  // Thunder (prepare.sh, patch 5): keep the read inside lut_sine (641 floats)
  if (!(phase > -1.0f / 512.0f)) phase = 0.0f;
  else if (!(phase < 1.25f)) phase = 1.2499999f;   // the largest float below 1.25: index 639, plus 1 = 640, the last entry
#endif
  return stmlib::Interpolate(lut_sine, phase, kSineLUTSize);
}"""
if s.count(old)!=1: sys.exit("plaits SineNoWrap patch failed")
open(p,'w').write(s.replace(old,new,1))
PY
# Rings / Clouds / Warps (found by AddressSanitizer in U30, 17 Nov, with a continuous parameter at exactly 1.0): stmlib::Interpolate(table, index, size) reads table[i] and table[i + 1]; at index == 1.0 the
# scaled index is exactly `size`, so i == size and table[i + 1] is one float past the (size + 1)-entry table. The fraction is then 0, so the result is table[size] whatever finite number is read (the web's
# wasm has finite bytes after it). Thunder's InterpolateTop (stmlib/dsp/dsp.h) returns table[size] without the read when i >= size; every other index runs the original lines. No output changes (the goldens and
# the pinned rows are the proof). The read sites, all in host builds only:
#   rings/dsp/resonator.cc  line 57   Interpolate(lut_stiffness, structure_, 256.0f)       lut_stiffness has 257 floats      trigger: structure == 1
#   rings/dsp/resonator.cc  line 60   Interpolate(lut_4_decades, damping_, 256.0f)         lut_4_decades has 257 floats      trigger: damping == 1
#   clouds/dsp/granular_processor.cc lines 278-279  Interpolate(lut_xfade_in / lut_xfade_out, dry_wet, 16.0f)   17 floats each   trigger: dry/wet == 1
#   clouds/dsp/granular_sample_player.h line 188  Interpolate(lut_grain_size, parameters.size, 256.0f)   257 floats   trigger: size == 1
#   beads/dsp/delay_controller.h lines 94, 104, 154  Interpolate(slave_envelopes / pw, shape, 5.0f)   6 floats each (local arrays)   trigger: shape == 1
#   warps/dsp/modulator.cc  lines 167-168 and 362-363  Interpolate(lut_xfade_in / lut_xfade_out, timbre / parameter, 256.0f)   257 floats each   trigger: timbre / the cross-fade parameter == 1
python3 - "$E/stmlib/dsp/dsp.h" "$E/rings/dsp/resonator.cc" "$E/clouds/dsp/granular_processor.cc" "$E/warps/dsp/modulator.cc" "$E/clouds/dsp/granular_sample_player.h" "$E/beads/dsp/delay_controller.h" <<'PY'
import sys
def sub(p, pairs):
    s=open(p).read()
    for old,new,cnt in pairs:
        if s.count(old)!=cnt: sys.exit(p+" InterpolateTop patch failed: "+old)
        s=s.replace(old,new)
    open(p,'w').write(s)
h,r,c,w,g,b=sys.argv[1:7]
sub(h,[("inline float Interpolate(const float* table, float index, float size) {",
"""// Thunder (prepare.sh): Interpolate for an index that can be exactly 1.0 (index * size == size): the fraction is 0, so table[size] is the result and table[size + 1] (past the table) is not read
inline float InterpolateTop(const float* table, float index, float size) {
  index *= size;   // the wasm build (the parity build of the web's wasm) keeps the original read
  MAKE_INTEGRAL_FRACTIONAL(index)
  float a = table[index_integral];
#ifndef __wasm__
  if (index_integral >= static_cast<int32_t>(size)) return a;
#endif
  float b = table[index_integral + 1];
  return a + (b - a) * index_fractional;
}

inline float Interpolate(const float* table, float index, float size) {""",1)])
sub(r,[("Interpolate(lut_stiffness, structure_, 256.0f)","InterpolateTop(lut_stiffness, structure_, 256.0f)",1),
       ("Interpolate(\n      lut_4_decades,","InterpolateTop(\n      lut_4_decades,",1)])
sub(c,[("Interpolate(lut_xfade_in, dry_wet, 16.0f)","InterpolateTop(lut_xfade_in, dry_wet, 16.0f)",1),("Interpolate(lut_xfade_out, dry_wet, 16.0f)","InterpolateTop(lut_xfade_out, dry_wet, 16.0f)",1)])
sub(w,[("Interpolate(lut_xfade_in, lut_index, 256.0f)","InterpolateTop(lut_xfade_in, lut_index, 256.0f)",1),("Interpolate(lut_xfade_out, lut_index, 256.0f)","InterpolateTop(lut_xfade_out, lut_index, 256.0f)",1),
       ("Interpolate(lut_xfade_in, parameter, 256.0f)","InterpolateTop(lut_xfade_in, parameter, 256.0f)",1),("Interpolate(lut_xfade_out, parameter, 256.0f)","InterpolateTop(lut_xfade_out, parameter, 256.0f)",1)])
sub(g,[("Interpolate(lut_grain_size, parameters.size, 256.0f)","InterpolateTop(lut_grain_size, parameters.size, 256.0f)",1)])
sub(b,[("stmlib::Interpolate(slave_envelopes, shape, 5.0f)","stmlib::InterpolateTop(slave_envelopes, shape, 5.0f)",1),("stmlib::Interpolate(pw, shape, 5.0f)","stmlib::InterpolateTop(pw, shape, 5.0f)",2)])
PY
# Cloud Seed: smaller delay buffers than the original (192 kHz, ~67 MB per instance), but, unlike the web's tfx/build.sh (32768 and 2400, which cover 48 kHz at best), sized for 96 kHz so that no tap reads
# or writes outside its own buffer at any rate the native core runs it at (U30; host builds only: the wasm build, which must equal the web's wasm, keeps 32768 and 2400): the late lines (1.5 x 530 ms = 795 ms = 76k samples at 96 kHz) and the multitap (up to 1 s) get 131072, the allpass
# stages (at most 31.6 ms + modulation = 3.3k samples at 96 kHz) get 4800. At 44.1 and 48 kHz with every delay inside the buffer the output is the same as the web's (a delay is read relative to the write
# position, the buffer length only matters when a tap is longer than it). The web's own buffers are smaller and its taps at 96 kHz, and at 48 kHz with the size control above about .9, read outside them.
cp -r "$D/CloudSeedCore" "$O/cs"; rm -rf "$O/cs/.git"; C="$O/cs"
$SED -i 's/DelayBufferSize = 192000 \* 2;/DelayBufferSize = THUNDER_CS_DELAY;/' "$C/DSP/ModulatedDelay.h" "$C/DSP/MultitapDelay.h"
$SED -i 's/DelayBufferSize = 19200;/DelayBufferSize = THUNDER_CS_ALLPASS;/' "$C/DSP/ModulatedAllpass.h"
for f in ModulatedDelay MultitapDelay ModulatedAllpass; do
  printf '#ifdef __wasm__\n#define THUNDER_CS_DELAY 32768\n#define THUNDER_CS_ALLPASS 2400\n#else\n#define THUNDER_CS_DELAY 131072\n#define THUNDER_CS_ALLPASS 4800\n#endif\n' | cat - "$C/DSP/$f.h" > "$C/DSP/$f.h.new" && mv "$C/DSP/$f.h.new" "$C/DSP/$f.h"
done
grep -q 'DelayBufferSize = THUNDER_CS_DELAY' "$C/DSP/ModulatedDelay.h" && grep -q 'DelayBufferSize = THUNDER_CS_ALLPASS' "$C/DSP/ModulatedAllpass.h" || { echo "Cloud Seed patch failed" >&2; exit 1; }
# Airwindows: no VST host (default: throw -> break, createEffectInstance removed)
A="$D/airwindows/plugins/LinuxVST/src"
for p in Density ToTape6 EQ Pressure4 Galactic TapeDelay2 DeRez2 PurestDrive Air3 Verbity2 ToVinyl4 Dubly Cabs Tube2; do
  mkdir -p "$O/aw/$p"
  for f in "$A/$p"/*; do sed 's/default: throw;/default: break;/; s/^AudioEffect\* createEffectInstance.*$//' "$f" > "$O/aw/$p/$(basename "$f")"; done
done
echo "prepared $O"
