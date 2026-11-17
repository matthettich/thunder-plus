# U27: the live engines Acid and Machine (rendered through renderLiveCore) and the Blip drum (renderBlip)

Branch `n3/u27` (from `native`). Gate command: `native/tools/parity.sh u27`. Work in progress; this file is kept current.

## State (first push)
- Ported line by line: `native/core/src/engine/xeng_live.{h,cpp}` (AcidCore, MachCore, renderLiveCore) and `xeng_blip.cpp` (BlipVoice + renderBlip, index.html's copy, NOT modfx's chains copy which differs).
- `test_engine_xeng3` (local, Release GCC): 185 of 185 factory voices of beta's `tests/golden/engines.json` (acid 96, mach 34, blip 55; counted by beta's own listing tool `listVoices`, the fixture tool fails if its set differs from a second count) equal beta's hash; 442 of 442 direct cases and core scripts equal the committed Linux hash and the local Node.
- Not yet: fuzz test, allocation-guard case in rt_harness, mutation checks, ASan/UBSan run, PR.

## What is where
- Reference/fixture tool `native/tools/engine-xeng3-dump.mjs` (`--fixture <beta>`, `--golden`, `--check`, plain = reference for ctest). Goldens `native/tests/golden/engine_xeng3_voices.json`, `engine_xeng3_voices_golden.inc` (beta's rows, copied), `engine_xeng3_golden.inc` (442 cases, Node 22 x86-64 Linux), `xeng3_ranges.json`.
- Test `native/tests/test_engine_xeng3.cpp`; ctest `engine_xeng3_ref`, `engine_xeng3`, `engine_xeng3_golden_current`.
