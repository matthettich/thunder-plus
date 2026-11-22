# FX1 notes (builder; branch n1/fx1-song-fx)

## Status (update at every push)
- DONE (compiles, thunder-render built): song reader for pump slots (fx/fxsong.cpp, tables fx/pumpparams.inc by tools/pump-table.mjs), MODFX slot kind (kModT0 + index, table fx/modfxtable.inc by tools/modfx-track-table.mjs, run by FxHost::modRun), pump state texts (MASTER analysis), master pump latency in SongRenderer::latencyFrames, shared MODFX seed counter in FxBank::load, refusal counters, FXV for MODFX slots.
- Chrome reference with ALL slots: `node native/tools/song-ref.mjs <song> <dir> --fx-all` (new flag), `render-compare.sh <song> --fx-all`. Made here for ELECTRO and BETA-TESTING-ELECTRO (the latter from the beta branch's sounds/, not in `native`).
- FIRST MEASURE: ELECTRO -16.9 dB, BETA 4.3 dB (not yet within -80): being debugged.
## Resume
Build: `cmake -S native -B build -DTHUNDER_DEPS_DIR=/home/claude/deps` (deps via native/core/third_party/fetch.sh <dir>), `cmake --build build --target thunder-render -j2`.
