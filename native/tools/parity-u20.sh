#!/bin/sh
# U20 parity gate: MODFX modules B, Spectral / Extreme stretch / Convolution reverb and the shared FFT cache. native/tools/parity.sh u20
#   builds test_modfx + test_libm + rt_harness and runs: libm / libm_golden_current (js_atan2 and js_hypot3 added: eight functions bit for bit against the committed golden of Node 22 on x86-64 Linux),
#   modfx_ref / modfx (the real chains-worklet.js in Node: the committed tests/golden/chains-modules.json hashes of spectral, stretch and convolve reproduced by the C++, and their direct cases (with the
#   song's impulse responses) bit-exact; the modules of U19 and any other unit are in the same run), modfx_golden_current (the committed .inc is what the JS gives now; Linux), rt_core (rt_modfx: every
#   module, these three included, under the allocation guard).
#   Nothing is skipped: a fixture that cannot run (no Node) fails.
# BUILD_DIR names the build folder (default /tmp/thunder-parity).
set -u
here=$(cd "$(dirname "$0")" && pwd)
native=$(cd "$here/.." && pwd)
build=${BUILD_DIR:-/tmp/thunder-parity}
cmake -S "$native" -B "$build" -DCMAKE_BUILD_TYPE=Release >/dev/null || { echo "FAIL u20 (configure)"; exit 1; }
cmake --build "$build" -j8 --target test_modfx test_libm rt_harness >/dev/null || { echo "FAIL u20 (build)"; exit 1; }
ctest --test-dir "$build" -R '^(libm|libm_golden_current|modfx_ref|modfx|modfx_golden_current|rt_core)$' --output-on-failure || { echo "FAIL u20"; exit 1; }
echo "PASS u20"; exit 0
