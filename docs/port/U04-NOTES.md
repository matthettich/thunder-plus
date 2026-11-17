# U04 notes: the LXR drum-synth engine as a separable (GPL) module

Branch `n1/u04` (on `native`). WORK IN PROGRESS (started 17 Nov). Gate: `native/tools/parity.sh u04` (to come).

## State of the work
- Seam: `native/core/include/thunder/lxr_seam.h` + `native/core/src/lxr/lxr_seam.cpp` (core, no LXR code); the module: `native/lxr/` (option `THUNDER_WITH_LXR`, default ON).
- Reference: `native/tools/lxr-dump.mjs` (to come), gate `native/tests/test_lxr.cpp` (to come).
