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

## 2. New loads: CONFIRMED, with findings

Measured in the page (1000 Hz sine, `loadAudioInto` with a real WAV, zero-crossing frequency of `ensure(pad).data`, which plays at rate 1 at C-4):
| build | synth slot (voice tune 24), 44.1 / 48 / 96 kHz | drum pad (tune 0) |
|---|---|---|
| origin/beta | 4000 Hz, layer tune 0 | 1000 Hz |
| PR | **1000 Hz**, layer tune -24, 1.000 s long | 1000 Hz, tune 0 |
All other load routes (browser Load, recorder keep, CHOP BREAKS, Resample, render-to-slot) call the same `assignSample` (read: 11039, 16016, 16307, 16740, 20940, 25515), so they get the same tune; not each run separately.

**Replacing the sample in an EXISTING Sample layer of an old song (F1, should fix / decide):** `assignSample` writes a brand new layer over `v.layers[v.sel]` (it never kept the old layer's tune; only PWM / Grains layers keep theirs). Measured: old layer tune 0 on slot 9 -> after a load it is **tune -24** (level reset to 0.9, envelope reset, as before). So the sample now plays at its own pitch at C-4 and the owner's existing notes on that instrument (written at C-2 to compensate) sound **two octaves lower** than before the replace. Unsynced notes only; a REPITCH-synced instrument is unchanged audibly (rate and loop compensate). The notes doc's table says "Editor Apply keeps the layer"; it does not say a plain Load over an old layer changes the tune. Not the least surprising: either keep the old layer's tune when it replaces a Sample layer (stays "fast", matches the notes) or tell the owner in the toast / punch list ("notes written at C-2 move down two octaves"). I recommend the toast / punch-list text at minimum.

**CHOP BREAKS / synced loads:** a 2 s sample, 4 beats at 120 BPM, synth slot: origin/beta REPITCH rate 0.25 on a 4000 Hz pad render; PR rate 1.0 on a 1000 Hz render; audible 1000 Hz and 2.000 s in both: sounds the same as before (CONFIRMED). **STRETCH and LOOP on a synth slot were wrong before** (4000 Hz audible, two octaves up) and are right now for new loads; old layers keep the old behaviour (pre-existing, not a regression).
**`rcdKeep` edge (F2, note):** REPRODUCED. A slot on another engine (voice tune 0, `tuneL` 24) gets a recorder take: layer tune 0, plays 1000 Hz now; when the engine is switched back to Layers (tune 24 restored) it plays **4000 Hz**. Two octaves too high, only after a deliberate engine round trip on that slot. Cheap fix: in `rcdKeep`, restore `v.tune = v.tuneL` when it switches the engine.

## 3. SYNC TO LINES

- Parser (`slParse`, run at LPB 4 and 8, 40 inputs): `24`, `2b`, `1b8`, ` 2 b 8 `, `2 bars + 8 lines`, `1b+8l`, `24 lines`, `2B`, `007` OK; `0`, `00` "At least 1 line."; `1025`, `999999`, `1b1024`, `256b` "At most 1024 lines."; `-1`, `b`, `1.5`, empty, `abc`, `1e3`, `0x10`, `b8`, non-ASCII digits give the "Type lines (24), bars (2b) or bars and lines (1b8)." hint. Never NaN, 0 or > 1024. CONFIRMED. (F7 note: a 7-digit entry such as `1000000` gets the format hint, not "At most 1024 lines." Harmless.)
- Lines -> seconds (`slSecs`, 32 lines): LPB 3 / 4 / 6 / 8 / 16 at 120 BPM = 5.3333 / 4.0 / 2.6667 / 2.0 / 1.0 s; at 60 / 90 / 131 / 174 BPM they scale as 60/BPM exactly (e.g. LPB 8: 4.0 / 2.6667 / 1.8321 / 1.3793). CONFIRMED.
- Sheet: Apply is disabled and Enter re-checks the parse, so an invalid length cannot be confirmed. CONFIRMED (code).
- `slSet` sets every instrument of the pad, one `saveS()` (one undo step); the PR's own browser tool covers one Undo; I did not re-run it.

Stale `lines` table (an instrument with `lines` set, then...):
| action | `lines` after | verdict |
|---|---|---|
| edit BEATS row, GUESS BEATS, CHOP BREAKS, recorder fit, Resample | dropped (`setBeats`) | right |
| Sync off in the sheet | kept, `sync` 0 | fine, inert |
| SYNC row set to OFF on INST | kept | note: turning SYNC back on resumes the old lines (the BEATS row shows them, so not silent) |
| change the instrument's PAD / load another sample into the pad | kept | same as `beats` today; length is per instrument, not per sample (note) |
| change song LPB | kept, time follows (by design, documented) | right |
| PERFORM part SAVE / LOAD (`partSnap` / `partLoad`, JSON copy of `insts`) | copied with the part | right (snapshot) |
| copy / paste instrument | no such feature in the app | n/a |
| reset instrument (`freshInst`) | no `lines` | right |

## 4. Older builds and the core

- Build 647 loader (`normSong` builds the instrument from `freshInst` and copies known fields only): a song saved with `lines` LOADS; `lines` is ignored and lost on its next save in 647. It plays **`beats`**, and `slSet` never writes `beats` (F3, should fix): the sheet leaves `beats` at whatever it was (default 4), so e.g. 2 bars at LPB 8 (8 beats) plays in 647 as 4 beats (twice as fast in REPITCH). Not "close". Fix: `slSet` also writes `I.beats = clamp(round(lines / lpb), 1, 64)` (newer builds ignore it while `lines` is set, `setBeats` already drops `lines` on the next BEATS write). Does not affect songs without the field.
- Core: `core/seq-core.js` `applyChop` is `function applyChop(){}` and `ensure()` returns null, so the call never runs; SYNC / SLICES do nothing on `seq=core`. CONFIRMED. The core has no `beats` / `lines` use, so not touching `core/` is right. The sheet's note ("Audio thread does not play SYNC yet (a known open bug: ...)") is truthful; "known open bug" is slightly alarming for an owner-facing sheet (F6 note, wording only).

## 5. UI

Button `#bSyncLines` ("Sync to lines...") sits beside Edit..., shown with it (Sample layer with data); INST row `> SYNC TO LINES` exists; wording "N bars + M lines" with bar = 4 x LPB lines confirmed in `slName` (24 lines; 2 bars (64 lines); 1 bar + 8 lines (40 lines) at LPB 8). No U+2028 / U+2029 in index.html (grep empty). CONFIRMED.

## Other findings
- F4 (should fix before merge): the PR does not merge cleanly into current `origin/beta` (648 landed): conflicts in `index.html` (BUILD / CHANGES), `sw.js`, `HANDOVER.md` (`git merge-tree`). Expected by the PR; needs a rebase and the build number renumbered.
- F5 (note): `tools/sampler-lines-check.mjs` follows the repo's `tools/*-check.mjs` pattern (`sampler-check.mjs`, `recorder-check.mjs`...), not in `npm test`: fine. No stray files.
- Conventions: BUILD 655 and sw.js `thunderplus-v655` agree, HANDOVER entry and punch list present, CHANGES line present.
