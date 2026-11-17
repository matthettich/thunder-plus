#!/bin/sh
# U29 parity gate: the second set of rendered-only engines table (+ loaded wavetable), vector, scanned, resynth (+ analysis) and bytebeat.  native/tools/parity.sh u29
#   builds test_engine_xeng2 + test_engine_xeng2_fuzz + test_libm + rt_harness and runs: libm / libm_golden_current, engine_xeng2_ref / engine_xeng2 (every factory voice of beta's tests/golden/engines.json whose engine is one of those, SHA-256
#   bit-exact: 333 voices; and 262 direct cases against the real ENGINE block, bit-exact against the committed Linux hashes on every platform and against the local Node on x86-64 Linux), engine_xeng2_golden_current (Linux: the committed
#   hashes are what the JS gives now), engine_xeng2_fuzz (extreme / NaN parameters and hostile data), rt_core (rt_engine_xeng2: the allocation guard).
#   Nothing is skipped: a reference that cannot run (no Node) fails.
# BUILD_DIR names the build folder (default /tmp/thunder-parity); JOBS the build parallelism (default 2).
set -u
here=$(cd "$(dirname "$0")" && pwd)
native=$(cd "$here/.." && pwd)
build=${BUILD_DIR:-/tmp/thunder-parity}
cmake -S "$native" -B "$build" -DCMAKE_BUILD_TYPE=Release >/dev/null || { echo "FAIL u29 (configure)"; exit 1; }
cmake --build "$build" -j"${JOBS:-2}" --target test_engine_xeng2 test_engine_xeng2_fuzz test_libm rt_harness >/dev/null || { echo "FAIL u29 (build)"; exit 1; }
ctest --test-dir "$build" -R '^(libm|libm_golden_current|engine_xeng2_ref|engine_xeng2|engine_xeng2_golden_current|engine_xeng2_fuzz|rt_core)$' --output-on-failure || { echo "FAIL u29"; exit 1; }
echo "PASS u29"; exit 0
