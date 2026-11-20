# Verification of PR #251 (build 650, Reset track) - INTERIM (server restart mid-run)

Head 268729f, base beta ba32739. Verifier did not build this. Status per check: CONFIRMED / WRONG / NOT CHECKED.

## 1. Data safety - PARTIAL
Method: real page (headless Chrome, `?seqtest=1`), real `resetTrack`, real `undoStep`; song JSON diffed structurally (only track t's column, mix, tfx, chainOn, block mutes, PERFORM/parts entries naming t, and blocks/phrases no longer reachable may change; no new dangling pointers); page `makeSeq` events of every other track compared before/after (script: /tmp/v251/real.mjs, not committed).
- CONFIRMED (ELECTRO tracks 0-9, each reset alone): others' JSON and page-sequencer events identical, t silent, Undo JSON byte-identical, Redo = post-reset JSON, second reset is a no-op with no undo step and Undo still returns the original. Includes feeder tracks of bus 3, the bus itself (track 3), a track sharing blocks with master (9).
- CONFIRMED (BETA-TESTING-ELECTRO tracks 0, 1): same.
- NOT CHECKED so far: BETA tracks 2-11, core-sequencer (runCore) comparison, and the constructed fixtures (shared phrase, block at two rows, block shared with master, sole-feeder bus incl. a bus that carries notes, PERFORM/LFO/parts, pump effect, sidechain, last track, hidden track).
- Note: ELECTRO already holds two dangling phrase references (241, 243); not caused by the PR.

## 2. Core changes - PARTLY CONFIRMED
- CONFIRMED: `node tools/make-core.mjs` output is byte-identical to the committed `core/seq-core.js`; `CORE_REV` '8a147t' in index.html matches.
- CONFIRMED by reading: the only core change is a new message case `stp` (one track's voices killed with stop=true, as `stopAll` does for all); nothing runs unless the page posts it, which only resetTrack does. Goldens before/after: NOT CHECKED.

## 3. While playing - NOT CHECKED (read only)
Concern to test: with the audio-thread core, rows the core already stepped (lookahead) and posted to the page as non-core events may arrive after the page's own `stp` and still sound for track t. Also whether the opt-in real-page test (tests/reset-track.test.js, TP_BROWSER_TESTS=1) would catch a hung voice: it asserts song cells, `coreSig().same` and undo, but not voice levels, so it would NOT catch a hung voice.

## 4. Choices - PARTIAL (reading)
Suspect: `makeSeq` silences the notes of tracks that are buses when PLAY starts. If a bus's only feeder is reset, the bus stops being a bus and its own notes (if any) will sound at the next PLAY: another track's sound changes. Being tested (fixture "bus carries notes"). Loop shortening and playhead: NOT CHECKED.

## 5. Key and menus - PARTLY CONFIRMED
- CONFIRMED: handler lives in `globalKeys` after `typing(e)`; requires mod and Shift, no Alt; plain Shift+T is not matched; `e.preventDefault()` applied by the caller; only three `KeyT` shortcuts exist.
- CONFIRMED: `electron/` unchanged; Windows/Linux have no app menu, Mac default menu has no Cmd+Shift+T. Not tested on a Mac.

## 6. npm test - NOT CHECKED (runs were killed twice by server restarts / load)

## 7. Conventions - CONFIRMED
BUILD 650, sw.js `thunderplus-v650`, HANDOVER entry and punch-list items present, tests/README row, no stray files, no raw U+2028/U+2029 in index.html or core-processor.js (count 0). Build 648/649 are in other PRs: expect a rebase conflict on BUILD/sw.js/HANDOVER.

Verdict so far: undecided (nothing wrong found yet; key checks 1, 3, 4, 6 incomplete).
