#!/bin/sh
# U18 parity gate: the pump effects as slot kinds of the track-effect host.  native/tools/parity.sh u18
#   builds test_pump + rt_harness and runs: pump_ref / pump (the real ThunderFx worklet in Node with the SHIPPED pump/pump.wasm: 80 cases, latency of every type, the order check, the gate
#   between threads; since web build 638 the shipped wasm is the pin's build (MASTER 77 parameters, latency 4454), so NOTHING is skipped; the exact list is enforced there on Linux x86-64),
#   pump_cases_current / pump_table_current (the committed case list and pump table are what the tools give now; Linux), rt_core (the host, pump kinds included, under the allocation guard).
#   Then the real gate: a pump.wasm BUILT FROM THE PIN (build-wasm.sh, wasi-sdk) is the reference of a second run of pump-dump.mjs + test_pump --pin: -100 dB below each reference's peak
#   (worst case printed), the cases that are bit-exact today still bit-exact (golden/pump_gate.inc), one named rule (x_two_hosts_same). (No committed hash of the native render: the strict pump build uses the system libm, so renders differ between toolchains; U18-NOTES.md.)
#   The lead's decision (16 Nov): docs/port/U18-NOTES.md, "The gate". GATE_WRITE=<file> makes the second run write that golden (never hand-edit it).
#   Nothing is skipped: a fixture that cannot run (no Node, no acorn, no wasm build) fails.
# BUILD_DIR names the build folder (default /tmp/thunder-parity). PIN_WASM_DIR names the folder with the pin-built pump.wasm (default /tmp/thunder-parity-u18-wasm; built there if absent;
# WASI_SDK = a wasi-sdk 25 folder, else build-wasm.sh downloads it).
set -u
here=$(cd "$(dirname "$0")" && pwd)
native=$(cd "$here/.." && pwd)
build=${BUILD_DIR:-/tmp/thunder-parity}
pin=${PIN_WASM_DIR:-/tmp/thunder-parity-u18-wasm}
cmake -S "$native" -B "$build" -DCMAKE_BUILD_TYPE=Release >/dev/null || { echo "FAIL u18 (configure)"; exit 1; }
cmake --build "$build" -j8 --target test_pump rt_harness >/dev/null || { echo "FAIL u18 (build)"; exit 1; }
rc=0
ctest --test-dir "$build" -R '^(pump_ref|pump|pump_cases_current|pump_table_current|rt_core)$' --output-on-failure || rc=1
[ -f "$pin/pump.wasm" ] || "$here/build-wasm.sh" "$pin" >/dev/null || { echo "FAIL u18 (pin wasm build)"; exit 1; }
meta="$pin/pump-pin-meta.json"
node "$here/pump-table.mjs" --meta "$meta" >/dev/null || { echo "FAIL u18 (pin meta)"; exit 1; }
ref="$pin/ref"; rm -rf "$ref"
node "$here/pump-dump.mjs" --wasm "$pin/pump.wasm" --meta "$meta" "$ref" >/dev/null || { echo "FAIL u18 (pin reference)"; exit 1; }
echo "---- against pump.wasm built from the pin"
if [ -n "${GATE_WRITE:-}" ]; then "$build/tests/test_pump" "$ref" --pin --write-gate "$GATE_WRITE" || rc=1; else "$build/tests/test_pump" "$ref" --pin || rc=1; fi
[ $rc -eq 0 ] || { echo "FAIL u18"; exit 1; }
echo "PASS u18"; exit 0
