# U26 notes (work in progress)

State at first push: BassCore and VoxCore ported (`bass_core.*`, `vox_core.*`, generated `vox_tables.inc`), wired into `xeng_voice.cpp`; live shell header `live_shell.h` (provisional) compiles.
Not yet done: tests, goldens (`engine_u26_golden.inc`), fuzz, mutations, page-live protocol write-up, PR.

## Choices for the lead to confirm
1. Exact mode delays output by BLK-1 samples (31 bass, 15 vox/acid/mach) so output is independent of host block size; Immediate mode has no delay but depends on host block cuts.
