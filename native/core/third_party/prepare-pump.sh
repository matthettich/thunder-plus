#!/bin/sh
# U33: the copy of the pinned pump checkout that HOST builds compile (core/pump/CMakeLists.txt). Any patch here is a read or write outside a table found by AddressSanitizer in the fuzz of the
# synths (tests/test_pumpsynth_fuzz.cpp), minimal, and commented with the file, the line, the table's size and what triggers it. The owner's code is not copied into the repo: this makes
# a copy in the build folder. The wasm builds (build-wasm.sh, the web app's pump.wasm / pump-synth.wasm) are NOT patched: their bytes stay what the pin builds (third_party/PINS.md).
# Since the re-pin to e43ee10 (pump PR #6, fix/synth-table-reads) the four patches this script carried (WOB sub phase wrap, BandWaves::read, GLOSS DigiWaves::read, the SynthBase::run insertion sort)
# are upstream and are GONE from here (docs/port/REPIN-NATIVE-NOTES.md). Instead the script FAILS when the checkout does not hold those fixes (a pin moved back to an older commit would silently
# reintroduce the over-reads and the per-block allocation).
# Usage: prepare-pump.sh PUMP_PIN_DIR OUT      (OUT gets common/ wasm/ plugins/)
set -e
P=$1; O=$2
[ -n "$P" ] && [ -n "$O" ] || { echo "usage: prepare-pump.sh PUMP_DIR OUT" >&2; exit 2; }
rm -rf "$O"; mkdir -p "$O"
for d in common wasm plugins; do cp -r "$P/$d" "$O/$d"; done
[ ! -d "$O/plugins/machine" ] || { echo "prepare-pump.sh: plugins/machine (GPL) is in the checkout" >&2; exit 1; }
# The upstream fixes this script used to patch in (each must be there exactly once):
need() { [ "$(grep -cF -- "$2" "$O/$1")" -eq 1 ] || { echo "prepare-pump.sh: $1 does not hold the upstream fix (expected once: $2): the pin is older than e43ee10" >&2; exit 1; }; }
need plugins/wob/WobEngine.hpp 'if (subPh >= 1.0) subPh -= std::floor(subPh);'
need common/dsp/Synth.hpp 'if (i >= n) { x -= n; i = 0; }'
need plugins/gloss/GlossEngine.hpp 'if (i >= kN) { x -= kN; i = 0; }'
need wasm/pump_synth.cpp 'while (j > 0 && m.offset < q[j - 1].offset)'
! grep -q 'std::stable_sort(' "$O/wasm/pump_synth.cpp" || { echo "prepare-pump.sh: pump_synth.cpp still calls std::stable_sort" >&2; exit 1; }
