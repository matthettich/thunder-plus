IN THE MIDDLE OF: code + goldens + seq_chop / render_chop tests pass locally (Release); next: rt_harness allocation guard, ASan/UBSan run, mutations, full ctest, PR.

# CHOP NOTES: the native sequencer chops sliced and synced instruments (`applyChop`), unit "chop", branch n1/chop

Spec: SEQ-LIVE-DESIGN.md "Queue item 3, the chop ruling"; DISPATCH-NOTES.md (i) and section 3. The page's `applyChop` (index.html:22333) is the specification.

## Checklist (updated in every commit)
- [x] 1. this plan
- [x] 2. `SeqChop` (seq/chop.h) + SeqOpts::chop + SeqEvent members + `Seq` applies chop
- [x] 3. voice: `ofsSec` / `durSec` honoured; dispatch counts `st` notes (new stat), plays them unstretched at `rate`
- [x] 4. render: `RenderPads::chop`, C API `thunder_render_set_pad_chop` / `thunder_render_set_pad_hits`, stats
- [x] 5. tools/seq-golden.mjs `--chop`: goldens made by the REAL applyChop text (+ proof the existing goldens are unchanged)
- [ ] 6. tests: seq goldens (incl. takeOver edit), render DC test, hostile (ASan/UBSan), allocation guard, mutations
- [ ] 7. full Release ctest, PR, CI green (never merge)

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

## Choices for the lead
(filled in as they come)

## Parked
(none yet)
