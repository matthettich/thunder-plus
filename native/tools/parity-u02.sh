#!/bin/sh
# Parity gate. Usage: native/tools/parity.sh u02 [OUTDIR]     (OUTDIR default /tmp/u02-wasm; needs node, python3, git, curl)
# U02 gate: "wasm built from CMake gives identical hashes to today's tfx.wasm on chains-modules.json", i.e. the AUDIO the modules produce.
# 1. builds tfx.wasm, thunder-dsp.wasm, mods.wasm with CMake + wasi-sdk (build-wasm.sh; canonical link order, so the bytes do not depend on
#    the machine);
# 2. makes a temporary copy of the tree and swaps the CMake files in (waves/dsp/*.wasm, chains/dsp/tfx.wasm, and the MODS_WASM_B64 base64 in
#    chains/chains-worklet.js and waves/waves-worklet.js). The committed files are never touched;
# 3. runs the golden tests in the copy exactly as `npm test` does (node --test, one process per file) and prints PASS / FAIL per golden.
# Byte equality with the committed files is printed as INFO only (the committed files came from unpinned upstreams).
# Exit status 1 if any line is FAIL.
set -e
[ "$1" = u02 ] || { echo "usage: parity.sh u02 [OUTDIR]" >&2; exit 2; }
R=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${2:-/tmp/u02-wasm}
if [ -z "$PARITY_SKIP_BUILD" ]; then sh "$R/native/tools/build-wasm.sh" "$OUT" >/dev/null; fi
for f in tfx thunder-dsp mods; do [ -f "$OUT/$f.wasm" ] || { echo "missing $OUT/$f.wasm" >&2; exit 2; }; done
for p in "waves/dsp/tfx.wasm tfx" "chains/dsp/tfx.wasm tfx" "waves/dsp/thunder-dsp.wasm thunder-dsp"; do
  set -- $p; if cmp -s "$OUT/$2.wasm" "$R/$1"; then echo "INFO  bytes equal   $1"; else echo "INFO  bytes differ  $1 (built $(wc -c <"$OUT/$2.wasm") B, committed $(wc -c <"$R/$1") B)"; fi
done
# U31 round-3 F4: the web's mods.wasm (embedded in both worklets) is the pinned bytes (FAIL otherwise); the built one is compared as INFO
sh "$R/native/tools/mods-wasm-check.sh" "$OUT/mods.wasm"
T="$OUT/tree"; rm -rf "$T"; mkdir -p "$T"
( cd "$R" && tar --exclude=./.git --exclude=./native --exclude=./stable --exclude=./beta --exclude=./node_modules -cf - . ) | ( cd "$T" && tar -xf - )
[ -d "$R/node_modules" ] && ln -s "$R/node_modules" "$T/node_modules"
cp "$OUT/tfx.wasm" "$T/waves/dsp/tfx.wasm"; cp "$OUT/tfx.wasm" "$T/chains/dsp/tfx.wasm"; cp "$OUT/thunder-dsp.wasm" "$T/waves/dsp/thunder-dsp.wasm"
python3 -I - "$OUT/mods.wasm" "$T/chains/chains-worklet.js" "$T/waves/waves-worklet.js" <<'PY'
import re,sys,base64
b=base64.b64encode(open(sys.argv[1],'rb').read()).decode()
for p in sys.argv[2:]:
    s=open(p,encoding='utf-8').read()
    n,k=re.subn(r"(MODS_WASM_B64\s*=\s*['\"])[A-Za-z0-9+/=]+(['\"])",lambda m:m.group(1)+b+m.group(2),s)
    if k!=1: sys.exit("expected exactly one MODS_WASM_B64 assignment in "+p+", found "+str(k))
    open(p,'w',encoding='utf-8').write(n)
PY
fail=0
run() {  # label files...
  label=$1; shift
  if ( cd "$T" && node --test "$@" >"$OUT/test-$(echo "$label" | tr ' /' '__').log" 2>&1 ); then echo "PASS  $label"; else echo "FAIL  $label   (log: $OUT/test-$(echo "$label" | tr ' /' '__').log)"; fail=1; fi
}
echo "== goldens rendered through the CMake-built wasm (tests run in a copy of the tree)"
run "tests/golden/chains-modules.json (chains.test.js)" tests/chains.test.js
run "tests/golden/chains-processor.json (chains.test.js)" tests/chains.test.js
run "tests/golden/chains-thunder-node.json (chains.test.js)" tests/chains.test.js
run "tests/golden/waves-modules.json (waves.test.js)" tests/waves.test.js
run "tests/golden/waves-processor.json (waves.test.js)" tests/waves.test.js
run "tests/golden/chains-modules.json again via waves.test.js" tests/waves.test.js
echo "== other tests that load these wasm files"
for t in block-size feedback idle-shortcut lverb fx-rest modules-blocks modules-clock modules-delay modules-expr modules-maths modules-scope wc-pass4 worklet-memory; do
  [ -f "$T/tests/$t.test.js" ] && run "tests/$t.test.js" "tests/$t.test.js"
done
echo "== the whole suite as npm test runs it"
run "npm test (node --test tests/*.test.js)" tests/*.test.js
[ $fail = 0 ] && echo "parity u02: PASS" || echo "parity u02: FAIL"
exit $fail
