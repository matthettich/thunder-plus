# verify-c2b: PR #311 "C2: the chains host live" (branch n1/c2-live, head 84e8a9e54291ffa34e773d498786f33726e23b48)

STATUS: FINAL. Verifier: independent; the PR's own notes were not trusted. Everything in the task list was run except what "WHAT I COULD NOT CHECK" lists.

## VERDICT: MERGE AFTER FIXES

No real-time-safety, ownership or leak defect found so far: the plan lifecycle (play / publish / chain_edit -> PlayPlan or ChainsCommit -> commit -> playBack_ / chBack_ -> settle + delete after the pop) holds on every path I traced, `inFlight_` cannot stick, `~LiveHost` / `~PlayPlan` order is right, and a 35 s two-thread stress (below) saw 0 audio-thread allocations, 0 bad returns, live allocations back to baseline after destroy.
But four behavioural defects are reproduced by my own tests (not caught by the PR's): findings 1 to 4. None can crash; 1 breaks the "E_BUSY changes nothing" contract (R5), 2 and 3 make the audio side diverge from the song the shell believes it published, 4 leaves the chains' transport stale. All four have small fixes.

## FINDINGS (by severity)

### 1. MEDIUM. A play() that answers E_BUSY because a chains plan is in flight still commits the song's 33 effects plans (R5 / R6 violated)
`LiveHost::play` plans the effects first (live_host.cpp ~419) and the chains plan after (~429). When `chainsDecide` returns E_BUSY, `unwindFx()` pushes every effects plan as a plain `FxCommit` ("plans already made must reach their hosts"): the failed play changes the effects of the host (also while another song is playing), `fxOut_` grows by 33 per attempt (two failed attempts without a collect exceed the `fxOut_ + 33 > 64` limit, so the next play is E_BUSY for that reason too), and `cfgOrd_` is consumed. Design R5: "E_BUSY, NOTHING changed".
Reproduction (test_vadv `det`, D6, F_CHAINS|F_EFFECTS|F_MASTER): song, play (its plan is in flight, no quantum ran), stop, idle publish with a different patch, play -> rc E_BUSY; after 4 quanta the audio side has applied 37 commands (2 expected: play1 + stop; 35 leaked FxCommits).
Fix: make the decision before anything irreversible: at the top of `play()` (beside the other cheap E_BUSY checks) refuse when a plan is in flight and the song would need one, or call `chainsDecide` BEFORE the effects planning and `chainsAbandon` it if the effects planning fails (abandon is clean; `unwindFx` is not).

### 2. MEDIUM. `chain_edit` moves the audio side but not `chainsSong_`: a later publish of the ORIGINAL values finds "Same" and sends nothing
`chainEdit` (live_host.cpp, "chainsSong_ does NOT move") pushes records; the next `chainsDecide` diffs the new song against `chainsSong_`, which does not know the edit. Sequence: song with osc1.freq 330 playing; chain_edit(osc1.freq = 900); shell cancels the gesture and publishes the original song -> diff Same -> nothing sent -> audio stays at 900 while the song says 330 (the "same values again" assumption of live.h holds only when the shell publishes the edited value).
Reproduction: test_vadv `det` D2: 10227 of 10240 samples differ from a host that never saw the edit.
Fix: keep a control-side overlay of the edited (id, name, value) triples since the last publish; at the next `chainsDecide` re-send every overlay entry whose value differs from the new song (or simply: after a chain_edit mark `chainsDirty_` and treat the next Same as Params with ALL params resent; clear on a structural plan).

### 3. LOW-MEDIUM. Param records are not ordered against structural plans in either direction
(a) DETERMINISTIC. A record still in the ring when a ChainsCommit lands is applied AFTER the swap (commands first, then the drain), i.e. an older value overrides the newer patch. Reproduction: test_vadv D1: chain_edit(freq 200), no quantum, structural publish with freq 523 -> 10239 of 10240 samples differ from a host that got only the publish (the audio plays 200, the song says 523; `chainsSong_` thinks 523 so nothing corrects it).
(b) RACE (analysis only, I could not reproduce it deterministically): control pushes the ChainsCommit to `cmds_` and then a record to `chainParams_`; if the audio thread has already left `applyCommands` (cmds_ empty) but has not yet reached `applyChainParams`, it drains the record against the OLD patch (module missing: dropped silently; or present with the old value) and the commit lands one quantum later with the song's values: the record is lost and no later diff resends it (case (c)/(d) of a publish, or chain_edit right after a publish). Window is microseconds per quantum, so rare, but it is silent and permanent.
Fix for both: give every record the number of plans pushed so far (`chPlanSeq_` at push time) and let the audio side keep `plansApplied_` (incremented in `ChainsCommit`, `tryStartPlay`, `dropPlay`); `applyChainParams` applies a record only when `rec.seq <= plansApplied_` (peek the ring head; stop at the first record from the future) and DROPS a record whose seq is lower than the latest plan's (a structural plan carries every value). Or route the records through `cmds_` so ring order is the order.

### 4. LOW-MEDIUM. The chains' transport (`lpb`, `ticks`) is not refreshed when no plan is made
The page's `chainsTempo` sends `{tempo: S.bpm, lpb: S.lpb||4, ticks: S.ticks||TICKS, playing}` at play, stop and after a TICKS / tempo edit. `ChainsHost::play(tempo)` only sets the tempo; `lpb` / `ticks` are whatever the last PLAN carried. A publish (idle or playing) that changes `ticks` or `lpb` with an unchanged chain patch (diff Same, busSame) makes no plan, so the nodes keep the old values.
Reproduction: test_vadv D3: song ticks 6 / lpb 4 played; stop; idle publish ticks 8 / lpb 3 (same patch); play -> node transport ticks 6, lpb 4 (page: 8 / 3).
Fix: `play(tempo, lpb, ticks)` from the Seq's song (or `songCur_`), and in `chainsDecide` count a changed lpb / ticks / bpm as "something differs" -> a transport-only command (or a keep-patch plan, whose `newSong_` already carries it).

## EVIDENCE SO FAR (more is added as runs finish)
- Release ctest subset (build-v, Release, head 84e8a9e5): `chains_song`, `live_threads`, `live_chains`, `live_chains_oom`, `live_chains_threads`, `chains_diff`, `chains_diff_golden_current` all Passed; `rt_harness` run directly: "LiveHost CHAINS: nodes 1, plans 5 ... no allocation in process" (also the C API pass).
- My two-thread stress (`test_vadv thr N`, source in docs/port/reviews/verify-c2b-tests/): audio thread rendering with random call sizes 1..1024 and trackIn/osc/smixer/vca patches, control thread random structural publish (8 song variants incl. CHN flips and ticks/lpb change), chain_edit (valid, removed ids, `_bn` / `_byp`), play, stop, hold, collect, poll, stats. Release 35 s: 436177 calls, 41659 E_BUSY, 0 bad rc, 57.1 M frames, 0 allocations on the audio thread (operator new counter, thread_local flag), live-allocation count back to its baseline after destroy (1 vs 1, with a plan deliberately left in flight at destroy). ThreadSanitizer (RelWithDebInfo, -fsanitize=thread) 40 s: 23075 calls, same result, 0 TSan warnings. `live_chains_threads` x3 under TSan: 0 warnings; `live_threads` under TSan: 0 warnings, 15 checks. R13 (control thread planning while the audio thread renders): no report in `cengine.cpp` or anywhere.
- Reading (item 1, real-time safety): every function the audio thread reaches in the diff allocates / locks / frees nothing: `ChainsHost::commit` (swapPatch returns the old patch, `setBusMap` clears a vector reserved to kMaxFeeds in the engine constructor, cengine.cpp:42), `beginBlock` (memmove in a fixed array), `event`, `play`, `stop`, `param` -> `ChainsEngine::setParam` (cengine.cpp:134: linear id search with string_view compare, `wparam` compares string_view with char*, `repin` resizes `bp` to its own size, `_bn` / `track` run `prepareDerived` on pre-sized scratch), `applyChainParams` (strlen conversion of NUL-terminated char[]), `dispatch`, `songEnded`, `render`, `publish`. `ChainsHost::stats()` is never called from LiveHost (grep: only `audioStats` on the audio side, `ctlStats` on the control side); the two thread-split field sets are disjoint members of one struct (no race by construction; TSan agrees).
- Ownership (item 2): traced every path of a `ChainsPlan`: play -> PlayPlan (`plan->chains`) -> `tryStartPlay` commit or `dropPlay` commit (stalled play replaced by a newer play, Stop, `applyPlay`) -> `giveBack` -> `playBack_` -> `collect()` pop -> `~PlayPlan` (settle, delete); publish -> `ChainsCommit` (`at` = -1, never in `pend_`) -> `applyOne` -> `chBack_` (Spsc<4>, one plan in flight) -> `collect()` pop -> settle -> delete; every failure after `plan()` goes through `chainsAbandon` (settle uncommitted + delete) or `delete plan` (PlayPlan destructor); a noop plan is deleted at once. One plan in flight (`inFlight_`) makes "committed after a newer plan" impossible. `inFlight_` is cleared only by `settle`, which every path reaches (collect, ~PlayPlan, ~LiveHost drain of cmds_, stalled, playBack_, chBack_); D4 (400 random ops) and the thread storm both ended with a structural publish accepted again. `chains_` is declared before `mix_` (destroyed after it) and all plans are settled and deleted in the body of `~LiveHost` (R14).
- R7 (item 4): both `busSlot_.adopt()` sites are `adopt() && !chains_` (live_host.cpp:915, :1025): adopted (freed by collect) and not applied; `Mixer::setPlan` is reached from `ChainsHost::commit` only when a chains host exists. `stats.chains_feed_mismatch` stayed 0 in the storm. One consequence noted: `play()` builds the PDC plan from the song's own `bm->plan` (live_host.cpp ~425) while the mixer keeps the commit's plan; they differ only after a refused / over-budget plan (the call then succeeds with the old chains): cosmetic (latency compensation computed for a bus plan the mixer does not have); not reproduced, not scored.
- Item 5 (`chain_edit`), test_vadv D5 all as designed: E_STATE before the first plan; `at != NOW` E_ARG; NULL E_ARG; n > 1 MiB E_LIMIT; value `1e999` (inf), string value, `"osc1\u0000zz"` (NUL smuggling: compared whole against the module ids, so not found), unknown id (all-or-nothing: the valid first op is not applied), id of trackIn / chainOut, non-param op, non-array: E_ARG; name of 33 bytes E_LIMIT; 256 ops fit exactly (E_BUSY while one record waits, 0 after a quantum), 257 E_LIMIT; a record for a module a swap removed is dropped by `setParam` (harmless); after the swap an edit of that id is E_ARG. A NUL inside a *name* is truncated by the audio side's strlen (acts as the prefix): harmless.
- Item 9 hygiene: no "lxr" in the added lines of native/core (existing hits are the pre-existing U04 seam and pump CMake comments); no unistd / alloca / VLA / big stack array added (largest new local: `ChainsWork`, a few hundred bytes plus BusPlan); only `native/` and `docs/port/` are touched (`git diff --name-only`); ABI: `thunder_live_opts` 56 -> 64 (`chains_bytes` appended after `reserved2`), `thunder_live_stats` 240 -> 320 (13 fields appended after `cmds_no_seq`, 7 x uint64 then 6 x uint32 = 80 bytes), `thunder_live_create` copies through `abiCopyIn` into a zeroed struct (an older caller gets 0 = default budget); `THUNDER_LIVE_ABI` unchanged (appends only), size asserts updated in test_live_h*.

- TSan `test_live_capi` (the C API, its threaded run): rc 0, 0 warnings (it needs > 280 s under TSan).

## MUTATION TABLE (my own seeds, each applied to the head, rebuilt, run, undone with `git checkout`; Release)
| # | seeded fault | live_chains | live_chains_threads | live_chains_oom | live_threads | chains_song |
|---|---|---|---|---|---|---|
| M1 | settle right after the ChainsCommit push (before the pop) | FAIL (iv: the second publish is no longer E_BUSY, patch count 6 not 5, plans 4) | pass | pass | pass | pass |
| M2 | commit skipped in `dropPlay` | FAIL | pass | pass | pass | pass |
| M3 | `busSlot_` applied with a chains host in `adopt()` | FAIL | pass | pass | pass | pass |
| M4 | param drain before `applyCommands` | FAIL | pass | pass | pass | pass |
| M5 | `chainsSong_` / bus / feed moved on the E_BUSY failure of `chainsDecide` | FAIL | pass | pass | pass | pass |
| M6 | queue not cleared in `ChainsHost::stop` | FAIL | pass | pass | pass | pass |
| M7 | `chains_->play()` removed from `tryStartPlay` | FAIL | pass | pass | pass | pass |
| M8 | keep-patch ignored (`plan(..., false)` always) | FAIL | pass | pass | pass | pass |
All eight are caught, all by `live_chains` alone (the other four binaries do not catch any: they are storms and sweeps, not oracles; M1's real race would need TSan, which is where `live_chains_threads` earns its keep). Only the M1 failure text was read (above); for the other seven the failing test is `live_chains` (the binary exits non-zero; ctest without --output-on-failure), the build of each mutated tree succeeded and the unmutated head passes (41 s earlier), so the failure is the mutation's. M3 and M4 are the two the PR's own notes said survived on the first run; they are killed now.

## SANITIZER MATRIX (all at head 84e8a9e5 plus my uncommitted test file)
| binary | TSan (RelWithDebInfo, -fsanitize=thread) | ASan + UBSan (THUNDER_SANITIZE=ON) |
|---|---|---|
| live_chains_threads | 5 runs, 0 reports | 0 reports, 9 checks ok |
| live_threads | 0 reports | not run |
| live_chains | 0 reports | 162 checks ok, 0 reports |
| chains_song (goldens arg) | 0 reports (23 exact, 300 LiveHost runs exact) | 0 reports |
| live_capi | 0 reports | 42 checks ok, 0 reports |
| live_chains_oom | n/a (counting malloc off under sanitizers) | strict +0 ok |
| chains_host | not run | ok |
| test_vadv thr (mine, 30-40 s) | 0 reports, 0 audio-thread allocations, baseline restored | same, 0 reports |
| test_vadv det (mine) | not run | 0 sanitizer reports (its 4 behavioural FAILs are findings 1 to 4) |

## FULL RELEASE CTEST
`cmake --build build-v -j2` (all targets, Release, head 84e8a9e5; my test_vadv is not registered with ctest): `ctest -j2`: 240 tests, 239 passed, 0 failed, 1 skipped (`pumpsynth_refused`, skipped by its own condition), 2410 s.

## SUGGESTED FIXES, in order
1. Finding 1: move the chains decision before the effects planning (or pre-check `inFlight_` at the top of `play()`); add to `live_chains`: "E_BUSY play commits nothing" (cmds_applied delta).
2. Finding 2: overlay of chain_edit values re-sent by the next `chainsDecide`; test = my D2.
3. Finding 3: sequence numbers on the param records against the plan count; test = my D1 (deterministic) and, for the race, a seam between `applyCommands` and `applyChainParams`.
4. Finding 4: transport lpb / ticks (and bpm) carried by play() and by a Same / Params publish; test = my D3.
Findings 1 to 4 each have a deterministic reproduction in docs/port/reviews/verify-c2b-tests/test_vadv.cpp (`test_vadv det` prints D1, D2, D3, D6 and fails exactly those four checks on the head; D4 and D5 pass). None of them needs a design change; none is a crash, a leak or a real-time violation.

## WHAT I COULD NOT CHECK
- Finding 3(b) (the cmds_ / chainParams_ interleaving) is analysis only: I have no seam between `applyCommands` and `applyChainParams` and the engine has no param getter to observe a lost record from outside.
- MSVC: nothing compiled with MSVC (no toolchain here); read only (no unistd, VLA or large stack arrays in the added code; `std::string_view`, `<cmath>` included).
- The page side (`index.html`) was read for `chainsTempo` / `tStop` only; whether the shell actually calls `chain_edit` with the same values as its next publish (the premise of live.h) was not checked; findings 2 and 3(a) are real only when it does not (a cancelled gesture, an edit overtaken by a publish).
- The allocation counts of my stress cover `operator new` only (a direct `malloc` on the audio thread is covered by the PR's `rt_harness` pass, which I ran: 0 allocations, but I did not audit the harness's hook coverage); free() on the audio thread was reviewed by reading only (no counter).
- The PR's mutation notes were not re-run; my 8 mutations are my own (M1..M8 above); only M1's failing assertions were read in detail.
- Bit-equality against SongRenderer (design "done when" 1) was taken from `live_chains` / `chains_song` passing (300 exact LiveHost runs), not re-derived.
- CI on the PR itself.
