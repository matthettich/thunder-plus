IN THE MIDDLE OF: nothing: all gates pass locally (full Release ctest 181/181, ASan+UBSan on the new tests); PR open, waiting for CI (the sanitizers job takes ~50 min). Never merge.

# CHOP NOTES: the native sequencer chops sliced and synced instruments (`applyChop`), unit "chop", branch n1/chop

Spec: SEQ-LIVE-DESIGN.md "Queue item 3, the chop ruling"; DISPATCH-NOTES.md (i) and section 3. The page's `applyChop` (index.html:22333) is the specification.

## Checklist (updated in every commit)
- [x] 1. this plan
- [x] 2. `SeqChop` (seq/chop.h) + SeqOpts::chop + SeqEvent members + `Seq` applies chop
- [x] 3. voice: `ofsSec` / `durSec` honoured; dispatch counts `st` notes (new stat), plays them unstretched at `rate`
- [x] 4. render: `RenderPads::chop`, C API `thunder_render_set_pad_chop` / `thunder_render_set_pad_hits`, stats
- [x] 5. tools/seq-golden.mjs `--chop`: goldens made by the REAL applyChop text (+ proof the existing goldens are unchanged)
- [x] 6. tests: seq goldens (incl. takeOver edit), render DC test, hostile (ASan/UBSan), allocation guard, mutations
- [x] 7a. full Release ctest (181 of 181), PR
- [ ] 7b. CI green on every job

## (a) What `applyChop` reads, and what is handed in
`applyChop(ev, I, pad, n, semis, bpm, data, sr)` reads: the instrument `I` (`slc`, `sync`, `beats`, `grain`, `smode`), the typed note `n` and the
pitch offset `semis` (= note - n: all in `makeSeq` already), the song tempo `nb`, `ev` (`slc` = the SLC command, `ofs` = STA, `t`, `cut`), and per pad:
- `loopSecs(pad, data, sr)`: from `kit.voices[pad]` (first enabled sample layer: its tune, fine, `a`, `delay`) and `data.length / sr`. NOT computable from
  the buffer alone (it reads the kit's layers). => HANDED IN: one double per pad.
- `slicePoints(pad, I, data, sr)`: `smode` off: the grid `loop * k / n` (pure arithmetic on the loop: the native side computes it with the same
  expression, same double arithmetic). `smode` on: `findHits` (256-frame energy flux over the buffer, ~15 lines of float maths over the samples):
  NOT ported; => HANDED IN per (pad, slice count n): the sorted points (1 to n doubles), computed by the host (the page: `slicePoints`).
  If the host did not hand them: the grid, and the count `chopNoHits` (an audible difference, so counted).
- `data`, `sr` are used only through those two. No transient detection, no stored markers in the song: `slc` / `smode` / `sync` / `beats` / `grain` are
  instrument fields the Song already carries (`SongInst`; a part's instruments from the part JSON, as `instOf` already does).
`ensure(pad)` returns null for a pad the kit does not have: then `applyChop` is NOT called (event unchanged, `firstRate` unchanged). Native: a pad with no
handed-in loop is the same case; it is counted (`chopSkipped`) because the condition `slc > 1 || sync` held.

## (b) Where the data enters
- New `native/core/src/seq/chop.h`: `class SeqChop` (control thread builds it, read-only while a Seq runs): `setLoop(pad, loop)` (finite, > 0, else
  false and nothing changes), `setHits(pad, n, pts, count)` (n 2..64, count 1..n, every point finite, 0 <= p <= 1e6; unsorted accepted, as the JS), lookup
  by `loop(pad)` / `hits(pad, n)`: array + sorted vector, no allocation on lookup.
- `SeqOpts::chop` (`const SeqChop*`, borrowed, must outlive the Seq, null = no chop data: every sliced / synced note is then "skipped" and unchopped). A
  `SeqSession` already owns the `SeqOpts`, so every Seq of a PLAY (successors after a `takeOver`) reads the same table; the table does not depend on the song.
- `RenderPads::chop` (a `SeqChop`) and the SongRenderer passes `&pads.chop` into the Seq's opts. C API (render.h): `thunder_render_set_pad_chop(r, pad, loop_secs)`
  (0 removes) and `thunder_render_set_pad_hits(r, pad, n_slices, pts, count)` (count 0 removes). Before the first read (E_STATE after), validated first, as `set_pad`.
  `set_pad` on a pad drops that pad's chop data (as it drops its variations): set the pad first.
- `step()` does the lookup + arithmetic on the stack: no allocation.

## (c) SeqEvent members (at the END of the struct)
`double ofsSec, durSec, stBpm, stBeats; uint8_t stMode, stGrain;` + bits `SH_CHOP` (ofsSec / durSec present) and `SH_ST` (the `ev.st` object present).
sizeof(SeqEvent) 416 -> 456 B (kSeqStepEvents 12288 x 456 = 5.6 MB, was 5.1 MB). `seq_plain.h` writes `ofsSec` / `durSec` only with SH_CHOP, so the
existing goldens are unchanged. `ev.cut` is set (SH_CUT) as in the JS (`min(cut, t + durSec / rate)`); retrigger copies carry the chop members (`Object.assign`).

## (d) The voice
core-processor's `startNote` already reads `ev.ofsSec` / `ev.durSec`: `off = rev ? max(0, dur - ofsSec - durSec) : ofsSec` instead of `ofs/256 * dur`; the
note ends at `ev.cut` (= t + durSec / rate, from applyChop). `VoicePool::note` gets exactly that (STUB at voice.cpp:174 replaced), same 1.5 ms fade-in rule.
`st` (sync 2 / 3): `SongRenderer::dispatch` counts `stats.notesStretched` (+ C API `notes_stretched`) and plays the pad UNSTRETCHED at `rate` from
`ofsSec` until `cut` (the stretched timeline's numbers on the unstretched pad: approximate placement; audible; owner's list when the Settings switch exists).

## (e) The golden, made by the JS
`tools/seq-golden.mjs --chop`: the core's `makeSeq` in a `vm` (as the live cases), `core/seq-core.js` text with ONLY its stub line (:5) changed at run time
(`function ensurePlay(){...} function ensure(){...} function varWant(){} function applyChop(){}` -> real `ensure` over the tool's pad table + `varWant`),
and the REAL text of `loopSecs`, `findHits`, `sliceMemo`, `slicePoints`, `applyChop` pulled out of index.html by name (brace matching) and appended to the
context. `sim:false`; `kit.voices` (layers) and `data` Float32Arrays are the tool's own test pad data. The golden stores: the song, the pad table's chop
data AS THE REAL `loopSecs` / `slicePoints` return it (so the C++ test hands in exactly what the JS computed), rows (events incl. `ofsSec`, `durSec` and
`st`, which `corePlain` drops: a `chopPlain` keeps `st`), sha256. Proof: the existing goldens still regenerate byte-identically (checked with git diff
after `--update`, and the compare mode) and the harness with the real functions but a song that has no slc / sync equals the committed ELECTRO golden.

## Cases (gate 1)
slc 4 with notes around the root; slc 16 with SLC; sync 1 on a loop whose tempo differs; sync 2 and 3 with STA notes; TPO (a bpm change in the song);
an edit case: a `takeOver` after an edit that changes `slc` (the JS side: the processor's `patch`).

## What was built (the result)
- `seq/chop.h` `SeqChop` (header only): per pad a loop length (`setLoop`, kSeqChopMinSecs 1e-6 .. 1e6, finite) and, for SMODE, up to n points for (pad, n) (`setHits`: n 2..64, count 1..n, each finite in 0..1e6; unsorted accepted). `SeqOpts::chop` (borrowed pointer; a `SeqSession` keeps the opts, so every Seq of a PLAY reads the same table).
- `Seq::applyChop` at the call site of the JS (`!sim && (slc > 1 || sync)`, after the note's fields, before RET copies; `firstRate` follows). Counters `chopped / chopSkipped / chopNoHits / chopStretched`, carried by `takeOver`.
- `SeqEvent` new members at the end: `ofsSec, durSec, stBpm, stBeats` (double), `stMode, stGrain` (uint8); bits `SH_CHOP`, `SH_ST`. **sizeof 416 -> 456 B; kSeqStepEvents x sizeof 5.1 MB -> 5.6 MB.**
- Voice: `ofsSec` / `durSec` (reversed: `max(0, dur - ofsSec - durSec)`), the end is `ev.cut` from applyChop. `SongRenderer::dispatch`: `notesStretched` for `SH_ST` notes (played unstretched at `rate`).
- C API: `thunder_render_set_pad_chop(r, pad, loop_secs)`, `thunder_render_set_pad_hits(r, pad, n_slices, pts, count)`; stats `notes_stretched`, `seq_chopped`, `seq_chop_skipped`, `seq_chop_no_hits` (appended). `set_pad` drops the pad's chop data.
- `tools/seq-golden.mjs --chop`: real `loopSecs / findHits / sliceMemo / slicePoints / applyChop` text from index.html at run time, `core/seq-core.js` with its stub line replaced at run time; proof: the four plain goldens through that harness with sim off equal the committed ones (ELECTRO has no sliced / synced instrument), and `git status` shows no existing golden touched. Goldens `native/tests/golden/seq-chop-{all,rev,edit}.json` (128 rows each; 464 / 464 / 396 chopped notes, 128 with st).

## Tests (counts)
- ctest `seq_chop`: 3 goldens (+ chop-all again through a session) all rows / sha / counts equal; the by-hand numbers (slc 4 grid, SMODE hits, sync 1 at two tempos, sync 2 / 3 st); skipped / no-table / no-pad / sim / slc 0 and 1; 10 slc x 6 sync x 2 smode hostile instruments with and without a table (60 x 2); SeqChop argument checks; unsorted / out-of-range hits; loop 1e-6 / 1e6 / 0.01 x bpm 1 / 40 / 255 / 100000 x 3 instruments.
- ctest `render_chop`: DC pad (0.1 0.2 0.3 0.4), slc 4: each note its slice's level (ratio to the DC level to 1e-4) for 0.5 s, exact silence after, bit-identical at blocks 1, 64, 128, 333, 4096 (L, R and stats); notes an octave up give the same output; no chop data -> counted + different; plain instrument unchanged; SMODE hits; sync 2 counted; set_pad drops chop; hostile arguments of both new functions (nine bad loops, four bad pads, bad hits, n, count, null, E_STATE after the first read); 9 slc x 4 sync x (real / zero-length pad) renders finite.
- `rt_harness` `run_seq_chop`: 3000 steps, 12000 chopped notes (3000 with st) under the allocation guard.
- Sanitizers (local, `-DTHUNDER_SANITIZE=ON`): `seq_chop`, `render_chop`, `rt_harness` clean. `test_seq_chop` is built with `-fno-sanitize=vptr` (a Seq's session `shared_ptr` control block is made by the -fno-rtti core: the same reason as `test_seq_takeover`).
- Full Release `ctest`: 181 of 181 passed (1 skipped by design).

## Mutations (one per new gate; each reverted)
- gate 1 (seq goldens): `sync == 1` rate line removed: `seq_chop` 10 FAIL lines. (JS-made goldens catch it.)
- gate 2 (render DC): the voice ignoring `ofsSec`: `render_chop` 7 FAIL lines.
- gate 3 (hostile): `setHits` without its point check: `seq_chop` 3 FAIL lines, `render_chop` 2.
- gate 4 (allocation): a `new` in `applyChop`: `rt_harness` aborts (SIGABRT).
- gate 5 (plain instruments / existing behaviour): applyChop run for every note (call condition and early return removed): `seq_chop` 12 FAIL lines, `render_chop` 2.

## Choices for the lead
1. **`ev.st` (sync 2 / 3) plays the pad UNSTRETCHED at `rate` from `ofsSec` until `cut`**, as ruled. `ofsSec` / `durSec` / `cut` are numbers of the STRETCHED timeline (scale = target / loop), applied to the unstretched pad: a STA offset or a slice of a stretch instrument lands at the wrong place and ends early or late; sync 2 / 3 notes without STA start at the top and end at `target`, which is the loop's own length only when the loop equals the song's beats. Counted in `notes_stretched`. Audible: owner's list when the Settings switch exists.
2. **Chop data is HANDED IN, SMODE hits included** (no port of `findHits`). The grid (SMODE off) is computed in C++ (`loop * k / n`, the JS's expression). A host that forgets the hits for an SMODE instrument gets the grid, counted (`seq_chop_no_hits`).
3. **A pad with no loop handed in is the JS's `ensure()` null case**: the note stays unchopped (rate unchanged), counted `seq_chop_skipped`. So a host MUST call `set_pad_chop` for every pad a sliced / synced instrument uses (after `set_pad`, which drops it).
4. **Validation limits** (not in the JS): loop 1e-6 .. 1e6 s, points 0 .. 1e6 s, slice count 2..64 (the song loader already clamps `slc` to 0..64). Unsorted points are accepted (the JS plays what it is given; `durSec >= 0.005` keeps it sane). A note whose chop would not be finite (extreme tempo) is left unchopped and counted as skipped.
5. `SLC` command value outside the points (the JS reads `pts[k]` = undefined = NaN offset): here slice 0.
6. Parts (PERFORM): a part's instrument is read for `slc / smode / sync / beats / grain` like its `pad` / `tsp` (not golden-tested: no part case; the JS reads the same live object).
7. `sizeof(SeqEvent)` 416 -> 456 B (+0.5 MB for the renderer's event buffer).
8. The length-estimate Seq of the C API (`thunder_render_length_estimate`) has no chop table: the song's length does not depend on chop.
9. core/seq-core.js :912 has `coreBufs`: the worklet DOES have pad data (the page sends them); the ruling's reason "the worklet has no pad data" is slightly off: only `applyChop` is the stub. A web-side fix is therefore possible by taking the stub out (the lead's separate item).

## Parked
(none)
