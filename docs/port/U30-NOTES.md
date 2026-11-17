# U30 notes: the ...waves engine (WORK IN PROGRESS)

Branch `n3/u30`. State at the first push: design done, nothing passes yet.

## Done
- `native/tools/waves-gen.mjs` -> `native/core/src/waves/wtable.inc`: scope, flags, ports and parameters of all 130 module types, generated from `waves/waves-worklet.js` (`--check` for drift).
- `native/core/src/waves/waves.h`: the API (Graph / VoicePool / WavesEngine / Host / WCtx / live entry points) as designed.
- `modfx` additive edits: `kMaxPorts` 12 -> 20 (the Mixer has 16 inputs); `Module::arm(Seeds&)` so that a module made ahead of its first block draws its seed at that block, as the JS's create() does (Filter, Sample & hold, BBD, Grain delay, Ring mod, Spring, Chaos override it).

## Design decisions made (to be confirmed by the lead)
1. States are made at patch preparation (off the audio thread) and armed (seed drawn) at the first block they run, in the JS order.
2. Live runner: latency of 127 samples (as U26) plus a cut of the graph block at every note / parameter event, so output is independent of the host block size and equals the offline sample-exact render.
3. No load shedding (fixed quality); the idle-skip of the page's live node is not carried over.

## Not done yet
wmods.cpp (waves-only modules, 33 tfx effect wrappers, Expression, Pitch delay/reverb, Scope), engine.cpp (graph, gather, render), live runner, reference tool, tests, fuzz, mutation checks, CI.

## Status (WIP 2)
- PASS: engine.cpp + wmods.cpp compile; both rows of tests/golden/waves-processor.json (quiet 9f094f39, hot 5d99c475) reproduce bit-exact in test_waves.
- NEXT: waves.test.js behaviours, feedback cases, per-module direct cases (Node dump tool), factory patches at 44.1/48/96k, fuzz, mutations, live runner (WavesLive), waves_pre_render / waves_live_render.
