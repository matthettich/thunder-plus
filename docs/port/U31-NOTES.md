# U31 notes: the ...chains engine in the core (work in progress)

Branch `n3/u31`. Source of truth: `chains/chains-worklet.js` outside the shared block (ChainsEngine, Graph, ChainsProcessor, ThunderChains, the chains-only modules).

## Plan
1. Reuse U30's graph: `PatchImpl` moved from `waves/engine.cpp` to `waves/graph.h` and takes a `ModTable` (where it called `winfo` / `wdef`). U30's gate (`ctest waves`) passes unchanged after the move.
2. Tables: `native/tools/chains-gen.mjs` writes `core/src/chains/ctable.inc` (145 module types, from the JS; `--check` for drift).
3. Chains-only modules (cmods.cpp): trackIn, chainOut, scin, scout, sccomp, vcf, vca, mixer, smixer, osc, wnoise, repeat, resample, vocoder, morph, 13 pump modules (spare pump instances, decision (e)), scope show. Everything else is reused: MODFX modules, U30's 33 effect wrappers and lfo / adsr / random / atten / expr (their JS is identical to waves').
4. Engine (cengine.cpp): focus run list, bypass with crossfade, buses, sends, sources (tap / key), busMap, ChainsProcessor mix-down, ThunderChains node (chunks of 128).
5. Gates: all 429 rows of `chains-modules.json` (one engine, sorted types, the JS test's order and seed run), `chains-processor.json`, `chains-thunder-node.json`; library modules by the -100 dB rule against Node; live runner (127-sample latency, sample-exact events); rt harness; fuzz; 16 mutations.

## Done so far
- `waves/graph.h` refactor, `kMaxPorts` 20 -> 32 (the Stereo mixer has 32 inputs), `js_log10` (V8 12.4 ieee754::log10, needed by Sidechain comp), `chains-gen.mjs` + `ctable.inc`, `chains/chains.h` (the API).

## Not done yet
Everything after step 2 (see the plan).
