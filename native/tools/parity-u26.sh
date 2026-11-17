#!/bin/sh
# U26 parity gate: the bass and voice engines (rendered path) and the live shell (PROVISIONAL interface).  native/tools/parity.sh u26
#   builds test_engine_u26 + test_live_u26 + test_engine_u26_fuzz + test_libm + rt_harness and runs: libm / libm_golden_current, engine_u26_ref / engine_u26 (every factory voice of beta's tests/golden/engines.json whose engine is bass or vox,
#   SHA-256 bit-exact: 90 voices; and the direct cases, core scripts and lyric parse batches against the real ENGINE block, bit-exact against the committed Linux hashes on every platform and against the local Node on x86-64 Linux),
#   live_u26 (the live shell equals the JS cores driven by the live rule, the rendered path and a rule written on the cores, at every host block size), engine_u26_golden_current (Linux), engine_u26_fuzz (extreme / NaN parameters,
#   combined fields), rt_core (rt_engine_u26: the allocation guard).
#   Nothing is skipped: a reference that cannot run (no Node) fails.
# BUILD_DIR names the build folder (default /tmp/thunder-parity); JOBS the build parallelism (default 2).
set -u
here=$(cd "$(dirname "$0")" && pwd)
native=$(cd "$here/.." && pwd)
build=${BUILD_DIR:-/tmp/thunder-parity}
cmake -S "$native" -B "$build" -DCMAKE_BUILD_TYPE=Release >/dev/null || { echo "FAIL u26 (configure)"; exit 1; }
cmake --build "$build" -j"${JOBS:-2}" --target test_engine_u26 test_live_u26 test_engine_u26_fuzz test_libm rt_harness >/dev/null || { echo "FAIL u26 (build)"; exit 1; }
ctest --test-dir "$build" -R '^(libm|libm_golden_current|engine_u26_ref|engine_u26|live_u26|engine_u26_golden_current|engine_u26_fuzz|rt_core)$' --output-on-failure || { echo "FAIL u26"; exit 1; }
echo "PASS u26"; exit 0
