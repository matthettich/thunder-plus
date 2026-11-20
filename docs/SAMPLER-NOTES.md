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
