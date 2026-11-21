# Native follows the web changes of 19-20 Nov (branch `n1/web-follow-20nov`, from `native` bf75dce)

Web references are `beta` at **cdd9439** (build 660), `index.html` / `core/seq-core.js` / `core/core-processor.js` line numbers of that commit. Native references are this branch.
Status lines are updated as the work goes: look at "STATUS" at the bottom first.

## How the references are made (read this before the test list)
The repository root of the `native` branch holds an OLD snapshot of the web app (`core/seq-core.js`, `index.html`); the native branch does not touch it. The native tools
(`seq-dump`, `seq-extra`, `voice-dump`, and the live / edit / chop golden tool) used to read that snapshot. Now:
- `native/tests/web-ref/` is a PIN of beta: `seq-core.js` and `core-processor.js` verbatim, `index-excerpt.js` = the text of the `index.html` functions the tests need
  (`applyChop`, `loopSecs`, `findHits`, `slicePoints`, `instBeats`, `osGrainSecs`, the envelope-lines functions), cut out BY NAME, never retyped; `BETA-COMMIT` = the sha.
  Made by `native/tools/web-pin.mjs` (`git archive origin/beta index.html core | tar -x -C DIR; node native/tools/web-pin.mjs DIR <sha>`).
- `native/tools/webref.mjs` is the one place that says where the pin is (`THUNDER_WEB_REF=root` reads the old root snapshot instead).
- `native/tools/seq-golden-web.mjs` = the vm part of the root `tools/seq-golden.mjs` (live-clock, edit, chop cases) reading the pin and writing `native/tests/golden/seq-*.json`.
  The root tool is untouched (outside the allowed paths) and now only serves the root snapshot; ctest `seq_edit_current` runs the new tool, and a new `seq_chop_current` runs `--chop`.
- `seq-dump.mjs` and `voice-dump.mjs` read the pin (so `seq_dump_current`, `seq_extra`, `voice_golden_current` are run against beta).
- NOT repointed (not part of this task, still the old root snapshot): `mix-dump.mjs` (`mix_golden_current`, `busplan`), `song-golden.mjs`, `u15-song-dump.mjs` and every other tool listed by `grep -l index.html native/tools`.

## GAP LIST (written before coding)

### 1. SYNC TO LINES (`insts[i].lines`, web #257) and the sampler pitch change
Web (beta):
- Field: `I.lines`, int 1..1024, optional; loader `index.html:21677` (`int(o.lines,0) > 0 ? clamp(..,1,1024) : delete`). Not in `freshInst`.
- `instBeats(I)` `index.html:23793` = `lines | 0 > 0 ? lines / (S.lpb || 4) : (I.beats || 4)`. THE ONLY READER of `lines` in playback: `applyChop` `index.html:23771` (`beats = instBeats(I)`,
  `target = beats * 60 / bpm`, `bpm` = the tempo of the row's note, i.e. `nb`: TPO command of the row else `api.bpm`) and, through it, `ev.st.beats = round(beatsS * 1e6) / 1e6` `:23775`,
  `scale = tS / loop`, `ofsSec`, `durSec`, `cut`. The song's LPB (`S.lpb`) only: not a phrase's `plpb`, not the LPB command (`api.lpb`).
- Other readers (UI / offline only, no sound): BEATS row `:27383-27386`, `syRepitchRow :27338`, sheet `:25730-25784`, `slSet :23828` (writes `lines` AND `beats` = round(lines/lpb) 1..64), `setBeats :23794` (drops `lines`).
  Offline stem length `:25922`, `:26034` reads `e.st.beats` (so it follows `lines` through the event).
- Sequencer (`core/seq-core.js`): `applyChop` is a STUB (`seq-core.js:5`), called at `:723` (`if (c)`); with no pad data the core never chops. The native sequencer is the first sequencer that
  chops on the audio thread, so everything above is mirrored in native's own `applyChop`.
- Sampler pitch part of the same PR (`assignSample :16220`, `smpOwnTune :16219`): a NEW Sample layer gets `tune = -(voice tune)`; replacing a sample in an existing Sample layer keeps its tuning.
  Nothing in `renderLayer`, `loopSecs`, `noteRate`, the tracker's rate or `applyChop` changed; a layer read from a song keeps the tune it was saved with.
Native today:
- `song.cpp:93-94` / `song.h:36` (`SongInst`: no `lines`); `seq.cpp:293-294` (song insts -> `PInst`), `:299-306` (PERFORM parts' insts, read raw as the page does), `seq.cpp:542-571` `Seq::applyChop`:
  `target = I.beats * 60 / bpm` (`:548`), `ev.stBeats = I.beats` (`:565`) (not rounded to 6 places).
- Sampler pitch: no native code assigns a sample to a layer (a layer's `tune` comes from the saved song through `eng_layer_from_json`); nothing to mirror. A future native host that
  loads samples must give a new Sample layer `-(voice tune)` clamped to +-48 (`smpOwnTune`) and keep an existing layer's tuning.
Must change: `lines` read by `readInst` (same clamp as the web loader) and by the parts' reader (raw, `| 0`); `PInst::lines`; `applyChop` uses `beats = lines > 0 ? lines / lpbSong : beats`
(`lpbSong = S.lpb || 4`), `stBeats` rounded to 6 places; no change to a song without `lines`.

### 2. Envelope times in lines (web #259, `docs/ENV-LINES-NOTES.md`)
Web (beta): fields `aU` / `atkL holdL decL relL`, `fU` / `fAtkL fDecL` (layer), `gU` / `gAtkL gHoldL gDecL gRelL` (voice, only with `gAmpOn`), `'l'` = lines, absent = ms
(`ENVL_LAYER` / `ENVL_VOICE` `index.html:5127`). The conversion is `envLinesFit(V, hold)` `:5134-5155`, called by `renderVoice` `:5065` before the render:
`ms = min(64000, lines / max(1, lpb) * 60 / max(1, bpm) * 1000)` (`linesMs :5129`), a stage with no `...L` field is `msLines(ms)` rounded to whole lines (`envLinesOf :5131`); only layers that are
`on` with `level > 0`; the pad is lengthened: `need` per lines-amp layer = `delay + burst * spacing` + (ADSR with `hold > 0`: `atk + dec` + one bar `240 / bpm`; ADSR no hold:
`max(gate, atk) + rel`; AHD: `atk + hold + dec`) + 5 ms; for the voice's own amp the same; `len = min(300, max(len, need))`, `hold' = hold ? min(300, max(hold, need)) : 0`.
TEMPO USED: `songBpm()` `:5119` = the SONG's `S.bpm` (the project tempo; 120 if unset) and `songLpb()` `:5126` = `max(1, round(S.lpb || 4))`, read WHEN THE RENDER STARTS (`renderAsync` posts them
`:8892`; the Worker's `S = {bpm}`), NOT the tempo at a note, NOT a TPO command ("A tempo command in the song does not re-time a rendered envelope", `:5124`). Live the page re-renders when
BPM / LPB change, settled by `lnTempo :8905` (300 ms quiet; a change under 0.5% of the tempo while playing is ignored; an LPB change always counts): a HOST policy, not part of the conversion.
Also `relOf :5013` (the note-off fade) reads `envMs(L, 'aU', 'rel')` at the current tempo.
Native today: there is no song-to-pad host in `native/`; pads arrive as rendered audio from the JS host (`thunder_render_set_pad`, `thunder-render`'s `index.json`). The ONLY native place that builds layer envelopes
from a saved voice is `engine_json.cpp:17` (layer: `atk hold dec sus rel`, filter `fAtk fDec`) and `:42-46` (voice: `gate len`, `gAtk gHold gDec gSus gRel`) into `EngLayer` / `EngVoice`
(`engine.h:68-105`), rendered by `eng_render_voice` with `EngRenderOpts{sr, hold, bpm}` (`engine.h:105-109`; no lpb). The note-off fade (`relOf`) is not computed natively: `kit[].rel` is handed in by the host.
Must change: read the unit fields in `eng_layer_from_json` / `eng_voice_from_json` (the ms fields stay as saved); a new `eng_env_lines_fit(V, hold, bpm, lpb)` (host-side, control thread, before
`eng_render_voice`) that turns lines into ms exactly as `envLinesFit` and lengthens `len` / `hold`; `eng_env_ms` for the host's `rel`. Songs / voices with no unit field: bit-identical (the function returns "unchanged").

### 3. Reset track's `stp` message (web #251)
Web (beta): `resetTrack :21523` sends TWO things: (a) the page player's event `rt.pl.play({type:'stp', t:currentTime, track:t})` `:21531` -> `index.html:23261`: for the track
(`all` false) `kill(v, ev.t, 0.004)` on every voice of `vs[track]`, clear the list, and `pmNotes(k, t, null)` (ends the NOTES-FROM notes of a pump creative effect); (b) for the audio-thread
core `corePost({op:'stp', track:t})` `:21538` -> `seq-core.js:1229`: `at = max(currentTime, d.t || 0)`; if `0 <= track < NT`: `kill(v, at, 0.004, TRUE)` for every voice of the track, clear the list.
The only difference between (a) and (b) in the core is `stop = true`: a voice BOOKED to start at or after `at` never sounds (`kill :529`), while with `stop = false` (the sequencer's STP command,
`seq-core.js:1525`, `fx.STP :619`) it is let go at `at` like any other. There is no MIDI part in either (the page's comment "which also ends its MIDI notes" is about pmNotes = the pump
NOTES FROM routing; `moEv` has no `stp`).
Native today: `SEV_STP` `seq.cpp:779` (the sequencer's STP command: `flag` = `fx.STP > 0`), dispatched by `Players::dispatch` `players.cpp:18` to `VoicePool::stp` `voice.cpp:224` and `SfPool::stp`
`sf_live.cpp:204`: both = the JS handler of `seq-core.js:1525` (stop = false, `k <= NT`). `MidiSched::onEvent` ignores `SEV_STP` (`midisched.cpp:490-515`), as the web's `moEv` does. There is NO native
equivalent of the host message (b).
Must change: `VoicePool::stopTrack(track, at)` / `SfPool::stopTrack` / `Players::stopTrack(track, at)` (kill with stop = true, track range `0 <= track < NT` as the JS, clear the list), a C-API-free
core method for the live host to call. Pump NOTES FROM (`pmNotes`) has no native counterpart in `Players` (the pump note path is not wired natively yet): recorded, not built.

### 4. Stretch modes (`I.sx`, OLDSCHOOL; web #261, #263)
Web (beta): `I.sx` (absent = 0) with `I.sync` picks the algorithm: `SY_PICK :23627` pairs (sync, sx): OFF (0,0), REPITCH (1,0), GRANULAR (2,0), LOOP (3,0), OLDSCHOOL (3,1). Loader `sxLoad :23642`:
`sx = trunc(+o.sx)`, kept only for a pair in `SY_PICK` (so only `sync == 3 && sx == 1`), else deleted; with `sx == 1` the OLDSCHOOL fields `og` (loop ms 0.5..250, default 12), `ou` (loop unit 0..6, int),
`orp` (ramp 0..3, int), `ox` (smooth % 0..50, int), `opt` (pitch -24..24 semitones, int) are clamped (non-numeric / empty / false = the default); otherwise they are deleted. Called from the song loader `:21678` and
for PERFORM parts' instruments that have `sx` or an OS key `:22161` (`sync` truncated and clamped 0..3 first). `applyChop :23770-23785` for `osy = sync == 3 && sx == 1`: `ev.st` gains `{sx:1, gs, rp, x, pt}`
(`gs = round(osGrainSecs(I, sb, lpbS) * 1e6) / 1e6`, `osGrainSecs :23673`: `ou > 0 ? OS_DIVS[min(6, ou)] * 60 / bpm / lpb : clamp(og, .5, 250) / 1000`), and with SLICES + SMODE + `rp != 0` the slice points are
the equal parts `loop * k / slc` instead of `slicePoints` (HITS belong to the source). The stretched buffer itself: `stretchData :23729` -> `oldschoolData :23684` (+ `osStarts`, `OS_RAMP_FN`, `stKey :23751`).
LIVE only (`live && bpm === S.bpm`): the tempo and LPB are the SETTLED ones (`stSettled :23762`, `STQ_QUIET` 300 ms, wall clock) for `ev.st.bpm`, `beats` and the scale.
Native today: `PInst` has no `sx` and no OS fields; `seq.cpp:565` fills `stMode / stBpm / stBeats / stGrain` only; `players.cpp:60` plays a sync 2 / 3 note UNSTRETCHED at `ev.rate` (counted in `notesStretched`).
Must change (not a stretch port): parse and carry `sx` and the OS fields (song loader and parts) exactly as `sxLoad`; `applyChop` fills `ev.st.{sx, gs, rp, x, pt}` for `sync == 3 && sx == 1`; the SLICES rule for `rp != 0`.
The sound does not change (the players ignore `ev.st`). See "What a stretch port will need" below.

### 5. Goldens against the current `beta`
Done first (before any change): the pinned beta's plain, live-clock, edit and chop cases were run against the committed goldens. Result: every golden is bit-identical EXCEPT `seq-edit-bus.json`
(see "Goldens changed"). The new goldens (lines, sx, the voice stp message) are additions.

## STATUS
(see the end of this file; updated at each commit)

- 20 Nov: gap list written; pin + new golden tool in; the only existing golden that moves is `seq-edit-bus.json`. Nothing built yet for items 1-4.
