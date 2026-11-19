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
  if nice cmake --build "$B" -j1 --target test_chains test_chains_live test_chains_mem test_chains_alloc test_chains_bypass >/dev/null 2>&1; then
    if timeout 600 "$B/tests/test_chains" $G >/dev/null 2>&1 && timeout 240 "$B/tests/test_chains_live" >/dev/null 2>&1 && timeout 240 "$B/tests/test_chains_mem" >/dev/null 2>&1 && timeout 240 "$B/tests/test_chains_alloc" >/dev/null 2>&1 && timeout 300 "$B/tests/test_chains_bypass" >/dev/null 2>&1; then echo "mutation '$name': SURVIVED (the tests did not notice)"; bad=$((bad+1)); else echo "mutation '$name': killed"; ok=$((ok+1)); fi
  else echo "mutation '$name': does not build"; bad=$((bad+1)); fi
  mv "$f.orig" "$f"; touch "$f"
}
run "bypass: a flipped switch does not flip" chains/cengine.cpp 'sw >= 1.5 ? !own : sw >= .5' 'sw >= 1.5 ? own : sw >= .5'
run "bypass: the crossfade starts a sample early" chains/cengine.cpp 'const double g = want ? ph : 1 - ph;' 'const double g = want ? ph - 1.0 / kBlock : 1 - ph + 1.0 / kBlock;'
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
run "vocoder spectral mode: the wet gain" chains/cmods.cpp 'out[i] = (float)(dry * (1 - mix) + wet * g * mix);' 'out[i] = (float)(dry * (1 - mix) + wet * g * mix * 1.01);'
run "osc: the level" chains/cmods.cpp 'O[i] = (float)(y * lv); ph += dt;' 'O[i] = (float)(y * lv * 1.001); ph += dt;'
run "resample: the slice table is cut short" chains/cmods2.cpp 'kRsSlices[(int)clamp((double)i32(io.p[3]), 0, 4)]' 'kRsSlices[(int)clamp((double)i32(io.p[3]), 0, 3)]'
# the verifier's mutations of PR #227 (M1..M13; M10 is the "right output is the left" line above, kept here as the verifier wrote it), each of which an ordinary test now kills
run "V M1 drain: same-sample events no longer stable" chains/clive.cpp 'while (ins > 0 && pend_[ins - 1].at > e.at) ins--;' 'while (ins > 0 && pend_[ins - 1].at >= e.at) ins--;'
run "V M2 postParam: id length check off by one" chains/clive.cpp 'std::strlen(id) >= sizeof e.id' 'std::strlen(id) > sizeof e.id'
run "V M3 live feed: right side no longer falls back to left" chains/clive.cpp 'put(k++, b ? b : a)' 'put(k++, b)'
run "V M4 setSidechain: R not copied from L when R absent" chains/cengine.cpp 'else if (L) std::memcpy(scR_, L, sizeof(float) * (size_t)n);' 'else if (false) std::memcpy(scR_, L, sizeof(float) * (size_t)n);'
run "V M5 pump: no new spare requested after a take-over" chains/cmods2.cpp 'wantSpare_.store(true); } } if (h_ < 0)' '} } if (h_ < 0)'
run "V M6 hostile tempo clamp raised to 1e6" chains/cengine.cpp 'transport_.tempo > 1e5 ? 1e5' 'transport_.tempo > 1e5 ? 1e6'
run "V M7 Bypass event: switch 0 allowed" chains/clive.cpp 'if (n >= 1 && n <= kBypSwitches) byp_[n - 1] = e.b;' 'if (n >= 0 && n <= kBypSwitches) byp_[n - 1] = e.b;'
run "V M8 drain: late events no longer pulled up to pos_" chains/clive.cpp 'if (e.at < pos_) { e.at = pos_; bump(ctr_.lateEvents); }' 'if (false) { e.at = pos_; bump(ctr_.lateEvents); }'
run "V M9 Track In: bound off by one" chains/cmods.cpp 'if (x.inL && x.inR && k < x.inChans)' 'if (x.inL && x.inR && k <= x.inChans)'
run "V M10 live: right output = left" chains/clive.cpp 'if (out.R && out.R != out.L) out.R[done + j] = get(1);' 'if (out.R && out.R != out.L) out.R[done + j] = get(0);'
run "V M11 bypass: a module settled in bypass is no longer fed silence" chains/cengine.cpp 'if (want && was) { std::memset(s.in[m.pIn], 0,' 'if (false) { std::memset(s.in[m.pIn], 0,'
run "V M12 Chain Out: level capped at 1.5" chains/cmods.cpp 'L[i] = (float)((double)L[i] + (double)io.in[0][i] * g);' 'L[i] = (float)((double)L[i] + (double)io.in[0][i] * (g > 1.5 ? 1.5 : g));'
run "V M13 service() no longer frees the retired patches" chains/clive.cpp '  for (auto& p : retired_) chains_free_patch(p.exchange(nullptr));
  node_.engine().serviceSparesOf' '  node_.engine().serviceSparesOf'
# the defects fixed after the verifier (queue rules, memory budget)
run "queue: no limit on patches in flight" chains/clive.cpp 'if (k < 0 || inflight >= kChainsSlots - 2) {' 'if (k < 0) {'
run "queue: a dropped Patch event keeps its slot" chains/clive.cpp 'waves::PatchImpl* p = slot_[k].exchange(nullptr); if (!p) return;
  for (int i = 0; i < kChainsSlots; i++) if (!retired_[i].load()) { retired_[i].store(p); return; }' 'waves::PatchImpl* p = slot_[k].load(); if (!p) return;
  for (int i = 0; i < kChainsSlots; i++) if (!retired_[i].load()) { return; }'
run "queue: no place is kept for the events that must not be lost" chains/clive.cpp '(e.kind == ChainsEvent::Param ? kChainsReserve : 0)' '0'
run "queue: parameter events do not coalesce" chains/clive.cpp 'if (same < 0) { bump(ctr_.droppedEvents); continue; }' 'if (true) { bump(ctr_.droppedEvents); continue; }'
run "queue: a full array drops the Playing event instead of making room" chains/clive.cpp 'const int old = victim(e);' 'const int old = -1;'
run "budget: the patch is not counted against it" chains/cengine.cpp 'if (need > (double)memBudget_) {' 'if (need > 1e300) {'
# lead review A..K
run "review F: an incoming Playing does not replace an earlier pending Playing" chains/clive.cpp 'for (int j = 0; j < npend_; j++) if (same(pend_[j], e)) return j;' 'for (int j = 0; j < 0; j++) if (same(pend_[j], e)) return j;'
run "review E: a removed Param is counted as coalesced" chains/clive.cpp 'if (pend_[old].kind == ChainsEvent::Param) bump(ctr_.droppedParams); else bump(ctr_.coalescedEvents);' 'bump(ctr_.coalescedEvents);'
run "review D: one place less spare in the mailbox" chains/clive.cpp 'inflight >= kChainsSlots - 2' 'inflight >= kChainsSlots'
run "review H: the bypass fade does not carry across cuts" chains/clive.cpp 'node_.engine().setCarryFade(true);' 'node_.engine().setCarryFade(false);'
run "review H: the carried fade restarts instead of continuing" chains/cengine.cpp 'const int f0 = carry ? m.fade : 0;' 'const int f0 = 0;'
run "review J: a dropped take is not counted" chains/cengine.cpp 'e->stats_.droppedTakes++;' ''
# verification round 2 (D5): the two test gaps the verifier found
run "round2 D5a: a NaN becomes 0 in the raw parameter path" waves/graph.h 'm.bp[(size_t)k] = m.base[(size_t)k]; return; }   // U31' 'm.bp[(size_t)k] = m.base[(size_t)k] == m.base[(size_t)k] ? m.base[(size_t)k] : 0; return; }   // U31'
run "round2 D5b: the carried fade follows the switches instead of its target" chains/cengine.cpp 'const bool want = carry ? m.fadeTo : sw < -.5' 'const bool want = sw < -.5'

nice cmake --build "$B" -j1 --target test_chains test_chains_live test_chains_mem test_chains_alloc test_chains_bypass >/dev/null 2>&1
echo "mutations: $ok killed, $bad not"
[ $bad -eq 0 ]
