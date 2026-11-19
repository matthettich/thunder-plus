# Verify PR #244 (n1/seq-takeover): independent verification

Verifier: Claude Fable 5.2, 19 Nov 2026. Worktree at e785d14b. No code on the branch changed. Version 1 (partial; updated below as work completes).

## Verdict (so far): SOUND WITH CONDITIONS (F1 blocks)

## Findings

### F1 (wrong sound: silent sequencer; blocks the merge)  `native/core/src/rt/snap.h` adopt(handover) + `session.h` seq_adopt + `seq.cpp` takeOver (`old.sess_ != sess_` refusal)
Two PLAYs back to back, the second one followed by an edit inside the same audio block. PLAY 2 makes a new session B and publishes B1 (its starting Seq, with start-up). An edit publishes B2, a *successor* (no start-up: `if (successor_) return;`). If the audio side adopts both in one call, `adopt` retires B1 unrun and calls `handover(A_running, B2)`. `takeOver` refuses (different session), so B2 keeps its default `Tr` (every track `on = false`): `alive()` is false and the sequencer is dead.
Reproduction (scratch, `/home/claude/vb244/scratch/twoplay.cpp`): session A playing 30 steps; session B `publish` twice; `seq_adopt`; result `alive=0 steps=0 events=0`; control with one snapshot: `alive=1 steps=50 events=90`.
Same shape without an edit: any path where a successor of a new session is the newest snapshot adopted while `cur_` belongs to another session. Fix options: in `adopt(handover)` hand over from the oldest popped snapshot of the newest's session when `cur_`'s session differs (B1 never ran, its start-up state is right), or make a refused takeOver of a successor fall back to a started state.
The committed tests miss it: `testSession` checks only that the refusal returns false; the thread test uses one session.

## Verified (so far)
- The 17 `--edit` goldens regenerate byte-identical (sha256, edits, baseOps) from `tools/seq-golden.mjs --edit --live-only` (JS vm path with the processor's own `patch` handler; node only). They are JS-made.
- Release `test_seq`, `test_seq_live`, `test_seq_takeover` pass here (24/24 golden runs, 17/17 + 17/17 edit goldens). takeOver with 4096 ids: mean 1.7 us, worst 18.4 us of 300 (builder: 5.6 us; the 2667 us block).
- Weak points seen in the tests: `testHostile` ends with `CHECK(... || true)` (vacuous); the thread test uses six songs that differ only in bpm / a deleted phrase, so registries never grow under it.

## Still to do
TSan / ASan stress with growing registries, extra JS comparisons (5 of my own), counter continuity, 4 mutations, 8 goldens.
