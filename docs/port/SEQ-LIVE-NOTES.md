# SEQ-LIVE NOTES (Part A of SEQ-LIVE-DESIGN.md; branch n1/seq-live)

**IN THE MIDDLE OF: full Release `ctest` (all gates written, mutated and green; the sanitizer run is clean); then the PR and CI.**

## What is here
- `Seq` (native/core/src/seq/seq.h, seq.cpp): `setTime`, `setBpm`, `shift`, `hasBase`, `base`, `hasBtT`, `btT`, `bt`, `badSets`; `kSeqBpmMin = 1e-3`, `kSeqBpmMax = 1e6`.
- `SeqRunner` (native/core/src/seq/runner.h, header-only): `start(Seq*)`, `hold()`, `stop()`, `pump(now, lead, evs, cap, stepOut, onStep)` -> `SeqPump {steps, skipped, held, ended, heldT}`, `heldPos()`, `skips()`, `ended()`, `holdArmed()`, `seq()`.
- JS goldens: `tools/seq-golden.mjs` (live cases), `native/tests/golden/seq-electro-live-{time,bpm,shift}.json`, `seq-electro-cmds-live-all.json`.
- Tests: `native/tests/test_seq_live.cpp` (ctest `seq_live`), `rt_harness.cpp` `run_seq_runner` (ctest `rt_core`).
- `SongRenderer::render` is untouched; `seq`, `seq_dump_current`, `seq_extra` and `render` outputs are unchanged.

## The open question: where does the JS reset `skips`?
Nowhere. `this.skips` appears only at core-processor.js :357 (stats), :778 (`this.skips = (this.skips || 0) + 1`) and :792 (sent with every `rows`). `play()` does not touch it, so it counts for the processor's whole life. `SeqRunner::start` keeps it, as the design said. (The page can tell a play's skips by differencing.)

## The live goldens: how they are made (and why not by the page)
The page's `makeSeq` is not reachable from outside index.html (it is inside a scope, not a global: `typeof makeSeq` is `undefined` in the page) and the page's `__seqTest.run` takes no clock operations. index.html may not change here. So the live cases run **core/seq-core.js's `makeSeq`** (the audio thread's copy) in a Node `vm`, the same loader as native/tools/seq-dump.mjs, and `tools/seq-golden.mjs` first **proves that path**: the four existing cases run through it and their sha256 and row counts must equal the committed goldens (made by the page in Chrome); if one differs, the live cases are neither written nor compared. Output of the proof, in this branch:

    ok   core-path proof electro: 1792 rows = the page's golden
    ok   core-path proof electro-mid: 1504 rows = the page's golden
    ok   core-path proof electro-loop: 800 rows = the page's golden
    ok   core-path proof electro-cmds: 1792 rows = the page's golden

(Chrome IS available here; `node tools/seq-golden.mjs` also ran the page path for the four existing goldens: all ok, files untouched.) The clock is written between steps with the processor's own statements (`time = t0`; `bpm = v`; `base += d; time += d; if (btT != null) btT += d`). Each live row is `{t, evs, clk: {time, base, bt, btT, bpm}}`, `clk` read after the step and before the ops, so the clock itself is gated, not only the events. Cases: `time = 1234.5678` before step 1; bpm 97.3 after step 40 and the song's 131 after step 120; shift +0.37 after step 25 and -0.011 after step 60; all of them on electro-cmds. `node tools/seq-golden.mjs --live-only` runs just these; `--no-live` skips them; `--update` rewrites them.
Goldens are in `native/tests/golden/` (not `tests/golden/`): the rule that only `tools/seq-golden.mjs` and `docs/port/` may change outside `native/` forbids new files in `tests/golden/`, and the design names `native/tests/golden/`.

## Gates and mutation checks (one per gate; each was broken, seen to fail, and restored)
| gate | mutation | result |
|---|---|---|
| live goldens: shift | `shift` leaves `btT_` alone | FAIL electro-live-shift and -cmds-live-all, first bad row 25 |
| live goldens: bpm | `setBpm` ignored | FAIL electro-live-bpm, -cmds-live-all, first bad row 40 |
| live goldens: time | `setTime` ignored | FAIL electro-live-time, -cmds-live-all, row 0 |
| runner: skip not at equality | `<` -> `<=` | FAIL "no skip at time == now - 0.03" |
| runner: at most 3 steps / later blocks | `kRunMaxSteps = 4` | FAIL "lead 1.0 s: 3, 3, 2, 0, 0" (got 4 4 0 0 0) |
| runner: hold at a step wrap | drop `sp < ls` | FAIL step-wrap case |
| runner: hold at a row change | drop `row != lr` | FAIL the plen-1 case (a row change where the step does not go down; a 16-step phrase cannot see this mutation, the step wrap catches it) |
| runner: held step never played | no `break` | FAIL (18 played, the held step reached onStep) |
| runner: `ended` once | drop `!ended_` | FAIL (ended 390 times) |
| runner: `stop()` | `stop()` no-op | FAIL |
| runner: `start()` resets hold | `start()` keeps hold | FAIL |
| runner: block sizes 1 / 128 / 4096 | skip threshold -0.2 (frame-dependent) | FAIL at 4096 frames (skips 15) and the skip tests |
| allocation guard | `new std::vector` in `pump`; `operator new` in `shift` | rt_harness aborts (exit 134) |
| hostile | remove the bpm clamp | FAIL (bpm 1e300 -> state not finite) |
| hostile | remove `setBpm`'s / `setTime`'s finite check | FAIL (refusal not counted) |
| hostile | remove `shift`'s overflow check | FAIL (second `DBL_MAX` shift) |
| hostile | remove only `shift`'s first `isfinite(d)` | PASS: not a gap, the overflow check catches a NaN / inf `d` as well (the result is not finite); the two checks are redundant on purpose |

## Hostile values
`test_seq_live` runs NaN, +-inf, -1, 0, +-1e300, +-DBL_MAX, DBL_MIN, 5e-324, 1e-9, 1e9, 1e-300, +-1e15 into each setter, before the first step and after 40 steps, then 600 runner blocks and 300 direct steps: at most 3 steps a block, state finite (a finite time or shift of 1e300 is accepted, as a finite number; everything else is checked), refusals counted. Then bpm 1e-9, 1e9, 1e-300, DBL_MAX for 5000 direct steps each. Clean under ASan + UBSan (`-DTHUNDER_SANITIZE=ON`, `test_seq_live` and `rt_harness`).
**Can a tiny or huge bpm hang `step()`?** No: a step is bounded work whatever the bpm (the only loops are over tracks, rows and the 3-step bound is the runner's). What an unclamped bpm does is make the state non-finite (bpm 1e300: `bt` and the line overflow), so `setBpm` clamps to [1e-3, 1e6] (a clamp counts in `badSets`, like a refusal). No real tempo is anywhere near either end (the JS UI: 20-300).

## Choices for the lead
1. **Live goldens come from core/seq-core.js in Node, not the page** (reasons and proof above). If you want the page's own `makeSeq`, index.html would need a `__seqTest` hook that takes clock ops (not allowed in this PR).
2. **Golden location**: `native/tests/golden/` (see above), not `tests/golden/`.
3. **`setTime` and `shift` also refuse non-finite values** (counted in `badSets`), and `shift` refuses a result that overflows; the design listed that only for `setBpm` and `shift`'s `d`. The JS would take them and make NaN times.
4. **`setBpm` clamps** to [`kSeqBpmMin`, `kSeqBpmMax`] = [1e-3, 1e6] (the design: "if it can [hang], clamp"; it cannot hang but goes non-finite). A clamp is counted in `badSets`.
5. **`shift` with no base moves `time_` only** as designed; with a base but no `btT` (cannot happen after a step) it moves base and time. `btT` is moved only when set, as the JS.
6. **`skips` is never reset** (JS), so `SeqRunner::start` keeps it; `skips()` is for the runner's whole life.
7. **`hold()` on a runner with no sequencer** just sets the flag, as the JS `hold` message does; `start()` clears it.
8. **The held step's positions** are kept in the runner (`heldPos()`), and the step itself is in the caller's `SeqStep` (its events are in the caller's buffer but were not handed to `onStep`); the host does what the JS does with `held` (stop every voice at `heldT`, tell the page).
9. **`pump` takes the callable as a template parameter** (no `std::function`: allocation-free). `stepOut` may be null (a local `SeqStep` is used).
10. The runner does not clamp or check `now` / `lead`: a NaN `now` makes both tests false, so nothing steps; an infinite lead is still bounded by the 3 steps.
11. The runner does not call `Seq::setLoop` or anything of PERFORM; only the clock.

## Parked
Nothing.
