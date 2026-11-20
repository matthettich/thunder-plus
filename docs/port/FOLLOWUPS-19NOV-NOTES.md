IN THE MIDDLE OF: item 3c (nan_sweep trackIn/chainOut/hostIn/out). Items 1, 2, 3a, 3b done.

# FOLLOWUPS-19NOV NOTES: small follow-ups PR (branch n1/followups-19nov, into native)

## Checklist (updated in every commit)
- [x] 1. verify-chop F1: reversed-chop render test (`render_chop`, ramp pad, slice back to front). Mutation `dur - ofsSec - durSec` -> `dur - ofsSec` FAILS 6 lines (notes 0-2, both checks); restored.
- [x] 2. verify-chop F4: `Seq::buildParts` clamps a part instrument's slc 0..64, sync 0..3, beats 1..64 (rounded), grain 0..4 exactly as song.cpp `cInt` (via `jint` + `to_int_sat`); test `testPartClamp` in `seq_chop` (part vs main instrument, same JSON, 13 hostile shapes: notes equal). Without the fix 11 FAIL lines. Behaviour change only for out-of-range part values (slc>64 etc.); beats 0 is now 1 as the main path (was 4).
- [x] 3a. U31 F1: mutate-u31.sh 'Chain Out: the track is not held in range' text updated to the D9 form (`clamp(((double)i32(v) - 1), ...`); all 49 entries checked to match exactly once; this entry run alone against a Release build: killed, script ends 0. (Full 49-entry run not repeated: ~1 h of builds on 2 cores.)
- [x] 3b. U31 F2: `test_chains_threads` now has random gaps on both threads (audio: short sleeps / yields between blocks; control: service() on 3 of 4 iterations, random pause, bursts of up to 400 back-to-back service() calls), a new patch every 6..35 iterations, four pump modules instead of one; ctest runs it 30 s (was 3 s) when CMAKE_CXX_FLAGS has -fsanitize=thread. Measured (TSan, RelWithDebInfo, DSP+pump ON, the D4 race reverted to a single `spareRaw_` buffer: `const int b = 0`): old test 1 in 10 runs of 3 s (verifier); new test 2 in 10 runs of 3 s and 3 of 5 runs of 30 s: better, NOT reliable. Fixed code: 10 of 10 runs of 3 s and 3 of 3 of 30 s clean (0 TSan reports). Why not more: the race window is the time the control thread spends in `makeSpare` while the audio thread has a no-spare block (a few hundred ns); a deterministic detection needs a yield / hook point inside `PumpMod::makeSpare` (audio-path product code: out of scope, "do not change audio behaviour"). There is no TSan job in CI (native.yml has ASan+UBSan only): adding one is a workflow change that cannot be tried from here; it would run `ctest -R chains_threads` in a `-fsanitize=thread` build (about 3 min to build, 30 s to run).
- [ ] 3c. U31 F3 nan_sweep over trackIn / chainOut / hostIn / out
- [ ] 3d. U31 F4 mods.wasm hash check
- [ ] 3e. U31 F5 / F6 / F7: cheap fix or recorded reason
- [ ] 4. full Release ctest, sanitizer runs, PR
