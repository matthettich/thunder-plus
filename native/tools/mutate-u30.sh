#!/bin/sh
# U30: the unit's own mutation checks. Each one changes ONE thing in the engine or a module (the graph sort's tie-break and queue, the feedback guard's constants, the voice-steal choice, a module constant,
# the live runner's latency and cut rule, the hostile-value guard), rebuilds test_waves and requires the test to FAIL; then puts the file back. Usage: mutate-u30.sh <build dir> (a Release build of the tree)
# It edits the working tree and restores it from a copy it makes first, so run it on a clean checkout of the unit (or one whose changes are committed).
set -u
B=${1:?build directory}; ROOT=$(cd "$(dirname "$0")/../.." && pwd); S=$ROOT/native/core/src/waves; T=$ROOT/native/tests; ok=0; bad=0
G="$T/golden/waves_cases.json --effects $T/golden/waves_effects.json --render $T/golden/waves_render.json"
run() {   # name file old new
  name=$1; f=$S/$2; cp "$f" "$f.orig"
  OLD=$3 NEW=$4 python3 - "$f" <<'PY'
import os,sys
p=sys.argv[1]; s=open(p).read(); o=os.environ['OLD']; n=os.environ['NEW']
if s.count(o)!=1: print('MUTATION TEXT NOT UNIQUE (%d): %s' % (s.count(o), o)); sys.exit(3)
open(p,'w').write(s.replace(o,n))
PY
  rc=$?
  if [ $rc -ne 0 ]; then mv "$f.orig" "$f"; touch "$f"; echo "mutation '$name': could not be applied"; bad=$((bad+1)); return; fi
  if nice cmake --build "$B" -j2 --target test_waves >/dev/null 2>&1; then
    if "$B/tests/test_waves" $G >/dev/null 2>&1; then echo "mutation '$name': SURVIVED (the tests did not notice)"; bad=$((bad+1)); else echo "mutation '$name': killed"; ok=$((ok+1)); fi
  else echo "mutation '$name': does not build"; bad=$((bad+1)); fi
  mv "$f.orig" "$f"; touch "$f"
}
run "sort: the queue starts from the last module" engine.cpp 'for (size_t i = 0; i < n; i++) if (indeg[i] == 0) q.push_back((int)i);' 'for (size_t i = n; i-- > 0;) if (indeg[i] == 0) q.push_back((int)i);'
run "sort: leftovers appended in reverse" engine.cpp 'for (size_t i = 0; i < n; i++) if (!inOrder[i]) order.push_back(mods[i].get());' 'for (size_t i = n; i-- > 0;) if (!inOrder[i]) order.push_back(mods[i].get());'
run "sort: a feedback cable counts for the order" engine.cpp '      if (c.fb) continue;
      adj[' '      adj['
run "feedback guard: the ceiling constant" engine.cpp 'else x = 1 + js_tanh(x - 1);' 'else x = 1 + js_tanh(x - 1.5);'
run "feedback guard: the lower bound" engine.cpp 'if (!(x > -1 && x < 1)) {' 'if (!(x > -1 && x < 1.5)) {'
run "feedback guard: a non-number is not silenced" engine.cpp 'if (x != x || x == INFINITY || x == -INFINITY) x = 0;' 'if (x == INFINITY || x == -INFINITY) x = 0;'
run "voice steal: the newest idle row" engine.cpp 'rows[(size_t)j].gate == 0 && (best < 0 || rows[(size_t)j].age < rows[(size_t)best].age)) best = j;' 'rows[(size_t)j].gate == 0 && (best < 0 || rows[(size_t)j].age > rows[(size_t)best].age)) best = j;'
run "voice steal: the same pitch is not reused" engine.cpp 'if (rows[(size_t)j].gate == 0 && rows[(size_t)j].pitch == pitch) { i = j; break; }' 'if (false) { i = j; break; }'
run "voice steal: the newest overall" engine.cpp 'if (rows[(size_t)j].age < rows[(size_t)a].age) a = j; i = a;' 'if (rows[(size_t)j].age > rows[(size_t)a].age) a = j; i = a;'
run "module constant: the VCO level" wmods.cpp '(2 * t - 1 - blep(t, dt)) * .8' '(2 * t - 1 - blep(t, dt)) * .81'
run "module constant: the ADSR decay rate" wmods.cpp 'level_ += (s - level_) * dt / jmax(d, 1e-4) * 6;' 'level_ += (s - level_) * dt / jmax(d, 1e-4) * 7;'
run "engine: a mono source into a voice is not split" engine.cpp 'const double g = c.srcAudio ? 1 / N : 1; if (b)' 'const double g = 1; if (b)'
run "effect map: the saturation drive range" wmods.cpp 'o[0] = .2 + u01(p[0]) * .8; o[1] = u01(p[1]); o[2] = u01(p[2]); o[3] = u01(p[3]); }' 'o[0] = .2 + u01(p[0]) * .6; o[1] = u01(p[1]); o[2] = u01(p[2]); o[3] = u01(p[3]); }'
run "engine: a released voice never sleeps" engine.cpp 'if (row.quiet >= quietFor) row.offAt = voices_.now - hold;' 'if (row.quiet >= quietFor * 100) row.offAt = voices_.now - hold;'
run "live: the latency is one sample short" waves.h 'kLiveLatency = 127' 'kLiveLatency = 126'
run "live: a block is not cut at an event" live.cpp 'if (npend_ > 0 && pend_[0].at < end) end = pend_[0].at;
      if (end > hostEnd) break;' 'if (end > hostEnd) break;'
run "hostile guard: parameters are not held in range" engine.cpp 'm.bp[(size_t)k] = v != v ? d.def : v < d.mn ? d.mn : v > d.mx ? d.mx : v;' 'm.bp[(size_t)k] = v;'
nice cmake --build "$B" -j2 --target test_waves >/dev/null 2>&1
echo "mutations: $ok killed, $bad not"
[ $bad -eq 0 ]
