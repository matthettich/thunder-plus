# SEQ LIVE DESIGN: the sequencer on a live clock (WIRING-DOSSIER D1, R1) and the chop ruling

Written by the lead, 19 Nov 2026, 08:50. Queue items 1 and 3 of PICKUP.md's START HERE (19 Nov, 07:30). Two PRs, in this order: A then B.
JS lines are `core/core-processor.js` of this checkout.

## Part A (PR 1, branch `n1/seq-live`): clock setters and the block runner

### A1. `Seq` gets the JS api's writable members (audio thread, no allocation)
| C++ | JS | Rule |
|---|---|---|
| `void setTime(double t)` | `seq.time = d.t0` (:422, :423) | sets `time_` only. Before the first `step()` the base is unset and `step()` takes it from `time_`, as the JS (`api.base == null`). |
| `void setBpm(double v)` | `seq.bpm = d.v` (:354) | sets `bpm_`; heard from the next `step()` (it reads `nb = api.bpm` at its top). **Deliberate difference:** a value that is not finite or not > 0 is ignored and counted (`badSets()`); the JS would take it and produce NaN times. |
| `void shift(double d)` | `base += d; time += d; if (btT != null) btT += d` (:421, :778) | with no base yet (never stepped) it moves `time_` only. `d` not finite: ignored and counted. |
| `bool hasBase()`, `double base()`, `bool hasBtT()`, `double btT()`, `double bt()` | the `rows` message's `base`, `btT` (:792) | getters. |

### A2. `SeqRunner` (`native/core/src/seq/runner.h`, header-only or with a .cpp): `process1`'s part 1 (:778-794) and the `play` / `hold` / `stop` state, statement for statement
- `start(Seq*)`: `hold = false`, `lr` / `ls` unset, `ended = false`, `skips` kept as the JS keeps it (check the JS: where is `this.skips` reset?).
- `hold()`: EXACT STOP armed. `stop()`: no sequencer.
- `pump(now, lead, evs, cap, stepOut, onStep)`, once per host block:
  1. catch-up skip: `alive && hasBase && time < now - 0.03` -> `skips++`, `shift(now + 0.02 - time)`;
  2. `while (alive && time < now + lead && n++ < 3)`: `step()`; the pattern-edge test on the highest `pos.row` and highest `pos.step` (`lr`, `ls`); at an edge with hold armed the step is NOT played: the runner reports `held {t, pos}` and drops the sequencer; else `onStep(evs, n, step)`;
  3. reports `ended` exactly once when the sequencer is no longer alive and nothing was held.
- The same constants as the JS (0.03, 0.02, 3). `lead` is the host's (0.25 s default, 0.6 s after PLAY, up to 1.2 s hidden).
- `SongRenderer::render` keeps its own loop in this PR (an offline render has no clock to fall behind): its outputs must stay bit-identical.
- RESUME's replay (`chaseRun`, :413-422) is NOT in this PR; only `shift`, which it needs.

### A3. Gates
- `tools/seq-golden.mjs` gets live-clock cases MADE BY THE JS `makeSeq` (never by hand): (1) `time = 1234.5678` before the first step; (2) `bpm` set to 97.3 after step 40 and back to the song's after step 120; (3) `shift(+0.37)` after step 25 (the skip) and `shift(-0.011)` after step 60; (4) all three together on `electro-cmds`. Committed as `native/tests/golden/seq-*-live*.json`; ctest `seq` (or a new `seq_live`) meets them exactly, like the existing four. The existing goldens stay byte-identical.
- `SeqRunner` unit tests with a hand-built song: the skip fires at `time < now - 0.03` and not at equality; at most 3 steps a block; with a long lead the rest follows in later blocks; hold ends at a row change AND at a step wrap on the same row, the held step's events never reach `onStep`; `ended` once; `stop()` then `pump` does nothing. Block sizes 1, 128, 4096 frames give the same event list (the runner sees times only).
- `rt_harness`'s allocation guard around `pump`, `setTime`, `setBpm`, `shift`: zero allocations.
- Hostile: NaN / inf / negative / 0 / 1e300 to each setter under ASan + UBSan; the sequencer stays finite and bounded (no step loop without end: note the `n++ < 3` bound is the runner's, add a test that a `bpm` of 1e-9 or 1e9 cannot hang `step()`; if it can, clamp in `setBpm` and say so in the notes).

## Part B (PR 2, after A is merged): the Song's lifetime under a running Seq (R1)

**The problem.** The JS edits `S` in place and the running sequencer reads it (`song` :328, `patch` :329; `S.perf` keeps its identity). The C++ `Seq` holds `const Song&`; `SnapSlot::adopt` retires the old Song, so the Seq dangles.

**The lead's decision: the Seq is published WITH its Song, and takes the running state over on the audio thread.**
- The control thread builds `SongSnap { Song song; SeqVars vars; std::unique_ptr<Seq> seq; }` for every song / patch while a sequencer runs (the Seq built with the running mode and opts, in "successor" mode: see counters), and publishes it through the one `SnapSlot`. One object, one lifetime: a Seq can never outlive its Song.
- Audio thread, on `adopt()`: `next.seq->takeOver(*cur.seq)`, then the old snapshot goes back to the control thread as today. `takeOver` allocates nothing and is bounded:
  - the 33 `Tr` (plain data), the clocks (`time_`, `base_`, `bt_`, `btT_`, `bpm_`, `tks_`, groove, lpb), both random streams and seeds, the loop, the PERFORM live state (xf, macros, fill, part), `dropped_`: copied;
  - the counters (`rv_`, `nthCnt_`, `nthGen_`, `c1Cnt_`): MOVED (pointer swap), which needs a layout that does not depend on the song's content. Part B changes `buildCounters` to a position-keyed layout fixed by the format and the mode (the worst case the header already states, about 17 MB in song mode), allocated once per PLAY; a successor Seq allocates none and takes them. The worker measures the cost on ELECTRO and the census songs first; if a fixed layout is too large for the phone target, the fallback is a remap table built on the control thread from positions (both layouts are immutable), applied in `takeOver` in bounded chunks: say which, with numbers, before building it;
  - what was sent for PERFORM targets (`sent*`, keyed by target id): carried over by the target's identity (the id's string), matched with a table prepared at construction (sorted hashes; no allocation, O(n log n) for n targets), so an edit that adds or removes a target does not shift the others;
  - track positions that no longer exist in the new song (a chain or phrase deleted under the playhead): `settle()` as the JS's next step would find them; write a test per case (cell cleared, chain shortened, phrase length cut below the step).
- Song-derived tables (bus plan, PERFORM scenes / macros / LFOs, parts, chase, `need_`) are rebuilt from the new song by the successor's constructor. The JS means edits to be heard while playing (`busPlanH = null` on `song`, `keepObj(perf)`); **before building, the worker makes the table of every make-time value in the JS `makeSeq` and whether an in-place edit reaches the running JS sequencer** (like DISPATCH-NOTES section 1), and lists each place where "rebuilt" differs from the JS. The lead rules on those.
- Gate: a new Node reference (`tools/seq-golden.mjs --edit`): the JS sequencer with the song edited in place between steps (a note changed ahead of the playhead, a chain row changed, bpm in the song changed, a perf scene changed, a phrase deleted under the playhead) against C++ with a `takeOver` at the same step: events equal exactly. Plus: takeOver at every step of ELECTRO to an identical song changes nothing (bit-identical to the existing goldens); `rt_harness` allocation guard on `takeOver`; a thread test (control publishes 1000 patches while the audio side steps) under TSan.

## Queue item 3, the chop ruling (the lead)

**The page's sequencer is the specification: sliced and synced instruments are chopped (`applyChop`, index.html :22333).**
Why: it is what an offline render plays and what the default (page) sequencer plays; `core/seq-core.js` has `applyChop` empty only because the
worklet has no pad data (`ensurePlay` returns null there). As far as the lead read it (`mine()` rows 9 and 11), with Settings on the audio core a
sliced / synced instrument's note plays the pad from its top, unchopped: that looks like a web-app defect, to be confirmed by ear by the owner and
then fixed on `beta` separately. The native Seq has the pads' data on its side of the seam, so it chops.
- New unit **"chop"** (Sonnet, after Part A; it touches `seq.cpp` at one place, :706 of the JS): the host hands in per pad the chop data
  `applyChop` reads (`loopSecs`, `slicePoints`), prepared at `set_pad` time; `Seq` applies `ofsSec` / `durSec` / `rate` / `cut` / `st` as the JS.
  Gate: a golden from the PAGE's `makeSeq` (not `sim`) on a song with slc 4, slc 16 + SLC command, sync 1, sync 2 with STA.
- `ev.st` (sync modes 2+, the stretched pad) stays the dossier's Q2: counted and played unstretched at `rate` until the stretch port exists; the
  count is a new stat. The lead's note: this is audible; it goes on the owner's list when the Settings switch exists.
