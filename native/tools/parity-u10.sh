#!/bin/sh
# U10 parity gate: latency compensation (pdcPlan) and the beat clock.  native/tools/parity.sh u10
#   builds test_pdc, test_pdcmix, test_beat, test_mix, rt_harness and runs: pdc (the plan against the page's own pdcPlan, 26 cases, exact),
#   pdcmix (the delays in the mixer, the renderer's trim), beat (the clock against the core processor's output, 15 cases, bit for bit),
#   the three golden_current checks, mix (U09: still bit-exact with the delay points in), rt_core (everything under the allocation guard).
#   Then, if the web app's reference render exists (render-compare.sh makes it with Chrome: THUNDER_REF, default /tmp/thunder-ref),
#   ELECTRO's render must still pass render-compare.sh (no latent effects: the plan is zeros and the output is U09's, to the bit).
# BUILD_DIR names the build folder (default /tmp/thunder-parity).
set -u
here=$(cd "$(dirname "$0")" && pwd)
native=$(cd "$here/.." && pwd)
build=${BUILD_DIR:-/tmp/thunder-parity}
cmake -S "$native" -B "$build" -DCMAKE_BUILD_TYPE=Release >/dev/null || { echo "FAIL u10 (configure)"; exit 1; }
cmake --build "$build" -j8 --target test_pdc test_pdcmix test_beat test_mix rt_harness thunder-render >/dev/null || { echo "FAIL u10 (build)"; exit 1; }
ctest --test-dir "$build" -R '^(pdc_ref|pdc|pdc_golden_current|pdcmix|beat_ref|beat|beat_golden_current|mix_ref|mix|rt_core)$' --output-on-failure || { echo "FAIL u10"; exit 1; }
ref=${THUNDER_REF:-/tmp/thunder-ref}/ELECTRO
if [ -f "$ref/ref.f32" ]; then
  THUNDER_BUILD="$build" sh -c "cd '$native/..' && bash native/tools/render-compare.sh sounds/ELECTRO.thunder" | tail -1 | grep -q '^render-compare: PASS' || { echo "FAIL u10 (ELECTRO render-compare)"; exit 1; }
else echo "(no reference render at $ref: ELECTRO render-compare skipped)"; fi
echo "PASS u10"; exit 0
