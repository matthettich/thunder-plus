#!/bin/sh
# U14 parity gate: the track-effect host (ThunderFx).  native/tools/parity.sh u14
#   builds test_fx + rt_harness and runs: fx_ref (the real ThunderFx in Node: 91 cases + the map() value sets), fx (every case within -80 dB, the figures printed),
#   fx_golden_current / fx_table_current (the committed golden hashes and effect table are what the JS gives now; Linux), rt_core (the host under the allocation guard).
# BUILD_DIR names the build folder (default /tmp/thunder-parity).
set -u
here=$(cd "$(dirname "$0")" && pwd)
native=$(cd "$here/.." && pwd)
build=${BUILD_DIR:-/tmp/thunder-parity}
cmake -S "$native" -B "$build" -DCMAKE_BUILD_TYPE=Release >/dev/null || { echo "FAIL u14 (configure)"; exit 1; }
cmake --build "$build" -j8 --target test_fx rt_harness >/dev/null || { echo "FAIL u14 (build)"; exit 1; }
ctest --test-dir "$build" -R '^(fx_ref|fx|fx_golden_current|fx_table_current|rt_core)$' --output-on-failure || { echo "FAIL u14"; exit 1; }
echo "PASS u14"; exit 0
