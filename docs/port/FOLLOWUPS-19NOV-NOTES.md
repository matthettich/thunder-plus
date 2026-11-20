IN THE MIDDLE OF: item 3b (TSan race test sensitivity). Items 1, 2, 3a done.

# FOLLOWUPS-19NOV NOTES: small follow-ups PR (branch n1/followups-19nov, into native)

## Checklist (updated in every commit)
- [x] 1. verify-chop F1: reversed-chop render test (`render_chop`, ramp pad, slice back to front). Mutation `dur - ofsSec - durSec` -> `dur - ofsSec` FAILS 6 lines (notes 0-2, both checks); restored.
- [x] 2. verify-chop F4: `Seq::buildParts` clamps a part instrument's slc 0..64, sync 0..3, beats 1..64 (rounded), grain 0..4 exactly as song.cpp `cInt` (via `jint` + `to_int_sat`); test `testPartClamp` in `seq_chop` (part vs main instrument, same JSON, 13 hostile shapes: notes equal). Without the fix 11 FAIL lines. Behaviour change only for out-of-range part values (slc>64 etc.); beats 0 is now 1 as the main path (was 4).
- [x] 3a. U31 F1: mutate-u31.sh 'Chain Out: the track is not held in range' text updated to the D9 form (`clamp(((double)i32(v) - 1), ...`); all 49 entries checked to match exactly once; this entry run alone against a Release build: killed, script ends 0. (Full 49-entry run not repeated: ~1 h of builds on 2 cores.)
- [ ] 3b. U31 F2 TSan race test sensitivity
- [ ] 3c. U31 F3 nan_sweep over trackIn / chainOut / hostIn / out
- [ ] 3d. U31 F4 mods.wasm hash check
- [ ] 3e. U31 F5 / F6 / F7: cheap fix or recorded reason
- [ ] 4. full Release ctest, sanitizer runs, PR
