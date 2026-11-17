#!/bin/sh
# U24 parity gate: the FM / additive / karplus / vosim / formant layer types of the engine (core/src/engine/layer_*.cpp).  native/tools/parity.sh u24
#   builds test_engine + test_engine_u24 + test_libm + test_fxu17 + rt_harness and runs: libm / libm_golden_current, the U22 engine tests (engine_ref / engine / engine_golden_current: nothing there may change),
#   engine_u24_ref / engine_u24 (the factory voices of U22's and U24's layer types with at least one of U24's: SHA-256 bit-exact against beta's tests/golden/engines.json, and the direct cases against the
#   committed Linux hashes of the real JS; the local Node comparison is enforced on x86-64 Linux only), engine_u24_golden_current (Linux: the committed hashes are what the JS gives now),
#   fxu17 (the pad cores the chain calls), rt_core (rt_engine, rt_engine_u24: the allocation guard).
#   Nothing is skipped: a reference that cannot run (no Node) fails.
# BUILD_DIR names the build folder (default /tmp/thunder-parity); JOBS the build parallelism (default 2).
set -u
here=$(cd "$(dirname "$0")" && pwd)
native=$(cd "$here/.." && pwd)
build=${BUILD_DIR:-/tmp/thunder-parity}
cmake -S "$native" -B "$build" -DCMAKE_BUILD_TYPE=Release >/dev/null || { echo "FAIL u24 (configure)"; exit 1; }
cmake --build "$build" -j"${JOBS:-2}" --target test_engine test_engine_u24 test_fxu17 test_libm rt_harness >/dev/null || { echo "FAIL u24 (build)"; exit 1; }
ctest --test-dir "$build" -R '^(libm|libm_golden_current|engine_ref|engine|engine_golden_current|engine_u24_ref|engine_u24|engine_u24_golden_current|fxu17_ref|fxu17|rt_core)$' --output-on-failure || { echo "FAIL u24"; exit 1; }
echo "PASS u24"; exit 0
