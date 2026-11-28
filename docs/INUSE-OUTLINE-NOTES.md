# In-use outline on the Synth page: notes (build 654, 19 Nov 2026)

Owner: "Can we lightly outline instruments that are in use in a song when viewing them on the synth page; this will allow me to easily tell which instruments are open and usable." Line numbers are `index.html` at build 654.

## 1. Where a sound source is a tile the owner might pick as "a free one"

- **The pad / slot grid, `#pads`** (3483), built by `buildPads` (14487), one `button.pad` per slot (`nslots()`: 16 to 64, or 8 + tracks with a kit slot per track). Drums and synths are the same tile; a synth slot only adds `.syn` (SYN badge). Desktop layout `body.sdesk` (1801 ff.) is one column of rows with the labels Drums / Synths / More pads; phone is 8 across, 46 px high (the `max-width:600px` block near 690). Painted by `renderPads` (14560): name, tooltip, `sel`, MIDI-learn classes. This is the only grid of pads; the Synth page has no separate list of slots.
- The cursor is `.pad.sel` (3.5 px border, filled with `--acc`, 3010 ff.), the flash `.pad.hit` (ink fill), MIDI learn `.pad.learn / .learning`, a file drop `.pad.droptgt` (dashed outline, 772). The pad's `box-shadow` is its offset block in the colour skins. The MONO / POLY frames are tracker things; they are not on the pads.
- Other places: the tracker's INSTRUMENTS list (`instList`, 26552: `.tinl button`, side panel and PAD mode); the Instrument page's IN USE row (26731); the Acid panel's instrument select (29998, already marks "· free"); Edit's Instrument... dialog. These list instruments, not pads.

## 2. The chain from a note to a sound

- A phrase row is `[n1 n2 n3 n4, vol, inst, fx...]` (`NCOL=4, VC=4, IC=5, FX0=6, RW=12`, 21076-ish). The player (`makeSeq`, 22340-22383): `ii = fx.INS ?? (r[IC] >= 0 ? r[IC] : T.inst)`, then `T.inst = ii` only on a row that has notes. `I = instsNow()[ii] || [0]`; the event's pad is `vtrk ? voiceForTrack(t) : I.pad` (26320). `S.insts[ii].pad` is the slot. The pad holds the voice (`kit.voices[pad]`: layers, an engine, a sample); GEN / MUT variations (`varPrefetch`, 11810) render the same instrument's pad with a variation key, so they add no pad; kits are the pads themselves.
- PERFORM parts: `S.parts[k] = {n, insts, mix, fx}` (21796); `PRT` switches `PF.part` and the player then reads `S.parts[k].insts` instead of `S.insts` (21953), so the same notes can play other pads.
- A phrase plays when a song cell reaches it: `S.song[y][t]` -> `S.chains[v]` (a block, list of `[phrase, transpose]`) -> phrase. `phraseTracks` (33577) does this for tracks 0 to NT-1 and returns phrase -> tracks; the master column (t = MT) holds master commands, no notes.

## 3. How the app already counts "used"

- CLEAR UNUSED PHRASES, `purgePhrases` (15325): song cells (t 0 to MT) -> blocks -> phrases; the same reachability as `phraseTracks` plus the master column. "Clear unused samples", `smpTidy` (15061): samples no pad / preset / project uses.
- USED ON (build 609): `padTracks` / `padUsedText` / `usedUI` (14510 ff.): tracks that play an instrument on this slot, **counting any I column or INS, with or without a note**, desktop only, memoised on `songVer`. The waveform line (`drawWave`, 14665) and the Instrument page's IN USE row (`instTracks0`, 33591) count the same way. `kitToSlots` (14067) and the Acid select (29998) count notes / I columns on any phrase.
- The change hook: `saveS` (21534) raises `songVer` on every edit, undo and load (a load is also a new `S`). `setView` (36270) and `refreshAll` call `renderPads`.

## 4. Decisions

- **In use on the song (solid):** a note in a phrase `phraseTracks` reaches plays an instrument whose pad is this one, or a PERFORM part would send that note to this pad. **Only off the song (dotted):** only phrases no song cell reaches do. **Free:** neither (an instrument pointing at the pad, or an I column with no note, does not count). The master column counts as off (its notes never play).
- Over-counting is the safe side (a free pad shown as used costs nothing; the reverse could cost a sound), so: a note with no instrument before it in its phrase counts for instrument 00; tracks hidden by TRACKS still count; any saved part counts.
- The "Used on: Track ..." text keeps counting bare I columns as before; the new "used by instrument ..." sentence is added after it. They can differ by an I column with no note.
- Cache: `padUseC`, key `songVer + vtrk + every instrument's pad + every part's pads + S`. Cost on the shipped songs (headless Chrome, `tests/sweep/inuse-outline.mjs`): ELECTRO 269 phrases about 0.5 ms; BETA-TESTING-ELECTRO 735 phrases about 1 ms; a cached call 10 to 50 microseconds. Reruns only after an edit / undo / load, when an instrument is re-pointed, a part's pad changes, or the kit-slot-per-track setting flips; never per draw.
- No setting to turn it off: on his songs 11 to 12 of 24 pads are outlined and none were dotted-only, so it is not noisy.

## 5. Look

`.pad.uOn, .pad.uOff` (684): `outline:1.5px solid color-mix(in srgb, currentColor 72%, transparent); outline-offset:-3px`, dotted for off-song, -5px on the cursor pad (inside its border). An outline moves nothing; `border` (cursor) and `box-shadow` (offset block) are untouched; `currentColor` is the tile's own text colour so every skin supplies the colour (dark ink on paper and the colour skins, light on the dark and terminal skins, the accent's text on the cursor pad). The cursor is a filled tile with a thick border, the flash a fully inverted tile; neither is a thin inner line. Checked in screenshots (desktop 1440 x 900, folded 412 x 915 at 2.5x) in Pastel light, IDM and Matrix. Legend `#padKey` (3325, shown only when something is outlined) and the info lines name instruments and notes.
