#!/bin/sh
# Parity gates of the native port, one case a unit: builds what the unit needs and prints PASS or FAIL (exit 0 / 1).
#   native/tools/parity.sh u08     the sampler voice player: ctest `voice` (every case of tools/voice-dump.mjs against the JS, -80 dB or better;
#                                  the figures are printed) and `rt_core` (the pool under the allocation guard)
#   native/tools/parity.sh u07     the sequencer: ctest `seq` (the four seq-electro goldens, every row hash and the sha256)
#                                  and, where Node is installed, `seq_extra` (cases the goldens do not reach, against the JS)
#   native/tools/parity.sh u10     latency compensation (pdcPlan) and the beat clock: ctest pdc, pdcmix, beat, mix, rt_core (parity-u10.sh)
#   native/tools/parity.sh u11     the master processor: ctest master (the worklet's output, meters and golden hashes), rt_core (parity-u11.sh)
#   native/tools/parity.sh u12     the master EQ biquads, the safety limiter, the master order in the renderer (parity-u12.sh)
#   native/tools/parity.sh u13     meters and events out (parity-u13.sh)
#   native/tools/parity.sh u15     the tempo-locked effects (Flex, Step gate, Retrigger): ctest libm, fxclk (73 cases bit-exact vs the real worklet), golden + table current, rt_core (parity-u15.sh)
#   native/tools/parity.sh u16     the JS-only effects Beat grains / scramble / Glitch / Vintage sampler: ctest libm, fxidm (242 cases bit-exact vs the real worklet), golden + table current, rt_core (parity-u16.sh)
#   native/tools/parity.sh u20     MODFX modules B (Spectral, Extreme stretch, Convolution reverb + the shared FFT): libm (atan2, hypot), modfx (the engine hashes and the direct cases of the three bit-exact vs the real worklet), golden current, rt_core (parity-u20.sh)
#   native/tools/parity.sh u19     the MODFX module block (54 modules): libm (log2), modfx (162 engine hashes + 904 direct cases bit-exact vs the real worklet), golden current + waves block identity, rt_core (parity-u19.sh)
#   native/tools/parity.sh u21     the MODFX modules C (17 modules + BBExpr; Marbles/Tides not gated): modfx (213 engine hashes + 1227 direct cases + 146 formulas bit-exact), golden current, rt_core (parity-u21.sh)
#   native/tools/parity.sh u17     the JS-only effects Pitch reverb / Pitch delay / Resonator / Degrade (track slots and pad applyFx): ctest libm, fxu17 (bit-exact vs the real worklet and applyFx), golden + table current, rt_core (parity-u17.sh)
#   native/tools/parity.sh u22     the engine foundation (tables, rng, blep, renderLayer, voice filter / amp / mod, renderVoice, mkVoice params, pad FX chain): engine (294 factory voices bit-exact vs beta's engines.json hashes + 160 direct cases + 1800 mod probes), golden current, rt_core (parity-u22.sh)
#   native/tools/parity.sh u14     the track-effect host: ctest fx (91 cases vs the real ThunderFx), golden + table current, rt_core (parity-u14.sh)
#   native/tools/parity.sh u02     the CMake-built effects wasm against every golden that renders through it (parity-u02.sh)
# BUILD_DIR names the build folder (default /tmp/thunder-parity).
set -u
here=$(cd "$(dirname "$0")" && pwd)
native=$(cd "$here/.." && pwd)
build=${BUILD_DIR:-/tmp/thunder-parity}
unit=${1:-}
case "$unit" in
  u07)
    cmake -S "$native" -B "$build" -DCMAKE_BUILD_TYPE=Release >/dev/null || { echo "FAIL u07 (configure)"; exit 1; }
    cmake --build "$build" -j8 --target test_seq >/dev/null || { echo "FAIL u07 (build)"; exit 1; }
    if ctest --test-dir "$build" -R '^seq(_extra)?$' --output-on-failure; then echo "PASS u07"; exit 0; else echo "FAIL u07"; exit 1; fi ;;
  u08)
    cmake -S "$native" -B "$build" -DCMAKE_BUILD_TYPE=Release >/dev/null || { echo "FAIL u08 (configure)"; exit 1; }
    cmake --build "$build" -j8 --target test_voice rt_harness >/dev/null || { echo "FAIL u08 (build)"; exit 1; }
    if ctest --test-dir "$build" -R '^(voice_ref|voice|voice_golden_current|rt_core)$' --output-on-failure; then echo "PASS u08"; exit 0; else echo "FAIL u08"; exit 1; fi ;;
  u10) shift; exec sh "$here/parity-u10.sh" u10 "$@" ;;   # latency compensation + the beat clock (parity-u10.sh)
  u11) shift; exec sh "$here/parity-u11.sh" u11 "$@" ;;   # the master processor (parity-u11.sh)
  u12) shift; exec sh "$here/parity-u12.sh" u12 "$@" ;;   # the master EQ + safety limiter + OUT order (parity-u12.sh)
  u13) shift; exec sh "$here/parity-u13.sh" u13 "$@" ;;
  u15) shift; exec sh "$here/parity-u15.sh" u15 "$@" ;;   # the tempo-locked effects (parity-u15.sh)
  u16) shift; exec sh "$here/parity-u16.sh" u16 "$@" ;;   # the Grains / Scramble / Glitch / VSamp effects (parity-u16.sh)
  u20) shift; exec sh "$here/parity-u20.sh" u20 "$@" ;;   # MODFX modules B (parity-u20.sh)
  u19) shift; exec sh "$here/parity-u19.sh" u19 "$@" ;;   # the MODFX modules (parity-u19.sh)
  u21) shift; exec sh "$here/parity-u21.sh" u21 "$@" ;;   # the MODFX modules C (parity-u21.sh)
  u17) shift; exec sh "$here/parity-u17.sh" u17 "$@" ;;   # Pitch reverb / Pitch delay / Resonator / Degrade (parity-u17.sh)
  u22) shift; exec sh "$here/parity-u22.sh" u22 "$@" ;;   # the engine foundation (parity-u22.sh)
  u14) shift; exec sh "$here/parity-u14.sh" u14 "$@" ;;   # the track-effect host (parity-u14.sh)   # meters and events out (parity-u13.sh)
  u02) shift; exec sh "$here/parity-u02.sh" u02 "$@" ;;   # the existing C++ DSP built from CMake: the goldens rendered through it (parity-u02.sh)
  u03) exec sh "$here/parity-u03.sh" "$@" ;;   # the pump DSP built from CMake: the goldens and pump's own compare (parity-u03.sh)
  *) echo "usage: parity.sh u02 [OUTDIR] | u03 | u07 | u08 | u10 | u11 | u12 | u13"; exit 2 ;;
esac
