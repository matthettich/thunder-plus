#!/bin/sh
# U22 parity gate: the engine foundation (tables, rng, blep, renderLayer, the oscillator scaffolding, voiceFilter / voiceAmp / voiceMod, renderVoice, mkVoice's parameters, the pad effect chain).  native/tools/parity.sh u22
#   builds test_engine + test_libm + test_fxu17 + rt_harness and runs: libm / libm_golden_current, engine_ref / engine (the 294 factory voices built only from sine / tri / saw / square / pulse and the noise types:
#   SHA-256 of renderVoice(v, 48000, null, hold) bit-exact against beta's tests/golden/engines.json, and against the local Node run of the real ENGINE block; 183 direct cases and 1800 voiceMod probes
#   bit-exact), engine_golden_current (Linux: the committed hashes are what the JS gives now), fxu17 (the pad cores the chain calls), rt_core (rt_engine: a voice without pad effects under the allocation guard).
#   Nothing is skipped: a reference that cannot run (no Node) fails.
# BUILD_DIR names the build folder (default /tmp/thunder-parity); JOBS the build parallelism (default 2).
set -u
here=$(cd "$(dirname "$0")" && pwd)
native=$(cd "$here/.." && pwd)
build=${BUILD_DIR:-/tmp/thunder-parity}
cmake -S "$native" -B "$build" -DCMAKE_BUILD_TYPE=Release >/dev/null || { echo "FAIL u22 (configure)"; exit 1; }
cmake --build "$build" -j"${JOBS:-2}" --target test_engine test_fxu17 test_libm rt_harness >/dev/null || { echo "FAIL u22 (build)"; exit 1; }
ctest --test-dir "$build" -R '^(libm|libm_golden_current|engine_ref|engine|engine_golden_current|fxu17_ref|fxu17|rt_core)$' --output-on-failure || { echo "FAIL u22"; exit 1; }
echo "PASS u22"; exit 0
