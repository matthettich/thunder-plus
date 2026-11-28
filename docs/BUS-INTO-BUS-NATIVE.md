# Bus into bus: what the native port (`native/core/src/mix/`) must change

Written 19 Nov 2026 with web build 651 (PR "Bus into bus", branch `beta-bus-into-bus`). A native worker can do this from this file alone.
Nothing in `native/` was touched by the web change. Web lines are `index.html` at build 651 (and the same code in `core/seq-core.js`, which is generated).
Native lines are `native/core/src/mix/mix.cpp` / `pdc.cpp` at `origin/native` of 19 Nov.

## What changed in the web app (the rules to mirror)

1. **Definition.** A track is a bus when another track's Track In reaches one of its Chain Outs, its CHN is on, and its OWN Track In (if any) does not reach its OWN Chain Out. Before: "it has no Track In at all". A track can be a source and a bus (a middle bus). Web `busPlan`, `index.html` 20773-20826.
2. **Edge order and loops.** Candidate edges (source track t, bus b) are collected for every Track In with CHN on, sorted by (t, b), and added in that order; an edge whose bus already reaches its source is NOT added: it goes to `loopEdges` (`P.loop = true`). So the higher-numbered source's cable is the one left out, whatever the module order.
3. **Closures.** `srcAll[t]` = every track upstream of t (feeds of feeds...), `busAll[t]` = every bus downstream of t. Sorted. The plan is acyclic.
4. **Solo.** `audible(t)`: solo wins, then mute, then "nobody soloed" = heard; otherwise heard when any track in `busAll[t]` or `srcAll[t]` is soloed. (Before: only direct `targets` / `feeds`.) Web `audible`, 21630-21635.
5. **Labels.** The CHN cell is always TWO characters: `ON`, `BS`, `MS` (was MISS), `LP` (new, a track whose cable is in `loopEdges` and whose CHN is on), `--`. Web `chnTx`, 20992-20996. (Text only; native has no Mixer drawing, but a UI over the core must use these.)
6. **Render / stems.** `busStemPlan(keepT, BP, audible)` (25229-25234): keepT gains every audible bus that has a kept source, repeated until nothing is added; `inBus(t) = pure[t] && some target of t in keepT`; stems are `keepT` minus `inBus`. `busClosure(keep)` (25236-25241): a render of a bus takes its audible sources with it, and theirs (a breadth-first walk over `feeds`, adding t only when it is audible, so a muted middle bus does not pull in what only feeds it). In the owner's patch the only stem is bus 14's.

Unchanged: `pure` (per Track In: `fed && !outs.has(t)`), `pre`, `feeds` lists (sorted, duplicates kept), `targets`, `chains_feed`, `pdc_plan` (already follows the longest path: its `atP` recursion is over `feedMask`, so a chain of buses needs no change), `Mixer::rebuild` (already orders every bus after its sources, so no block of delay is added; the loop fall-through at `mix.cpp` 217 is never reached because the plan is acyclic).

## The changes, by native function

### `bus_plan` (`mix.cpp` 111-152)
- Replace `ownIn` (lines 125-126: a Track In of the track exists) with `ownRet`: collect, for EVERY Track In module with a valid track (CHN or not), the Chain Out tracks it reaches (`outs`, the BFS you already do at 132-136); `ownRet |= 1 << t` when `outs` has t. Do this in a first pass over the Track In modules (store `outs` per module, in module order).
- Edge pass: for each stored module with `chainOn[t]`, for each b in `outs` ascending: skip `b == t`, `ownRet` has b, or `!(cfeed >> b & 1)`; otherwise push `{t, b, module}` to a candidate list. Sort the list by (t, b) (stable; equal keys keep module order). Then for each candidate: `if (reaches(b, t)) { P.loop = true; add (t, b) to P.loopEdges; continue; }` else add to `adj[t]`, `feeds[b]`, `P.targets[t] |= 1 << b`, mark the module fed.
- After the edges: for each fed module: `pure |= 1 << t` when `!(outs >> t & 1)`; `pre |= 1 << t` when its `pre` param is non-zero (as now, lines 146-147, but per stored module).
- New `BusPlan` fields: `loopEdges` (a small fixed array of `{src, bus}` pairs, capacity kSongNT * kSongNT is enough; or two masks `loopSrc[t]` = buses whose cable from t was left out), `uint32_t srcAll[kSongNT], busAll[kSongNT]` (closure masks, computed from `feedMask` / `targets` by a DFS or by repeated OR until no change; kSongNT is small).
- The `BusPlan()` constructor zeroes the new fields.

### `mix_audible` (`mix.cpp` 154-162)
Line 159 becomes `on = (P.busAll[t] & solo) != 0 || (P.srcAll[t] & solo) != 0;`. The order (solo, then mute, then no solo) is unchanged.

### `Mixer` (`rebuild`, `want`)
No change. Add a test that a three-deep chain renders with no block of delay (an impulse in track 1's input leaves track 4's `post_` in the same block).

### `pdc_plan` (`pdc.cpp` 20-52)
No change. Add a three-deep case to `test_pdc.cpp` (web test: `tests/bus-into-bus.test.js`, "latency plan follows the chain": tracks 01 10 ms, 02 100 ms, 08 50 ms, 14 25 ms: T = 175 ms, `f[01]` = 90 ms, `c[14]` = 150 ms, `c[08]` = 100 ms).

### Stems (`setStem` and the stem list; U34b, PR #246, not merged when this was written)
When the offline song render picks its tracks: `keepT` = audible tracks with notes; then close it as `busStemPlan` does (a bus is added when audible and any source is in keepT, until nothing is added); the stem list is keepT without the tracks for which `pure[t]` and a target of t is in keepT. A bus's own stem render keeps `busClosure({bus})`. `setStem` for a stem of track t must set the kept set to `busClosure({t})` (everything upstream that is heard only through it), as the web `pass(evs, new Set([t]), ...)` does. Sources that are NOT pure (sends) keep their own stems (see the separate finding below: the web app double counts their direct sound in the bus's stem; do not copy that until the lead has ruled).

### Sequencer
`makeSeq` reads `busT = {t : feeds[t] non-empty}` and drops the notes of those tracks; no change needed beyond `bus_plan`. `hearT` (LFOs) already closes over `targets` transitively in the JS; check the native twin does too.

## Fixtures

`tests/fixtures/bus-into-bus-native.json` (generated by `node tools/make-bus-fixtures.mjs` from the web app's own functions; the web test fails if it is stale). 20 cases in the shape `native/tests/test_busplan.cpp` already reads: `{id, S:{chainPatch, chainOn[16], mix[16]:{mute, solo}}, want:{...}}`. `want` has what the C++ test already compares (`feeds` = `feedMask` per track, `targets`, `lists` (the feeds list per bus), `pure`, `pre`, `loop`, `chainsFeed`, `audible`) plus the new fields to add to the comparison:
- `loopEdges`: `[[source, bus], ...]` (0-based track numbers);
- `srcAll`, `busAll`: closure masks per track;
- `labels`: the 16 CHN cell strings (`ON`, `BS`, `MS`, `LP`, `--`), if the native side computes them;
- `keepT` and `stems` (only in cases that give notes): the render's track list and the stem list, 0-based.

Cases: `owner` (Track In 1, 2, 3, 4, 5, 6, 9, 13 -> Stereo mixer -> Chain Out 8; Track In 8 -> Chain Out 14) with eight solo / mute variants (`solo 14`, `solo 08`, `solo 01`, `solo 01 + 10`, `mute 08`, `solo 14 mute 08`, `solo 01 mute 14`, `solo 14 mute 03`, `solo 14 + mute 14`); the same with CHN off on 08 and on 14; `chain 1-2-3-4` (three deep) with three solos; `send 4, 5 -> 6 (own outs)` (the 25 Oct shape, unchanged answer); `loop 08 <-> 14` in both module orders (`loopEdges` = [[13, 7]], label `LP` on 14).
`tools/mix-dump.mjs --busplan` (native branch) should be extended to emit the same new fields so the existing shipped-song cases keep their golden; for the two shipped songs the plan is identical to before (web test: "compatibility").

## Separate finding (not changed; for the lead to verify)

**Stems double-count a send's direct sound.** `pass()` (web `index.html` 25243) takes a kept bus's sources with it; a send source (not pure: it keeps its own Chain Out) keeps its `dl -> out` path (`busSync`, `routed` = pure only), so the bus's stem contains the send's direct sound AND its bus part, while the send's own stem (`inBus` false) contains the direct sound again. The stems then add up to the mix plus the send's direct sound. Minimal patch that shows it: `tests/fixtures/bus-into-bus-native.json` case `send 4, 5 -> 6 (own outs)` gives `stems [3, 4, 5]` (tracks 04, 05 and the bus 06), i.e. three stems for one mix; in a browser, render that song with stems and null the sum of the three WAVs against the mix: the residue is the direct sound of 04 and 05. A fix would render a bus stem with the sends' direct paths muted (`pl.applyMix` leaves them on today). Not part of this change.
