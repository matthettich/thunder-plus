#!/bin/sh
# U11 parity gate: the master processor (master-worklet.js).  native/tools/parity.sh u11
#   builds test_master + rt_harness and runs: master_ref (the worklet in a Node vm: 54 cases), master (every case within -80 dB, meters, the
#   18 hashes of tests/golden/master.json), master_golden_current, rt_core (the processor under the allocation guard).
# BUILD_DIR names the build folder (default /tmp/thunder-parity).
set -u
here=$(cd "$(dirname "$0")" && pwd)
native=$(cd "$here/.." && pwd)
build=${BUILD_DIR:-/tmp/thunder-parity}
cmake -S "$native" -B "$build" -DCMAKE_BUILD_TYPE=Release >/dev/null || { echo "FAIL u11 (configure)"; exit 1; }
cmake --build "$build" -j8 --target test_master rt_harness >/dev/null || { echo "FAIL u11 (build)"; exit 1; }
ctest --test-dir "$build" -R '^(master_ref|master|master_golden_current|rt_core)$' --output-on-failure || { echo "FAIL u11"; exit 1; }
echo "PASS u11"; exit 0
