#!/bin/sh
# U28 parity gate: the rendered-only engines tone, modal, swarm, drift, piano, ep, organ, breath, strings, fmdrum, wind, phase, partials, skin, chip and KeysUtil.  native/tools/parity.sh u28
#   builds test_engine_xeng + test_libm + rt_harness and runs: libm / libm_golden_current, engine_xeng_ref / engine_xeng (every factory voice of beta's tests/golden/engines.json whose engine is one of those, SHA-256 bit-exact:
#   1068 voices; and 313 direct cases against the real ENGINE block, bit-exact against the committed Linux hashes on every platform and against the local Node on x86-64 Linux), engine_xeng_golden_current (Linux: the committed
#   hashes are what the JS gives now), rt_core (rt_engine_xeng: the allocation guard).
#   Nothing is skipped: a reference that cannot run (no Node) fails.
# BUILD_DIR names the build folder (default /tmp/thunder-parity); JOBS the build parallelism (default 2).
set -u
here=$(cd "$(dirname "$0")" && pwd)
native=$(cd "$here/.." && pwd)
build=${BUILD_DIR:-/tmp/thunder-parity}
cmake -S "$native" -B "$build" -DCMAKE_BUILD_TYPE=Release >/dev/null || { echo "FAIL u28 (configure)"; exit 1; }
cmake --build "$build" -j"${JOBS:-2}" --target test_engine_xeng test_libm rt_harness >/dev/null || { echo "FAIL u28 (build)"; exit 1; }
ctest --test-dir "$build" -R '^(libm|libm_golden_current|engine_xeng_ref|engine_xeng|engine_xeng_golden_current|rt_core)$' --output-on-failure || { echo "FAIL u28"; exit 1; }
echo "PASS u28"; exit 0
