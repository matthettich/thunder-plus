# Verification of PR #245 (n1/chop): the native sequencer chops sliced and synced instruments

Verifier: independent (Claude Fable 5.2). Branch under test: n1/chop at 7b106dc4. STATUS: PRELIMINARY (first push); updated below as the checks finish.

## Verdict so far: pending (gate 1 and 2 clean; see the sections)

## 1. Golden path honesty
- `node tools/seq-golden.mjs --chop` regenerated with `--update`: `git status` clean afterwards, so the three chop goldens are byte-identical; the four proof lines (electro, -mid, -loop, -cmds through the real-function harness) pass.
- `make-core.mjs` run on the branch: `core/seq-core.js` unchanged (the generated core is in step with index.html).
- The functions are real: `loopSecs`, `findHits`, `sliceMemo`, `slicePoints`, `applyChop` are cut out of index.html by name at run time (brace matching), not copied. The tool holds no copy.
- What the tool stubs: the pad data and the kit layers are the tool's own (4 pads, bursts on a noise floor). `ensure` / `ensurePlay`: the tool replaces the stub line of core/seq-core.js, but core/seq-core.js:911-912 reassigns `ensurePlay = pad => coreBufs.get(pad)` and `ensure = ...` right after, so the tool's `__ensure` is dead code and the effective `ensure` is `coreBufs.get` (which, unlike the page's `ensure` at index.html:8856, does not test `kit.voices[i]`). No difference for the cases (every pad is in both), but the harness `ensure` the notes describe is not the one that runs. Not a hidden difference in what applyChop computes.
- Transient detection (`findHits`) is real JS on the tool's bursts; the C++ never ports it (hits handed in).
- Not covered by a golden: the page's `ensure` rendering a synth pad (the page's `data` is then a rendered buffer at padSR, so `loopSecs` = `data.length / sr`): the host must hand that number in; nothing in the C++ checks it.
- Parts (PERFORM): the C++ reads a part's instruments' slc / sync / beats / grain (seq.cpp buildParts); no golden, no differential case (see F4).

## 2. Differential (JS text vs C++), see the code blocks at the end
Result: 420 + 3 x 700 = 2520 random instrument/track combinations (7 per song, song / sect / phrase modes), 74 000+ notes (retrigger copies included), compared field by field with exact equality (t, track, pad, rate, ofs, ofsSec, durSec, cut, rev, st mode/bpm/beats/grain): 0 mismatches, 0 last-ulp differences, 0 skipped / no-hits counts. Covered: notes 0..127 (far below and above the root, negative n - ROOT), slc in {0,2,3,4,5,7,8,16,17,31,32,63,64, random 2..64}, smode on/off, sync 0..3, beats in {1,2,3,4,5,7,16,64} or unset, grain 0..4 or unset, ofs {0,1,64,128,255}, rev, tsp -24..24, commands SLC 0..255, STA 0..255, KIL, TPO, RET, PLY, PIT, FIN, ARP, PBN, PSL, OFF, TSP; 22 050 / 44 100 / 48 000 Hz pads with random layers.
Not reachable from a loaded song (the loader clamps like the page's loader at index.html:20377): slc > 64, beats 0, non-integer beats.
