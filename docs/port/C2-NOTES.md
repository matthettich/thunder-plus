# C2-NOTES: the chains host LIVE (unit C2b, branch `n1/c2-live`)

Spec: `docs/port/C2-DESIGN.md` (R1..R15, "C2b: done when"). Status is at the top; it is updated with every push.

## STATUS
**DONE** (pending the lead's review; full ctest result in "Tests run" below). Stage 1, stage 2 (C2a merged twice: cf5e9b7, then 37163b0 with the `-fexceptions` fix) and stage 3 are finished. Nothing is left to do except the PR review.
- Code: `native/core/src/chains/chost.{h,cpp}` (`keepPatch`, `live`, `play`, `stop`, `param`, thread-split stats), `native/core/src/live/live_host.{h,cpp}` (`chainsDecide` / `chainsFinish` / `chainsAbandon` / `chainEdit` / `applyChainParams`, `ChainsCommit`, `chBack_`, `PlayPlan::chains`, `tryStartPlay`, `dropPlay`, `adopt`, `render`, `dispatch`, `songEnded`, `collect`, `~LiveHost`), `live.h` / `live_api.cpp` (F_CHAINS, CAP_CHAINS, `chains_bytes`, stats, `thunder_live_chain_edit`).
- Tests (all new files registered in `native/tests/CMakeLists.txt`): `live_chains` (`test_live_chains.cpp`), `live_chains_oom`, `live_chains_threads`, `chains_song` level 3 (LiveHost vs the goldens, 300 runs), `rt_harness` pass 4, size asserts of `test_live_h*.`

## Round 2 (the lead's line-by-line read, PR #311 on 84e8a9e)
- **L1 (done)**: `Spsc::avail()` (consumer snapshot, `rt/spsc.h`); `quantum()` takes `nChainParams = chainParams_.avail()` at the top of step 1, BEFORE `retryStalledPlay` / `applyCommands`, and `applyChainParams(n)` pops at most n. Reason (in the code comment): the producer pushes a commit before the records that follow it, so a record in the snapshot has its commit visible to the command loop that runs after the snapshot. The `!stalledPlay_` rule stays. Test seam `LiveHost::setTestCommandSeam` (between the command loop and the drain); `live_chains`: the seam publishes a structural change and then `chain_edit`s: the output equals the run where both were sent before the next quantum; plus an `avail()` unit check (there is no separate Spsc test file: it is in `live_chains`). Mutation **M9 "drain everything"** (`applyChainParams(0xFFFFFFFF)`): KILLED by `live_chains`.
- **L2 (done)**: in `LiveHost::play` the chains decision is now made BEFORE the effects plans (after the session / snapshot / PlayPlan / bus message and the top-of-function room checks): a refusal (E_BUSY / E_MEMORY) returns with nothing sent. A failure of the effects planning or of the PDC plan calls `chainsAbandon(cw)` beside `unwindFx()`. `cw.plan` is handed to the PlayPlan right before the Play command is pushed. This amends R6 for `play()`. `live_chains`: a play refused E_BUSY by a plan in flight (with F_EFFECTS | F_MASTER) leaves stats and the command ring untouched; the run with the refused call equals the run without it.
- **L3 (done)**: `live.h`, `thunder_live_chain_edit`: the shell's duty (the next publish must carry the values last sent by `chain_edit`; a publish with the OLD value is seen as "no change" and the edited value stays) and the `_bn` / `track` text (allocation-free `prepareDerived`).
- **L4a**: confirmed by tests in `live_chains` (transport section): a publish while HELD, and one after ENDED -> IDLE, make no plan; the following play makes it (plans + 1), the node runs the new patch and the same song published again makes no plan (`chainsSong_` matches).
- **L4b**: `bus_plan_equal(BusPlan(), bus_plan(<song with no chainPatch>))` is true (test line). Finding while testing it: `chainsDecide` compared the CHN flags with an all-false record when nothing was ever planned, so a song with no chains but `chainOn` flags set made an (empty) plan at its first play. Fixed: the flags are compared only once a node was planned (`chainsPlanned_`); before that `chains_feed` and the bus plan decide. `live_chains` `testNoChains`: no plan, no node, no refusal for such a song over play + 2 publishes.
- **L4c (report only)**: `ChainsHost::commit` with `p->newSong_` (the song's bpm / lpb / ticks differ from the previous plan's: `haveLast_ && !(song == lastSong_)`) sets `tp_ = p->song_` (tempo, lpb, ticks of the song) and re-applies the transport to every node. So a tempo that a SEV_TEMPO / TPO event had set is OVERWRITTEN by the song's bpm until the next SEV_TEMPO. That is what the page's `chainsTempo` does on a bpm edit (it sends the song's tempo); the sequencer's tempo consumer is seeded and emits only on a change, so a TPO row playing later re-sends its value. A publish that changes only the chain patch (not bpm / lpb / ticks) does not touch `tp_`. No change made; not checked against a page run that mixes a TPO command with a bpm edit.

## Departures from / decisions beyond the design (all small)
1. `ChainsHost::play` goes through not-playing first when already playing (the page's `tStart` calls `tStop(true)` first), so `startN` bumps once per play even over a play.
2. The param-record drain is skipped while `stalledPlay_` is set (a record applied before the stalled play's plan is committed would be lost).
3. `chain_edit`: more than 256 ops is `E_LIMIT` (it can never fit), not `E_BUSY`; `E_BUSY` is only "no room now". `E_STATE` before the first plan (`chainsSong_` empty) and without F_CHAINS.
4. `chainsDecide` turns a Params diff into a full plan when a record does not fit its fixed fields (id 47 / name 31 bytes) or there are more than 256.
5. ENDED -> IDLE (the 0.3 s timer) calls `chains_->stop()`; HELD does not (the page's `tp.on` stays true until the stop). A tempo edit needs no call of its own (SEV_TEMPO event).
6. `chains_plans` counts plans handed to the audio side.
7. A test seam (`setTestTap`, `chainsHostForTest`, `mixerForTest`) exists in `LiveHost` (test only, one null-pointer test per quantum).
8. `native/tests/CMakeLists.txt`: `test_chains_song` links `Threads::Threads` and gets `THUNDER_SANITIZE_BUILD` under THUNDER_SANITIZE.

## ABI
`THUNDER_LIVE_ABI` is left at 1. `thunder_live_opts` (56 -> 64, `chains_bytes`) and `thunder_live_stats` (240 -> 320) grew only at the END and both follow the header's own `size` rule (an older caller's struct is read / written to its own size: checked in `live_chains`, C API section). New function `thunder_live_chain_edit`, new flag F_CHAINS (64), new cap CAP_CHAINS (0x200). Nothing else changed.

## Transport (R10), what is sent when
Mirrors the page's `chainsTempo` (`{tempo, lpb, ticks, playing: tp.on}`): PLAY: `ChainsHost::play(tempo of the Seq)` after the tempo seed (not-playing first if playing, queue cleared, then playing); STOP command: `stop()` (queue cleared); ENDED -> IDLE after 0.3 s: `stop()`; HELD: nothing (keeps playing until the stop); tempo change: SEV_TEMPO through the sequencer to `event()`; BYP / CHC: SEV_CHC. Created not playing (`stop()` at create), so a host that never plays has idle nodes.

## Mutation table (each seeded fault was seen to FAIL a test; run on the final tests)
| fault | caught by |
|---|---|
| M1 commit skipped in `dropPlay` | `live_chains` (play + play + stop + play with a plan: the node must end with S1's patch) |
| M2 `busSlot_` applied in `tryStartPlay` with a chains host | `live_chains` (a refused / over-budget play leaves the mixer's bus plan alone) |
| M2b `busSlot_` applied in `adopt` with a chains host | `live_chains` (refused patch with another bus plan while playing) |
| M3 settle before the pop (control settles a plan it just handed over) | `live_chains` (plus TSan for the real race) |
| M4 `beginBlock` after the mixer | `live_chains`, `chains_song` |
| M5 queue not cleared at stop | `live_chains` |
| M5b queue not cleared at play | `live_chains` |
| M6 param drained before the commands | `live_chains` (chain_edit right after a structural publish) |
| M7 `chainsSong_` moved on a failed (E_BUSY) call | `live_chains` |
| M8 `keepPatch` ignored | `live_chains` (node / patch pointer kept, state kept) |
First run: M1, M2, M6 SURVIVED; tests were added for them (the two blocks before "the mixer's bus plan is the commit's alone" in `testEdits`, and the check in `testRefused`) and all three were then killed.

## Tests run (see the final report for the head sha)
Release, `native/build-c2b`: `ctest` full (result at the bottom of this section). ThreadSanitizer (manual `-fsanitize=thread` configure, RelWithDebInfo): `live_chains_threads` x4 and `live_threads`: no report. ASan + UBSan (`THUNDER_SANITIZE`): `live_chains`, `live_chains_threads`, `live_chains_oom`, `chains_host`, `live_capi`: clean (`chains_song` under ASan was NOT run: the sanitizer build was removed before it was run with its case-file argument; Release is green). rt_harness (allocation guard) pass 4: no allocation in process with plans, commits, param records and chain_edit. `live_chains_oom`: strict +0 leak sweep over play, structural / keep-patch / param publish, destroy in every state (R14), and no allocation in `process()` around `_bn` / `_byp` / param records / commits (setParam of `_bn` and `track` re-derives with no allocation: confirmed under the counting malloc).

## For the lead
1. (FIXED upstream) C2a `chains_diff` leaked on `std::bad_alloc`: cause `cdiff.cpp` missing from the `-fexceptions` list; fixed in 37163b0 and merged here. `live_chains_oom` is now strict (+0). No new .cpp was added under `native/core/src` by this unit, so nothing new needs the flag.
2. `git fetch --depth=N` on this clone writes a shallow boundary that makes the next merge fail with "unrelated histories"; I had to `git fetch --unshallow origin`. Not a code issue; builders should fetch without --depth.
3. The engine's known `preparePatch` bad_alloc leak (U31) was not met by `live_chains_oom`'s patches (allowance 0).
