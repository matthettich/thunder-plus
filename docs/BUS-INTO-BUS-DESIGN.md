**Status (19 Nov 2026, analysis phase only): I am in the middle of the read-only analysis of "Bus into bus" (HANDOVER To-do "Bus into bus, FIRST"). Nothing in the app is changed. The lead rules on the proposals in Part 2, then the build phase starts.**

Base: `beta` at `ba32739` (build 647 + docs). Line numbers are `index.html` unless a file is named; `core/seq-core.js` is generated from `index.html` by `tools/make-core.mjs`, so its lines
are copies (listed where they matter). `beta/` and `stable/` in the repo are deployed copies of the app, not sources; they are not listed. Native lines are from `origin/native` (`FETCH_HEAD`, `native/core/src/mix/`).
I ran one scratch script (not committed) that cuts `busPlan`, `chainsTracks`, `chainsFeed` and `chainOutTargetsOf` out of `index.html` into a vm and feeds them the owner's patch; its output is quoted in 1.3.

---

# Part 1. The code as it is

## 1.1 The words, and where each is decided

| Word | Meaning today | Where |
|---|---|---|
| **chains track** (`chainsTracks()`) | a track that has a Chain Out module in the patch (any Chain Out with `params.track` = that track, 1..NT) | `chainsTracks` 20939-20944 (`ids.add(k - 1)` at 20942) |
| **`chainsFeed(t)`** | CHN switch on **and** `chainsTracks().has(t)`. This is "the track gets a chains node and its sound is replaced by the chain's return" | 20945: `!!(S.chainOn && S.chainOn[t] && chainsTracks().has(t))` |
| **source** | a track that has a Track In module, CHN on, whose sound (cables, breadth-first) reaches a Chain Out of another track `b` that is a bus. It is added to `feeds[b]` (list of sources of bus b) and `targets[t]` (buses t feeds) | `busPlan` 20773-20812; the walk 20793-20808 |
| **bus** | a track `b` with `chainsFeed(b)` and **no Track In of its own anywhere in the patch**. `isBusT(t) = busPlan().feeds.has(t)` | the rule is the `ownIn` set, 20790-20791, used at **20800**: `if (b === t \|\| ownIn.has(b) \|\| !chainsFeed(b)) continue;`; `isBusT` 21086 |
| **"pure" source** | a source whose Track In sound does not also reach a Chain Out of its own track: `pure.add(t)` when `fed && !outs.has(t)` (20806). A pure source is cut from the master (`routed`, 22504, 22533-22534) and is heard only through its bus. A source that is not pure (it keeps its own Chain Out and also feeds a bus) is a **send** (`isSendT` 20833) | 20806 |
| **`BT`** | in the Mixer drawing, `busPlan().targets`: the map "source -> the buses it feeds" (30370: `const ct = chainsTracks(), BT = busPlan().targets;`) | 30370 |
| **`ct`** | in the same line, `chainsTracks()`, the set of tracks that have a Chain Out | 30370 |
| **PRE** | `pre.add(t)` when that Track In's "Bus send" is "Before its fader" (20807) | 20807 |
| **loop** | a source -> bus edge that would close a cycle (`reaches(b, t)`) is dropped and `P.loop = true` (20801). Nothing displays `P.loop`; grep shows no reader outside the plan itself | 20801 |

### The four Mixer labels (CHN row)

Row drawing, 30370-30375:
`on = chainsFeed(t)`, `flag = S.chainOn[t]`, `bs = !on && flag && BT.has(t)`,
`tx = on ? 'ON' : bs ? 'BS' : flag && !ct.has(t) ? 'MISS' : '--'` (30372). Colour: `on || bs` -> `cf` (lit), `flag` only -> `v`, else `e`.

Status line (the info line when the cursor is on the CHN cell), 30063-30073:
- `ON`: "<track> <kind> · chains ON (track runs through the chains patch)"
- `BS`: "... chains BS: feeds the bus on track(s) NN (heard there, not on its own[, before its fader]) · EDIT toggles"
- `MISS`, first cause (the track's `chainOutTargetsOf` list, minus itself, is not empty): "MISS: its Chain Out feeds track(s) NN, whose CHN is off (switch that track on, or this one) · EDIT toggles" (30069-30072)
- `MISS`, second cause: "MISS: the patch needs a Chain Out for this track, or a bus it can feed · EDIT toggles"
- `--`: "off · EDIT toggles"

`chainOutTargetsOf(t)` (20948-20961) walks the cables from every Track In of track t and lists the Chain Out tracks it reaches **ignoring CHN and ignoring the bus rule**; that is why its sentence says "whose CHN is off" even when the real reason is the `ownIn` rule (see 1.3).

## 1.2 Who else reads the plan (every reader, one-level assumptions marked **[1-level]**)

`grep -n "busPlan\|isBusT\|\.feeds\|\.targets\|\.pure\|\.pre\b"`:

| Reader | Line | Use | One level? |
|---|---|---|---|
| `busPlan` itself, `busGroups`, `isSendT`, `busCls`, `feedsBus`, `busOf` | 20773, 20816, 20833, 21091, 21093, 21094 | colours, kinds, "bus of tracks ..." | colours: a bus that feeds a bus keeps its own colour; fine |
| `chainsNode` | 20872 | `busMap:(busPlan().feeds.get(track) \|\| []).slice(0, 32)` is the node's Track In list | fine: a node only asks for its own sources |
| `pdcPlan` | 21023-21041 | `atP(t) = L(t) + max(atP(source))` recurses through `feeds` | **already follows chains** (see 1.6) |
| `isBusT` users | 20015 (`pumpAllowed`), 20114, 26581, 27772, 29440-29475, 30274-30277, 33589, 36166, 37334, 37369 | look and feel of a bus track, "no notes onto a bus" (`busNo`), pump FX allowed on buses, Pattern view never folds a bus | fine |
| **`audible(t)`** | 21573-21577 | solo: `for (const b of P.targets.get(t)) if solo(b)`; `for (const u of P.feeds.get(t)) if solo(u)` | **[1-level]** |
| **`makeSeq`** (page and audio thread) | 21889-21891 (core copy 255-258) | `hearT` closes over `targets` **transitively** (while-loop); `busT = {t : isBusT(t)}` drops a bus's notes | `hearT` fine; `busT` is a flat set, fine |
| `busSources` (BUS PLAY) | 28928 | walks `feeds` transitively | fine |
| `unBus` | 23966-23972 | takes `feeds.get(t)` of one bus, gives each source its own straight Track In / Chain Out | one level, harmless (see 2.7) |
| `busSync` (the audio graph) | 22501-22529 | wires every bus's sources; `routed` = pure sources | fine for any depth (1.4) |
| `pdcSync` | 22537-22553 | sets `dl`, `fdl`, `fdlP`, `_cd` from `pdcPlan` | fine |
| **`pass()`** (offline mix and stems) | 25161-25164 | `for (const b of keep) for (const t of P.feeds.get(b)) if (audible(t)) k2.add(t)` | **[1-level]** |
| **`renderOut`** | 25245-25250, 25283-25289 | `keepT` gets a bus when `src.some(t => keepT.includes(t))`, once, in `Map` insertion order; `inBus(t) = pure && targets(t).some(b => keepT.includes(b))` | **[1-level and order dependent]** |
| RENDER PART | 25370 | `pass(..., new Set(part.tracks), ...)` | inherits `pass` |
| Mixer drawing and status | 30063-30073, 30276-30277, 30370-30375 | labels | see 1.3 |
| `core/seq-core.js` copy of `busPlan` | 126-180, `core/core-processor.js` 327-334 reset the cache on 'load' / 'song' | only `makeSeq` reads it (`busT`, `hearT`) | the copy is generated: editing `index.html` + `node tools/make-core.mjs` changes it |
| CPU panel | 34484, 34638 | one line per track that has `b.chn`: `NN chn` and `Chains · <track>` | nothing assumes depth; a middle bus is one more node it already lists |
| "Muted tracks rest" / silent-track sleep (`thunder-tfx` p1, 20580-20620; `fxRest` 23059) | `fxRest(t, audible(t))` | rests a track's **effects** node while `audible(t)` is false | uses `audible`, so it inherits the 1-level solo rule |

Not found: any reader of `P.loop`; any reader of `pre` outside `busSync` and the plan.

## 1.3 The owner's patch on paper

Patch: Track In 1, 2, 3, 4, 5, 6, 9, 13 -> Stereo mixer -> Chain Out 8; Track In 8 -> Chain Out 14. CHN on for 1-6, 8, 9, 13, 14.

`chainsTracks()` = {8, 14}. `chainsFeed`: 8 true, 14 true, 1-6, 9, 13 **false** (no Chain Out of their own).

`busPlan()`:
- `ownIn` (20790-20791) = every track that has a Track In module = {1-6, 8, 9, 13}. **Track 8 is in it** because of the second Track In.
- For Track In 1 (and 2-6, 9, 13): outs = {8}. At 20800, `ownIn.has(8)` is true, so `continue`: **no edge, not fed**, not in `feeds`/`targets`/`pure`.
- For Track In 8: outs = {14}; 14 is not in `ownIn`, `chainsFeed(14)` is true: edge 8 -> 14. `feeds {14: [8]}`, `targets {8: [14]}`, `pure {8}`.

The scratch run printed exactly this: `feeds [[14,[8]]] targets [[8,[14]]] pure [8] loop false` and, for tracks 1, 2, 6, 9, 13: `feed false`, `outTargets [8]`.

Mixer, tracks 1-6, 9, 13: `on` false; `BT.has(t)` false (they are not in `targets`) so `bs` false; `flag` true and `ct.has(t)` false -> **MISS** (30372). Tracks 8 and 14: ON.

Status line on 01: `tg` is undefined; `ot = chainOutTargetsOf(0)` = [8] -> **"MISS: its Chain Out feeds track 08, whose CHN is off"** (30069-30072). That sentence is **wrong here**: 08's CHN is on. The real reason is the `ownIn` rule (1.5). (Separate defect, see Part 2 item A.)

### What it sounds like in that state

- **Tracks 1-6, 9, 13**: no chains node (`routeTrack`: `wantChn = chainsFeed(t)` false, 23121), not pure, not routed. Each plays **dry to the master through its own FX, VOL and pan** (`inp -> dry -> ins -> g -> cg -> p -> dl -> out`, 22567-22572, `dry` gain 1). Not silent, not through the first bus. The first bus's reverb is never heard on them.
- **Track 8**: has a chains node (ON). `chainsNode` is made with `busMap = feeds.get(7) = []` (20872), so its Track Ins 1-6, 9, 13 read silence (worklet 6298-6303 fills the slot only for mapped tracks), the mixer sums silence, Chain Out 8 returns silence. `chnIn` fades `wet` to 1 and `dry` to 0 (23097-23105): **track 8's own notes are muted by the chain return**, and 8 is pure so its `dl` is cut from the master. Track 8 is silent.
- **Track 14** (a bus: `feeds.has(13)`): the chain node runs Track In 8 -> Chain Out 14 and reads track 8's `p` (the silence above). **Silent.** Its notes (if any) are dropped by `makeSeq` (see below).
- Net: the master hears the eight sources dry; both buses are silent. That matches "the reverb I put on the first bus doesn't play".

### Both sequencers
`makeSeq` is the same code in the page (`index.html` 21889) and in the audio-thread core (`core/seq-core.js` 255-258; the core gets `S` by message and resets its plan cache in `core/core-processor.js` 327-334). `busT` = {14} only (`isBusT` is `feeds.has`). So on **both**: tracks 1-6, 8, 9, 13 play their notes (8's go nowhere audible); track 14 plays commands only. Mute / solo is page-side only (`applyMix` 23066-23078, `audible`); the core has no `audible`.

### Offline render and stems (`pass`, `keep`, `inBus`)
- `renderOut`: `keepT` = audible tracks with notes (1-6, 8 if it has notes, 9, 13). `for (const [b, src] of BP.feeds)` (25246) adds 14 only if track 8 has notes in `keepT`. `inBus(8)` = pure and 14 in `keepT`: no stem for 8 in that case. Every other source has its own stem (`inBus` false: not in `targets`).
- `pass(evs, keep, ...)`: `k2` adds track 8 when 14 is kept. With `chnOk`, the offline graph builds the same nodes and `busSync` with the same empty `busMap` for 8. So the mix and the stems match live: dry sources, silent 8 and 14. Stems add up to the mix.
- Not a bus-into-bus case, but seen on the way: see "separate findings" at the end (send double count in a bus stem).

## 1.4 The audio graph (live and offline share `makePlayer`)

Per track (22557-22573, comment block 22554-22558):
`voices -> inp (TRIM) -> dry ----------\ (fades 1 -> 0 when the chain comes in)`
`inp -> chains node -> wet --------------> mid -> [track FX node `fx`] -> ins -> g (VOL, mute/solo) -> cg (TVL/TVA) -> p (pan) -> dl (PDC delay) -> out`
`ins -> pre (mute/solo gain) -> PRE sends`; `inp` also feeds the Sidechain In taps (`tapNode`, 22449-22465), the Track In meter (`anIn`) and `ai`.

- `routeTrack` (23117-23160): builds / drops the chains node (`wantChn = chainsFeed(t)`), sends only the patch diff (`chnDiff`), fx node, rewires `dry`/`wet` into `mid` (`chnIn` 23097), then `sendSync`, `busSync()`, `pdcSync()`.
- `chainsNode` (20862): six inputs: 0 the track (`inp`), 1 sidechain bus, 2 taps, 3 sends (a block late), 4 and 5 bus feeds (16 stereo pairs each, `busmap`); outputs: 0 the return, 1 the sends.
- **Where a Track In taps a track.** Inside a node, `TrackIn.process` (worklet 402-411) copies slot `k` of the host buffer. The node fills slot `this.track` from input 0 (`inp`, **before the track's own chain and FX**) and slot `t` for each mapped source from inputs 4/5 (worklet 6292-6303), skipping `t === this.track`. So: a **Track In of the node's own track = its own notes after TRIM**; a **Track In of another track = that track's `p`, after its chain return, FX, VOL, pan, and the feed delay**, or `pre` (after chain and FX, before VOL and pan) when the Track In says "Before its fader" (`P.pre`, `busSync` 22518-22521). "After its fade" is the default (`q.p -> q.fdl -> q.spl`).
- **Where a Chain Out returns.** `ChainOut.process` (worklet 455-467) adds into `ctx.busL(k)`; the node outputs only `busesL[this.track]` (6310-6312), i.e. only its own track's Chain Outs; **each node runs only the modules that lead to a Chain Out of its own track** (`_active`, worklet 5941-5962; modulators and Sidechain Ins included). The return goes `-> wet -> mid`, i.e. **before** that track's own FX, VOL, pan, delay and meters. A bus track therefore replaces its own dry notes with the chain return (dry is faded to 0).
- **A bus track's own notes**: `makeSeq` drops them for any track in `feeds` (commands only); if they did play they would go to `inp`, be read only by a Track In of that track in that node, and be muted by `dry -> 0`.
- **Mute / solo**: `applyMix` sets `g` (and `pre`) from `audible(t)`: a muted source adds nothing to the bus (post and pre both shut); a muted bus is silent at its `g` while its chain node and sources keep running; solo of a source keeps its bus, solo of a bus keeps its sources (one level, 21573-21577). Mute beats the bus rule (`mute -> false` comes before the `targets` test).
- **Sidechain sends**: a Sidechain Out runs in one node (the lowest Chain Out track of its undirected connected module group, worklet 5949-5958) and goes through `sendBus -> sendDly (128 samples) -> every node's input 3` (22485-22498). "Track N" as a Source reads `inp` of N (own notes), so a bus cannot be a Source by number (HANDOVER 15 Nov). A bus-to-bus chain made with Track Ins does not join the two buses' cable groups, so each keeps its own Sidechain Out node.
- **Bus wiring** (`busSync` 22501-22529): for each bus with a live node, a splitter on each source's `p` (after `fdl`), 32-channel mergers -> inputs 4/5; `busmap` message; pure sources lose `dl -> out`. Everything is Web Audio nodes in an **acyclic** graph for any depth, so the browser runs a source's whole path (node, fx, fader, pan, feed delay) before the bus's node in the same render quantum: **no block of delay** for a chain of buses. A `DelayNode` (`fdl`) is on every source -> bus edge; only in a cycle would Web Audio raise it to one quantum (128 samples).
- **Worklet order inside a block**: there is none to worry about: each track has its own node and engine; Track In slots are filled from the node's inputs at the start of `p1` (6292-6303), Chain Out is written during `e.process` and returned at the end (6310-6312). Cross-track order is the browser's graph order. Not found anywhere: a shared buffer between two nodes.

## 1.5 Why the old rule says "ownIn"

HANDOVER-ARCHIVE 2828-2834 (25 Oct): with tracks 4 and 5 keeping their outs and also feeding track 6 (one shared mixer feeding Chain Out 4, 5 and 6) the breadth-first reach from Track In 4 contains Chain Out 5 and the reverse, "4 feeds bus 5 / 5 feeds bus 4", a false feedback pair. The fix was "a Chain Out for a track that has any Track In is that track's own output, never a bus", accepted by the owner as: "you cannot send into a track that also has its own input". The owner's patch (19 Nov) is that exact accepted consequence: Track In 8 exists, so 8 can never be a bus, so the first bus does not exist.

Important: the test that matters for the original bug is **not** "a Track In of track b exists" but "the Track In of b reaches Chain Out b" (a track's own return). In the 25 Oct case both hold; in the owner's patch Track In 8 reaches only Chain Out 14. That distinction is the whole design in 2.1.

## 1.6 Latency plan

`pdcPlan(sr)` 21023-21041 (comment 21008-21017): `atP(t) = L(t) + max over feeds.get(t) of atP(q)` is recursive with a loop guard `busy`, so the arrival at a bus follows the longest path through any depth. `f` (feed delay): per bus, `m - at(q)` for each source, max-ed per source (one value per source for all its buses). `routed` = pure sources; `T` = max arrival over non-routed tracks; `d(t) = T - at(t)` (0 for a routed source); `c(t) = at(t) - L(t)` is how late the track's input is (the beat clock delay of its effects). **The plan already compensates chained buses**; the comment at 21002 ("A bus's latency isn't compensated anywhere: see pumpLatTxt") is out of date (the 1 Nov build, `pdcPlan`, did it). `pdcSync` applies `d`, `f` to `dl`, `fdl`, `fdlP` and `c` to each fx node's clock delay (22537-22553; live: a 50 ms glide, 10 ms at PLAY, idle -> 0 in `auto`). Render: `pass` adds `round(T * sr)` to `lat` and trims it (25167).

Limit that exists already and chaining makes likelier: `f[q]` is one number per source, applied before a splitter that feeds **all** the buses of that source; a source that feeds a middle bus and the end bus directly would line up with only one of them (1.2, `pdcPlan`'s `f.set(q, Math.max(...))`).

## 1.7 Native (`origin/native`, `native/core/src/mix/`)

- `bus_plan` (`mix.cpp` 111-152) is a line-for-line port: `ownIn` at 125-126, the skip at **141** (`if (b == t || (ownIn >> b & 1u) || !(cfeed >> b & 1u)) continue;`), `reaches(b, t)` -> `P.loop` at 142, `pure` at 146. Output: `feeds`, `feedLen`, `nFeeds`, `feedMask`, `targets` (masks of direct neighbours), `pure`, `pre`, `loop`. Tests: `native/tests/test_busplan.cpp` compares against the JS through `tools/mix-dump.mjs --busplan` ("hand-made patches (...a track with its own Track In...) and every song in sounds/. Exact."); goldens in `native/tests/golden/mix_golden.inc`.
- `mix_audible` (`mix.cpp` 154-162) is the one-level solo rule: line 159 `(P.targets[t] & solo) != 0 || (P.feedMask[t] & solo) != 0`.
- `Mixer::rebuild` (208-217) already builds `order_` so **every bus comes after its sources** (passes over tracks until all feeds are `done`; a loop falls through to the tail order and would read last block's `post_`). Fixed-size arrays: `kMixFeedMax` 32 sources.
- `pdc_plan` (`pdc.cpp` 20-52): the same recursive `atP` over `feedMask`; same single `f[q]` limit.
- Stems: `setStem` does not exist on `native` yet (not found by `git grep`); U34b (stems) is PR #246, open, not merged (`docs/port/PICKUP.md`). When it lands it needs the closure in 2.6.

---

# Part 2. The design (proposals; the lead rules)

## 2.1 Definition (item 4)

**Proposal.** A track is *a bus* when some Track In of a **different** track reaches one of its Chain Outs, its CHN is on, and **its own Track In (if it has one) does not reach its own Chain Out** (it is not an "own return"). A track is *a source* when it has a Track In, its CHN is on, and that Track In reaches a Chain Out of a bus. A track can be both. The relation "t feeds b" is a directed graph; it must be acyclic.

Code: in `busPlan` replace the `ownIn` set (a Track In exists) with `ownRet` (a Track In of b reaches a Chain Out of b). Today's 25 Oct case keeps its answer (Track In 4 reaches Out 4). It computes `outs` per Track In anyway (the loop at 20793-20799); do a first pass to collect `outsOf[t]`, then `ownRet = {t : outsOf[t].has(t)}`, then the edge pass. Everything else (`feeds`, `targets`, `pure`, `pre`, `busSync`, `chainsNode`, `pdcPlan`) works unchanged for the owner's patch, as read in Part 1.

Order and delay: **dependency order, no added block**. The Web Audio graph and the worklet slots already do this (1.4); native's `Mixer::rebuild` already orders buses after sources (1.7). A middle bus's `p` feeds the next bus's input 4 in the same render quantum.

Unchanged by design: a track whose own Track In reaches its own Chain Out and that is also fed by other tracks is still "an own return, not a bus" (the accepted 25 Oct consequence); and a middle bus is always **pure** (its Track In reaches only the next bus), so it is heard only through the next bus. A bus that is heard at the master **and** feeds another bus ("parallel") is not possible with this definition (ruling needed, 2.8 R5).

**Cycles (ruling R2).** Owner's rule (17 Nov): the editor accepts any cable, the Mixer says what happens. Two options:
- **A (recommended for the first build).** As today: the edge that would close the loop is left out of the plan (`P.loop`), the rest plays, and the Mixer says which cable is left out and that nothing feeds back. No feedback, no new risk. Make it deterministic: add edges in order of (source track, bus track) instead of module order, so the same patch always loses the same cable (the higher-numbered source's). Record `P.loopEdges = [[t, b], ...]`.
- **B (later, if wanted).** Keep the edge, one block (128 samples, 2.7 ms at 48 kHz) late. Web Audio already does that for a cycle through a `DelayNode` (`fdl` is on every edge); native's `order_` fallback reads last block's buffer; the worklet's `e.busMap` would need no change. Cost: a real feedback loop through reverbs and compressors can run away; the chains editor's `fb` cables already carry the same risk and the same word, so the cable would be marked "feedback" in the editor. More work in the plan (cycle-aware `pdcPlan`: `busy` guard already returns `v`, which is wrong for a cycle), more things to hear.

## 2.2 What each track says (item 5)

Labels stay `ON`, `BS`, `MISS`, `--`; one new label, **`LOOP`**, for a track whose cable is left out (R3). All status lines keep the existing form "<track> <kind> · chains <label> ...".

**The owner's patch:**
- 01-06, 09, 13: `BS` (they are in `targets` now). Line: "chains BS: feeds the bus on track 08 (heard there, not on its own) · EDIT toggles" (existing text).
- 08 (kind BUS): `ON`. Line: "chains ON (track runs through the chains patch) · bus of tracks 01, 02, 03, 04, 05, 06, 09, 13 · feeds the bus on track 14 (heard there, not on its own)" (the middle part is `busOf`, 21094; the last part is the BS sentence).
- 14 (BUS): `ON`. Line: "chains ON (track runs through the chains patch) · bus of track 08" (adds `busOf`; new for every bus, one line).

**Edge cases**

| Case | Label | Status line (proposed words) |
|---|---|---|
| A source feeds two buses (08 and 14) | `BS` | "BS: feeds the buses on tracks 08, 14 (heard there, not on its own) · EDIT toggles" (existing code already pluralises) |
| First bus 08 has CHN off | sources `MISS`; 08 `--`; 14 `ON` | sources: "MISS: its Chain Out feeds track 08, whose CHN is off (switch that track on, or this one) · EDIT toggles" (now true; the sources then play dry). 14: "chains ON · nothing reaches it yet: its Track In 08 needs CHN on for track 08 · EDIT toggles" (new, from a `chainInSourcesOf(b)` that mirrors `chainOutTargetsOf`) |
| Bus 08 ON feeds a bus 14 whose CHN is off | 08 `ON`, 14 `--` | 08: "chains ON · bus of tracks 01-06, 09, 13 · its Track In 08 feeds track 14, whose CHN is off, so 08 plays on its own (switch 14 on) · EDIT toggles". Sound: 08 is not pure (its edge to 14 is not in the plan), so it plays to the master and the first bus is heard. "A source is never silenced by a bus that isn't there" (the existing rule) |
| Middle bus 08 muted | unchanged | the mute shows as today; add to 08's CHN line "· muted: nothing reaches track 14 from it". Sources stay `BS` (they are still running into 08) |
| Middle bus 08 soloed | unchanged | 08's solo keeps its sources and 14. Fader-row line: "solo (keeps tracks 01-06, 09, 13 and bus 14)" |
| A source (01) soloed | unchanged | "solo (keeps the buses it feeds: 08, 14)". End bus 14 is heard (today it would not be: solo is one level) |
| End bus 14 soloed | unchanged | "solo (keeps what feeds it: 08 and tracks 01-06, 09, 13)" |
| A track that is source and bus (08) | `ON` | as above; kind stays BUS |
| A cycle (08 feeds 14, 14 feeds 08) | `LOOP` on the track whose cable is left out; the other `ON` / `BS` | "LOOP: track 14 feeds track 08 and track 08 feeds track 14; the cable from 14 to 08 is left out so nothing feeds back (the 08 to 14 part plays) · EDIT toggles". Option B words: "LOOP: 14 feeds 08 one block (2.7 ms) late" |
| Own return that is also fed (today's accepted case) | unchanged `ON` | "chains ON ..." plus, new: "other tracks' Chain Outs into this track are its own return, not a bus" is not added (not asked for); stays as today |

Pop-up for the owner's original confusion: today's 01 line is the wrong sentence (1.3). Part 2 item A below fixes it on its own.

## 2.3 Latency, stems, render, audio-thread sequencer (item 6)

- **Latency.** `pdcPlan` and its native twin already take the longest path (1.6). With the plan changed, nothing else moves. Test it (2.7). The `f` per source limit (a source that feeds two buses of different depth) can be fixed later with one splitter and one delay per edge (JS `busSync`, `pdcSync`, native `Mixer` `fd_`); not proposed now (R6).
- **Stems.** Rule: the stem of a bus is the bus **and everything upstream of it that is heard only through it** (all pure sources and pure middle buses, transitively); a pure track whose bus is rendered has no stem of its own (that is today's `inBus`, made transitive by adding the middle bus to `keepT` first); a send (not pure) keeps its own stem. In the owner's patch the stems are: **14 = everything on the first bus and the second bus; no stem for 01-06, 08, 09, 13**; the sum-of-stems rule stays "stems add up to the mix". Code: `pass()` closes `keep` over `feeds` transitively (a helper `busClosure(keep, P, audible)`), `renderOut` builds `keepT` to a fixpoint (a bus is added when any of its sources is kept, repeat until nothing is added), `inBus` stays as is once `keepT` has the middle buses.
- **Render.** The offline graph is the same `makePlayer`; the plan being acyclic is enough. The tail rule (`tail`: 8 s when any kept track has `chainsFeed`, 25259) already covers it.
- **Audio-thread sequencer.** `makeSeq` reads only `busT` and `hearT`; both are already right for chains (a middle bus is in `feeds`, its notes are dropped, `hearT` closes over `targets`). After the `busPlan` change run `node tools/make-core.mjs` so `core/seq-core.js` gets the same plan; the core's cache is reset by the existing 'load' / 'song' messages.

## 2.4 Code changes, smallest first, each with its test (item 7)

A. **Fix the MISS sentence.** (`chainOutTargetsOf` use at 30069-30072.) Say "whose CHN is off" only when that is true; otherwise (the target's CHN is on but the plan has no edge) use the real reason (own Track In / loop). About 6 lines, one small pure helper `chnMissWhy(t)` so it can be tested in a vm. Test: vm test on the owner's patch (before the plan change: "not a bus: it has its own Track In"; after: never reached). Repo test scripts can check this. Independent of everything else.
B. **`busPlan`: `ownIn` -> `ownRet`, deterministic edge order, `P.loopEdges`.** `index.html` 20790-20808 (about 12 lines), regenerated `core/seq-core.js`. Tests (`tests/bus-into-bus.test.js`, new; same `cut()` + vm pattern as `tests/app-logic.test.js` 262-312): the owner's patch exactly (`feeds {8:[1..6,9,13], 14:[8]}`, `targets`, `pure` = {1-6, 8, 9, 13}, `loop` false); the three existing 25 Oct tests unchanged; three-deep chain; a source into two levels; a cycle (loop edge left out, same one for either patch order); CHN-off combinations; and a **compatibility test**: for every song in `sounds/` and `tests/fixtures`, the plan of the new function equals the plan of the old one unless the patch has a Track In of a track that does not reach its own Chain Out and a Chain Out of that track is reached by another Track In (the owner's shape). Browser not needed.
C. **`audible()` solo closure.** `index.html` 21573-21577 (about 8 lines): the source rule uses `targets*` (transitive up) and `feeds*` (transitive down), mute still beats it. Test: vm, chain 1 -> 8 -> 14: solo 1 keeps 8 and 14; solo 14 keeps 8 and 1; solo 8 keeps 1 and 14; sibling 2 (also into 8) is not kept by solo 1. Native twin: C++ test in `test_busplan.cpp` format.
D. **Render closure.** `pass()` 25164 and `renderOut` 25245-25250 (about 12 lines, one helper). Test: the helper in a vm (closure and fixpoint, insertion-order independence). The actual render: a headless Chromium run (the `tests/sweep/*.mjs` style), see item F.
E. **Mixer text.** 30063-30073 and 30370-30375: `busOf` on ON lines, the middle-bus BS sentence, `LOOP`, "nothing reaches it yet" (needs `chainInSourcesOf(b)`, about 15 lines), solo / mute words (about 10 lines). Test: the pure helper strings in a vm; the drawing by the existing sweep screenshots.
F. **Sound test (a browser or Node-with-worklets run).** Build the patch twice: (i) sources -> mixer -> gain X -> Chain Out 8, Track In 8 -> gain Y -> Chain Out 14; (ii) sources -> mixer -> X -> Y -> Chain Out 14; VOL E0, pan 80 on every track, PDC off. Render both with `renderOut`; expect the two mixes to null (below -120 dB, ideally bit-equal) and the stems of (i) to add up to the mix. Second run with PDC on and a limiter (latency) as the middle bus, a direct source into the end bus: arrival times line up. The repo's `vm` scripts cannot run a worklet; `tests/off-pump-latency.test.js` (stubbed `busPlan`) covers `pdcPlan` with feeds `{14:[8], 8:[1,2]}` without a browser. **Only the owner's ears** (or an agent's null test) can say the reverb on bus 1 now plays on both buses; no script says "sounds right".
G. **Comments / docs.** Out-of-date comment at 21002; HANDOVER.

**Native (to stay in step; on the `native` branch only, not in this PR):**
- `bus_plan` (`mix.cpp` 125-126, 141-142): the same `ownRet` and edge order; `loopEdges` (a small array or mask pair).
- `mix_audible` (154-162): transitive up / down closure (compute up and down masks once in `bus_plan`, store in `BusPlan`, e.g. `upMask[t]`, `downMask[t]`; or loop in `mix_audible`). `feedMask` and `targets` stay direct-only.
- `pure`: unchanged (a middle bus is pure through its own edge).
- `Mixer::rebuild`: already dependency-ordered; add a test with a three-deep chain.
- `pdc_plan`: no change; add a three-deep case to `test_pdc.cpp`.
- Stems (`setStem`, U34b, PR #246): not on `native` yet; the stem rule in 2.3 must be in it (a bus stem includes the transitive pure sources; a pure source or middle bus has no stem of its own).
- Goldens: add the owner's patch and a three-deep chain to `tools/mix-dump.mjs --busplan` and regenerate `native/tests/golden/mix_golden.inc`.

Size of the web change: about 6 functions touched (`busPlan`, `audible`, `pass`, `renderOut`, the Mixer drawing and info function, the new helpers `busClosure` / `chnMissWhy` / `chainInSourcesOf`), roughly 60-80 lines of app code, 150-250 lines of tests, plus the generated `core/seq-core.js` change. Native: about 40 lines plus tests.

## 2.5 Risks (item 8)

- **Saved songs.** The only plan that changes is a track that has a Track In which does not reach its own Chain Out **and** is the target of another track's Track In: today every source of such a bus is MISS and plays dry; that is the owner's shape. Everything else yields an identical plan (compatibility test in B on all shipped songs; the owner's own songs need the same check: run the old and new `busPlan` on the autosave file and diff). Edge order: songs that today have a loop could lose a different cable if the order is changed from module order to (source, bus) order (R3); they were partly broken already.
- **A song that today has the shape will sound different** (the first bus now works): sources go from dry-at-master to pure and bus-only, the first bus's mix reaches the second bus; track 8's silence goes away. This is the intended fix; say it in the release note.
- **CPU.** No new node per feature: a middle bus already has a chains node (it was "ON"); each source adds a splitter and a feed delay, as for any bus. `_active` stops a node running modules that are not its own. Worst case: many chained buses with heavy effects; the CPU panel already lists each node. In the cycle option B an uncontrolled feedback loop is the CPU and level risk.
- **Clicks.** Existing: (1) `busSync` rebuilds all feed wiring when the plan key changes and cuts / restores `dl -> out` of pure sources without a fade (22504-22527): patching the owner's shape live flips eight tracks at once from master to bus. Same as for any first bus today; a 10-20 ms ramp on `dl` routing would remove it (not proposed here). (2) PDC changes glide (50 ms). (3) CHN switch crossfade is 20 ms. Nothing new is introduced by depth.
- **Chains editor meters.** The Track In meter of a track that is a bus reads `anIn` = the track's own input (22 near `chnIn`: `b.inp.connect(n.anIn)`), while the module in the next bus's node receives that track's `p`. The jack meters of the module itself (engine telemetry) are right; not checked in a browser.
- **`unBus`** (23966) on a middle bus: gives each source its own Track In -> Chain Out and the bus a straight pair; its Track In to the next bus stays, so the bus then becomes a send into the next bus. Acceptable; say it in the toast if wanted.

## 2.6 Rulings needed

R1. Definition: `ownRet` (a Track In of b that reaches Chain Out b) in place of "any Track In of b". Yes / no.
R2. Cycles: option A (edge left out, said in words) or B (one block late). I recommend A first.
R3. Which cable a loop leaves out: by (source track, bus track), higher source first to go. Changes today's choice for songs that already have a loop. OK?
R4. New label `LOOP`, and the extra words on `ON` lines ("bus of track NN", "feeds the bus on track NN"). OK?
R5. Is "a bus heard at the master and also feeding a second bus" (parallel) wanted? Not possible with this definition; needs a per-Track-In switch or patch field.
R6. Per-edge feed delays (a source into two buses of different latency): now or later? Later.
R7. Stem rule (the end bus carries everything upstream; no stems for pure middle buses and pure sources). OK?
R8. Is fixing the unrelated findings below in this change or separately?

## 2.7 Separate findings (not part of this change)

1. **MISS sentence is wrong** whenever the cause is the `ownIn` rule (1.3, item A): "whose CHN is off" while the target's CHN is on. Visible today.
2. **Stems double-count a send (by reading; not run).** In `pass()` a kept bus pulls in its sources with `k2`; a send source (not pure) keeps `dl -> out` (22504), so in the bus's stem the send plays direct **and** through the bus, while its own stem (`inBus` false, 25248) plays direct again. The sum of stems exceeds the mix by the send's direct sound. Needs a render check before treating it as a bug.
3. `P.loop` is computed and never read: a cycle is silently dropped today (nothing says so).
4. Stale comment 21002 ("A bus's latency isn't compensated anywhere"): `pdcPlan` does it.
5. `pdcPlan`'s loop guard (`busy`) returns a partial value for a cycle; harmless while cycles are dropped, wrong for option B.
