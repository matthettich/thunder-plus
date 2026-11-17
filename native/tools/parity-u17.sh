#!/bin/sh
# U17 parity gate: Pitch reverb, Pitch delay, Resonator and Degrade (track-effect slot kinds 96..99 and the pads' applyFx).  native/tools/parity.sh u17
#   builds test_fxu17 + test_fxidm + test_fxclk + test_libm + rt_harness and runs: libm / libm_golden_current (js_sin, js_cos, js_exp, js_pow, js_tanh bit for bit against the committed golden of Node 22 on
#   x86-64 Linux), fxu17_ref / fxu17 (the real ThunderFx worklet and the real applyFx in Node: every case BIT-EXACT, + the map() value sets), fxu17_golden_current (the committed golden hashes
#   are what the JS gives now; Linux), fx_table_current (the effect table now holds ids 96..99), fxidm / fxclk (U16's and U15's cases still exact), rt_core (the host under the allocation guard, rt_fxu17).
#   Nothing is skipped: a fixture that cannot run (no Node, no acorn) fails.
# BUILD_DIR names the build folder (default /tmp/thunder-parity).
set -u
here=$(cd "$(dirname "$0")" && pwd)
native=$(cd "$here/.." && pwd)
build=${BUILD_DIR:-/tmp/thunder-parity}
cmake -S "$native" -B "$build" -DCMAKE_BUILD_TYPE=Release >/dev/null || { echo "FAIL u17 (configure)"; exit 1; }
cmake --build "$build" -j8 --target test_fxu17 test_fxidm test_fxclk test_libm rt_harness >/dev/null || { echo "FAIL u17 (build)"; exit 1; }
ctest --test-dir "$build" -R '^(libm|libm_golden_current|fxu17_ref|fxu17|fxu17_golden_current|fx_table_current|fxidm_ref|fxidm|fxclk_ref|fxclk|rt_core)$' --output-on-failure || { echo "FAIL u17"; exit 1; }
echo "PASS u17"; exit 0
