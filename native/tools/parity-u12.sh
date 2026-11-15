#!/bin/sh
# U12 parity gate: the master EQ (Chromium's BiquadFilterNode chain), the master chain's order + the safety limiter.  native/tools/parity.sh u12
#   builds test_eq + test_master + rt_harness and runs: eq_ref (the JS model of the page's eqBuild + Chromium's biquads), eq (31 cases bit-exact, normMst, the
#   chain's order and latencies), eq_golden_current, rt_core (the chain under the allocation guard). With Chrome + playwright installed it also runs
#   tools/eq-webaudio-check.mjs (the model against a real OfflineAudioContext: EQ response within 0.01 dB). Then, if the web app's reference renders exist
#   (render-compare.sh makes them: $THUNDER_REF, default /tmp/thunder-ref), ELECTRO must still pass render-compare.sh and the --master case is run and printed.
# BUILD_DIR names the build folder (default /tmp/thunder-parity).
set -u
here=$(cd "$(dirname "$0")" && pwd)
native=$(cd "$here/.." && pwd)
build=${BUILD_DIR:-/tmp/thunder-parity}
cmake -S "$native" -B "$build" -DCMAKE_BUILD_TYPE=Release >/dev/null || { echo "FAIL u12 (configure)"; exit 1; }
cmake --build "$build" -j8 --target test_eq test_master rt_harness thunder-render >/dev/null || { echo "FAIL u12 (build)"; exit 1; }
ctest --test-dir "$build" -R '^(eq_ref|eq|eq_golden_current|rt_core)$' --output-on-failure || { echo "FAIL u12"; exit 1; }
if [ -x /usr/bin/google-chrome ] && [ -f /opt/npm-tools/node_modules/playwright/index.mjs ] || [ -n "${CHROME:-}" ]; then
  node "$here/eq-webaudio-check.mjs" | tail -1 | grep -q 'PASS' || { echo "FAIL u12 (eq-webaudio-check)"; exit 1; }
else echo "(no Chrome / playwright: eq-webaudio-check skipped)"; fi
ref=${THUNDER_REF:-/tmp/thunder-ref}
if [ -f "$ref/ELECTRO/ref.f32" ]; then
  THUNDER_BUILD="$build" sh -c "cd '$native/..' && bash native/tools/render-compare.sh sounds/ELECTRO.thunder" | tail -1 | grep -q '^render-compare: PASS' || { echo "FAIL u12 (ELECTRO render-compare)"; exit 1; }
  THUNDER_BUILD="$build" sh -c "cd '$native/..' && bash native/tools/render-compare.sh sounds/ELECTRO.thunder --master" | tail -3
else echo "(no reference render at $ref: ELECTRO render-compare skipped)"; fi
echo "PASS u12"; exit 0
