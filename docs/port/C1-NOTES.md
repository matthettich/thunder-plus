# C1-NOTES: the chains host, offline (unit C1)

Branch `n1/chains-c1` (from `native`, with PR #279's `n1/chains-c1g` merged: golden tool and goldens). Status is at the top; update it with every push.

## Status
- DONE: `native/core/src/chains/chost.{h,cpp}` (`ChainsHost`, `ChainsPlan`), `ChainsEngine::setBusMap` capacity, `SongRenderer::setChains` + dispatch cases, `thunder_render_opts.chains`, `thunder-render --chains`, CMake,
  `test_chains_song` (all golden cases exact through Mixer+ChainsHost, and through SongRenderer where a case has no events), `test_chains_host`.
- DONE too: `n1/chains-c1g` re-merged (22 cases, all exact, the Chrome chc rule in); the six mutations below.
- NEXT: full Release ctest (running), ASan+UBSan on chains / render / mix / bus tests, the PR.
- OPEN QUESTIONS: see the end.

## Design as built
- One `ChainsNode` per track that is heard, has CHN on and a Chain Out (`plan()` control side: nodes, prepared patches, bus plan, first-call statics primed). `commit(plan, mixer)` audio side: nodes and maps, then `Mixer::setChain` hooks, `Mixer::setPlan` LAST.
  What it replaces (old patches after a swap, removed nodes) stays in the plan; the control thread frees it by deleting the plan. (The brief said "returns what it replaced": it is returned in the plan object, `retiredPatches()` / `retiredNodes()`.)
- `hook` (the `MixInsertFn`): applies due events (once per quantum), feeds -> `NodeIn.feeds` in bus-map order (a mismatch of track numbers is counted, cannot happen with a plan from `bus_plan`), `ChainsNode::process` in place. A failing engine block = silence (the engine clears its buses), counted.
- Events (`SongRenderer::dispatch` -> `event()`): 256 fixed, time-sorted, drops counted; applied at the start of the first quantum a hook runs in whose index is >= the event's. TWO rules: tempo ceil(t / q), q = 128.0 / sr; chc ceil(ceil(t * sr) / 128) (lead's ruling, Chrome measured).
- Transport: the song's (tempo, lpb, ticks, playing) applied to a fresh node after its patch loaded (G5/G6) and after every `swapPatch`; tempo events move the host's copy too.
- A refused patch (one document for every node) makes the whole plan a no-op: the playing patches, bus maps and bus plan stay. Found by the test: the hostile document's own bus plan had emptied the bus map of the playing node.
- `setBusMap` fix: `busMap_.reserve(kMaxFeeds)` in the constructor AND the loop stops at kMaxFeeds (the node reads at most 32; with the reserve alone a call with more than 32 would allocate). Reserve + cap, not a fixed array variant: the one-line change in the constructor keeps `busMap()` a vector for every existing reader.
- `SongRenderer::setChains(bool)`: after `setStem`, before the first block; `setStem` is refused once chains are on. Hooks only on `heardMask()` tracks.

## Mutations (each made, seen to fail, undone)
| mutation (in chost.cpp) | killed by |
|---|---|
| feed pairs in reverse order in `hook` | 16 of 22 golden cases (hash), `hook == direct ChainsNode` |
| `chc` applied one quantum early (`chcQuantum` - 1, and separately `applyDue(qi + 1)`) | the plan check against `expect.eventQuanta`; with `applyDue(qi + 1)` the hashes of the f cases and the negative controls |
| transport not re-applied after a swap | `test_chains_host` (after the swap equals a direct node with the song's transport, the transport is the song's); no golden case swaps a patch |
| an un-heard track hooked in a stem (`heard` ignored in `plan`) | h/stem-plain-track, h/stem-source-only (plan and hash), `test_chains_host` section 4 |
| `ceil` in float (tempo rule; chc rule separately) | g/tempo-change; f/byp-fuzz-quantum-start; the rule checks of `test_chains_host` |
| the tempo rule used for `chc` | f/byp-chrome-rule-k5 |
