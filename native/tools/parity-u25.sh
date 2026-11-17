#!/bin/sh
# U25 parity gate: the Plaits / Elements layer types of the engine (core/src/engine/layer_pe.cpp on the strict library core/dsp/pe_host.cc).  native/tools/parity.sh u25
#   builds test_engine_u25 + rt_harness (+ what the engine tests need) and runs: libm / libm_golden_current, the U22 engine tests (nothing there may change), engine_u25_ref / engine_u25 (the real ENGINE block
#   with the web's own thunder-dsp.wasm: the wasm's pre-renders injected must give the wasm's samples bit for bit; every case within -100 dB or left out by name with its class; the native hashes), rt_core
#   (rt_engine_u25: the allocation guard). Node is needed (a reference that cannot run fails). U25_WASM names another thunder-dsp.wasm (for example the one native/tools/build-wasm.sh makes from the pins).
# BUILD_DIR names the build folder (default /tmp/thunder-parity); JOBS the build parallelism (default 2).
set -u
here=$(cd "$(dirname "$0")" && pwd)
native=$(cd "$here/.." && pwd)
build=${BUILD_DIR:-/tmp/thunder-parity}
cmake -S "$native" -B "$build" -DCMAKE_BUILD_TYPE=Release >/dev/null || { echo "FAIL u25 (configure)"; exit 1; }
cmake --build "$build" -j"${JOBS:-2}" --target test_engine test_engine_u25 test_libm rt_harness >/dev/null || { echo "FAIL u25 (build)"; exit 1; }
ctest --test-dir "$build" -R '^(libm|libm_golden_current|engine_ref|engine|engine_golden_current|engine_u25_ref|engine_u25|rt_core)$' --output-on-failure || { echo "FAIL u25"; exit 1; }
echo "PASS u25"; exit 0
