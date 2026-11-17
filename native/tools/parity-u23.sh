#!/bin/sh
# U23 parity gate: the layer types click, wavetable, pd, chip, supersaw, pwm, sample (loop, vintage modes) and grains, on top of U22's engine.  native/tools/parity.sh u23
#   builds test_engine + test_engine_u23 + test_libm + test_fxu17 + rt_harness and runs: libm / libm_golden_current, the U22 engine tests (engine_ref / engine / engine_golden_current: nothing U22 gates may change),
#   engine_u23_ref / engine_u23 (every factory voice of beta's tests/golden/engines.json whose layers are of U22's or U23's types, SHA-256 bit-exact: 61 voices; and 311 direct cases against the real ENGINE
#   block, bit-exact against the committed Linux hashes on every platform and against the local Node on x86-64 Linux), engine_u23_golden_current (Linux: the committed hashes are what the JS gives now),
#   rt_core (rt_engine, rt_engine_u23: the allocation guard).
#   Nothing is skipped: a reference that cannot run (no Node) fails.
# BUILD_DIR names the build folder (default /tmp/thunder-parity); JOBS the build parallelism (default 2).
set -u
here=$(cd "$(dirname "$0")" && pwd)
native=$(cd "$here/.." && pwd)
build=${BUILD_DIR:-/tmp/thunder-parity}
cmake -S "$native" -B "$build" -DCMAKE_BUILD_TYPE=Release >/dev/null || { echo "FAIL u23 (configure)"; exit 1; }
cmake --build "$build" -j"${JOBS:-2}" --target test_engine test_engine_u23 test_fxu17 test_libm rt_harness >/dev/null || { echo "FAIL u23 (build)"; exit 1; }
ctest --test-dir "$build" -R '^(libm|libm_golden_current|engine_ref|engine|engine_golden_current|engine_u23_ref|engine_u23|engine_u23_golden_current|fxu17_ref|fxu17|rt_core)$' --output-on-failure || { echo "FAIL u23"; exit 1; }
echo "PASS u23"; exit 0
