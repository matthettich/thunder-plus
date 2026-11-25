# Independent verification of PR #314 (n1/pfx-pad-effects, f8970236): PARTIAL REPORT (in progress)

Verdict: not yet decided (partial, written at ~35 min).

## Checked so far
- Setup: head f897023672f2e9597e1b68734dfd3cab7b8e261f, Release build under native/build-v, full ctest running (240 tests).
- Check 1 (golden tool is the page's code): `native/tools/padfx-dump.mjs` slices the real `/* ===ENGINE START=== */ .. END` block out of index.html and runs it in vm with the page's own tfx.wasm, thunder-dsp.wasm and the shared MODFX modules of chains-worklet.js. Not a re-implementation. Drift detection works: one flipped hex digit in padfx_golden.inc -> `--check` exits 1; changing the page's rvMix blend by 0.1% -> exits 1; changing the EQ bass offset by 1e-4 -> exits 1.
- Check 2 (mapping, read line by line against index.html FX table): eq, comp, reverb(blend idx), delay, beads, tgate, fshift, cab, xmod, comb, space, room, ladder, density, tape, crush, chorus (1.6 vs tfx 1.4 handled), rings, clouds, MODFX modFxRun (latency pull-back 2048 = SPX_N, tempo, non-finite -> default, fxKey type_/on_): all match. No mapping error found yet.
- Check 3 (callers): on origin/n1/n2-live-shell the only production caller of eng_render_voice / xeng_render_voice is android pad_filler.cpp, which leaves `EngRenderOpts::libFx` at its default (true): correct (it gets the effects). Nothing on origin/native calls them outside tests. Tests not touched by the PR that use EngRenderOpts (u25/u26/xeng fuzz, test_envlines, rt_harness on n2) now run with libFx true.
