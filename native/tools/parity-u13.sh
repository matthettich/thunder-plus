#!/bin/sh
# U13 parity gate: meters and events out.  native/tools/parity.sh u13
#   builds test_meter + rt_harness and runs: meter_ref (the page's analyser maths in Node: 7 signals), meter (peak / rms within 0.1 dB of it, blocks of 128 and 512,
#   the batched event's layout and rate), rt_core (the meters fed from the mixer's pairs inside the allocation guard).
# BUILD_DIR names the build folder (default /tmp/thunder-parity).
set -u
here=$(cd "$(dirname "$0")" && pwd)
native=$(cd "$here/.." && pwd)
build=${BUILD_DIR:-/tmp/thunder-parity}
cmake -S "$native" -B "$build" -DCMAKE_BUILD_TYPE=Release >/dev/null || { echo "FAIL u13 (configure)"; exit 1; }
cmake --build "$build" -j8 --target test_meter rt_harness >/dev/null || { echo "FAIL u13 (build)"; exit 1; }
ctest --test-dir "$build" -R '^(meter_ref|meter|rt_core)$' --output-on-failure || { echo "FAIL u13"; exit 1; }
echo "PASS u13"; exit 0
