# U25 notes: Plaits / Elements layer types (WORK IN PROGRESS, 16-17 Nov)

Branch `n3/u25` (on `native`). Gate command (once finished): `native/tools/parity.sh u25`.

## State at the first push
- Done, builds: strict library `thunder_dsp_pe` (`native/core/dsp/pe_host.{h,cc}` + CMake group; upstream plaits + elements files, `-O3 -fno-fast-math -ffp-contract=off`), engine layer types
  `plaits` / `elements` (`core/src/engine/layer_pe.{h,cpp}`, `layer_pe_params.h`, edits in `engine.{h,cpp}`, `engine_json.cpp`), Node reference `native/tools/engine-u25-dump.mjs`
  (real ENGINE block + real `waves/dsp/thunder-dsp.wasm`, a fresh wasm instance per voice), test `native/tests/test_engine_u25.cpp`.
- First measurement (317 cases): about 73 within -100 dB (mostly Elements, float output), the rest between -100 and -60 dB, 12 worse than -60, 3 NaN, 1 crash (`edge_el_under`).
- Finding so far: Plaits' output is int16 (`Voice::Render` quantises), so any one-LSB flip between the fast-math wasm and the strict build is about -80 dB of a 0.4 peak: the -100 dB line cannot hold for a case with even one flipped sample.
- Next: ASan / UBSan run, decide the by-name list, committed native hashes, rt_harness case, parity script, CI.
