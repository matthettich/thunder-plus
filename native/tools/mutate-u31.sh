#!/bin/sh
# U31: the unit's own mutation checks. Each one changes ONE thing in the …chains engine, its modules or the live runner (the BYP rule and its crossfade, the Source key map, the sends' one block of delay, the
# written-bus reset, the latency and the cut rule of the live runner, the pump's bad-output limit, constants of the sidechain compressor, the vocoder, Morph, the Osc, Resample's table, the bypass numbers,
# the Chain Out track clamp, the stereo fallback of the live input), rebuilds test_chains and test_chains_live and requires one of them to FAIL; then puts the file back.
# A mutation that makes the test hang (an event never applied) counts as killed (timeout). Usage: mutate-u31.sh <build dir> (a Release build of the tree). It edits the working tree and restores it from a copy it makes first: run it on a tree whose changes are committed.
set -u
B=${1:?build directory}; ROOT=$(cd "$(dirname "$0")/../.." && pwd); S=$ROOT/native/core/src; T=$ROOT/native/tests; ok=0; bad=0
G="$T/golden/chains_cases.json --effects $T/golden/chains_effects.json"
run() {   # name file old new   (file relative to native/core/src)
  name=$1; f=$S/$2; cp "$f" "$f.orig"
  OLD=$3 NEW=$4 python3 - "$f" <<'PY'
import os,sys
p=sys.argv[1]; s=open(p).read(); o=os.environ['OLD']; n=os.environ['NEW']
if s.count(o)!=1: print('MUTATION TEXT NOT UNIQUE (%d): %s' % (s.count(o), o)); sys.exit(3)
open(p,'w').write(s.replace(o,n))
PY
  rc=$?
  if [ $rc -ne 0 ]; then mv "$f.orig" "$f"; touch "$f"; echo "mutation '$name': could not be applied"; bad=$((bad+1)); return; fi
  if nice cmake --build "$B" -j1 --target test_chains test_chains_live >/dev/null 2>&1; then
    if timeout 600 "$B/tests/test_chains" $G >/dev/null 2>&1 && timeout 300 "$B/tests/test_chains_live" >/dev/null 2>&1; then echo "mutation '$name': SURVIVED (the tests did not notice)"; bad=$((bad+1)); else echo "mutation '$name': killed"; ok=$((ok+1)); fi
  else echo "mutation '$name': does not build"; bad=$((bad+1)); fi
  mv "$f.orig" "$f"; touch "$f"
}
run "bypass: a flipped switch does not flip" chains/cengine.cpp 'sw >= 1.5 ? !own : sw >= .5' 'sw >= 1.5 ? own : sw >= .5'
run "bypass: the crossfade starts a sample early" chains/cengine.cpp 'const double g = want ? (double)(i + 1) / kBlock : 1 - (double)(i + 1) / kBlock;' 'const double g = want ? (double)i / kBlock : 1 - (double)i / kBlock;'
run "bypass: the switch number is not used" chains/cengine.cpp 'const double sw = n ? bsw_[n] : -1;' 'const double sw = -1;'
run "bypass numbers: the lowest free one skips 15" chains/cengine.cpp 'while (k <= 15 && used[k]) k++; if (k > 15) break;' 'while (k <= 14 && used[k]) k++; if (k > 14) break;'
run "source key: a track is off by one" chains/cengine.cpp 'const int k = iv - kSends - 1; const int c = e->hasTapMap_' 'const int k = iv - kSends; const int c = e->hasTapMap_'
run "source key: a send reads the wrong one" chains/cengine.cpp 'return e->sendPrev_ + (size_t)n * kBlock;' 'return e->sendPrev_ + (size_t)(n ^ 1) * kBlock;'
run "sends: the standalone block of delay is lost" chains/cengine.cpp 'void ChainsEngine::endStandaloneBlock() { std::memcpy(sendPrev_, sendOut_, sizeof sendPrev_); standalone_ = true; }' 'void ChainsEngine::endStandaloneBlock() { standalone_ = true; }'
run "buses: a written bus is not marked as free again" chains/cengine.cpp 'sizeof(float) * kBlock); bw_[k] = 0; }' 'sizeof(float) * kBlock); }'
run "Chain Out: the track is not held in range" chains/cengine.cpp 'return (int)clamp((double)(i32(v) - 1), 0, kChannels - 1); }' 'return (int)(i32(v) - 1); }'
run "mix-down: the soft clip is left out" chains/cengine.cpp 'L[i] = (float)modfx::soft((double)L[i]); R[i] = (float)modfx::soft((double)R[i]);' 'L[i] = L[i]; R[i] = R[i];'
run "live: the latency is one sample short" chains/chains.h 'constexpr int kChainsLatency = 127' 'constexpr int kChainsLatency = 126'
run "live: a block is not cut at an event" chains/clive.cpp 'if (npend_ > 0 && pend_[0].at < end) end = pend_[0].at;' ''
run "live: an event lands a sample late" chains/clive.cpp 'while (npend_ > 0 && pend_[0].at <= pos_)' 'while (npend_ > 0 && pend_[0].at < pos_)'
run "live: the right input falls back to nothing" chains/clive.cpp 'put(k++, in.inR ? in.inR : in.inL);' 'put(k++, in.inR);'
run "live: the right output is the left" chains/clive.cpp 'out.R[done + j] = get(1);' 'out.R[done + j] = get(0);'
run "pump: a bad output is not noticed" chains/cmods2.cpp 'if (!(a > -8 && a < 8 && b > -8 && b < 8)) { bad = true; break; }' 'if (!(a > -80 && a < 80 && b > -80 && b < 80)) { bad = true; break; }'
run "sidechain compressor: the ratio floor" chains/cmods.cpp 'rat = jmax(1, io.p[1]);' 'rat = jmax(2, io.p[1]);'
run "sidechain compressor: the gain slope" chains/cmods.cpp 'js_pow(10, -over * (1 - 1 / rat) / 20)' 'js_pow(10, -over * (1 - 1 / rat) / 21)'
run "vocoder: the talk-mode saturation" chains/cmods.cpp 'js_tanh(y * 1.6) * .625' 'js_tanh(y * 1.6) * .63'
run "morph: the wet gain" chains/cmods.cpp 'out[i] = (float)(dry * (1 - mix) + wet * g * mix);' 'out[i] = (float)(dry * (1 - mix) + wet * g * mix * 1.01);'
run "osc: the level" chains/cmods.cpp 'O[i] = (float)(y * lv); ph += dt;' 'O[i] = (float)(y * lv * 1.001); ph += dt;'
run "resample: the slice table is cut short" chains/cmods2.cpp 'kRsSlices[(int)clamp((double)i32(io.p[3]), 0, 4)]' 'kRsSlices[(int)clamp((double)i32(io.p[3]), 0, 3)]'
nice cmake --build "$B" -j1 --target test_chains test_chains_live >/dev/null 2>&1
echo "mutations: $ok killed, $bad not"
[ $bad -eq 0 ]
