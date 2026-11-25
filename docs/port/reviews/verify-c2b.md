# verify-c2b: PR #311 "C2: the chains host live" (branch n1/c2-live, head 84e8a9e54291ffa34e773d498786f33726e23b48)

STATUS: PARTIAL (first push). Verifier: independent; the PR's own notes were not trusted. Not yet run: TSan / ASan matrix, mutation table, full ctest (see "What was not checked yet" at the end; this file is updated as they finish).

## VERDICT (provisional): MERGE AFTER FIXES

No real-time-safety, ownership or leak defect found so far: the plan lifecycle (play / publish / chain_edit -> PlayPlan or ChainsCommit -> commit -> playBack_ / chBack_ -> settle + delete after the pop) holds on every path I traced, `inFlight_` cannot stick, `~LiveHost` / `~PlayPlan` order is right, and a 35 s two-thread stress (below) saw 0 audio-thread allocations, 0 bad returns, live allocations back to baseline after destroy.
But four behavioural defects are reproduced by my own tests (not caught by the PR's): findings 1 to 4. None can crash; 1 breaks the "E_BUSY changes nothing" contract (R5), 2 and 3 make the audio side diverge from the song the shell believes it published, 4 leaves the chains' transport stale. All four have small fixes.

## FINDINGS (by severity)

### 1. MEDIUM. A play() that answers E_BUSY because a chains plan is in flight still commits the song's 33 effects plans (R5 / R6 violated)
`LiveHost::play` plans the effects first (live_host.cpp ~419) and the chains plan after (~429). When `chainsDecide` returns E_BUSY, `unwindFx()` pushes every effects plan as a plain `FxCommit` ("plans already made must reach their hosts"): the failed play changes the effects of the host (also while another song is playing), `fxOut_` grows by 33 per attempt (so the NEXT play hits the `fxOut_ + 33 > 64` E_BUSY too), and `cfgOrd_` is consumed. Design R5: "E_BUSY, NOTHING changed".
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

## CHECKED AND FOUND SOUND (evidence in the sections below as they are filled in)
(see "Evidence" - to be completed)

## WHAT WAS NOT CHECKED YET
TSan runs (R13 and the thread test), ASan+UBSan, the mutation table, the full Release ctest, MSVC-safety by compiler (read only), the C API (`live_capi`) beyond a read of live_api.cpp.
