#!/bin/sh
# U31 round-3 F4: "mods.wasm stays the web's bytes". Usage: mods-wasm-check.sh [built mods.wasm]   (needs python3; no toolchain)
# 1. the web's mods.wasm is the base64 in MODS_WASM_B64 of chains/chains-worklet.js and waves/waves-worklet.js: both must decode to the same bytes, and those bytes must have the sha256 in
#    native/tools/mods-wasm.sha256 (a change of the web's mods.wasm must be made on purpose: update the pin and re-run `parity.sh u02`). Exit 1 on a mismatch.
# 2. with a built mods.wasm (build-wasm.sh output) as argument: its sha256 is compared with the web's; printed as INFO (the other wasm files are compared the same way in parity-u02.sh), exit status untouched.
set -e
R=$(cd "$(dirname "$0")/../.." && pwd); PIN=$(cut -d' ' -f1 "$R/native/tools/mods-wasm.sha256")
python3 -I - "$R/chains/chains-worklet.js" "$R/waves/waves-worklet.js" "$PIN" "${1:-}" <<'PY'
import re,sys,base64,hashlib
pin=sys.argv[3]; hs=[]
for p in sys.argv[1:3]:
    s=open(p,encoding='utf-8').read(); m=re.findall(r"MODS_WASM_B64\s*=\s*['\"]([A-Za-z0-9+/=]+)['\"]",s)
    if len(m)!=1: sys.exit("mods-wasm-check: expected exactly one MODS_WASM_B64 in "+p+", found "+str(len(m)))
    hs.append(hashlib.sha256(base64.b64decode(m[0])).hexdigest())
if hs[0]!=hs[1]: sys.exit("mods-wasm-check: FAIL the two worklets embed different mods.wasm bytes ("+hs[0][:12]+" vs "+hs[1][:12]+")")
if hs[0]!=pin: sys.exit("mods-wasm-check: FAIL the web's mods.wasm is "+hs[0]+", the pin says "+pin+" (changed on purpose? update native/tools/mods-wasm.sha256 and re-run parity.sh u02)")
print("mods-wasm-check: PASS the web's mods.wasm (both worklets) is the pinned "+pin[:12])
if sys.argv[4]:
    b=hashlib.sha256(open(sys.argv[4],'rb').read()).hexdigest()
    print("INFO  mods.wasm built %s the web's bytes (%s)" % ("equals" if b==hs[0] else "DIFFERS from", b[:12]))
PY
