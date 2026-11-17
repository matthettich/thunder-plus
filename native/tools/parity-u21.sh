#!/bin/sh
# U21 parity gate: the shared MODFX modules C (18 modules: Simple delay, Filter, Low/high cut, Classic/Bus/Glue comp, Noise gate, Auto-pan, Pump, Pitch correct,
#   Vowel filter, Rotary speaker, Granular delay, Reverse echo, Envelope filter, Bytebeat with the BBExpr compiler, Chaos). native/tools/parity.sh u21
#   Runs: libm, modfx_ref / modfx (the real chains-worklet.js in Node: all committed tests/golden/chains-modules.json hashes reproduced by the C++ engine-style run, and every direct case
#   bit-exact against the committed hashes, plus the 146-formula BBExpr table), modfx_golden_current (the committed .inc is what the JS gives now; Linux), rt_core (every module under the allocation guard),
#   and the chaos / Bytebeat table generator's own --check.
#   Marbles and Tides are in the gate by the lead's decision (16 Nov): the native mods library is strict, each render / case is within -100 dB of the fast-math wasm's reference, the exact ones stay
#   exact, the native render is pinned by committed hashes (docs/port/U21-NOTES.md, "Marbles / Tides: the decision").
# BUILD_DIR names the build folder (default /tmp/thunder-parity).
set -u
here=$(cd "$(dirname "$0")" && pwd)
native=$(cd "$here/.." && pwd)
build=${BUILD_DIR:-/tmp/thunder-parity}
cmake -S "$native" -B "$build" -DCMAKE_BUILD_TYPE=Release >/dev/null || { echo "FAIL u21 (configure)"; exit 1; }
cmake --build "$build" -j8 --target test_modfx test_libm rt_harness >/dev/null || { echo "FAIL u21 (build)"; exit 1; }
ctest --test-dir "$build" -R '^(libm|libm_golden_current|modfx_ref|modfx|modfx_golden_current|rt_core)$' --output-on-failure || { echo "FAIL u21"; exit 1; }
node "$here/modfx-gen-chaos.mjs" --check || { echo "FAIL u21 (modfx-gen-chaos --check)"; exit 1; }
echo "PASS u21"; exit 0
