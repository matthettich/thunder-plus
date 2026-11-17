#!/bin/sh
# U33 parity gate: the pump synths hosted natively.  native/tools/parity.sh u33
#   builds test_pumpsynth + test_pumpsynth_fuzz + rt_harness and runs: pumpsynth_ref / pumpsynth (the web's PsynCore.render with the SHIPPED pump/synth/pump-synth.wasm in Node: 507 cases; -100 dB below each case's peak,
#   the bit-exact cases kept exact on Linux x86-64, 39 cases left out by name with a structural check, parameter tables and KINDS equal to the page's meta, latency), pumpsynth_cases_current / pumpsynth_table_current (Linux),
#   pumpsynth_fuzz (combined fields; FUZZ_COMBINED / FUZZ_DIRECT raise it), rt_core (rt_pumpsynth: the allocation guard).
#   Then the same gate against a pump-synth.wasm BUILT FROM THE PIN (build-wasm.sh, wasi-sdk 25; byte-identical to the shipped one, third_party/PINS.md): PIN_WASM_DIR names the folder holding it (default /tmp/thunder-parity-u18-wasm;
#   built there if absent; WASI_SDK = a wasi-sdk 25 folder, else build-wasm.sh downloads it). GATE_WRITE=<file> makes the first (shipped-wasm) run write golden/psyn_gate.inc (never hand-edited).
#   Nothing is skipped: a fixture that cannot run (no Node, no wasm build) fails.
# BUILD_DIR names the build folder (default /tmp/thunder-parity); JOBS the build parallelism (default 2).
set -u
here=$(cd "$(dirname "$0")" && pwd)
native=$(cd "$here/.." && pwd)
build=${BUILD_DIR:-/tmp/thunder-parity}
pin=${PIN_WASM_DIR:-/tmp/thunder-parity-u18-wasm}
cmake -S "$native" -B "$build" -DCMAKE_BUILD_TYPE=Release >/dev/null || { echo "FAIL u33 (configure)"; exit 1; }
cmake --build "$build" -j"${JOBS:-2}" --target test_pumpsynth test_pumpsynth_fuzz rt_harness >/dev/null || { echo "FAIL u33 (build)"; exit 1; }
rc=0
ctest --test-dir "$build" -R '^(pumpsynth_ref|pumpsynth|pumpsynth_cases_current|pumpsynth_table_current|pumpsynth_fuzz|rt_core)$' --output-on-failure || rc=1
[ -f "$pin/pump-synth.wasm" ] || "$here/build-wasm.sh" "$pin" >/dev/null || { echo "FAIL u33 (pin wasm build)"; exit 1; }
ref="$pin/psyn-ref"; rm -rf "$ref"
node "$here/psyn-dump.mjs" --wasm "$pin/pump-synth.wasm" "$ref" || { echo "FAIL u33 (pin reference)"; exit 1; }
echo "---- against pump-synth.wasm built from the pin"
if [ -n "${GATE_WRITE:-}" ]; then "$build/tests/test_pumpsynth" "$ref" --write-gate "$GATE_WRITE" || rc=1; else "$build/tests/test_pumpsynth" "$ref" || rc=1; fi
rm -rf "$ref"
[ $rc -eq 0 ] || { echo "FAIL u33"; exit 1; }
echo "PASS u33"; exit 0
