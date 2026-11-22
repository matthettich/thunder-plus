# FX1 notes (builder; branch n1/fx1-song-fx, PR #286)

## Status
DONE (code + tests): pump slots and MODFX slots as track effects, pump mastering on the master, pump state texts (MASTER analysis), master pump latency in SongRenderer::latencyFrames (PDC), shared MODFX seed counter in FxBank::load (master first, then tracks 0..31), refusal counters (FxBank::refused, never silent), FXV for MODFX slots.
Reader: native/core/src/fx/fxsong.cpp; tables fx/pumpparams.inc (tools/pump-table.mjs) and fx/modfxtable.inc (tools/modfx-track-table.mjs), both with --check. Goldens: tests/golden/fxsong_cases.json (tools/fxsong-dump.mjs).
Tests: tests/test_fxsong.cpp (reader bit-exact vs the page code; MODFX slot via FxHost == module driven directly, 82 cases; pump slot via FxHost == hand-made entry, 50 cases; refusals; state texts; master latency; ELECTRO 25 of 25 hosted; FXV).

## Done-test: NOT met (-80 dB), measured
Chrome all-slot reference made with existing tools plus new flags: `song-ref.mjs <song> <dir> --fx-all` (also FXKEEP=type list, PDCOFF), `render-compare.sh <song> --fx-all`; thunder-render needs `--pdc` (page default is LATENCY COMP auto). Chrome itself is only -73 dB repeatable here (tfx-only baseline).
- ELECTRO all slots: -16.9 dB. Master Flex/Retrig alone: -17.5 dB (pre-existing; earlier --fx runs dropped those slots on both sides so it was never compared). Without Flex/Retrig: -69.4 dB.
- BETA all slots: -5.9 dB. Flex + pump glitches about -9.5 dB, cause unproven (suspect sub-sample PDC clock delay). Master pump chain alone -69 dB.
- Per type (single-type FXKEEP): pump types and most MODFX -80..-91 dB; gdelay -91.5; compressor -79.9; dub+delay -78.9; lverb/room -69.9; autopan -73.6 (suspect tempo-estimate jitter in the synced LFO); bbd -51.4 (BETA); BETA MODFX group -55.3 overall.
## Local test state (this head, 1f35ca3 plus the uncommitted-then-pushed fix below)
Local ctest (226): all pass except render_api_oom (ELECTRO, operator-new injection): a failed create leaks. valgrind on a normal ELECTRO render with all slots: no definite leaks. Cause: bad_alloc thrown inside the pinned pump plugin's own constructors (pumpfx_new_verb / pump::verb::Engine::Engine leak their partial members; code under pump/, not ours); also small leaks at failures after pumps were made (9 to 36 KB, source not found). FX1OFF experiment: with pump slots refused the test has no pump leaks; mod-only gives 4 small ones. Fixed here (not re-run): PumpSlot::make and make_pump are now exception safe (unique_ptr, delete on throw). Decision for the lead: either tolerate the plugin-ctor leaks in the test (count a pump_create_oom flag) or make the plugin ctors safe. CI will say.
## Audio-path functions added (native/core/src)
fx/fxsong.cpp:143 FxHost::modRun (MODFX block); fx/fxsong.cpp:111 fx_make_mod and :135 fx_mod_set (control side); fx/fxhost.cpp:240 psetMod, :246 applyPset(n), :279 slotRun dispatch to modRun, plan()/commit()/applyEntry changes (mod branch); fx/fxpump.cpp pumpClock (bpm_, startN_), pump::state_set :72, PumpSlot::giveState; fx/fxbank.cpp:44 FxBank::event (FXV for MODFX), :70 masterPumpLatency; render/render.h:123 SongRenderer::latencyFrames (+ master pump latency).
## Branch state
#281 (chains C1) merged into native after this branch was cut; NOT rebased. Expect conflicts in render.cpp / both CMakeLists.
## Not done
Pump FXV (refused, counted), sidechain (sck>0) and NOTES FROM (refused, counted; C3 not wired), refusal counters not in the C ABI, Convolve song IRs, BETA golden not committed (song lives on beta only), Chrome draws seeds only for kept tracks while native draws for all tracks with slots. Next step: fix or tolerate the oom test, rebase on native, then master Flex/Retrig vs Chrome, BETA Flex+PDC, bbd, autopan.
## Resume
Build: `cmake -S native -B build -DTHUNDER_DEPS_DIR=/home/claude/deps` (deps via native/core/third_party/fetch.sh <dir>), `cmake --build build --target thunder-render -j2`.
