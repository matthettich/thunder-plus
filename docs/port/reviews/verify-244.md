# Verify PR #244 (n1/seq-takeover): independent verification

Verifier: Claude Fable 5.2, 19 Nov 2026. Branch `n1/seq-takeover` at e785d14b. No code on the branch changed; scratch tests live outside the repo (not committed).

## Verdict: SOUND WITH CONDITIONS

The hand-over is memory-safe and race-free in everything I could drive (TSan and ASan + UBSan stress, 65 to 70 s each, with growing registries and new PLAYs). The JS equivalence is exact on the builder's 17 goldens and on 9 further cases of mine. One real defect (F1: two PLAYs back to back leaves a dead sequencer) must be fixed before the host wires it; F2 to F4 are test gaps.

## Findings

### F1 (wrong sound: silent sequencer after PLAY + edit in one audio block; BLOCKS the merge)
`native/core/src/rt/snap.h` `adopt(handover)`, `native/core/src/seq/session.h` `seq_adopt`, `native/core/src/seq/seq.cpp` `takeOver` (`old.sess_ != sess_` refusal) and `Seq(..., successor)` (`if (successor_) return;`, no start-up).
PLAY 2 makes a new session B and publishes B1 (its starting Seq). An edit publishes B2, a successor (no start-up: every `Tr.on` false). If the audio side adopts both in one call, `adopt` retires B1 unrun and calls `handover(A_running, B2)`; `takeOver` refuses (other session) and B2 keeps default `Tr`: `alive()` is false, nothing plays.
Reproduction (scratch `twoplay.cpp`, Release): session A, 30 steps; new session B, `publish` twice; `seq_adopt`; then `alive=0 steps=0 events=0`. Control with one B snapshot: `alive=1 steps=50 events=90`.
Fix: when `cur_`'s session differs from the newest's, hand over from the oldest popped snapshot of the newest's session (B1 never ran: its start state is right), or have a refused takeOver of a successor build a started state. Add a test: publish two snapshots of a new session while another session runs.

### F2 (test gap) `native/core/src/seq/seq.cpp:439` `trGen_` carry-over
Mutation: drop `std::memcpy(trGen_, old.trGen_, ...)`. The committed `seq_takeover` PASSES (exit 0, no FAIL) and so do my 9 extra cases. The successor then has `trGen_ = 1` while the chunk generations hold the old values, so an NTH counter would be reset at the first use after an adoption on any track that had `fresh()` (JMP, wake, settle) before. No test has an edit after a track restarted with NTH rows in play. Not a code defect found, but the carry is unprotected.

### F3 (test gap) `seq.cpp` `mtch_` carry-over
Mutation: drop ` mtch_ = old.mtch_;` survives (committed and my cases). The master column's TCH value is run state set by a command.

### F4 (note) the thread test and the hostile test are weak
`test_seq_takeover.cpp` `runThreads`: six songs that differ only by bpm / one deleted phrase and a vars-only message, one session; the registries never grow and no second PLAY happens, so none of the page / chunk / id growth, rollback, or session hand-over runs concurrently. `testHostile` ends with `CHECK(h.seq()->dropped() == 0 || true, ...)` (vacuous). My stress (below) covers what the committed one does not.

### F5 (note) API foot-guns and growth
- `SnapSlot::adopt()` (plain) on a `SnapSlot<SongSnap>` after the first snapshot retires `cur_` while the runner still holds its Seq: dangling. Documented in the notes; nothing prevents it. Consider deleting the overload for `SongSnap` (a `static_assert` / separate type).
- Slots are never reused: one PLAY with many edits that add new NTH / CND / REP rows grows the chunk registry up to 512 chunks x 101 KB = 52 MB worst case (control thread allocates; each Seq keeps a 4 KB table). Pages are bounded at 17 MB. Acceptable, but say so in the notes (the notes give only ELECTRO's 1.26 MB).

## Checks done

1. Thread / memory safety. Reading: registries are touched on the control thread only; each Seq copies the page table and chunk table at construction; the audio thread reads only its own tables and the pages / chunks, which the control thread never touches (except to free the ones a failed publish staged, which no Seq points at); the session is freed after the last snapshot (refcount on the control thread only). `takeOver` from another session is refused (and returns false, nothing copied: but see F1). Stress (scratch `stress.cpp`, not committed): control thread builds random edit patches (replace a whole phrase with random notes and NTH / CND / REP / RND / PRB / RET / ARP / SED rows, chains of 1 to 16 rows, song rows added / cut with `len`, chains / phrases / cells deleted, plen, insts nulled, bpm, perf with up to 700 then 5000 distinct target ids, perf deleted) and publishes; audio thread runs `seq_adopt` + `step` unpaced; whenever the sequencer dies a new PLAY (new session) is published. Results:
   - TSan (Release, `-fsanitize=thread`): 70 s, 249 published, 259 adoptions, 1.07 M steps, 10 new PLAYs, no report; a second run in "fast" mode (300 pre-built songs, publish at full speed): 65 s, 4508 published, 4621 adoptions, 1.57 M steps, 206 new PLAYs, no report.
   - ASan + UBSan (leak check on): 65 s, 772 published, 844 adoptions, 3.99 M steps, 72 new PLAYs; fast mode 65 s, 5606 published, 3916 adoptions, 3.36 M steps, 706 PLAYs; no report, exit 0. Registry reached 1323 ids, 345 slots, 622 pages.
   - Not exercised by me under concurrency: the ring-full rollback path (my publishes never found the ring full; the committed `testSession` covers it single-threaded).
   - Held items I checked by reading: a snapshot replaced before adoption (retired unrun; `takeOver` valid from any earlier Seq of the session); session ended while the audio side holds a Seq (the SongSnap holds a `shared_ptr` to it).
2. takeOver real-time rules. No allocation, lock or unbounded loop in the code (33 `Tr`, arrays up to 4096 ids). Measured here (Release, 2 cores under build load): registry full (4096 ids), mean 1.7 us, worst of 300 runs 18.4 us (builder: worst 5.6 us; the larger figure is machine load); `rt_harness` "seq takeOver: 300 adoptions, worst 6.6 us, no allocation". A 128-frame block at 48 kHz is 2667 us: under 1 percent.
3. Correctness vs the JS. `node tools/seq-golden.mjs --edit --live-only` regenerates all 17 edit goldens and the 4 live goldens: sha256, edits and baseOps byte-identical; the JS path is `core/seq-core.js` `makeSeq` driven by the processor's own `patch` / `bpm` / `loop` handler text (checked equal to core-processor.js in the tool). The goldens exercise: busT (edit-bus: 0 rows differ in the JS, so it proves a negative; mutation M below catches it), hearT / NIs (edit-ntr), bpm (edit-bpm, edit-cmds), macro resting values (edit-perf), perfFirst, instrument 0 (edit-instrument-removed). NOT exercised: an edit of `S.ticks`, `S.lpb`, `S.grv`, `S.dlen` (ruling 1 lists tks / groove / lpb as carried). My 9 extra cases (own JS goldens made by a copy of the tool with other `EDIT_SONGS`; the C++ side is the committed `editCase` pointed at them), all EQUAL with one snapshot per edit and with three published and the last adopted: v-retrig-ahead (RET values changed one step ahead on tracks mid-retrig, three times), v-arp-chain-swap (chains swapped while ARP runs, ARP values changed), v-nth-added (NTH / CND / REP rows added to phrases that are playing, removed and re-added), v-nth-dense (NTH on many rows from the start, loop over rows 0 to 3 for 900 steps, then NTH / CND rows added, a row's command replaced, a chain cell changed: 48 rows differ from the unedited run), v-loop-and-edit (loop set, bpm op and patch in the same block, then loop and chain shortening, a cell cleared), v-sect-edits (sect mode: NTH / CND added, chain row added, row 0 nulled, chain cut, plen cut), v-phrase-edits (phrase mode: NTH / REP added, plen 9 then 4, the phrase deleted), v-song-grows (song rows appended, PRB added; weak: 0 rows differ), v-perf-add-target (new scene ids, macro target, LFO added, scene key deleted: 1745 rows differ).
4. Counter continuity. v-nth-dense and v-nth-added are equal to the JS: existing NTH / CND counters keep their values after rows are added; no slot aliasing seen. Mutation M2 (the slot key aliases rows s and s+16) is caught by my cases (5 FAIL lines) but NOT by the committed test: the committed goldens never put NTH / CND / REP on rows 16 to 31 of a phrase that matters.
5. Mutations (applied to a scratch copy of `seq.cpp`, linked ahead of the library, committed `test_seq_takeover` run; the branch itself untouched):
   | mutation | committed test | my cases |
   |---|---|---|
   | M1 drop `trGen_` carry | SURVIVES (F2) | survives |
   | M2 slot key `pid*32 + (s & 15)` | SURVIVES | caught |
   | M3 drop `hasLoop_ / loopY0_ / loopY1_` | caught (edit-seed-loop row 288) | n/a |
   | M4 drop `pfFill_ / pfPart_` | caught (edit-perf row 40) | n/a |
   | M5 drop `sentOrd_` | caught (edit-perf row 36) | n/a |
   | M6 drop `mtch_` | SURVIVES (F3) | survives |
   | M7 drop `pno_ / pnoHas_` | caught (edit-perf row 60) | n/a |
6. Goldens and non-session path. Release `test_seq` (4 of 4), `test_seq_live` (96 hostile cases), `rt_harness` and `test_seq_takeover` (24 of 24 golden runs through a session, with a takeOver at every step, 17 of 17 + 17 of 17 edit goldens) pass here. The non-session constructor (private one-Seq session) is kept and exact (`test_seq`, `test_seq_live` use it).

## Not done
The 8 per-golden ASan `--quick` run of the committed test (the builder ran it; I ran only my own ASan / TSan stress). No test of `S.ticks / lpb / grv / dlen` edits (the goldens do not cover them; a scratch case would be a quick add).
