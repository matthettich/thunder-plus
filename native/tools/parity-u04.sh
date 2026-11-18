#!/bin/sh
# U04 parity gate: the optional LXR module.  native/tools/parity.sh u04
#   OPEN configuration (THUNDER_WITH_LXR=ON): builds test_lxr + test_lxr_fuzz + test_lxr_seam + rt_harness and runs lxr_ref / lxr (the web's LxrCore.render with the SHIPPED lxr/lxr.wasm in Node: 818 cases,
#   the kit bytes equal, the exact list kept exact on Linux x86-64 GCC, every other case named in golden/lxr_leftout.inc with its numbers and a structural check, order independence, the live path),
#   lxr_tables_current / lxr_cases_current, lxr_fuzz, lxr_seam, lxr_boundary, rt_core (the allocation guard).
#   CLOSED configuration (THUNDER_WITH_LXR=OFF, a second small build without the DSP and pump libraries): lxr_seam (LXR refused, the rest unchanged) and lxr_boundary over every binary.
# The shipped lxr.wasm IS the build of the repository's source (lxr/build.sh, wasi-sdk 25: same sha256, native/lxr/LICENSES.md and third_party/PINS.md), so there is no second, pin-built, run.
# BUILD_DIR / CLOSED_DIR name the build folders (defaults /tmp/thunder-parity-u04, /tmp/thunder-parity-u04-closed); JOBS the build parallelism (default 2). Nothing is skipped.
set -u
here=$(cd "$(dirname "$0")" && pwd)
native=$(cd "$here/.." && pwd)
build=${BUILD_DIR:-/tmp/thunder-parity-u04}
closed=${CLOSED_DIR:-/tmp/thunder-parity-u04-closed}
cmake -S "$native" -B "$build" -DCMAKE_BUILD_TYPE=Release -DTHUNDER_WITH_LXR=ON >/dev/null || { echo "FAIL u04 (configure)"; exit 1; }
cmake --build "$build" -j"${JOBS:-2}" --target test_lxr test_lxr_fuzz test_lxr_seam rt_harness >/dev/null || { echo "FAIL u04 (build)"; exit 1; }
rc=0
ctest --test-dir "$build" -R '^(lxr_ref|lxr|lxr_tables_current|lxr_cases_current|lxr_fuzz|lxr_seam|lxr_boundary|rt_core)$' --output-on-failure || rc=1
cmake -S "$native" -B "$closed" -DCMAKE_BUILD_TYPE=Release -DTHUNDER_WITH_LXR=OFF -DTHUNDER_WITH_DSP=OFF -DTHUNDER_WITH_PUMP=OFF >/dev/null || { echo "FAIL u04 (closed configure)"; exit 1; }
cmake --build "$closed" -j"${JOBS:-2}" --target test_lxr_seam >/dev/null || { echo "FAIL u04 (closed build)"; exit 1; }
echo "---- closed configuration (THUNDER_WITH_LXR=OFF)"
ctest --test-dir "$closed" -R '^(lxr_seam|lxr_boundary)$' --output-on-failure || rc=1
[ $rc -eq 0 ] || { echo "FAIL u04"; exit 1; }
echo "PASS u04"; exit 0
