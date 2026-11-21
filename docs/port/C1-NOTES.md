# C1-NOTES: the chains host, offline (unit C1)

Branch `n1/chains-c1` (from `native`, with `n1/chains-c1g-2` merged: golden tool and 23 goldens, case l and `noteTracks`). PR #281. Status is at the top; update it with every push.

## Status (round 2, after the verifier's "MERGE AFTER FIXES" on e2dcb4c)
- DONE: `native/core/src/chains/chost.{h,cpp}` (`ChainsHost`, `ChainsPlan`), `ChainsEngine::setBusMap` capacity (and the `busmap` op capped the same), `SongRenderer::setChains` (returns a status) + dispatch + `beginBlock`,
  `thunder_render_opts.chains`, the chains counters appended to `thunder_render_stats`, `thunder-render --chains`, CMake, `test_chains_song`, `test_chains_host`.
- Golden: 23 of 23 cases exact through Mixer+ChainsHost AND 23 of 23 through SongRenderer (the 7 event cases included: the golden's events go in through `chainsHost()->event`, as `dispatch` does).
- F1..F8 and items 3, 6a: see below. Verified: `chains_song`, `chains_host`, `render_abi`, `render_bus_chain`; the full Release ctest and ASan+UBSan (see the end).

## Ownership protocol (F1; chost.h has it in full)
1. `plan()` (control) reads the control view (`ctlNode_`: the nodes of the last SETTLED plan) and never writes it; fresh nodes and prepared patches belong to the plan alone. Out of memory: null, everything freed, view untouched.
2. One plan in flight: a second `plan()` before the first is settled returns null and counts `busyRefused`. A refused plan (patch refused, over budget) is a no-op: it is never in flight.
3. `commit()` (audio) adopts the plan's nodes, swaps the prepared patches in and leaves what it replaced in the plan; a second commit of the same plan is nothing; it never dereferences a null `node_[t]` (a stale swap plan for a node that is gone frees its patch with the plan).
4. `settle()` (control), once, whether committed or not: a committed plan's nodes become the control view, an abandoned plan leaves it as it was. A plan must be settled before the next `plan()`.
5. Then delete the plan: it frees what it still owns (never-adopted nodes / patches) and what commit retired. No path leaks (the test counts live allocations: abandoned plan, hostile plan, bad_alloc at every allocation of `plan()`, host destroyed with plans in any state).

## Design as built
- One `ChainsNode` per track that is heard, has CHN on and a Chain Out. `commit(plan, mixer)` (audio): nodes and maps, then `Mixer::setChain` hooks, `Mixer::setPlan` LAST.
- Heard set offline (F4, the page's `pass()`, index.html:25909-25913): `audible & (noteTracks | busClosure)`: `ChainsHost::keepMask(plan, audible, noteTracks)`. `SongRenderer::setChains` finds the note tracks by a static scan of the song tables
  (`noteTracksOf` in render.cpp: a track that is not a bus and has a note column 0..119 in a phrase some chain of it reaches on any song row). The page uses `notes.some(e => e.track === t)` over the notes the sequencer made, so the scan is a SUPERSET (a note a mute, a CND or a loop never plays still counts). A stem takes its own heard set. The `inMask | nodes` masking in the test is gone.
- Events: `event()` stores FRAMES (int64) in a fixed time-sorted array of 4096 (F2; worst real row 32 tracks x 30 = 960); a full array drops and counts. `beginBlock(frame)` is called by the renderer once per quantum before `mix_.render` (item 3); the hook has no clock of its own.
  TWO rules: tempo due = ceil(t / q) * 128, q = 128.0 / sr; chc due = ceil(ceil(t * sr) / 128) * 128 (Chrome-measured, C1G-NOTES "Chrome k-rate check"). The 128 grid is the page's absolute grid of the render; a live host passes its own frame counter.
- Transport (F6): the host keeps the song transport current with no node (a tempo event with no node is applied to `tp_` at once, not queued; a chc with no node is counted `chcNoNode` and dropped on arrival). A plan for a changed song (`newSong`) replaces the host's transport; otherwise the host's (moved by tempo events) stays. Applied to a fresh node and again after every swap.
- Memory (F3, ruling D7): one total budget per host, default 1.5 GiB, settable at construction (`ChainsHost(sr, bytes)`, `setChains(true, bytes)`). `plan()` adds up, BEFORE building, `chains_module_bytes` (cmem.inc) per module over the nodes the plan would have, plus 300 KB a node (buses, sends), twice the patch for a node that is swapped (the second set exists during the swap). Over budget: the whole plan is refused (no-op, playing chains stay), counted `overBudget`.
- F7: `try`/`catch` only under `#if defined(__cpp_exceptions)`, as `cengine.cpp`.
- F5: `setChains` returns 0 or `kChainsLate / NoMemory / Refused / OverBudget`; `thunder_render_create` fails with a text ("chains: ..."). Counters appended to `thunder_render_stats` (offsets 200..248, uint64): events applied, dropped, bad, chc without a node, failed blocks, feed mismatches, refused (plans refused or busy). `thunder-render` prints them to stderr only when one of dropped / bad / chcNoNode / failed / mismatch / refused is non-zero.
- `setBusMap`: `busMap_.reserve(kMaxFeeds)` in the constructor and the loop stops at kMaxFeeds; the control-side `busmap` op stops there too (verifier item 8).
- Order rule (render.h): `setStem` first, `setChains` after it and before the first block; `setStem` is refused once chains are on (tested); `setChains(false)` takes the hooks and the bus plan off again (tested: the render equals one that never had chains).

## Memory figures (F3; from `test_chains_song`, which prints them for cases a and b)
| song | patch | per track (patch + 0.29 MB node) | nodes | total | during a patch swap |
|---|---|---|---|---|---|
| ELECTRO (a/electro: Track In 1, 2, 3, 7 -> Stereo mixer -> Chain Out 4) | 0.40 MB | 0.69 MB | 1 | 0.69 MB | 1.09 MB |
| BETA-TESTING-ELECTRO (b: Track In 1-6, 9 -> mixer -> Chain Out 8) | 0.58 MB | 0.88 MB | 1 | 0.88 MB | 1.45 MB |
Both are far under 1.5 GiB. The budget matters for a song with many CHN tracks that each get a node (32 nodes of a 0.5 MB patch = 28 MB; a patch with Resample / reverbs costs more).

## Known page quirk (item 6a): NOT replicated
A tempo event whose `ceil(t / q) * q` is at or after the render's length (or not > 0) is sent by the page BEFORE quantum 0 (index.html:23229-23231: `at = Math.ceil(ev.t / q) * q; if (!(at > 0 && at < N / sr)) send now`). The native renderer does not know the render length and the host applies it at its due frame, which is never reached. Only a trailing tempo event, i.e. after the last audible frame, is affected; the golden's `eventQuanta` check applies the page's rule to the expectation only. Not reproduced.

## Mutations (each made, seen to fail, undone)
| mutation | killed by |
|---|---|
| feed pairs in reverse order in `hook` | 16 of 22 golden cases, `hook == direct ChainsNode` |
| `chc` applied one quantum early | the plan check against `expect.eventQuanta`; the hashes of the f cases |
| transport not re-applied after a swap | `test_chains_host` section 2 |
| un-heard track hooked in a stem | h/stem-plain-track, h/stem-source-only, `test_chains_host` section 4 |
| float `ceil` in the rules | g/tempo-change; f/byp-fuzz-quantum-start; the rule checks |
| tempo rule used for `chc` | f/byp-chrome-rule-k5 |
| `commit` skips `setBusMap` on a swapped node (verifier 1) | 2b: the map follows a swap whose feeds change |
| `put` unstable (`>=`) (2) | same frame, the event given last wins |
| `tp_.tempo = e.v` removed (3) | section 10: the tempo of an applied event survives the next plan |
| `moved` only the first time / always (4) | section 10 (changed song takes its transport; same song keeps the events') |
| `plan()` writes the control view (5) | 7a (abandoned plan): crash / fresh-cost check |
| `noop` guard removed in `commit` (6) | section 3, hostile document with another bus plan (the mixer's plan and the node's map stay) |
| `eventsApplied` counted for a node that is gone (9) | section 5, "the node went away" |
| busy check removed / second commit allowed / null-node guard removed / settle adopts an abandoned plan | 7b / 7c / 7d / 7a |
| plan destructor frees nothing (fresh nodes; retired patches) | live-allocation count (7a, 7e, sweep; glibc, no sanitizer) |
| budget ignores the swap's second set | section 8 |
| `setChains(false)` leaves the hooks / `setStem` allowed after chains / `beginBlock` not called | chains_song (crash on the dangling hook; the setStem check; 17 cases) |
Still NOT killed, and why: (8) `setPlan` first / hooks last in `commit`: unobservable single-threaded (the mixer's `rebuild` runs inside each call; no block runs between); (10) the priming calls at the top of `plan()` removed: the first lazy call then happens inside `plan()` anyway, on the control thread, so no test can see it
(the audio side makes no call to those tables); (11) the `kMixFeedMax` clamp of `nBus`: `bus_plan` already caps `nFeeds` at `kMixFeedMax`, so the clamp is redundant (an equivalent mutant); (7) the `appliedQ_` gate no longer exists (the frame rule replaced it).

## thunder-render on ELECTRO (synthetic pads: 24 pads, 0.35 s sine plus noise, `/tmp/c1pads/index.json`)
```
T=native/build-c1/tools/thunder-render/thunder-render
$T sounds/ELECTRO.thunder /tmp/c1pads out.wav --frames 480000 --block 64            # also --block 4096, and each with --chains
```
Result: all four exit 0 with an EMPTY stderr; `e-64.wav == e-4096.wav` and `echains-64.wav == echains-4096.wav` byte for byte (cmp); the chains render differs from the plain one (max |diff| 0.1545, peak 1.4428 plain, 1.3340 with chains, RMS 0.164 / 0.145); no NaN, no inf in the chains file. No counters printed (all zero).

## test_render_bus_chain
Kept. The real host (`opts.chains = 1`, Track In -> Chain Out) gives the stand-in's mix bit for bit (max |diff| 0); stems and heard masks equal. It now also checks `chains = 2` (E_ARG text), a refused chain patch (the create fails with a "chains" text) and the stats fields.

## Open questions / not done
- Pump / MODFX track effects (FX1), live (C2), taps / sends / sidechain (C3), the CHN switch ramp, Resample, patch edits during play: not in C1.
- The static note-track scan is a superset of the page's (see Design). A song whose only note on a CHN source is never played (muted by CND / a mute that is not a track mute) keeps that source heard.
- `commit` returns void: what it replaced is in the plan (`retiredPatches()` / `retiredNodes()`), freed by deleting the plan after `settle`.
- `chc` for the same switch on two events in one quantum: the later wins (stable order); pinned by the same-frame test for tempo.
