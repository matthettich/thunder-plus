#!/bin/sh
# Parity gate. Usage: native/tools/parity.sh u02 [OUTDIR]     (OUTDIR default /tmp/u02-wasm)
# U02: hashes the CMake-built wasm modules (native/tools/build-wasm.sh) against
#   (a) the committed files the app loads: waves/dsp/thunder-dsp.wasm, waves/dsp/tfx.wasm, chains/dsp/tfx.wasm and the MODS_WASM_B64
#       base64 in chains/chains-worklet.js (mods.wasm), and
#   (b) what dsp/build.sh, dsp/tfx/build.sh and dsp/mods/build.sh produce from the SAME pinned upstreams (proves CMake == build.sh).
# Upstream commits move: (a) can FAIL while (b) PASSes, and then only the pins differ from the ones the committed files were built with.
# Exit status 1 if any line is FAIL.
set -e
[ "$1" = u02 ] || { echo "usage: parity.sh u02 [OUTDIR]" >&2; exit 2; }
R=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${2:-/tmp/u02-wasm}
SDK=${WASI_SDK:-$(dirname "$OUT")/u02-wasi-sdk}
TP="$R/native/core/third_party"
fail=0
h() { sha256sum "$1" | cut -c1-16; }
cmp_line() {  # label built other
  if [ ! -f "$3" ]; then echo "SKIP  $1 (no $3)"; return; fi
  if cmp -s "$2" "$3"; then echo "PASS  $1  $(h "$2")"; else echo "FAIL  $1  built $(h "$2") ($(wc -c <"$2") B) vs $(h "$3") ($(wc -c <"$3") B)"; fail=1; fi
}
for f in tfx thunder-dsp mods; do [ -f "$OUT/$f.wasm" ] || { echo "missing $OUT/$f.wasm: run native/tools/build-wasm.sh $OUT first" >&2; exit 2; }; done
# mods.wasm is committed only as base64
python3 -I - "$R/chains/chains-worklet.js" "$OUT/committed-mods.wasm" <<'PY'
import re,sys,base64
s=open(sys.argv[1],encoding='utf-8',errors='replace').read()
m=re.search(r"MODS_WASM_B64\s*=\s*['\"]([A-Za-z0-9+/=]+)['\"]",s)
open(sys.argv[2],'wb').write(base64.b64decode(m.group(1)) if m else b'')
PY
echo "== (a) CMake build vs the committed files"
cmp_line "tfx.wasm vs waves/dsp/tfx.wasm" "$OUT/tfx.wasm" "$R/waves/dsp/tfx.wasm"
cmp_line "tfx.wasm vs chains/dsp/tfx.wasm" "$OUT/tfx.wasm" "$R/chains/dsp/tfx.wasm"
cmp_line "thunder-dsp.wasm vs waves/dsp/thunder-dsp.wasm" "$OUT/thunder-dsp.wasm" "$R/waves/dsp/thunder-dsp.wasm"
cmp_line "mods.wasm vs chains-worklet.js MODS_WASM_B64" "$OUT/mods.wasm" "$OUT/committed-mods.wasm"
echo "== (b) CMake build vs dsp/build.sh, dsp/tfx/build.sh, dsp/mods/build.sh on the same pins"
REF="$OUT/ref"; rm -rf "$REF"; mkdir -p "$REF"
sh "$TP/fetch.sh" "$OUT/deps" >/dev/null
cp -r "$OUT/deps/eurorack" "$REF/eurorack"; rm -rf "$REF/eurorack/stmlib"; cp -r "$OUT/deps/stmlib" "$REF/eurorack/stmlib"
cp -r "$R/dsp" "$REF/dsp"      # build.sh writes next to itself: build in a copy
A="$OUT/deps/airwindows/plugins/LinuxVST/src"
( cd "$REF/dsp/tfx" && E="$REF/eurorack" D="$OUT/deps/DaisySP" A="$A" C="$OUT/deps/CloudSeedCore" WASI_SDK="$SDK" sh build.sh >/dev/null 2>&1 )
( cd "$REF/dsp/mods" && E="$REF/eurorack" WASI_SDK="$SDK" sh build.sh >/dev/null 2>&1 )
( cd "$REF/dsp" && E="$REF/eurorack" D="$OUT/deps/DaisySP" A="$A" SYSROOT="$SDK/share/wasi-sysroot" RESDIR="$(ls -d "$SDK"/lib/clang/*|head -1)" sh build.sh >/dev/null 2>&1 )
cmp_line "tfx.wasm vs dsp/tfx/build.sh" "$OUT/tfx.wasm" "$REF/dsp/tfx/tfx.wasm"
cmp_line "mods.wasm vs dsp/mods/build.sh" "$OUT/mods.wasm" "$REF/dsp/mods/mods.wasm"
cmp_line "thunder-dsp.wasm vs dsp/build.sh" "$OUT/thunder-dsp.wasm" "$REF/dsp/dsp.wasm"
[ $fail = 0 ] && echo "parity u02: PASS" || echo "parity u02: FAIL"
exit $fail
