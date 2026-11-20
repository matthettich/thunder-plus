# Verification of PR #257 (build 655, branch `beta-sampler-pitch-sync`), independent

Checked at PR head `cbb3466`. Baseline = `origin/beta` index.html (648: differs from the PR's merge base `ba32739` only in BUILD and one CHANGES line).
Method for the A/B runs: headless Chrome through the app's own `?seqtest=1` hook, serving `origin/beta:index.html` for "before" and the PR's index.html for "after"; scripts were mine, not the PR's (kept outside the repo).

## 1. Old songs and kits sound identical: CONFIRMED

(a) Diff read hunk by hunk (`git diff ba32739 HEAD -- index.html`, 283 lines). Changed behaviour, in full:
- `assignSample`: a NEW Sample layer gets `tune: smpOwnTune(v)`. The only change to what a layer is given.
- `normSong`: one added line for the optional `I.lines` (absent -> `delete I.lines`). No tune is touched.
- `applyChop`: `beats = instBeats(I)`; for an instrument without `lines` this is `I.beats || 4`, the old expression; `st.beats` is rounded to 6 places (an integer is unchanged).
- Writers of BEATS now call `setBeats` (same `beats` value, plus `delete I.lines`): pickChopBreak, rcdKeep fit, Resample, GUESS BEATS, the BEATS row.
- Display only: `pitchReadout`, INST BEATS row text, help, CHANGES, BUILD.
No change to `renderLayer`, `loopSecs`, `noteRate`, the tracker rate formula, `layerFromJSON` / `voiceFromJSON` / `kitFromJSON`, presets, undo, paste, `partSnap` / `partLoad`, the editor Apply. Every place a saved layer is read (`layerFromJSON` -> `freshLayer(j)`, song/kit/preset load, undo, layer paste 9569, kit copy) keeps the saved tune: none was edited, and measured below.

(b) Shipped files: `sounds/ELECTRO.thunder`, `sounds/BETA-TESTING-ELECTRO.thunder` are the only shipped songs/kits; **neither contains a Sample layer** (and no `"type":"sample"` anywhere else in the repo), so there is nothing shipped that the fix could touch. Both loaded on both builds: song JSON hash, instrument hash, kit JSON hash, 1792 / 2304 sequencer rows (`run('song')`) all identical (`ce56a3a2/23e211a2/1b88add2` and `88f1f7f7/bad22e2a/3e72a030`).

(c) Constructed old-style song (ELECTRO + Sample layers at tune 0 in synth slots 9 and 10 (voice tune 24) and in drum pad 2, sources 44.1 / 48 / 96 kHz; ten instruments covering SYNC off / REPITCH / STRETCH / LOOP / SLICES 8 / SLICES 16 + SYNC, notes C-2, C-4, C-5, played through `makeSeq` (page sequencer, non-sim, so `applyChop` runs; 115 events)): song JSON, `normSong(songOut)` instruments, kit JSON, `kitFromJSON(kitToJSON)` layer tunes (all still 0), the rendered pad data of pads 2 / 9 / 10 (`ensure`), layer paste, kit copy, and all 115 events: **byte-identical** on origin/beta and the PR head.

## 3 (partial, parser). Type-in parser: CONFIRMED (details below as the review grows)
