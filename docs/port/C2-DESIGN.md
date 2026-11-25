# C2: the chains host LIVE (design and rulings; the lead, Fable, 24 Nov 2026, 17:30 Mountain)

Read first: `native/core/src/chains/chost.h` (the whole header comment: the ownership protocol), `docs/port/C1-NOTES.md`, `docs/port/CHAINS-HOST-DESIGN-DRAFT.md` sections 4.2 to 4.9 and 9,
`docs/port/U37-P5-NOTES.md`, `native/core/src/live/live_host.{h,cpp}` (how `FxPlan` travels: `fxPlan` / `fxCommit` / `commitFxLive` / `fxBack_` / `collect()` is the model for everything here).
Nothing outside `native/` and `docs/port/` changes. Two PRs into `native`: **C2a** `n1/c2-cdiff` (control-side diff, no audio code) and **C2b** `n1/c2-live` (the host and `LiveHost`).

## What C2 gives
A song's chains play through `LiveHost` exactly as through `SongRenderer::setChains` (bit-equal for a song played from its start), and the chain follows edits while the song plays: a parameter move
without a reset, a structural edit by a whole-patch swap (ruling D4 (a): state reset, a click accepted), a CHN switch or bus change by a plan that leaves untouched nodes' patches alone.
NOT in C2: sidechain taps and sends (C3); the 20 ms CHN ramp, Resample takes, the pump `service()` driver (C4); a web host.

## Rulings (R1..R15)

**R1 Flag and cap.** `THUNDER_LIVE_F_CHAINS 64u` (create makes a `ChainsHost(sr, budget)`; a host that is not `valid()` fails create with "out of memory"). `THUNDER_LIVE_CAP_CHAINS 0x200u`, set in every build.
`thunder_live_opts` gets `uint64_t chains_bytes` APPENDED (the `size` rule of P5: older callers get 0 = `kChainsHostDefaultBudget`). Without the flag everything below is off and the host behaves bit for bit as today
(`busSlot_` still feeds `Mixer::setPlan`); `thunder_live_chain_edit` answers E_STATE. Remove "chains" from `live.h`'s NOT IN THIS API text and make the "a bus EXISTS only with a chains host" lines true.

**R2 Heard set, live.** Every track (`heard = 0xFFFFFFFF`): the page's `routeTrack` gives a node to every `chainsFeed` track; mute and solo are the mixer's. (`keepMask` is an offline rule.)

**R3 When the control side acts** (in `play()` and in `songPublish()`'s PLAYING path; an IDLE publish builds nothing, as today: the next play does). The control side keeps `chainsSong_` (a `shared_ptr<const Song>`: the song whose
patch and `chainOn` the audio side has, or will have once what is in flight lands) and `chainsBus_` (its `BusPlan`) and `chainsFeed_` (its `chains_feed` mask). For a new song S:
```
d = chains_diff(chainsSong_ ? chainsSong_->chainPatch : none, S.chainPatch)        // C2a
busSame = bus_plan(S) equals chainsBus_  &&  chains_feed(S) == chainsFeed_  &&  chainOn equal
(a) no nodes ever planned and chains_feed(S) == 0 and busSame            -> nothing
(b) d.kind == Same   && busSame                                            -> nothing
(c) d.kind == Params && busSame                                            -> the param records into the edit ring (R8); no plan
(d) d.kind == Params or Same, !busSame                                     -> a KEEP-PATCH plan (R4) + the param records
(e) d.kind == Structural (or the first plan)                               -> a full plan (existing nodes: preparePatch + swap)
```
A param record that does not fit its fixed fields (R8) makes the diff Structural. `chainsSong_` / `chainsBus_` / `chainsFeed_` move forward ONLY when the call returns OK.

**R4 `ChainsHost::plan` gets `bool keepPatch = false`.** With it an existing node gets no prepared patch (its `Trk.patch` stays null: `commit` then only sets its bus map; its module state survives), the budget counts `pb`
(not `2 * pb`) for it; fresh nodes load the document as before; removed nodes as before. Default false: C1's callers and goldens are untouched.

**R5 One plan in flight: E_BUSY.** `collect()` runs first in both calls (it already does) and settles what came back. If `plan()` then returns null with `busyRefused` moved: `THUNDER_LIVE_E_BUSY`, `cmd_busy`+1, NOTHING changed
(the shell retries after the next callback: the contract of every edit). Null for memory: E_MEMORY. A plan that comes back `noop()` (the patch refused, or over budget) is NOT an error: the call succeeds, the song plays with the chains it had
(none: dry), a counter says so (R9), the plan is deleted at once (a noop plan needs no settle) and `chainsSong_` does NOT move (the next publish tries again).

**R6 Order of failure points** (verify-p4 finding 1 stands: the MIDI post is the LAST thing that can fail). In both calls: everything that allocates, then the chains plan, then the rings' room (`cmds_.space()` now needs
one more for the ChainsCommit; the edit ring's room for the param records), then `postMidi()`, then the pushes that cannot fail. Any failure after the plan was made ABANDONS it on the control side: `chains_->settle(p); delete p;`
(uncommitted: the control view stays). No path may leave `inFlight_` set or leak (the bad_alloc sweep proves it).

**R7 Commit points (audio).**
- A PLAY: `PlayPlan` gets `ChainsPlan* chains` and `ChainsHost* chainsHost`. `tryStartPlay` commits it (`chains_->commit(p->chains, *mix_)`) where `busSlot_.adopt() -> setPlan` is today, BEFORE `mix_->load`. `dropPlay` commits it too:
  as with the effects plans, **every plan handed to the audio side is committed**, so the control side's `chainsSong_` is always what the audio side ends with. The plan rides back inside the PlayPlan (`playBack_`);
  `~PlayPlan` (control) does `if (chains) { chainsHost->settle(chains); delete chains; }`.
- A PLAYING publish: `LiveCmdType::ChainsCommit` (`p` = the plan, `at` = NOW always), pushed right AFTER `songSlot_.publish`. `applyOne`: commit, then `chBack_.push(plan)` (`Spsc<ChainsPlan*, 4> chBack_`; room is guaranteed: one
  plan in flight). `collect()`: pop, `settle`, `delete`, count. A ChainsCommit never waits in `pend_` (`at` < 0); `~LiveHost` settles and deletes one found in `cmds_`, in `chBack_`, in a queued / stalled / returned PlayPlan.
  The commit can land one step earlier in the quantum than the song's adoption (step 1 against step 2), or a few quanta earlier when the adoption is held back by a full return ring: accepted (the bus message has the same looseness today).
- **With a chains host the mixer's bus plan is set ONLY by `ChainsHost::commit`.** `busSlot_` messages are still adopted (so they are freed) but NOT applied: a bus plan newer than the nodes' bus maps would hand a node the wrong
  feeds (`feedMismatch`). Both `busSlot_.adopt()` sites (`tryStartPlay`, `adopt`).
- The settle / delete of a returned plan happens only after the pop of the ring it came back through (release on push, acquire on pop): that is the happens-before for `committed_` and the retired nodes (chost.h, THREADED HOST).

**R8 Parameter edits.** `Spsc<LiveChainParam, 256> chainParams_`, `struct LiveChainParam { char id[48]; char name[32]; double v; }` (NUL-terminated; an id or name that does not fit is not representable: the publish path goes
Structural, `chain_edit` answers E_LIMIT). Drained at step 1 AFTER the commands (as `applyFxParams`: a param sent after a commit meets the new patch): for every node `engine().setParam(id, name, v)` (new audio-side
`ChainsHost::param(id, name, v)`; every node holds the whole patch, as the page posts the op to every node). Check that `setParam` allocates nothing for every module family (the allocation guard, R12); if one does, report it, do not work around it.
`int thunder_live_chain_edit(thunder_live* L, const uint8_t* ops_json, size_t n, int64_t at)`: a JSON array of the page's `chnDiff` ops. All or nothing. E_STATE without F_CHAINS or with no `chainsSong_`; `at` must be
THUNDER_LIVE_NOW in this version (else E_ARG; the parameter is kept for the ABI); n above 1 MiB E_LIMIT; any op that is not `param`, an id that is not a module of `chainsSong_`'s patch, a non-finite value, or a module whose type is
`trackIn`, `chainOut`, `scin`, `scout` (their params move the bus plan / the sources: those go through a publish) is E_ARG; more records than the ring has room for is E_BUSY. The shell still publishes the song later; the publish then
finds the same values (case (c): the same params sent again, harmless).

**R9 Stats.** Appended to `thunder_live_stats` (size rule): `chains_nodes`, `chains_blocks`, `chains_failed_blocks`, `chains_events`, `chains_events_dropped`, `chains_events_bad`, `chains_chc_no_node`, `chains_feed_mismatch`
(audio, through `pub_[]`), `chains_plans`, `chains_refused`, `chains_over_budget`, `chains_busy` (control, atomics in `LiveHost`). **`ChainsHost::stats()` returns a copy of a struct both threads write: `LiveHost` must not call it.**
Add audio-side and control-side accessors that read only their own thread's fields (or split the struct in two); the control side's counters that `statsGet` (ANY thread) reads are `LiveHost` atomics.

**R10 Transport, live** (mirror the page's `chainsTempo`, `index.html` around line 23075, and say in the notes what it sends at play, stop and a tempo change). `ChainsHost` gets a `playing_` flag (default TRUE: offline is
untouched and its goldens stay exact) that `applyTransport` uses instead of the literal `true`, and two audio-side calls: `play(double tempo)`: `tp_.tempo = tempo`, the event queue is cleared, `playing_ = true`, the
transport applied to every node (`setTransport`: startN + 1 as it starts playing); `stop()`: the queue cleared, `playing_ = false`, applied to every node. `LiveHost::create` calls `stop()` once (a live host starts not playing).
`tryStartPlay`: commit, then `chains_->play(runner_.seq()->bpm())` (beside `players_->live().tempo`). The Stop command: `chains_->stop()`. HELD and ENDED: follow what the page does for `tp.on` there (read it; say it in the notes).
Nodes stay hooked while IDLE (their tails ring out; the mixer keeps running), as the page's.

**R11 Events.** `LiveHost::dispatch`: `SEV_CHC` -> `chains_->event(e)` and return (no host: counted ignored as today); `SEV_TEMPO` -> `chains_->event(e)` AND on to the Players. `LiveHost::render` calls
`chains_->beginBlock(qStart)` FIRST (the ENGINE frame: the marked hook point; engine quanta start on multiples of 128, so both due rules are exact). A stop and a started play clear the queue (R10): look-ahead events of a Seq that is gone are not applied.

**R12 Real time.** `commit`, `beginBlock`, `event`, `param`, `play`, `stop`, `hook` allocate nothing, lock nothing, free nothing; proven by the existing allocation guard run through `LiveHost::process` with chains on,
including the quantum of a commit and of a swap. Live nodes are made with `EngineOptions.inlineService = false` (`ChainsHost` gets a constructor flag `live`; offline stays true): a spare is never built on the audio thread.
Until C4 nobody services pump modules inside a live chain (documented in `live.h`; the owner's songs have none).

**R13 `plan()` runs while the audio thread plays the nodes it reads.** `preparePatch` is documented const / safe beside `process()` (chains.h). PROVE it: a TSan test with the control thread planning in a loop while the audio thread
renders. Anything TSan reports in `cengine.cpp` (for instance a `lastError_` write from a const call) is reported to the lead, not patched silently.

**R14** `~LiveHost`: the audio thread has stopped; every plan wherever it is gets settled and deleted before `chains_` is destroyed; `chains_` is destroyed AFTER the mixer's hooks can no longer run (the mixer holds raw hooks into it).

**R15** `SongRenderer` is not changed, except what R4 / R10 / R12 add as defaulted arguments. All C1 tests and the 23 goldens must pass unchanged.

## C2a: `chains/cdiff.{h,cpp}` (control side only)
```
namespace thunder {
enum class ChainsDiffKind { Same, Params, Structural };
struct ChainsParamOp { std::string id, name; double v; };
struct ChainsDiff { ChainsDiffKind kind = ChainsDiffKind::Same; std::vector<ChainsParamOp> params; };
ChainsDiff chains_diff(Json prev, Json next);     // the page's chnDiff (index.html, `function chnDiff`), classified
bool bus_plan_equal(const BusPlan& a, const BusPlan& b);
}
```
`Structural` when chnDiff would give ANY op other than `param` (`patch`, `add`, `remove`, `text`, `connect`, `disconnect`), when `prev` is missing / not a `chains.patch` and `next` is, when a param value is not a finite number
(the page compares with `!==`: a string or boolean param that differs is Structural here), or when either document is malformed in a way chnDiff would throw on. `Same` when chnDiff gives `[]`. Both missing: `Same`.
Note `pos` and other fields chnDiff ignores are ignored. Done-test: a Node tool (`native/tools/chains-diff-dump.mjs`) that pulls the REAL `chnDiff` / `chnClone` out of `index.html` (as the other dump tools load page code, with
`--check` against drift) and writes at least 60 golden pairs (the repo songs' patches with single edits of each op kind, the st2 / ti2 rule, the feedback mark, text, duplicate ids, hostile shapes); `ctest chains_diff` must
classify every pair as the page's ops say and give the same param list in the same order. Mutations listed in `docs/port/C2A-NOTES.md`.

## C2b: done when
1. `live_chains`: `LiveHost` (F_CHAINS | F_EFFECTS | F_MASTER) equals `SongRenderer` with `setChains` bit for bit on the golden songs C1's `chains_song` builds (events cases included) and on ELECTRO, for call sizes
   1 / 128 / 333 / 480 / 4096 and frame0 0 / 37 / 2^31; through the C API too (`live_capi` style). Without F_CHAINS: today's output, unchanged.
2. Edits while playing: (i) `chain_edit` of a param, a publish that changes the same param, and a direct `setParam` at that quantum give the same samples; (ii) a structural publish swaps (retired patch freed by `collect()`,
   live-allocation count back to its level); (iii) a CHN switch on another track leaves an untouched node's state (keep-patch: no retired patch; its output equals a run without the switch until the bus plan differs);
   (iv) two structural publishes with no callback between: the second is E_BUSY and host, scheduler and `chainsSong_` are unchanged; (v) play / stop / play: transport and startN as R10; (vi) a refused patch and an over-budget plan: the call succeeds, counters move, sound continues.
3. Threads: a TSan test (audio thread rendering; control thread: structural publishes, param edits, play, stop, collect) clean; R13's test.
4. The allocation guard through `process` with chains (R12); the bad_alloc sweep over `play` and `songPublish` with chains (R6); destroy in every state (R14).
5. A mutation table in `docs/port/C2-NOTES.md` (each seeded fault seen to fail): commit skipped in `dropPlay`; `busSlot_` still applied with a chains host; settle before the pop; `beginBlock` after the mixer;
   the queue not cleared at stop; param drained before the commands; `chainsSong_` moved on a failed call; `keepPatch` ignored.
6. Full Release ctest and the sanitizer subset green locally; CI on the PR decides.

## For the lead to read line by line
Every line of `chost.{h,cpp}` that changes; in `live_host.cpp`: `tryStartPlay`, `dropPlay`, `applyOne` (ChainsCommit), the param drain, `adopt` (the two `busSlot_` sites), `render`, `dispatch`, `collect`, `~LiveHost`,
and the control side of `play` / `songPublish` between the plan and the pushes (R6).
