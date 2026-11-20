# Verification of PR #251 (build 650, Reset track)

Head 268729f, base beta ba32739 (verified as the PR stands; beta has since moved, the rebase is the merge train's job). Independent verifier; nothing in the PR's notes was trusted.

## Verdict: MERGE AFTER FIXES (one should-fix, no must-fix)

## 1. Data safety - CONFIRMED
Method: real page (headless Chrome, `?seqtest=1`), real `resetTrack`, real `undoStep`. Structural diff of the song JSON: only track t's column, mix[t], tfx[t], chainOn[t], block-mute bit t, PERFORM / parts entries naming t, and blocks / phrases no longer reachable may change; no new dangling pointers; every other key byte-identical. Events of every OTHER track (and the master) compared before / after on the page's `makeSeq` and, for the marked fixtures, on `core/seq-core.js` (`runCore`). Undo must give the exact JSON, Redo the post-reset JSON, a second reset must change nothing and add no undo step, and Undo after it must give the original.
- ELECTRO tracks 0-9 (each alone, incl. the bus 3, its feeders 0,1,2,6, track 9 sharing with master): PASS. Core compared on 0, 3, 9.
- BETA-TESTING-ELECTRO tracks 0-11 (bus 7, feeders 0-5, 8): PASS. Core compared on 0, 7, 11.
- Constructed on ELECTRO (core compared): phrase shared by t and another track under different block ids (blocks freed, phrases kept): PASS. Block used by t at rows 0, 80 and 200: PASS. Block used by t and the master column: PASS. Feeder with 3 other feeders: PASS. Sole feeder of a bus: PASS (JSON). Bus itself reset (CHN kept): PASS. Feeder that is also a send: PASS. PERFORM scenes / macros / locks / LFOs / parts naming t and others: PASS. Pump effect on t and on the bus, with other tracks sidechaining from t: PASS. Track 0 with block mutes and pump: PASS. LAST track 31 (ntr 32, sharing with track 5): PASS. Track 15 (last visible): PASS. Hidden track 20 (beyond ntr): reset refuses, song untouched: PASS.
- Early hand-built fixtures first "failed" Undo only because my unnormalised song differs from what `normSong` returns; after normalising the fixture all pass. ELECTRO already holds two dangling phrase references (241, 243): pre-existing.
- Not covered: smuteX (needs NT > 32), a real Mac.

## 2. Core - CONFIRMED
`node tools/make-core.mjs` output is byte-identical to the committed `core/seq-core.js`; CORE_REV '8a147t' matches. The only change is a new message `stp` (one track's voices killed with stop = true, what `stopAll` does for all, `this.T(t).length = 0`). Nothing in the core sends or acts on it unless the page posts it, which only `resetTrack` does. Core and sequencer tests in the full suite are unchanged (see 6).

## 3. While playing - CONFIRMED (by measurement)
Reading: the page player gets `stp`; the core gets the song patch at once, then `stp`. Measured on the real page, ELECTRO playing, reset track 2 (a busy track), analyser on its bus: peak 0.0177 before, 0.0000 250 ms after and still 0.0000 over the next 2.5 s, on the Page sequencer and on the audio-thread core; the song keeps playing (rows advance); `coreSig().same` is true. So no hung voice and no late rows from the core's look-ahead were audible on that track for these events. Not proven for page-played engine / MIDI events already in flight from the core (no such fixture).
The shipped opt-in page test (`TP_BROWSER_TESTS=1`) asserts cells, `coreSig().same` and undo only; it would NOT catch a hung voice (no level check). It did not run in `npm test`.

## 4. Choices
- Name / colour reset, PERFORM and part entries removed: Undo restores all (JSON identical, tested). Acceptable.
- Loop shortening: the song ends at its last used row, so resetting the only longest track shortens the song; Undo restores it. Observed: playing from a row that exists only because of the reset track, the song simply ends (stops) after the reset. Surprise but not data loss. Note.
- Sidechain: other tracks' effects whose Source is the reset track stay set and stop ducking (their sound changes). Documented choice. Note.
- **F1 (should fix): a bus that stops being a bus changes another track's sound.** Fixture "bus 3 carries notes in its blocks; its SOLE feeder 0 is reset": events of other tracks changed. `makeSeq` mutes the notes of bus tracks; once the only feeder gets CHN off, the track is no longer a bus and its own notes play at the next PLAY. Undo restores it. Suggest: keep CHN on when the track is the sole feeder of a bus that has notes (or warn in the toast).
- "A bus keeps CHN on" itself: fine; resetting a bus left feeders and the others' events unchanged.

## 5. Key and menus - CONFIRMED (read plus the repo's tests)
Handler in `globalKeys` after `typing(e)`; needs mod + Shift, no Alt; Shift+T alone (a KEYS note key) cannot match; caller does `preventDefault`; only three `KeyT` shortcuts exist, Ctrl/Cmd+T and Alt+T unchanged. `electron/` untouched: Windows / Linux have no app menu; the Mac default menu has no Cmd+Shift+T. NOT CHECKED on a real Mac or in a browser tab (browser keeps the key: documented, menus exist).

## 6. npm test - CONFIRMED, no new failure
Two full runs on the PR head: 1572 tests, 1543 pass, 28 skipped, 1 fail, both times: `worklet-memory.test.js` "FlexCore ... render bit-identically to build 562" (flex 7: 1 sample differs). It fails identically on the PR's base ba32739, so it is pre-existing (not this PR). `smp-edit` passed both times (did not flake).

## 7. Conventions - CONFIRMED
BUILD 650, sw.js `thunderplus-v650`, CHANGES entry, HANDOVER dated entry and punch-list items, tests/README row; no stray files; no U+2028 / U+2029 in index.html or core-processor.js (grep count 0). Builds 648 / 649 / 656 merged since: BUILD, sw.js, CHANGES and HANDOVER will conflict on rebase.
