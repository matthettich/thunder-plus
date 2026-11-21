# Stretch build notes (steps 1 and 2 of docs/STRETCH-DESIGN.md on branch `beta-stretch-design`)

## Checklist (updated in every commit; this is what is in the middle of)
- [x] PR 1, build 659, branch `beta-stretch-picker`: picker, `I.sx`, REPITCH sentence, no audio change (code, tests and build commit done; waiting only on the PR)
  - [x] fixtures recorded from origin/beta (1dfbf7ad, build 658): `tests/fixtures/stretch-before-659.json`
  - [x] code in `index.html`
  - [x] tests `tests/stretch-picker.test.js`
  - [x] build commit (BUILD, sw.js, changelog, HANDOVER, punch list), push, PR
- [x] PR 2, build 660, branch `beta-stretch-oldschool` (stacked on PR 1) (code, tests, build commit done; waiting only on the PR)

## Choices for the lead
Both PRs (read with PR 1's list first):
1. OFF stands alone at the top of the picker (the design lists it under Repitch); the groups shown are Repitch, then Character, because Clean and Drums have no modes yet. The groups appear as captions in the app's own list (`askSheet` heads), on the INST page by pressing EDIT on SYNC (left / right still step through the modes), and as captions over chips in the sheet.
2. LOOP keeps its own entry (the design calls it "OLDSCHOOL (classic)"); OLDSCHOOL (new) is a separate entry after it, so the picker never changes what an old song's LOOP is.
3. Picker names: STRETCH is shown as GRANULAR; `SYNC_MODES` (STRETCH) stays as the stored classic list.
4. Params of OLDSCHOOL are dropped when the instrument leaves OLDSCHOOL (Undo restores them), so a song that does not use it has none of the fields; `og` loads clamped.
5. SMOOTH: the prototype faded the first grain's leading edge and the last grain's trailing edge with no partner (a half-level start); the build gives them full weight. Measured max step at SMOOTH 5 percent is 1.69x the source's own step on a 220 Hz sine (the design measured 1.5x; it depends on the phase), 1.0 to 1.13x at 25 and 50 percent; the tests' bounds are 1.8 and 1.3 and "no step over 4x".
6. LOOP BY divisions: 1, 1/2, 1/4, 1/8, 1/16, 1/32 of a line (the design listed up to 1/16). LOOP in ms has 0.5 ms steps. PITCH is semitones only (no cents yet).
7. The GRAIN row is replaced by the OLDSCHOOL rows in OLDSCHOOL; it still shows in every other mode, including OFF and REPITCH where it does nothing (as before).
8. (Superseded by the verifier's fixes, see 12.) Tempo drag: found that a new tempo is a new cache key and a synchronous main-thread stretch at the next note, per changed tempo. Fixed for live notes only with `stLiveBpm` (300 ms quiet, 0.5 percent tolerance while playing); a TPO command's own tempo is exact; a static tempo is bit-identical. This also applies to the classic GRANULAR and LOOP while the tempo moves (their audio is unchanged for any given tempo). If you would rather keep the classic modes strictly as they were, it is one condition in `applyChop` (`sy >= 2 && live`).
9. The OLDSCHOOL golden (`tests/fixtures/oldschool-golden-660.json`) is recorded from this implementation (a regression golden for the native port), not from an independent source.
10. The "Audio thread" limit is stated in the sheet (build 655's note) and in the SYNC row's note; nothing about the audio-thread sequencer changed.
11. Beta moved from build 657 to 658 while I started (the first fetch gave a53cee66, the branch is on 1dfbf7ad): PR 1 is built on 1dfbf7ad.

12. After the verification (review/verify-263): the tempo settle is OLDSCHOOL only (classic modes and REPITCH exact, as on beta; tested bit-identical during tempo changes); no tolerance (1 BPM at 201 BPM applies after the window); the 300 ms is counted from the change (`stqSee`, 50 ms timer), and PLAY, a song load and undo / redo settle at once; LPB is settled with the tempo. Edge rule for SMOOTH: reads past the sample's ends hold the edge sample (only when SMOOTH > 0; the hard cut is as it was). Ramp: the grain starts run 0 .. last position where a grain still reaches the last sample, so the last grain reads to the end (FWD) / REV's last grain starts at 0. **The OLDSCHOOL golden changed for this reason** (ramp positions moved, fade edges); the classic fixtures did not. A PERFORM part's OLDSCHOOL values are normalised (`normParts`), `og: null` is the default 12. An older build SAVES OLDSCHOOL as LOOP (HANDOVER says so).
