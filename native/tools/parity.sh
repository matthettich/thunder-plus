#!/bin/sh
# Parity gates of the native port, one case a unit: builds what the unit needs and prints PASS or FAIL (exit 0 / 1).
#   native/tools/parity.sh u08     the sampler voice player: ctest `voice` (every case of tools/voice-dump.mjs against the JS, -80 dB or better;
#                                  the figures are printed) and `rt_core` (the pool under the allocation guard)
#   native/tools/parity.sh u07     the sequencer: ctest `seq` (the four seq-electro goldens, every row hash and the sha256)
#                                  and, where Node is installed, `seq_extra` (cases the goldens do not reach, against the JS)
#   native/tools/parity.sh u10     latency compensation (pdcPlan) and the beat clock: ctest pdc, pdcmix, beat, mix, rt_core (parity-u10.sh)
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
  u02) shift; exec sh "$here/parity-u02.sh" u02 "$@" ;;   # the existing C++ DSP built from CMake: the goldens rendered through it (parity-u02.sh)
  *) echo "usage: parity.sh u02 [OUTDIR] | u07 | u08 | u10"; exit 2 ;;
esac
