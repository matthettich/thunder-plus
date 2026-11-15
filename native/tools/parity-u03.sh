#!/bin/sh
# U03 parity gate (called by `native/tools/parity.sh u03 [OUTDIR]`; OUTDIR default /tmp/u03-wasm; needs node, python3, git, curl, a host c++).
# Gate: every test / golden that renders through pump gives the same result with pump.wasm and pump-synth.wasm built by CMake from the pinned
# matthettich/pump as with the committed files (the AUDIO hashes, not the files' bytes; same decision as U02).
# 1. builds pump.wasm and pump-synth.wasm (build-wasm.sh: CMake + wasi-sdk);
# 2. makes a temporary copy of the tree, swaps them in (pump/pump.wasm, pump/synth/pump-synth.wasm); committed files are never touched;
# 3. runs the pump tests in the copy exactly as `npm test` does (node --test, one process per file), then the whole suite;
# 4. INFO: byte equality, and whether pump-meta.js / pump-synth-meta.js built from the pin carry the same sha1 stamp as the committed ones
#    (that stamp is the evidence for "the pin matches the shipped wasm");
# 5. pump's own wasm/compare.cpp + compare.mjs: the native build of pump_wasm.cpp against the CMake-built pump.wasm.
# Exit status 1 if any line is FAIL.
set -e
R=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${2:-/tmp/u03-wasm}
mkdir -p "$OUT"
if [ -z "$PARITY_SKIP_BUILD" ]; then sh "$R/native/tools/build-wasm.sh" "$OUT" >/dev/null; fi
for f in pump pump-synth; do [ -f "$OUT/$f.wasm" ] || { echo "missing $OUT/$f.wasm" >&2; exit 2; }; done
P=${THUNDER_DEPS_DIR:-$R/native/core/third_party/_deps}/pump
[ -f "$P/wasm/pump_wasm.cpp" ] || { echo "missing pinned pump checkout at $P (run native/core/third_party/fetch.sh)" >&2; exit 2; }
echo "pump pin: $(git -C "$P" rev-parse HEAD)"
for p in "pump/pump.wasm pump" "pump/synth/pump-synth.wasm pump-synth"; do
  set -- $p; if cmp -s "$OUT/$2.wasm" "$R/$1"; then echo "INFO  bytes equal   $1"; else echo "INFO  bytes differ  $1 (built $(wc -c <"$OUT/$2.wasm") B, committed $(wc -c <"$R/$1") B)"; fi
done
# the parameter / preset tables, from the pinned headers exactly as wasm/build.sh makes them; compare the sha1 stamps
${CXX:-c++} -std=c++17 -O1 -I"$P/common" "$P/wasm/pump_meta.cpp" -o "$OUT/pump_meta"
"$OUT/pump_meta" > "$OUT/meta.json"; "$OUT/pump_meta" synth > "$OUT/synth-meta.json"
python3 -I - "$OUT" "$P/plugins" "$R" <<'PY'
import json, re, sys, os, hashlib
out, plug, root = sys.argv[1:4]
for kind, src, var, js, committed in (('fx', 'meta.json', 'PUMP_META', 'pump-meta.js', 'pump/pump-meta.js'),
                                      ('synth', 'synth-meta.json', 'PUMP_SYNTH_META', 'pump-synth-meta.js', 'pump/synth/pump-synth-meta.js')):
    meta = json.load(open(os.path.join(out, src)))
    for k in meta['types']:
        info = open(os.path.join(plug, k, 'DistrhoPluginInfo.h')).read()
        m = re.search(r'#define PUMP_PLUGIN_ABOUT\s+"((?:[^"\\]|\\.)*)"', info)
        meta[kind][k]['about'] = m.group(1) if m else ''
    s = json.dumps(meta, separators=(',', ':'), ensure_ascii=False)
    h = hashlib.sha1(s.encode()).hexdigest()
    open(os.path.join(out, js), 'w').write('// sha1 ' + h + '\nglobalThis.' + var + ' = ' + s + ';\n')
    old = re.search(r'// sha1 ([0-9a-f]{40})', open(os.path.join(root, committed)).read()).group(1)
    print('INFO  %-34s %s  (pin: %s, committed: %s)' % (committed, 'meta stamp EQUAL' if h == old else 'meta stamp DIFFERS', h[:10], old[:10]))
PY
T="$OUT/tree"; rm -rf "$T"; mkdir -p "$T"
( cd "$R" && tar --exclude=./.git --exclude=./native --exclude=./stable --exclude=./beta --exclude=./node_modules -cf - . ) | ( cd "$T" && tar -xf - )
[ -d "$R/node_modules" ] && ln -s "$R/node_modules" "$T/node_modules"
cp "$OUT/pump.wasm" "$T/pump/pump.wasm"; cp "$OUT/pump-synth.wasm" "$T/pump/synth/pump-synth.wasm"
fail=0
run() {  # label files...
  label=$1; shift
  log="$OUT/test-$(echo "$label" | tr ' /()' '____').log"
  if ( cd "$T" && node --test "$@" >"$log" 2>&1 ); then echo "PASS  $label"; else echo "FAIL  $label   (log: $log)"; fail=1; fi
}
echo "== goldens rendered through the CMake-built pump wasm (tests run in a copy of the tree)"
run "tests/golden/pump-synth.json (pump-synth.test.js)" tests/pump-synth.test.js
echo "== every other test that loads pump.wasm / pump-synth.wasm (grep: pump/pump.wasm, pump/synth/pump-synth.wasm, PSROOT)"
for t in $(cd "$T" && grep -lE "pump/pump\.wasm|pump-synth\.wasm|pump/synth/|PSROOT|ROOT \+ 'pump" tests/*.test.js | sort); do
  [ "$t" = tests/pump-synth.test.js ] && continue
  run "$t" "$t"
done
echo "== the whole suite as npm test runs it"
run "npm test (node --test tests/*.test.js)" tests/*.test.js
echo "== pump's own native-vs-wasm check (wasm/compare.cpp + wasm/compare.mjs; the native build of pump_wasm.cpp against pump.wasm)"
mkdir -p "$OUT/cmp/build"
# compare.cpp's own header comment omits the creative effects' units (wasm/fx/*.cpp): link them as wasm/build.sh does (shim before common)
cmpok=1; FXO=""
${CC:-cc} -O2 -w -DPFFFT_STATIC_DEFINE -c "$P/common/pump_pffft.c" -I"$P/common" -o "$OUT/cmp/pffft.o" 2>"$OUT/cmp/build.log" || cmpok=0
for p in tube snap gate echo spring verb grain vox tune crush swirl stutter chop twang master strip; do
  [ $cmpok = 1 ] && { ${CXX:-c++} -O2 -w -std=c++17 -DPUMP_SYNC_KERNELS=1 -I"$P/wasm/shim" -I"$P/plugins/$p" -I"$P/common" -c "$P/wasm/fx/$p.cpp" -o "$OUT/cmp/fx_$p.o" 2>>"$OUT/cmp/build.log" || cmpok=0; }
  FXO="$FXO $OUT/cmp/fx_$p.o"
done
if [ $cmpok = 1 ] && ${CXX:-c++} -O2 -w -std=c++17 -DPUMP_SYNC_KERNELS=1 -I"$P/common" "$P/wasm/compare.cpp" $FXO "$OUT/cmp/pffft.o" -o "$OUT/cmp/build/pump_compare" 2>>"$OUT/cmp/build.log"; then
  cp "$OUT/pump.wasm" "$OUT/cmp/pump.wasm"; cp "$OUT/pump-meta.js" "$OUT/cmp/pump-meta.js"
  if ( cd "$OUT/cmp" && node "$P/wasm/compare.mjs" . "$P/tests/data/mix.f32" 4 >"$OUT/cmp/compare.log" 2>&1 ); then
    # compare.cpp documents "type 0-8" (the nine mastering effects) and still selects presets by those nine's tables, so for the 16
    # creative effects (types 9+) its native side is not given the same values as the wasm side: reported as INFO, not gated (PINS.md).
    res=$(awk '/^(EQ|COMP|LIMIT|CALM|TONE|BAND|WIDE|HEAT|METER) /{ n=$0; sub(/.* lat */,"",n); split(n,x," "); m=$0; sub(/.*native */,"",m); sub(/\).*/,"",m); if (x[1]+0!=m+0) lat++; d=$0; sub(/.*diff\/out */,"",d); sub(/ dB.*/,"",d); if (c==0||d+0>w) w=d+0; c++ } END{printf "%d %d %.1f", c, lat+0, w}' "$OUT/cmp/compare.log")
    set -- $res
    if [ "$1" -ge 40 ] && [ "$2" = 0 ] && awk "BEGIN{exit !($3 <= -80)}"; then echo "PASS  wasm/compare (types 0-8, $1 effect/preset runs): native build equals pump.wasm, worst difference $3 dB (limit -80), every latency equal (log: $OUT/cmp/compare.log)"
    else echo "FAIL  wasm/compare (types 0-8, $1 runs): $2 latencies differ, worst difference $3 dB (log: $OUT/cmp/compare.log)"; fail=1; fi
    echo "INFO  wasm/compare for the 16 creative effects (types 9+) is not comparable: see PINS.md"
  else echo "FAIL  wasm/compare.mjs did not run (log: $OUT/cmp/compare.log)"; fail=1; fi
else echo "FAIL  wasm/compare.cpp did not build (log: $OUT/cmp/build.log)"; fail=1; fi
[ $fail = 0 ] && echo "parity u03: PASS" || echo "parity u03: FAIL"
exit $fail
