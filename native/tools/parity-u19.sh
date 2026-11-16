#!/bin/sh
# U19 parity gate: the shared MODFX module block (54 modules). native/tools/parity.sh u19
#   builds test_modfx + test_libm + rt_harness and runs: libm / libm_golden_current (js_log2 added: six functions bit for bit against the committed golden of Node 22 on x86-64 Linux),
#   modfx_ref / modfx (the real chains-worklet.js in Node: the committed tests/golden/chains-modules.json hashes reproduced by the C++ engine-style run, 162 renders, and 904 direct cases
#   bit-exact), modfx_golden_current (the committed .inc is what the JS gives now; Linux), rt_core (rt_modfx: every module under the allocation guard).
#   Nothing is skipped: a fixture that cannot run (no Node) fails. The shared block of chains-worklet.js and waves-worklet.js is checked identical by modfx_golden_current (and every modfx-dump run).
# BUILD_DIR names the build folder (default /tmp/thunder-parity).
set -u
here=$(cd "$(dirname "$0")" && pwd)
native=$(cd "$here/.." && pwd)
build=${BUILD_DIR:-/tmp/thunder-parity}
cmake -S "$native" -B "$build" -DCMAKE_BUILD_TYPE=Release >/dev/null || { echo "FAIL u19 (configure)"; exit 1; }
cmake --build "$build" -j8 --target test_modfx test_libm rt_harness >/dev/null || { echo "FAIL u19 (build)"; exit 1; }
ctest --test-dir "$build" -R '^(libm|libm_golden_current|modfx_ref|modfx|modfx_golden_current|rt_core)$' --output-on-failure || { echo "FAIL u19"; exit 1; }
echo "PASS u19"; exit 0
