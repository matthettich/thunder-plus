#!/bin/sh
# U16 parity gate: the JS-only track effects Beat grains, Beat scramble, Glitch sequencer and the Vintage sampler.  native/tools/parity.sh u16
#   builds test_fxidm + test_libm + rt_harness and runs: libm / libm_golden_current (js_cos, js_sin, js_tan, js_log and js_tanh bit for bit against the committed golden of Node 22 on x86-64
#   Linux), fxidm_ref / fxidm (the real ThunderFx worklet in Node: 242 cases, every one BIT-EXACT, + the map() value sets), fxidm_golden_current (the committed golden hashes are what the JS
#   gives now; Linux), fx_table_current (the effect table now holds ids 100..103), fxclk (U15's cases still exact), rt_core (the host under the allocation guard, rt_fxidm).
#   Nothing is skipped: a fixture that cannot run (no Node, no acorn) fails.
# BUILD_DIR names the build folder (default /tmp/thunder-parity).
set -u
here=$(cd "$(dirname "$0")" && pwd)
native=$(cd "$here/.." && pwd)
build=${BUILD_DIR:-/tmp/thunder-parity}
cmake -S "$native" -B "$build" -DCMAKE_BUILD_TYPE=Release >/dev/null || { echo "FAIL u16 (configure)"; exit 1; }
cmake --build "$build" -j8 --target test_fxidm test_fxclk test_libm rt_harness >/dev/null || { echo "FAIL u16 (build)"; exit 1; }
ctest --test-dir "$build" -R '^(libm|libm_golden_current|fxidm_ref|fxidm|fxidm_golden_current|fx_table_current|fxclk_ref|fxclk|rt_core)$' --output-on-failure || { echo "FAIL u16"; exit 1; }
echo "PASS u16"; exit 0
