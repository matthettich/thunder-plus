#!/bin/sh
# U15 parity gate: the tempo-locked track effects (Flex, Step gate, Retrigger).  native/tools/parity.sh u15
#   builds test_fxclk + test_libm + rt_harness and runs: libm / libm_golden_current (js_cos, js_sin, js_tan, js_log bit for bit against the committed golden of Node 22 on x86-64 Linux), fxclk_ref / fxclk (the real
#   ThunderFx worklet in Node: 73 cases, every one BIT-EXACT, + the map() value sets), fxclk_golden_current / flexspec_current (the committed golden hashes and the Flex
#   pattern table are what the JS gives now; Linux), fx_table_current (the effect table now holds Retrigger, Step gate, Flex), rt_core (the host under the allocation guard).
#   Nothing is skipped: a fixture that cannot run (no Node, no acorn) fails.
# BUILD_DIR names the build folder (default /tmp/thunder-parity).
set -u
here=$(cd "$(dirname "$0")" && pwd)
native=$(cd "$here/.." && pwd)
build=${BUILD_DIR:-/tmp/thunder-parity}
cmake -S "$native" -B "$build" -DCMAKE_BUILD_TYPE=Release >/dev/null || { echo "FAIL u15 (configure)"; exit 1; }
cmake --build "$build" -j8 --target test_fxclk test_libm rt_harness >/dev/null || { echo "FAIL u15 (build)"; exit 1; }
ctest --test-dir "$build" -R '^(libm|libm_golden_current|fxclk_ref|fxclk|fxclk_golden_current|flexspec_current|fx_table_current|rt_core)$' --output-on-failure || { echo "FAIL u15"; exit 1; }
echo "PASS u15"; exit 0
