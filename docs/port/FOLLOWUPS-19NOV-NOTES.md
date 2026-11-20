IN THE MIDDLE OF: item 2 (verify-chop F4: PERFORM-part path clamps slc / beats like the main path). Item 1 done.

# FOLLOWUPS-19NOV NOTES: small follow-ups PR (branch n1/followups-19nov, into native)

## Checklist (updated in every commit)
- [x] 1. verify-chop F1: reversed-chop render test (`render_chop`, ramp pad, slice back to front). Mutation `dur - ofsSec - durSec` -> `dur - ofsSec` FAILS 6 lines (notes 0-2, both checks); restored.
- [ ] 2. verify-chop F4: part (PERFORM) instruments clamp slc / beats / sync / grain as `song.cpp` does; test
- [ ] 3a. U31 F1 mutate-u31.sh stale entry
- [ ] 3b. U31 F2 TSan race test sensitivity
- [ ] 3c. U31 F3 nan_sweep over trackIn / chainOut / hostIn / out
- [ ] 3d. U31 F4 mods.wasm hash check
- [ ] 3e. U31 F5 / F6 / F7: cheap fix or recorded reason
- [ ] 4. full Release ctest, sanitizer runs, PR
