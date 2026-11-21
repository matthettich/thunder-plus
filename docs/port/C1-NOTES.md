# C1-NOTES: the chains host, offline (unit C1)

Branch `n1/chains-c1` (from `native`, with PR #279's `n1/chains-c1g` merged: golden tool and goldens). Status is at the top; update it with every push.

## Status
- DONE: `native/core/src/chains/chost.{h,cpp}` (`ChainsHost`, `ChainsPlan`), `ChainsEngine::setBusMap` capacity, `SongRenderer::setChains` + dispatch cases, `thunder_render_opts.chains`, `thunder-render --chains`, CMake,
  `test_chains_song` (all golden cases exact through Mixer+ChainsHost, and through SongRenderer where a case has no events), `test_chains_host`.
- DONE too: `n1/chains-c1g` re-merged (22 cases, all exact, the Chrome chc rule in); the six mutations below.
- DONE too: full Release ctest 210 tests, 0 failed (1 skipped: pumpsynth_refused); ASan+UBSan on every test whose name has chains / render / mix / bus (all pass; chains_mem and chains_alloc skip themselves under a sanitizer); the PR.
- NEXT: nothing in C1's scope; see the open questions.
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

## test_render_bus_chain
Kept and passing. The stand-in CAN be replaced by the real host: with `opts.chains = 1` (a Track In -> Chain Out patch) the two-bus mix differs from the stand-in's by max |diff| 0 (bit-identical); stems and heard masks equal too. A block of the test now checks that; the stand-in stays for the older cases (live = true keeps hooks on muted buses, which the offline host does not do).

## Open questions / not done
- Pump / MODFX track effects (FX1), live (C2), taps / sends / sidechain (C3), the CHN switch ramp, Resample, patch edits during play: not in C1. Memory budget is still per patch (D7 (a), one budget per host, not done).
- A tempo event whose ceil(t/q)*q is at or after the render's length is sent by the page BEFORE quantum 0 (index.html:23229); the native renderer does not know the render length, so such an event is simply never applied. Only a trailing event; not reproduced.
- Heard set offline is `audibleMask()`: a CHN track with a Chain Out but no notes gets a node (the page's `keep` is tracks with notes plus bus closure). Only a chain that makes sound from nothing (Osc) would differ.
- `commit` returns void: what it replaced is in the plan object (`retiredPatches()` / `retiredNodes()`), freed by deleting the plan.
- `chc` for the same switch on two events in one quantum: the later wins (stable order); not pinned by a golden.
- Statics are primed in `plan()` (cinfo / chainsTable / cdef); the allocation audit over hook, commit (two swaps) and the event apply shows 0 allocator calls.
