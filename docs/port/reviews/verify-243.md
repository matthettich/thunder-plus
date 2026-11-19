# Independent verification of PR #243 (Seq clock setters and SeqRunner), merged as 956c377

Verifier: independent, did not build it. Nothing on `native` was changed. Build: Release + ASan + UBSan, targets `test_seq`, `test_seq_live`, `rt_harness` only.

## Verdict: SOUND WITH CONDITIONS

`SeqRunner::pump` equals the JS pump in 78 of 78 differential scenarios (below); the goldens are real JS output and regenerate byte-identically. The conditions are two test gaps (F3) and two documented, JS-faithful limits (F1, F2) the owner should know about. No defect in the shipped code was found that needs a fix before the next unit.

Committed gates re-run here, all green under ASan + UBSan: `test_seq_live` (4 of 4 live goldens, 96 hostile cases, runner tests), `test_seq` (4 of 4), `rt_harness` (allocation guard).

## 1. The goldens

- `node tools/seq-golden.mjs --live-only` -> the four core-path proofs ok (1792 / 1504 / 800 / 1792 rows equal the page-made goldens' sha256 and row counts) and the four live cases ok.
- `node tools/seq-golden.mjs --live-only --update` rewrote all four live goldens; `git status` afterwards showed no change: byte-identical regeneration.
- The live goldens are produced by `core/seq-core.js` `makeSeq` in a Node `vm`, the audio thread's own copy, with the clock written by the processor's own statements. They are JS output, not hand-made. The proof compares the full sha256 plus row count of each old page-made golden, which covers every row; the comparison is not partial.
- Things the gate does not do (all Low, no action required): `--no-live` skips the live cases and the proof silently with exit 0; a missing live golden file is written and reported ok by the mjs (ctest `seq_live` still fails on a missing file, so CI is covered); the mjs compares only the sha256, the `live` ops block in the golden is read by C++ and is not cross-checked against `LIVE_SONGS` (a change to the ops changes the sha, so it cannot drift silently).
- The goldens use `once` + `sim` and not `live: true`. The processor uses `live: true` (`wakeOk` depends on it). Covered by the differential below.

## 2. `pump` against the JS, statement by statement, plus a differential

Read statement by statement: skip condition and shift, `n++ < 3`, edge bookkeeping (`lr` set only when `row >= 0`; first step never an edge; `pos` empty leaves `lr`/`ls` alone), held drops the sequencer and clears hold, `ended` only when not held and reported once, `start()` resets hold/lr/ls/ended and keeps skips. No ordering difference found.

Differential (scratch, not committed; `/home/claude/scratch243` on the verifier box): `ref.mjs` extracts the real text of `core/core-processor.js` lines 774-795 and runs it unchanged (stubs for `ev`, `stopAll`, `port`) against `core/seq-core.js` `makeSeq` with `live: true, sim: true`; `drv.cpp` runs the same scenario through `SeqRunner` + `Seq`. Scenario: 400-1300 blocks of random `now` (mostly 128 frames; zero steps, 0.3 s and 2 s jumps, backwards jumps, negative `now`), random `lead` in {0.25, 0.6, 1.2, 0, -0.1, 0.001, 0.05}, random `hold`, `bpm`, `stop`, `play` (t0 ahead of and behind `now`). Ten song/sect/phrase modes on ELECTRO (loops, `row` 9, rows 54 and 55 so the song ends). Trace per step: time, sha256 of the event row, bpm, all positions; per block: time, bpm, base, btT, alive, skips; held, end.

Result: 78 scenarios, identical traces; 252 holds, 12 ends, skips up to 20. Under ASan + UBSan, no report.

Cases asked about, answered:
- `pos` empty / no valid track: `lr`, `ls` untouched, as the JS.
- The block after `held`: runner has no sequencer, does nothing; `hold()` while empty arms the flag, `start()` clears it (JS same).
- `stop()` then `start()` with the same finished Seq: reports `ended` once more. The JS cannot do this (`play` always makes a new sequencer); harmless (see F6).
- Hold armed before the first step: the first step is never an edge (as the JS, `lr == null`). Scratch test T1 ok.
- `cap` smaller than the step's events: see F4.
- `lead` <= 0 or negative `now`: no step, no skip; correct and equal to the JS (T6, and in the differential). Infinite `now`: F5.

## 3. Own tests (scratch `mytests.cpp`, ASan + UBSan, all pass)

- T1 skip while hold armed (also hold armed before the first step): the skip does not make an edge; hold fires at the row change after 16 played steps.
- T2 `setBpm` between two steps of one pump (called from inside `onStep`): equal to direct stepping with the same call; spacing after the change uses the old line once, then the new one (as the JS, which reads `bpm` at the top of `step`). Cross-thread use would be a data race (plain double, no atomic); the host contract must keep it on the audio thread.
- T3 `shift` before the first step then `setTime` (setTime wins, first step at t0, base = t0); `setTime` then `shift` (first step t0 + d, base and btT agree); `setTime` after the first step: F1.
- T4 sect and phrase mode through the runner: phrase mode loop 2..5 with hold: steps 2..5 play, the wrapped step (6 -> 2) is held; correct. Sect mode, chain of 3 rows of 16 steps: hold at the first phrase wrap after 16 steps. Sect mode with 1-step phrases: never held: F2. The differential also covers sect and phrase on ELECTRO.
- T5 cap 1 and cap 0 / null buffer: no overrun, `n == cap`, drops counted: F4.
- T6, T7: lead/now and stop/start, above.

The edge rule (highest `row`, highest `step`) is faithful to the JS. In phrase mode `row` is constant, so the edge is exactly the step wrap or loop wrap: correct for loops.

## 4. Mutations of my own (not in the builder's table)

| mutation | committed tests | differential |
|---|---|---|
| M7 `hold_` not cleared on held | FAIL (caught) | n/a |
| M1 drop `hasBase()` from the skip condition (skip before the first step) | PASS (not caught) | caught, 41 of 41 |
| M6 `sp < ls_` -> `sp <= ls_` | PASS (not caught) | not caught on ELECTRO; changes only a one-row, one-step-phrase loop (confirmed by T4b / a scratch case) |

All mutations were reverted; the worktree is clean.

## Findings

| id | sev | where | what | follow-up PR? |
|---|---|---|---|---|
| F1 | Low | `seq.cpp` `Seq::setTime`; `seq.h:130` | `setTime` after the first step does not change any step time (step time = `base_ + rel0`); it only moves `time_`, which the runner gates on. `setTime(1e6)` mid-play stalls the runner. Same as the JS (`time` is rewritten after each step). Repro: T3c in the scratch tests. The header says "before the first step"; the API does not enforce it. | No code. One doc line in SEQ-LIVE-NOTES or a host-side rule: never call `setTime` on a running Seq (use `shift`). |
| F2 | Low (JS-faithful) | `runner.h:55-57` | The edge test ignores `cr` and looks only at (highest row, highest step going down). Sect mode with 1-step phrases, or a song with one row and a 1-step phrase looping, can never be held: EXACT STOP never fires, the song plays on. Repro: sect mode, `chains [[0,0],[0,0],[0,0]]`, `plen [1]`, `hold()` -> 67 steps played, no held (T4b). It is what the JS does; the port must not fix it silently. | No for this PR; owner decision, a JS change first (include `cr` and `phrase`), then the port. |
| F3 | Medium-Low (test gap) | `native/tests/test_seq_live.cpp` | Two runner mutations survive the committed tests: (M1) skipping before the first step. This is realistic: the page sends `t0` slightly behind `currentTime`, and the JS does not skip until the first step has set the base. (M6) `<=` in the step-wrap edge. | Yes, small test-only PR: (a) a Seq with `t0 = now - 1.0` never stepped: first `pump` must give `skipped == false`, `skips() == 0`, first step at `t0`; (b) one-row, 1-step-phrase loop with hold armed stays unheld, pinning F2 as intended. |
| F4 | Low | `runner.h:53-64` | With `cap` smaller than a step's events the step is truncated (`step.dropped > 0`, `Seq::dropped()`), and `onStep` still gets a short list; `SeqPump` has no `dropped` field. A dropped OFF hangs a live note. Host must pass `kSeqStepEvents` (12288); the runner does not enforce it. | Optional: add `dropped` to `SeqPump`, or a host-side assert of `cap >= kSeqStepEvents`. |
| F5 | Low (pathological) | `runner.h:50` | `now = +inf` or beyond ~1e308: `skips_++` and `skipped = true` fire although `shift` refused the result (counted in `badSets`), then 3 steps play at the stale time every block. Not reachable from a real audio clock. | Optional: count the skip only if `shift` took effect, or ignore a non-finite `now`. |
| F6 | Info | `runner.h:36-38` | `stop()` leaves `ended_`/`hold_` as the JS does; `start()` with the same finished Seq reports `ended` again. Fine. | No |
| F7 | Info | goldens | Goldens cover `once` + `sim`, not `live: true`; the 78-scenario differential did cover `live: true`, all equal. | No |

Not tested: `chaseRun` / RESUME (not in this PR); the hold's engine/voice side (host).
