# Sampler notes: a loaded sample's pitch, and SYNC to a length in lines (build 655)

Line numbers are `index.html` in this branch (build 655) unless a file is named. Written before the fix, from the code, then
checked by rendering (`tests/sampler-pitch-sync.test.js`, `tests/sampler-harness.js`).

## Part A: a loaded sample plays very fast

Owner, 19 Nov, desktop, build 647: "When I load a sample into the sampler, it plays back very fast. It plays normally when you
click it, but loading plays it quickly as well. It seems like normal pitch is at C-2." Later: "I'm using the sampler in layers."
So "the sampler" is the **Sample layer type** of the layered voice (`V.layers[i]`, `type:'sample'`, `L.sample = {data, data2, sr, name, id}`),
on whatever slot the voice is. There is no separate sampler page.

### How a pad's pitch is made

1. **One render per pad, at its own pitch.** `renderLayer` (4383) gives each layer the frequency
   `f0 = BASE * 2^((L.tune + V.tune + L.fine/100)/12)` (4385, `BASE` = C2, 3844). A Sample layer (4322) reads its data at
   `inc * S.sr / BASE` per output frame with `inc = f0 / sr` (4236, 4324): so **layer tune + voice tune = 0 plays the file at its own
   speed, whatever the file's rate** (44.1 / 48 / 96 kHz all give rate 1: the `S.sr` factor is the file's rate).
2. **A note then moves that render from C-4.** Every way of hearing the pad plays the cached render at
   `rate = 2^(semitones from C-4 / 12)`:
   - **Tap on the pad** (`padPress` 14538, Shift+Space, F6): `play(i)` (9049) is `holdNote(i, 0)` on a synth slot (9025) and a
     plain buffer source (rate 1) on a drum pad. Rate 1.
   - **The keyboard** on the Synth page: `st = scaleKey(kOct*12 + pn) - ROOT` (14598), `kOct` starts at 4 and `setOct` (35633)
     changes it, `ROOT = 48` (18859) is C-4. Z at octave 4 is `st = 0`, rate 1 (`holdNote` on a synth slot, `playNote` 9009 on a drum
     pad; both through `noteRate` 9018, which only adds the song's microtuning). Octave 2 gives `st = -24`, rate 1/4.
   - **The tracker**: `rate = 2^((note + I.tsp - ROOT)/12)` (the page player in `makeSeq`, the audio-thread core's copy at
     `core/seq-core.js:700`, and `preview()` 25112). An instrument that points at the slot plays C-4 (note 48, TRANSPOSE 0) at rate 1.
     (SLICES / SYNC rewrite the rate in `applyChop` 23252, below.)
   So **C-4 is the rendered pad as it is, on every route.** Nothing in them knows what kind of slot it is.
3. **Slot kinds differ only in the voice's tune.** "Drum pads: one-shots (AHD), C2 at tune 0. Synth slots: ADSR, tuned up two octaves
   so C-4 plays middle C" (10105; `factorySynths` 5181, `PS` 10107, the empty synth slot 9494 and `mkVoice` for poly tracks 14889
   all give `V.tune = 24`). Right for an oscillator (65.4 Hz * 4 = 261.6 Hz = middle C); wrong for a sample, which has no pitch of its own to
   move up.

### The cause

`assignSample` (15983, build 647) made the new layer with `freshLayer({... sample})`: **layer tune 0**, ignoring the voice's tune.
Loaded into a synth slot (voice tune +24) the sample was therefore rendered at +24 semitones, four times too fast, and its natural
pitch sat at **C-2** (note 24, `st = -24`) instead of C-4. Measured on build 647: a 1000 Hz sine sample in a synth slot comes out at
4000 Hz at C-4 (`tests/sampler-pitch-sync.test.js`). On a drum pad (voice tune 0) it was already right.

### Why "it plays normally when you click it"

The click that sounds right is the **SAMPLE BROWSER's own preview** (16305, `buf: stBuf(...)`: the raw file, no pad, no tune). The sound
after **loading** (`playFresh(pad)`, 8880, from `loadAudioInto` 16008, the browser's Load 16305 and `rcdApply` 16747) is the pad's render played
at rate 1, so it carries the +24. A tap on the pad itself would also have been fast. **The load preview has no bug of its own**: it plays
the pad as it is (rate 1) and was faithfully playing the wrongly tuned render. Fixing the tune fixes the preview.

### Every way a sample gets into a layer

| Path | Code | Layer it makes | Tune before | Tune now |
|---|---|---|---|---|
| Load sample / SAMPLE BROWSER Load / file picker / drop on the window | `loadAudioInto` 16008, browser 16305 | `assignSample` | 0 | -(voice tune) |
| Record... "Keep in pad" | `rcdKeep` 16731 | `assignSample` | 0 | -(voice tune) |
| CHOP BREAKS | `pickChopBreak` 11023 (turns the other layers off, SLICES 16, SYNC 1, BEATS 4) | `assignSample` | 0 | -(voice tune) |
| Resample (bounce a pad / the song into a slot) | 20938 | `assignSample` | 0 | -(voice tune) |
| Render a part / stem to a slot | 25400 | `assignSample` | 0 | -(voice tune) |
| Any load over a layer that is already a Sample layer with data (20 Nov, review F1) | `assignSample` keeps its tune, fine and pitch envelope | the old layer's | unchanged (a toast says so when C-4 is not the sample's own pitch) |
| Editor Apply (replaces the data of the layer that is there) | `rcdApply` 16747 | none: keeps the layer and its tune | unchanged | unchanged |
| PWM or Grains layer taking a sample | `assignSample` first branch | none: keeps its own clock and tune | unchanged | unchanged |
| Layer preset / kit / song / undo containing a sample | `layerFromJSON` 14865, `voiceFromJSON`: `freshLayer(j)` | the saved tune is the tune it gets | unchanged | unchanged |
| Layer preset onto another slot kind | 12275: `L.tune = l.tune + vt - v.tune` | re-pitched so it sounds the same | unchanged | unchanged |
| Freeze (`frzSample` 14033) | a layer that never plays | n/a | n/a | n/a |

On a drum pad `smpOwnTune` is 0, so those loads are bit for bit as before. A non-Layers engine's voice tune is 0 while it is on
(9815 keeps the Layers tune aside as `tuneL`); `rcdKeep` switches such a slot to Layers without restoring `tuneL`, so its first sample is
tuned against 0 and sounds right now but would sound +24 if the owner later toggles the engine away and back. Left as it was (not the
owner's case); listed under "Choices for the lead".

### Sync can hide it

CHOP BREAKS and "fit to tempo" set SYNC to REPITCH. `applyChop` scales the rate by `loop / target`, where `loop = loopSecs(...)` (23203) is
the sample's length *at the layer's tune*. On a synth slot the loop was 4 times shorter, so the rate came out 4 times smaller and cancelled:
a synced break sounded right in both builds, and with a drum pad it is unchanged. Only an unsynced sample was fast. (The second cause
the brief asked about does not exist: `rcdKeep` 16731 and CHOP BREAKS turn SYNC on only for a pattern take / a break at the song's
tempo, with BEATS set from the take; a long sample loaded normally keeps SYNC off.)

### The fix, and why no old song changes

**At load time**: `assignSample` gives a new Sample layer `tune: smpOwnTune(voice)` = minus the voice's tune, clamped to a layer's
range (15982). Voice tune + layer tune = 0, so the sample plays as it is at C-4 from the keys, the tap, the tracker and the load
preview, on a synth slot (-24) and on a drum pad (0). Nothing else is touched: **`renderLayer`, `loopSecs`, `noteRate`, the tracker's
rate formula and `applyChop` are unchanged**, a layer read from a song, kit, preset or undo step keeps the tune it was saved with
(notes written low to compensate keep working), and the editor's Apply keeps the layer's tune. There is no new field and no marker: the
tune the layer is *given* is the only change, and only `assignSample` gives one. `tests/sampler-pitch-sync.test.js` renders an old-style
layer (hash equal to the build 647 render at 44.1 / 48 / 96 kHz), runs `applyChop` over 216 cases on an old-style synth slot and a drum pad
against numbers recorded from build 647 (`tests/fixtures/sampler-chop-before-655.json`), and checks that a fresh load renders at the file's
own pitch and length at 44.1 / 48 / 96 kHz on voice tunes 24 and 0.

If the owner later changes the voice's Tune slider, the sample follows it (voice tune is for everything in the voice): the layer
compensates the *load-time* voice tune only. `pitchReadout` (14307) now says "plays the sample at its recorded pitch at C-4" when
layer + voice tune is 0, else "+N st from its recorded pitch at C-4"; it used to say "0 st plays the sample at its recorded pitch", which
was true only when the voice's tune was also 0.

## Part B: sync a sample to a length in lines (SYNC TO LINES)

Owner: "Can we add a button that syncs it to a line length and lets you choose or type in how long the sync is?" and, about
envelope lengths: "Bars + lines would be great, since I often think of phrases as bars."

### How SYNC and BEATS work today

- **Where they live.** On the INST screen (`instRows`, the PAD block): SYNC (`I.sync` 0..3, `SYNC_MODES` = OFF / REPITCH / STRETCH / LOOP),
  BEATS (`I.beats` 1..64), GRAIN (`I.grain`, STRETCH and LOOP only), `› GUESS BEATS`, SLICES / SLICE BY. Saved per instrument in the song
  (`S.insts[i]`; the loader clamps `sync` 0..3, `beats` 1..64).
- **What a note gets.** `applyChop(ev, I, pad, n, semis, bpm, data, sr)` is called for a note whose instrument has SLICES or SYNC.
  `loop = loopSecs(pad, data, sr)`: the sample's length from the layer's start at the layer's pitch. `target = BEATS * 60 / bpm`.
  - **REPITCH (1):** `rate *= loop / target`: faster or slower, the pitch moves. No buffer is made.
  - **STRETCH (2) / LOOP (3):** `ev.st = {mode, bpm, beats, grain}`; the page player asks `getBuf(pad, rev, st)` for a buffer exactly `target` long
    made by `stretchData` (the cache key `stKey` has mode, bpm, beats, grain); `ofsSec` / `durSec` are scaled by `target / loop`.
  - Called from the page sequencer's `makeSeq` (bpm = the tempo at that row), `preview()` (keyboard, INST audition) and `playSlices`.
- **Per instrument, but the sample is per pad.** Many instruments may play one pad. The existing fit code sets all of them
  (`S.insts.forEach(I => { if (I.pad === pad) ... })`: `rcdKeep`, CHOP BREAKS, Resample). SYNC TO LINES does the same (`slSet`).
- **Lines.** A line is a tracker row: `S.lpb` lines per beat (1..16, new songs 8); a bar (4 beats) is `4 * lpb` lines. A phrase can have its own
  LPB (`S.plpb`, `plpbOf`) and the LPB command changes it for following rows; SYNC has no phrase: `applyChop` only gets the tempo.
- **Audio-thread sequencer: SYNC does not play there.** `core/seq-core.js` is generated; its `applyChop` is the stub `function applyChop(){}` and
  `ensure()` / `ensurePlay()` return null (line 5), and the call (core ~:706) is `if (c){ applyChop(...) }`, so it never runs: synced / sliced
  instruments play unchanged there (`core-processor.js:247` only keeps the page's pad buffer for them). That is the known open bug. Not fixed
  here (the core would need the pad's layers for `loopSecs`, the stretch buffers and the slice points: not a few lines). The sheet says so
  when Settings > Audio > Sequencer is Audio thread.

### What a length in lines means, and where it is stored

- **Storage (exact, optional field):** `I.lines`, an integer 1..1024 (64 beats at LPB 16, the BEATS ceiling), per instrument, next to `beats`.
  **Absent in every old song and in a new instrument** (`freshInst` untouched; the loader deletes a missing or bad value). `instBeats(I)` =
  `lines / S.lpb` when `lines > 0`, else `I.beats || 4`: for old songs the old expression exactly. Chosen over converting to BEATS because
  12 lines at LPB 8 = 1.5 beats is common and BEATS is a whole number.
- **Only the page's `applyChop` reads it.** The core's copy is the stub, so `core/seq-core.js` does not change
  (`node tools/make-core.mjs --check` is clean). `st.beats` is the fractional beats rounded to 6 places.
- **BEATS stays truthful:** with `lines` set the row reads "2 bars (64 lines) = 8 beats = 3.66 s", steps from the beats equivalent, and writing
  BEATS (the row, GUESS BEATS, a pattern-take fit, CHOP BREAKS, Resample) calls `setBeats`, which writes `beats` and drops `lines`:
  the last one written wins, they never fight.
- **LPB rule: the length is in LINES at the SONG's LPB and follows it.** Change LPB later and the lines stay, the time changes (32 lines = 2.00 s at
  LPB 8 / 120 BPM, 4.00 s at LPB 4). A phrase's own LPB and the LPB command do not enter. The sheet's "Phrase N" choice converts a phrase with its
  own LPB into song lines (16 lines at LPB 4 = 32 at LPB 8) and is left out when that is not a whole number.
- **Wording** (identical to the envelope work on `beta-env-lines`): "N bars + M lines" (`slName`): "2 bars (64 lines)", "1 bar + 8 lines (40 lines)",
  "24 lines"; 1 bar = 4 beats = `4 * lpb` lines.

### The sheet

`syncToLines(pad)`: the Sample layer's header button `Sync to lines…` (beside `Edit…`, shown for a Sample layer with data), the INST row
`› SYNC TO LINES`, a Help sentence. `syncLinesSheet` is built like `askSheet` (same classes, keys, Escape, backdrop): chips for 8 / 16 / 32 / 64 / 128
lines and 1 / 2 / 4 / 8 bars (merged) and the phrase under the cursor; a type-in (`24` lines, `2b` bars, `1b8` or `2 bars + 8 lines`; whole
numbers, 1..1024 lines; a bad entry gets a reason and Apply is off); the mode (REPITCH / STRETCH / LOOP; starts at the instrument's mode when SYNC
is on, else REPITCH); one sentence ("1 bar (32 lines) = 4 beats at 8 lines a beat = 1.83 s at 131 BPM; the sample is 2.10 s: it will play 15% faster
(the pitch moves). The length is in lines, so it follows LPB."); Sync off, Cancel, Apply. Apply: `slSet` on every instrument of the pad, one
`saveS()` (one undo step), a toast, and the pad plays once. With no instrument on the pad it uses (and points) the first one no phrase uses, as
CHOP BREAKS does.

### What the native port needs (`native/` and `docs/port/` not touched)

An optional per-instrument integer `insts[i].lines` (1..1024) in the song file. Where the native sequencer rewrites a SYNC / SLICES note (the
page's `applyChop`): `beats = lines > 0 ? lines / song.lpb : (beats || 4)`; `target = beats * 60 / bpm`; for STRETCH / LOOP the stretch length
is that `beats` rounded to 6 decimals (part of the cache key). A missing `lines` is the old behaviour; writing BEATS removes `lines`. (The JS
audio-thread core has no `applyChop`, so a native sequencer implements all of it.)

### Choices for the lead

1. Fix at load, no marker: a later change of the voice's Tune moves the sample too. A render-side marker would change playback for every layer.
2. PWM / Grains layers taking a sample keep their own tune; only the Sample layer type was fixed.
3. (Done, review F2) `rcdKeep` into a slot on another engine now restores the Layers tune (`tuneL`) when it switches the slot to Layers, as switching back does, so the corrected tune cancels the right number. Only a recorder keep into such a slot is affected.
4. Lines are whole numbers (no halves; the field could hold them). Use bars + lines, or LPB 16.
5. The type-in is one field (`24`, `2b`, `1b8`), not two BARS / LINES fields.
6. Max 1024 lines; the song's LPB, never a phrase's.
7. On the audio-thread sequencer the sample plays unsynced for now (the sheet says so calmly): test SYNC TO LINES on the Page sequencer.

### Review fixes (20 Nov, `docs/reviews/verify-257.md`)

- **F1 (the lead's ruling):** loading a sample over an existing Sample layer (with data) keeps that layer's `tune`, `fine`, `pAmt` and `pDec`
  exactly, so notes already written sound the same; only a NEW Sample layer (the slot had none) gets minus the voice tune. When the kept
  tuning leaves C-4 off the sample's own pitch, `assignSample` returns and toasts (after the loader's own toast) e.g. "Kept this layer's tuning: it
  plays at its own pitch at C-2. Set TUNE to −24 for C-4." (note and TUNE computed from layer + voice tune + fine). No toast when C-4 is right.
- **F3:** `slSet` also writes `beats` = the lines / LPB rounded to a whole number, 1..64, so build 647 and the released app (which ignore `lines`)
  play something close. `lines` stays the truth for this build; writing BEATS still drops it. OFF leaves `beats` alone.
- **F2:** done, see choice 3. **F6:** the Audio thread line is "On the Audio thread sequencer the sample plays unsynced for now; that is being worked on."
  **F7:** a too-large entry (any number of digits) says "At most 1024 lines (32 bars at 8 lines a beat)."
