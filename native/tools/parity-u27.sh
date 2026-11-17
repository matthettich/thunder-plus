#!/bin/sh
# U27 parity gate: the live engines acid and mach (renderLiveCore), the Blip drum and the cores driven by scripts.  native/tools/parity.sh u27
#   builds test_engine_xeng3 + test_engine_xeng3_fuzz + test_libm + rt_harness and runs: libm / libm_golden_current, engine_xeng3_ref / engine_xeng3 (every factory voice of beta's tests/golden/engines.json whose engine is acid, mach or blip, SHA-256
#   bit-exact: 185 voices; and the direct cases and core scripts against the real ENGINE block, bit-exact against the committed Linux hashes on every platform and against the local Node on x86-64 Linux), engine_xeng3_golden_current (Linux),
#   engine_xeng3_fuzz (extreme / NaN parameters, combined fields), rt_core (rt_engine_xeng3: the allocation guard).
#   Nothing is skipped: a reference that cannot run (no Node) fails.
# BUILD_DIR names the build folder (default /tmp/thunder-parity); JOBS the build parallelism (default 2).
set -u
here=$(cd "$(dirname "$0")" && pwd)
native=$(cd "$here/.." && pwd)
build=${BUILD_DIR:-/tmp/thunder-parity}
cmake -S "$native" -B "$build" -DCMAKE_BUILD_TYPE=Release >/dev/null || { echo "FAIL u27 (configure)"; exit 1; }
cmake --build "$build" -j"${JOBS:-2}" --target test_engine_xeng3 test_engine_xeng3_fuzz test_libm rt_harness >/dev/null || { echo "FAIL u27 (build)"; exit 1; }
ctest --test-dir "$build" -R '^(libm|libm_golden_current|engine_xeng3_ref|engine_xeng3|engine_xeng3_golden_current|engine_xeng3_fuzz|rt_core)$' --output-on-failure || { echo "FAIL u27"; exit 1; }
echo "PASS u27"; exit 0
