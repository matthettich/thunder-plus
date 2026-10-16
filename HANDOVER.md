# Thunder Plus — handover notes

Pick-up notes for continuing this work in a later session. Read this together with
[`README.md`](README.md) (the app's own docs) and [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).
Keep this file up to date at the end of a work session.

## Where things stand (13 Oct 2026, morning)

- **One repo now:** `matthettich/thunder-plus` (this one), branch `main`, pushed, nothing uncommitted.
  `matthettich/Thunder-` is **retired** (13 Oct): its page forwards to this app and its service worker removes
  itself. Don't build or edit it; its old app code is in its history (last build 58a0a43).
- **Commits this session** (Plus, oldest first):
  - `2dcda77`: 14 new effects, 6 oscillators, tempo-synced modulation, …chains Steps/Clock/Repeat, and the
    song phrase-number fix.
  - `eebb19e`: 9 more effects (comb, transient, trance gate, multiband, frequency shifter, vinyl, dub, cab, tube).
  - The **break-chopping** commit on top (slices, sync, stretch), plus this file.
  - Thunder- has a matching rebuild after each of them.
  - 13 Oct: the **Beads crash fix** and effect crash guards (see below), with a Thunder- rebuild.
  - 13 Oct: **one app, two setups** (Settings → App → Mobile) and the first-PLAY render fix (see below);
    then the owner chose **one app, one repo**: Thunder- retired (forwarding page), `build-thunder.py` removed.
  - 13 Oct: **pad rendering moved to a Worker** (see below), and the offer to open ...Thunder's saved work.
- **App:** no build step. `index.html` is the whole tracker/synth app; `chains/index.html` and
  `waves/index.html` are the two modular patch editors loaded in frames.
- **Cache-busters now:** `chains-worklet.js?v=15`, `waves-worklet.js?v=11`, `master-worklet.js?v=2` (index.html, the editor
  pages, sw.js). Cache is `thunderplus-v79`.

## Licensing rule (the owner may sell the app later)

Only **MIT / BSD / ISC / zlib / Apache** code goes in. **Never GPL/LGPL**: no Surge, Vital, Dexed, Helm,
OB-Xd, Odin2, ZynAddSubFX, Dragonfly or Calf, and not the DaisySP-LGPL add-on (DaisySP's main library is MIT;
its comb filter and ReverbSc are LGPL, which is why the comb resonator is our own code). Mutable Instruments'
STM32 modules are MIT; its AVR ones are GPL. Every addition gets:

1. a credit in `THIRD_PARTY_NOTICES.md`;
2. a **generic name** in the UI, with "based on X" in its credit (FX_CREDIT in `index.html`, help text
   in `chains/index.html`). Product names are trademarks even when the code is MIT.

Verified this session: Beads is MIT in upstream `pichenettes/eurorack`, and so is Cloud Seed Core. The VST 3
SDK is MIT since 3.8 (Oct 2025), for a future VST port; iPlug2 or DPF are permissive frameworks; JUCE needs
a licence.

## What was added this session

### Effects (pad FX, track FX and …chains, all on one wasm module)

`dsp/tfx/tfx.wasm` (also base64 in `index.html`'s `tfxWasm` tag, and copied to `chains/dsp/` and
`waves/dsp/`). Effect ids, in `dsp/tfx/tfx.cc`:

| id | key (app) | UI name | source |
|---|---|---|---|
| 0–10 | rings … beads | (existing) | unchanged, bit-identical output |
| 11–18 | flanger, phaser, tremolo, odrive, folder, autowah, pshift, ladder | Flanger … Ladder filter | DaisySP (`fx_ds2.cc`) |
| 19 | xmod | Cross-mod | Mutable Warps, run at 96 kHz (`fx_warps.cc`) |
| 20 | space | Space reverb | Cloud Seed (`fx_cloudseed.cc`) |
| 21–24 | lofi, drive, air, room | Lo-fi, Drive, Air, Room reverb | Airwindows DeRez2, PurestDrive, Air3, Verbity2 |
| 25–29 | comb, transient, tgate, ott, fshift | Comb resonator … Frequency shifter | **our own code** (`fx_extra.cc`) |
| 30–33 | vinyl, dub, cab, tube | Vinyl, Dub tape, Speaker cab, Tube | Airwindows ToVinyl4, Dubly, Cabs, Tube2 |

- **…chains** shows all of them except the DaisySP phaser (…chains already had its own JS phaser). They're in
  the palette group "More effects".
- **Cloud Seed's buffers** are shrunk at build time (`build.sh` seds `DelayBufferSize`) from ~67 MB to ~5 MB
  an instance. `fx_cloudseed.cc` caps size/pre-delay so lines stay inside those buffers.
- **Trance gate** needs the tempo and a restart:
  - In track FX, `fxRestartN` is bumped in `tStart()` and the effect lists are resent; the gate waits 80 ms,
    the sequencer's lead time.
  - In …chains it uses `ctx.transport.startN`, which the engine counts in `setTransport` when playing goes true.
  - On pads it uses `songBpm()`.

**How the three tables are made:** one spec, [`tools/fxgen.py`](tools/fxgen.py), generated the pad-FX
rows (`FX`), track-FX rows (`TFX`), the …chains worklet `effect()` defs and the editor `MODULES` entries, so
their ranges match. It prints JS to paste and edits nothing. To add effects, add a batch to it (as `FX2` was)
and paste each part. If you edit by hand, keep the worklet `params` and editor `params` identical (the check
below catches it). The Overdrive's default output was trimmed after generating (to 0.2), so the spec
differs from the app there.

**Gotcha found:** never call an effect parameter `type`. Pad FX objects keep their effect key in `f.type`,
and `freshFx()` merges `init` over it (the Speaker cab's model is `model` for that reason).

### Synth

- **Oscillators** (`newOsc()` just above `makeOsc()`): `supersaw`, `pd` (8 CZ-style waves), `chip`
  (4-bit, chip clock), `formant`, `vosim`, `grains`. Grains plays the layer's sample; Load sample keeps a Grains
  (or PWM) layer's type.
- **Mod matrix:**
  - LFO shapes 4 = S&H and 5 = smooth random (`hashRnd`, deterministic per render).
  - `lfoNSync` / `seqSync` lock to the song tempo (`SYNC_NAMES`/`SYNC_BEATS`).
  - Synced pads re-render when the tempo changes: `ensure()` caches the bpm used.

### …chains modules (`chains/chains-worklet.js`, JS)

- **`steps`:** 8-step CV with gate, synced or clocked.
- **`clock`:** gates with swing, plus `ramp` and `bar` ramp outputs.
- **`repeat`:** beat repeat, reverse and tape stop. It keeps 4 s of history; tape stop keeps recording and
  reads behind it.
- **`DIV_BEATS`** is shared by all three. All three restart on `transport.startN`.

### Break chopping (instrument level, `index.html`)

- **Instrument fields:**
  - `slc`: 0 off, 2–64 slices.
  - `smode`: 0 grid, 1 hits.
  - `sync`: 0 off, 1 REPITCH, 2 STRETCH, 3 LOOP.
  - `beats`: 1–64.
  - `grain`: 0–4, which is 1/64–1/4 beat.

  They are saved in songs (`normSong` reads them).
- **Code** (above `rtBuf`):
  - `loopSecs()`: the sample layer's length at its pitch, else the pad's length.
  - `findHits()`: onset detection.
  - `slicePoints()`: memoised.
  - `stretchData()`: STRETCH is Hann OLA; LOOP is hard loops with 1.5 ms crossfades. Both sync the grain to
    the beat.
  - `applyChop()`: rewrites a note event (`ofsSec`, `durSec`, `rate`, `st`, `cut`).
- **The player** `startNote` takes `getBuf(pad, rev, st)` and plays from `ofsSec`. `rtBuf` and the render's
  `getB` build stretched buffers keyed by `stKey(st)`.
- **Notes pick slices:** `(note − ROOT) mod slc`. The typed note's pitch is ignored; TRANSPOSE and pitch
  commands still apply.
- **INST rows:** SLICES, SLICE BY, SYNC, BEATS, GRAIN, › GUESS BEATS, › PLAY SLICES.

### Bug fix: song phrase numbers with sects hidden

With sects hidden, typing 14 on the Song screen made sect 14. Phrase 14 already had notes, so
`ensureSectPhrase` put phrase 03 in it: Song showed 14 and Arrange showed 03.

- Song cells now show and take **phrase** numbers (`songShown`, `sectForPhrase`, `setSongPhrase`; typed hex
  writes on the second digit).
- An empty sect uses its own-numbered phrase whenever no other sect plays it (`phraseInSect`).
- The owner's existing song still has one block pointing at phrase 03 (they were told to retype 14).

### Samples up to 30 s, stored in IndexedDB (and a start-up kit bug)

- **Limits raised to 30 s:** Load sample (`fileIn`), the sample layer's Hold (30000 ms), and the voice's Max len
  (0.1–30 s, now a squared slider). The voice-amp render cap is 32 s.
- **Why the limit existed:** the kit autosaved to localStorage (~5 MB) with samples inside as base64. A long
  sample filled it, and `save()` then fell back to saving without samples, so they vanished on reload.
- **Now** (`/* samples in IndexedDB */` block, near `save()`):
  - Each sample has an `id` and its 16-bit PCM is stored once in IndexedDB (`thunderplus-samples`; Thunder:
    `thunder-samples`).
  - The localStorage kit holds `{ref:id}` (`kitToJSON('ref')`).
  - On start-up and `setKit`, `resolveSamples()` fills them in, re-renders those pads and drops unused ones
    (`smpTidy`).
  - Old kits with base64 inside are moved over on start-up.
  - Kit and project FILES still embed the audio (`kitToJSON(false)`).
  - Those variables are `var`, because `save()` can run before that part of the script.
- **Bug fixed (from `5c9aa79`, earlier on 12 Oct):** `kitFromJSON` called `nslots()` (a `const` defined later) while
  the saved kit was read at start-up. It threw, so every reload started from the factory kit. It now uses
  `slotsNow()`. Verified: a renamed kit and a 25 s sample both survive a reload.

### Fix (13 Oct): Beads crash, and effects that crash are bypassed

- **Found by a stress test** of every effect through the real …chains param maps (defaults, min, max and
  random settings): Beads with **Freeze on and Size at exactly 1** divides by zero after ~1.5 s. A wasm trap
  stops the whole module, so on a Mixer track it silenced that track's effects until a reload, and in
  …chains it silenced the track's chain.
- **Fix:** Size is capped at 0.995 in all three Beads maps (`BEADS_SIZE_MAX` in the worklet, the `TFX` and
  pad-FX `beads` maps in `index.html`). The wasm is unchanged.
- **Guards** (so any future effect bug costs only that effect):
  - …chains `effect()`: a throwing `tfx_process` frees the handle and passes the sound through.
  - Mixer track FX (`TFX_SRC`): each slot's input is copied first; a throwing slot is bypassed, its handle
    freed and `{failed:key}` posted (the page toasts). `set()` remakes it when its settings next change.
- **Also seen, not changed:** Space reverb, Air, OTT and EQ reach the ±8 output clamp at extreme settings
  (Air also at some random mid settings). Loud, not broken.

### One app: Mobile (...Thunder) and Desktop (...Thunder Plus) (13 Oct)

The owner found keeping two apps in step hard (12 Oct's Plus work reached Thunder late), so they are one app
with a run-time setting.

- **Settings → App → Mobile** (`#pApp`, first in `GLOBAL_SECS`; `platform`, `setPlatform()`,
  `applyPlatform()` beside `let view`). Saved in `thunderplus-platform-v1`.
  - Mobile hides the Waves/Chains tabs (`applyPlatform` rebuilds `segView`), redirects `setView` away from
    them, makes `openWaves`/`openChains`/`focusChainHit` do nothing, drops Export/Import chains from File and
    the Project rows, swaps the waves layer's edit button for a note (`.wvEdit`/`.wvMobNote` CSS), and hides
    the brand's "+" (title `...Thunder`).
  - Switching to Mobile stores Desktop's input (`thunderplus-desktop-input-v1`) and goes to PAD; switching
    back restores it. A first run in Mobile starts in PAD.
  - …waves layers and …chains effects still **play** in Mobile, so files are the same in both.
  - `PF_DEFAULT`: Mobile on a coarse pointer, else Desktop.
- **...Thunder retired (13 Oct, the owner's choice):** one app, one repo.
  - `matthettich/Thunder-` now holds only a forwarding `index.html` (clears `thunder-*` caches, unregisters its
    service worker, then `location.replace('../thunder-plus/')`), a self-removing `sw.js`, README, manifest and
    icons. Tested locally with the old service worker installed: it was removed and the page forwarded.
  - **Saved work:** both apps lived at matthettich.github.io, so Plus reads Thunder's `thunder-kit-v1`,
    `thunder-song-v1` and IndexedDB `thunder-samples` (only reads). `thunderOffer()` (2.5 s after start-up)
    asks once, with `confirm()`, when Thunder's work differs from Plus's. Yes: it downloads Plus's song and
    kit as `<song> project (before Thunder import).json`, then opens Thunder's and copies its samples into
    `thunderplus-samples`. No: it keeps Plus's. The answer is stored in `thunderplus-thunder-import-v1`.
    Nothing is asked when Plus has no work of its own (it already opened Thunder's) or both are the same.
    **File → Open ...Thunder's saved work…** appears while Thunder's data exists.

### Performance: what costs what (13 Oct, measured)

Headless Chromium with a phone-sized screen and CPU throttled 4× (Playwright + CDP
`Emulation.setCPUThrottlingRate`):

- **Playback is fine:** median frame 16.7 ms while the demo song plays in the tracker.
- **Pad rendering is the cost**, all on the UI thread: drum pads 10–90 ms each, the 8 synth slots
  0.8–1.9 s each (they render their whole Max len, 6–8 s, at 8 layers). At full speed: 170–380 ms each.
- **First PLAY froze ~8 s** (2 s at full speed): pads rendered at start-up assumed 48 kHz, the first sound made
  the real context (44.1 kHz here), `audio()` marked every pad dirty and `tStart()` rendered them all at once.
- **Fixed:**
  - `srGuess` asks the browser for its rate at start-up (a context made and closed at once), and `ensure()`
    renders at that rate.
  - `audio()` no longer marks every pad dirty (`ensure()` already re-renders a pad whose rate differs).
  - `prerender()` renders one pad per idle moment (`requestIdleCallback`), after the wasm has loaded and
    again when audio starts.
  - Result at 4×: first PLAY takes 144 ms once the kit has rendered (about 10 s after opening at that
    speed), and 2.7 s if pressed straight away.
- **Then the Worker (below) took pad rendering off the UI thread altogether.**
- **Not worth a switch:** the …chains engine's draft quality only trims Vocoder bands (and turns on by itself
  under load), …waves' draft mutes modules, and the editors cost nothing until opened. So Mobile hides
  editors but doesn't change sound or quality.

### Pad rendering in a Worker (13 Oct)

- **What:** `rwStart()` builds a Worker from this page's own `/* ===ENGINE START=== */` … `/* ===ENGINE END=== */`
  block (the synthesis code: oscillators, `renderVoice`, pad FX), plus a small glue script. The block must
  stay free of DOM use at top level; the Worker has no `document`, `window` or `localStorage`.
  - **Wasm:** the loader compiles each wasm once (`plMod`, `txMod`) and shares the modules with the …waves
    engine and the Worker. This also fixed the double base64 decode.
  - **…waves:** the Worker `importScripts` `waves/waves-worklet.js`.
  - **Tempo:** the Worker's `S` only carries `bpm`.
- **Main-thread API** (next to `ensure`):
  - `ensure(i)`: up to date, now, rendered here if needed. Used by chops, slice info and anything that must
    be exact.
  - `ensureLive(i)`: for playing. Returns the last render while the Worker makes the new one. Used by
    `buffer()`, `rtBuf*` and `tStart()`.
  - `bgRender(i)`: one Worker job per pad. An edit made meanwhile re-sends the newest state when the job
    lands, and `padRev[]` stops an older result replacing a newer one.
  - `showCur()`: draws the current pad now and refreshes it in the background.
  - `renderAsync()`: the song render's pads also go to the Worker.
- **Fallbacks:**
  - No Worker, or a Worker that errors (`rwDown`): everything renders on the UI thread as before.
  - A voice that can't be posted: that pad renders on the UI thread.
  - A …waves voice when the Worker couldn't load …waves: that pad renders on the UI thread.
  - Both "no Worker" and "Worker crashes" were tested.
- **Same sound:** the Worker's output is bit-identical to the old main-thread render for the factory kit,
  every oscillator type (sample/grains/PWM with a sample), every pad FX (34) and a …waves patch.
- **Measured at 4× CPU throttle, phone screen:**

  | | Before | After |
  |---|---|---|
  | First 6 s after opening | 17 frames, 2 s freezes | 359 frames, no long tasks |
  | Dragging a synth-pad slider for 3 s | 32 frames over 100 ms, 2 s stalls | no frame over 33 ms |
  | PLAY | ~0.1–0.2 s | ~0.1–0.2 s |

  On a real phone, rendering still takes as long, but in the background. A pad tapped before its first
  render lands still renders on the UI thread (`ensureLive` with no cache).

## Modulation shown on sliders, as in ...Seeds (13 Oct, evening)

A cable into a slider's small purple jack (`p:<param>`) now always shows the ...Seeds treatment in **…waves and
…chains**: the value turns purple and bold, and a 3 px purple mark under the slider shows the live position.

- **…chains in …Thunder Plus was blind before:** the page runs no engine there, and the host never passed the track
  engines' `mods` telemetry on. Now each chains node keeps `n.mods`, and the …chains-screen meter timer in
  `index.html` merges them (first track engine wins per id/param) into `chainsEmbed.meters({peaks, load, mods})`.
- **…waves poly modules were blind with no note held:** telemetry read voice #0 only. `telemetry()` now rebuilds each
  modulated value from the cables: newest gated voice (else newest awake one), and mono sources (a free-running LFO)
  still count when every voice is asleep.
- **Cabled but no live data** (audio not started, no track routed through …chains): `paintMods` still marks the row,
  at the slider's own value; a 200 ms timer repaints when telemetry has been quiet for 400 ms (also clears unplugged).
- Telemetry now posts every 12 blocks (~31 Hz) instead of 24, so the mark moves smoothly.

## CPU meter, dropouts and clicks (13 Oct, evening)

- **The meter read 0% on browsers without `renderCapacity`** (headless Chromium 141; likely the phone too) unless
  track effects or …chains were on: pads, bass and the master never reported. Now the bass worklet and the master
  worklet report a Date.now()-averaged load like track FX do; `fxLoads()` lists `MST dyn` and `Bass` too.
- **Dropouts:** `glitchScan()` (every 100 ms) uses `ctx.playbackStats.underrunEvents` where it exists, else the slip
  between `getOutputTimestamp()`'s audio and page clocks (> 12 ms = one dropout; it follows slow drift). A dropout
  turns the meter red for 3 s; the tooltip and the click toast give the count. **Untested on a real device:**
  headless Chromium's fake output waits for a stalled audio thread, so it never underruns (no false positives seen).
- **Clicks:** a late `kill()` (UI thread stalled) fades from `currentTime`, not from a past time (which was an
  instant cut). Notes started part-way into a sample (slices, offsets) get a 1.5 ms fade-in.
- **Phone defaults:** latency is `latOf()`: the person's pick (`aset.latPick`), else Balanced in Mobile and Lowest on
  desktop. The sequencer looks 0.25 s ahead in Mobile (0.15 s on desktop).
- **Track FX sleep:** a `thunder-tfx` node whose input is silent (< 1e-7) and whose output has been below 1e-6 for
  1 s outputs silence without running its effects; any input wakes it; settings changes reset it. Long reverbs
  take ~10 s to get there. Measured: two reverb tracks 5–8% → 0% once tails die.

## Shared modules: …chains and …waves have the same effects and utilities (13 Oct, evening)

- **One source, two copies.** `chains/chains-worklet.js` holds a `/* ===SHARED MODULES START=== … END=== */` block (the
  DSP) and `chains/index.html` a `/* ===SHARED EDITOR START=== … END=== */` block (editor entries, help text, palette
  groups, displays). **Edit them in …chains, then run `python3 tools/sync-shared.py`**, which copies both into …waves.
  Each MODULES map spreads/assigns `SHARED_MODULES`; `mergeShared()` adds the palette groups (skipping a module the
  palette already lists); help falls back to `SHARED_ABOUT[t]` then `d.about`.
- **Moved into the blocks** (so …waves now has them): Beads, the 22 "More effects" (flanger, tremolo, overdrive,
  wavefolder, auto-wah, pitch shifter, ladder, cross-mod, space, lo-fi, drive, air, room, comb, transient, trance gate,
  multiband squash, frequency shifter, vinyl, dub, cab, tube), envelope/gate/pitch followers, slew (now labelled
  "Slew limiter"), sample & hold, quantizer, Blades, meta modulator, phaser, width, ping-pong delay.
- **New, original code:** Meter (VU + peak, pass-through, Trim), Limiter (2 ms look-ahead peak limiter, running-max
  detector, hard ceiling), Utility (gain/pan/mono/polarity/mute), Ring mod (own carrier sine/tri/square/noise or the
  Carrier input; Freq CV ±4 oct), Spring reverb (two springs: delay + 18 stretched z⁻² all-passes in feedback, drip
  pre-emphasis), Plate reverb (Dattorro's 1997 figure-eight tank, written from the paper), Ensemble (3-voice string
  ensemble or the two-speed 80s polysynth chorus), Math, Logic, Clock divider, Euclidean.
- **Displays:** a module def with `show(st)` returns numbers; both engines' `telemetry()` sends them as `shows`. Editor
  entries with `display:'meter'|'gr'` get `buildShow()`, painted by `paintShows()` from `onMeter` and, in …Thunder
  Plus, from the host (`index.html` keeps each chains node's `shows` and forwards them with the meters, like `mods`).
- **Checked:** every shared module run in Node on both worklets (no NaN, no runaway, tails decay; harness was a
  scratch script, not committed); spring RT60 1–3.5 s, plate 1.75 s at default; limiter holds −0.3 dB with 10 dB GR;
  plate costs ~4.6% of a desktop core (first version was 32%). Browser: all palette entries present in both editors,
  every new module adds, Meter/Limiter displays live standalone (…waves) and embedded (…chains in Thunder Plus).
- The Effects page's pad/track FX (`FX`/`TFX` in index.html) are separate and unchanged; the new reverbs aren't there
  yet (they're JS, not in the wasm).

## PAD mode: OPTION+arrows in Phrase move around the Song grid (13 Oct, evening)

- The owner expected OPTION+arrow on the phrase screen to go to the phrase above/below/left/right of it on the Song
  grid, not phrase number ±1. `optDir(dir, grid)`: the PAD d-pad (`op` held) and Alt+arrows in PAD mode pass
  `grid`; with PATTERN NAV on (default) `phraseGrid()` finds this phrase's block (`followPhraseBlock`), moves to the
  nearest filled block that way (same track for ▲▼, same row for ◀▶, skipping empties), opens its first phrase and
  sets `cTrack`/`cSect` as opening it from Song would. At an edge it toasts and stays. A phrase not in the song
  steps by number as before; `[`/`]` and KEYS-mode Ctrl/⌘+▲▼ still step by number. Help, the PATTERN NAV caption
  and the phrase hint say so. Shift+Option+arrows (`shiftAltArrow`) is unchanged.

## Mastering close on phones, master bus kept on FX, Dimension / BBD delay / Harmonizer (13 Oct, evening)

- **Mastering window:** its × sat in the wrapping header and got lost on phones. Now `.mwx` is a labelled CLOSE
  button pinned top-right of `.mwhead` (which reserves 120 px for it), and the phone's Back closes the window
  (`popstate` → `closeMst()` before the other handlers).
- **Master bus forgotten on FX:** `go()` pointed FX at `cTrack` whenever you came from any screen but the Mixer, so
  Mixer master → FX → Project → FX landed on a track. It was a bug (the master is FX target `NT`, no master track
  needed). Now only Song, Sect and Phrase (`fromTrack`) point FX/Mixer at the edited track; FX on the master → Mixer
  goes to the master row (y 7), FX on a track leaves the master row.
- **New shared modules (original code):** Dimension (two antiphase BBD lines cross-mixed for width, four modes),
  BBD delay (bandwidth from a 4096-stage chip at that time, saturating feedback that self-oscillates boundedly,
  wow/flutter, Age hiss, time glide = pitch bend, tempo sync, Time CV), Harmonizer (two voices, interval menus,
  level/pan each, detune, delay, window; splice-aligned delay-line shifter: each jump picks the lag that best
  correlates with the playing waveform, then crossfades). Checked: echo at 250.2 ms for 250 ms; harmonizer within
  0.3% by autocorrelation (the plain two-head version was ~2% out); CPU 2.5% / 4% / 4.5% of a desktop core.

## Voice engine: a singing robot (13 Oct, evening)

- **What:** synth slots can set `V.engine = 'vox'` (Synth → Engine: Voice). Settings in `V.vox` (`normVox`; `txt` is the
  lyrics, the rest numbers); `voxParams(V)` folds in the voice's Tune, Drive, Level. Original code, no third-party code.
- **`class VoxCore`** (ENGINE block, self-contained like BassCore; tables are static getters so `toString()` carries
  them into the worklet):
  - `parse(txt)` → syllables `{on:[…], nu, co:[…], w, word}`. Words: the dictionary (`dict()`, ~250 common words incl.
    contractions) else `rules()`: letter-to-sound rules written after the 1976 NRL rules (public domain), format
    `left|MATCH|right|PH` with `_` for the word edge. `[ARPAbet]` in brackets is used as is; `-` forces a syllable break.
    `syllabify`: one consonant between vowels starts the next syllable, of more the first closes this one; NG never
    starts one.
  - Synthesis: glottal-pulse table ↔ polyBLEP saw (Robot), tilt (Bright), breath noise (louder in the open phase;
    Voice 0 = whisper), five cascade resonators (Klatt form; F4/F5 fixed, all × Size), frication noise through its own
    resonator in parallel, DC block, envelope, glide, delayed vibrato. Controls glide to targets (~12 ms formants).
  - Notes: `on(leg, n, same)` plans syllable n: onset consonants (stops = closure + burst + aspiration; affricates =
    closure + burst + frication), then the vowel held (`hold`); `off()` plays a diphthong's offglide, the coda, then
    releases. `busy()` is true while the coda plays.
- **Live:** `thunder-vox` worklet (`VOX_PROC`, in the same Blob as the bass one) with AudioParams rate/gate/trig/leg plus
  `syl` and `same`. `makePlayer`'s `bassNode` makes a `vox` node per track+pad (key `t:pad:vox`); `bassNote` counts
  syllables per node (`sylN`, a slide keeps the current one) and `stopAll` resets them, so PLAY starts the lyrics over.
  `isLivePad` = bass or voice; offline renders await `bassReady` for either. Load shows in the CPU meter's Bass entry.
- **Pads/Worker/exports:** `renderVox` sings the whole line at the pad's pitch, one syllable per `sylLen` (Pad step).
- **UI:** `#voxWrap` card (lyrics textarea with syllable chips, Voice / Expression / Timing rows `VU`, presets
  `VOX_PRE`, Say it). `typing()` now counts textareas. Tracker side panel shows Level, Robot, Size, Vibrato, Glide.
- **Checked:** phonemes for test sentences; vowel spectra peak at their formants; 3.9% of a desktop core; a 12-note
  melody through `pass()` (the offline tracker path, same worklet) sings every note, no NaN; Worker pad render; live
  node outputs (peak 0.54) and reports load. Not yet judged by ear on the owner's phone.
- **Ideas next:** a tracker command to jump to syllable N; CMU-dictionary pronunciations; per-note vowel colour.

## Fix: tapping a pad didn't select it (13 Oct, evening)

- `vuHit()` called `slotAt(hits, i)`, which pads `hits` with `null`, then pushed into `hits[i]`: the first tap on any pad
  threw, so `padPress` never reached `select(i)` (and the VU loop's `hits[i].filter` could throw the same way). Now
  `hits[i]` is made an array first, and `padPress` selects the pad before sounding it, so a sound error can't stop it.

## Blip engine, Blip drum and Life (13 Oct, evening): Newscool-style generative drums

The owner asked for drums like Reaktor's Newscool (eight simple tone voices driven by a Game of Life sequencer).
All original code; the name stays generic.

- **`class BlipVoice`** (one copy in index.html's ENGINE block, one in the SHARED MODULES block; keep them identical):
  parabolic tone, kick (pitch drop ×2^(kick·5·env), Kick time), FM (sine at fmR × pitch, phase mod), ring mod
  (ringF Hz), noise, click, linear attack + exponential decay, drive, bipolar SVF (filt < 0 LP, > 0 HP, resonance
  level-compensated), grain stutter (re-hits every grainT, ×0.72 each). `render(out, a, b, g)` ADDS into out.
- **Blip engine on pads** (`V.engine = 'blip'`, settings `V.blip`, `normBlip`, `renderBlip`): any pad, drums too. The
  Engine row now shows on every pad (Layers / Bass / Voice / Blip); on drum pads Bass and Voice render one-shots.
  Card `#blipWrap` (`BLU` rows, `BLIP_PRE` presets, Random). Tracker side panel shows Level + six blip rows.
- **Blip drum module** (`blipdrum`, shared): eight voices on gate inputs t1–t8, per-voice params `p k f m n d a x` + voice
  number (pitch, kick, FM, ring, noise, decay, level, filter), hidden in the card and edited as bars (`display:'bars'`,
  `Edit` picks the row); globals FM/ring ratio, kick time, decay scale, drive (on the mix), LFO → pitch, width,
  feedback delay (tempo divisions), level. `show()` returns each voice's level, so the bars light as they play.
- **Life module** (`life`, shared): 16 × 8 torus, board in hidden params b0–b7 (row bitmasks), edited on a canvas
  (`display:'life'`; Clear / Random / Glider / Blinker). The playhead advances one column per Step (or per Clock pulse);
  live cells in that column fire their row's gate (Fire on Born: only new ones); after each sweep, one generation
  (rules Life, HighLife, Seeds, Maze, Day & Night). Loop N reloads the drawing every N generations (0 = evolve).
  Changing the drawing reloads it. Outputs g1–g8, Any, Count. `show()` = [x, gen, rows…] for the live grid.
- **Editors:** a param whose ui has `hide:true` gets no row (both editors' buildNode). `buildShow` routes 'bars' and
  'life' to `buildBars`/`buildLife`; `paintShows` feeds them; a 90 ms timer redraws them.
- **Checked:** the glider walks correctly; Life → Blip drum renders (no NaN), both editors add, cable, run live (grid
  advances, bars light), tapping the grid toggles cells, dragging bars sets values; Blip on drum pad 0 renders.
  CPU: Life ≈ 1–2%, Blip drum depends on how many voices ring (all eight constantly ≈ 20% of a desktop core worst case).

## Degrade (lo-fi) everywhere (13 Oct, evening)

- **`class DegradeCore`** (original; three identical copies: index.html ENGINE block, the track-FX worklet Blob is built
  as `DegradeCore.toString() + TFX_SRC`, and the SHARED MODULES block): bits (continuous, 24 = off), rate (sample and
  hold), noise floor (RMS = noise² × 0.25, −∞…−12 dBFS, constant across Colour), Colour (one-pole darkening,
  loudness-compensated), Follow (noise × input envelope, so silence stays clean), separate Dry/Wet. Stereo, in place.
- **Pad FX** `degrade` (`js:true`: `fxReady` doesn't wait for wasm; `applyFx` runs it first). **Track FX** `degrade` (TFX id
  99, `LOFI`): the worklet's `set()` makes a JS slot (`s.js`) instead of a wasm one and `process()` runs it; JS slots are
  never `tfx_free`d. It is NOT in `tools/fxgen.py` (hand-added). **Module** `degrade` in …chains/…waves (rate 0–1 there).
- **Checked:** noise RMS matches the formula at every Colour; Follow 100% drops the hiss to −47 dB 0.35 s after the
  sound; pad FX −24 dB floor at Noise 50%; track FX through the worklet −18 dB floor at NOISE 180 (vs −240 without).

## Resonator (chord) (13 Oct, evening)

- **`class ResonCore`** (original; three identical copies like DegradeCore; the track-FX Blob is now
  `DegradeCore + ResonCore + TFX_SRC`): up to 5 Karplus–Strong loops tuned to Note + a chord (`CHORDS`, 12 shapes),
  one-pole damping in the loop (Bright), feedback from a T60 (Decay: 0.05 × 160^decay s), Tube = negative feedback with
  the loop halved (odd harmonics, same pitch), loop length minus the damping filter's delay, DC block, tanh out,
  Spread pans the voices. Measured: in tune (110/220/440 Hz exact; String and Tube), < 1% of a core.
- **Pad FX** `reson` (js), **track FX** `reson` (`RESO`, id 98, in `TAILS`), **module** `reson` (Note CV: 0.5 = A4,
  0.125/oct, the knob transposes from C3). Rings and Comb resonator are still there; this is the chord one.
- Test gotcha: `pass(evs, …, getB)` needs a real `getB` (a buffer per pad), or the notes are silent.

## Karplus–Strong: Pluck string layer and …waves String (13 Oct, evening)

- **`class KSString`** (original; two identical copies: index.html ENGINE block and waves-worklet.js, outside the shared
  block because String is …waves-only): `pluck(P, bright, pos, amp, rnd)` adds a noise burst (one-pole darkened,
  circular comb for the pick position, zero-mean, normalised) to the loop; `tick(P, t60, a)` = linear-interpolated
  read at P − (1−a)/a (damping filter delay), one-pole damping, loss per pass from T60 (cached), DC-blocked out.
  Measured: within a few cents from 55 Hz to 1.76 kHz; 0.1% of a core per voice.
- **Layer type `karplus`** ("Pluck string", Bright / Decay = 0.08 × 100^b s) in `TYPES` + `newOsc`; plucks on its first
  sample with the layer's seeded random, so renders repeat. **…waves module `string`** (poly, Pitch + Gate in; Tune,
  Bright, Decay, Position, Mute = ring time cut while the gate is low, Level).

## 13–14 Oct (night): fixes, presets, four engines, pitch FX, Pitch Track

Commits on `main`: 9ababb0 (fixes), e21c605 (presets + song-load fix), e8f2970 (engines, poly voice, icon), e6b1f5c
(vintage sampler, pitch FX, Pitch Track). Cache is `thunderplus-v83`; `chains-worklet.js?v=16`, `waves-worklet.js?v=12`.

- **Big fix: the saved song was lost on every reload.** `normSong` called `nslots()` (a const declared later) while
  `let S = …` ran at start-up; the throw fell into the catch and loaded `demoSong()`. Now `nslotsEarly()`.
- **Layout:** `fitCheck()` also shrinks the text to the widest line (Song/Mixer have 16+ tracks, more than BASEW);
  MONO/POLY `.tgbox` boxes are positioned in em; `fonts.ready` / `loadingdone` redraw. Settings (`.gset`) is a fixed
  sheet on screens ≤ 600 px.
- **Phrases list** (File ▾ → Phrases…, `phlOpen`/`phlDraw`/`phlPlace`): phrases with notes, filter "not on the song".
  **`pendPlace`**: moving from an empty song block into Phrase opens a fresh phrase and `placePending()` (via `saveS`)
  puts it in that block on its first note.
- **Voice:** rests (`. , ; : ! ? _` → `{rest:true}` syllables, `on()` silences); `hiss` (Consonant noise) and `clar`
  (Clarity: narrower bandwidths + presence) params; softer aspiration/burst; Breath default 0.3. **Poly tracks:**
  `polyVox()` in `makePlayer` uses up to 4 vox nodes per track+pad (`bassNode(t, pad, j)`), chord notes share a syllable.
- **Presets:** `factoryVoicePresets()` / `factoryLayerPresets()` (layer presets carry `vt`, the pad tune they were
  written for, and are re-pitched), song presets in `S.presets` (`normPresets`; samples by ref; `smpTidy` keeps them),
  File → Export song presets / Import presets (exports embed samples).
- **Engines** (ENGINE block, table `XENG`, settings in `V[V.engine]`, `normX`/`xParams`/`renderX`):
  `AcidCore` and `MachCore` are live (worklets from `XPROC(name, core)` in the bass Blob; AudioParam `acc` = accent;
  the sequencer sets `ev.acc` when volume ≥ C0); `ToneDrum.render` and `ModalCore.render` are rendered. One shared card
  `#xWrap` built from `XUI[kind]` (rows, sections, presets). Deck instrument rows are generic.
- **Vintage sampler** (sample layers): `L.vint` / `L.vintF`, `VINT` table, `vintData` (cached per sample) + `vintOsc`.
- **Pitch delay / Pitch reverb:** `PitchFxCore` (two-head granular shifter in/after a delay or a small reverb; loop gains
  kept under 1 — the reverb is normalised by (1−fb) and the shifter by 0.75). Pad FX `pdelay`/`preverb` (js), track FX
  ids 97/96 in the TFX worklet Blob.
- **…chains Pitch Track** (`pitchtrk`, shared block, synced to …waves): YIN on a 4× decimated buffer, ~90 runs/s.
- **Icon:** the two dogs' faces split down the middle, `icons/plus-*` (`?v=2` in manifest, page and sw).
- **Not yet heard on the phone:** all of the above was checked headless (renders, no NaN, levels, live nodes through
  `pass()`). Wavetable import (Serum-style 2048-sample frames) is deferred at the owner's request.

## 14 Oct (midday): swing timing fix, four dog dances

- **Swing/row timing fixed** (song `v:6`). A row used to last `pair·line·(sw or 1−sw)` with `pair = 2·swB`, so every row
  lasted SWING NOTE's number of lines (16ths, the default: 2) and LPB 4 played 8th-note rows. Now `2·line·(…)`: a row
  averages one line whatever SWING NOTE says. **Old songs are converted in `normSong`** (v < 6): LPB ÷ swB (else BPM ÷
  swB when LPB doesn't divide), phrase LPBs ÷ swB, TICKS × swB, TIC values × swB, so DEL/RET/KIL/OFF/PSL/ARC keep
  their length. Verified against the previous build: identical note times for 50/66/58/60% swing, swB 1–3, LPB 3/4/6,
  with DEL and RET in the phrase. `swingNote()` toasts once ("now reads LPB 2, 12 ticks…"). New songs default to LPB 4
  (true 16ths; the owner's earlier "LPB 8 default" was compensating for this bug). Grooves with a 0 (= "ticks a
  step") digit in old songs would stretch with the new ticks; not converted.
- **Settings → App → Show …waves and …chains on Mobile** (`mobEd`, `thunderplus-mobile-editors-v1`, off by default; the
  row only shows in Mobile). `noEditors()` = Mobile and not ticked replaces `isMobile()` in every editor gate (tabs,
  setView, openWaves/openChains, chains focus, File/Project chains rows); CSS uses `body.noed` for the waves-layer button.
- **Master phrases are commands only** (`mstView()` = `cTrack === MT`): `pCols()` gives just the FX columns, the phrase draw
  skips notes/V/I and neighbours, the picker and EDIT+▲▼ use `FXLIST_MST` / `MST_FXC`, letters only match those, the hint
  and the stage (master FX chain) change. Same row format underneath, so a phrase can still be used anywhere. Also fixed:
  EDIT×2 on the LEN/LPB line (`c.y === -2`) threw in `editDouble`.
- **Dogs:** legs are now groups (`.leg.ll/.lr`, `--lL/--lR`). `DOG_MOVES` up / down / legs / both; `dogsFollow()` (on each
  beat) advances the dance when the followed track (cursor's, else the first playing) starts a phrase: another block, or
  its step wrapping back (a repeat). Verified live: a new dance every 16 rows.

## 14 Oct (late morning): Piano, Tine EP, Organ, SoundFont / General MIDI

Cache `thunderplus-v85` (sw.js no longer caches `sounds/`: the app keeps fonts in IndexedDB).

- **Keys engines** (ENGINE block, rendered like Modal; `XENG`/`XUI` `piano`, `ep`, `organ`): `KeysUtil` (rotating-phasor
  partial banks, `finish()` = the usual normalise/drive/trim), `PianoCore` (stiff-string partials, hammer/strike-point
  spectrum, two-stage decay, 1–3 detuned strings, thump, 3-mode soundboard, dampers/pedal; ≤12 s), `TineEPCore` (tine +
  tone bar + bell pings through a 1/(1−kx)² pickup; tremolo), `OrganCore` (9 drawbars with foldback, percussion, click,
  rotary with horn Doppler, Pluck = the M1 "Organ 2"-style house organ preset). Checked in Node: no NaN, 10–400 ms each.
- **SoundFont engine** (`V.engine = 'sf'`, numbers in `V.sf` via XENG, font in `V.sfFont` / `V.sfFontName`). Code is in
  the UI part of the script (before `XUI`): `parseSF` (SF2.04: zones with preset/instrument generator merging, modulators
  merged by identity over `SF_DEFMODS`), `sfVoice` (generators + velocity/key-number modulators; velocity→attenuation is
  skipped because the tracker volume already sets loudness), IndexedDB `thunderplus-fonts`, `sfLoad`, `sfSample` (SF3
  Ogg via `decodeAudioData` on an OfflineAudioContext; SF3 loop points are relative to the sample start, SF2 absolute),
  `sfPick` (linked stereo pairs → one stereo buffer; FluidR3's pairs are unlinked, so each half plays with its own pan).
  **Player:** `startNote` hands SoundFont pads to `sfNote` (per-zone buffer source + volume-envelope gain + low-pass with
  the mod envelope + zone pan, one voice with `srcs` for kill). `pass()` awaits `sfPrepare(evs)`; `tStart` starts it in
  the background. **Pad render** (`renderX` → `globalThis.__sfRender`, because the ENGINE block can't see the UI code):
  C-4 at velocity 100. `rwCan` keeps these pads off the Worker. **UI:** `sfPanel()` (font select, preset list, Load
  .sf2/.sf3, Remove) at the top of the engine card; XUI presets are GM shortcuts.
- **GM sounds:** `sounds/FluidR3Mono_GM.sf3` (MuseScore's MS_General v0.1 fork of FluidR3Mono, MIT, 23.7 MB) with its
  licence beside it; fetched by `sfFetchGM()` on first use. Tested headless: loads in ~0.6 s, piano + drum kit decode in
  ~1.9 s, offline and live playback, OFF releases, kits.
- **Not done:** SoundFont LFOs (vibrato/tremolo), chorus/reverb sends, linked modulators, CC modulators.

## 14 Oct (morning): master column, tempo-locked FX, swarm, skins, Settings

Cache `thunderplus-v84`. Worklet versions unchanged.

- **Master column (MST):** song column `MT` (= `NT`, index 32: `S.song` rows are now `NT + 1` long, `normSong` reads it).
  Song screen only (after the poly tracks, amber `.tgbox.gx`, `--tk-mst`); not in Arrange. The song cursor sits on it
  as `cs.song.x === MT` (`move()` steps from the last track to it; `draw()` doesn't clamp it); `cTrack = MT` when its
  phrase is open (sub label MASTER, no neighbours). `trackWin(c, lab, extra)` reserves its room only when the tracks
  overflow. `makeSeq` runs `tr[MT]` like a track but plays no notes; `toggleBlockMute` refuses it (smute is a 32-bit mask).
- **Commands** (end of `FXC`): `FXO XY` (slot X 1–4, 0 = all; Y 0 off, 1 on, 2–F on for Y rows then back),
  `FLX` (Flex pattern 0–16), `GAT` (Step gate pattern 0–15), `RTG XY` (X rate 1–8, Y rows). `fxEvs()` in `makeSeq`
  turns them into `{type:'fxc', track, p, v}` events; in the MST column `VOL` becomes `{type:'mvol'}`. Rows for momentary
  lengths are the average played row (`pair * line / 2`, see the swing note below).
- **How they reach the audio, sample-accurately live and offline:** `thunder-tfx` now has AudioParams `bp` (a-rate:
  the song position in beats) and k-rate `sw0`–`sw3`, `flx`, `gat`, `rtg` (−1 = as set). The player owns a
  ConstantSource **beat clock** (`clock`, ramped by `{type:'beat'}` events that `makeSeq.step` emits every step, wraps
  at 16 beats, −1 stopped) connected to every effect node's `bp` (`clockTo`, `setMfx` for the master node). `fxc`
  events are `setValueAtTime` on the node's param; `stopAll` → `resetSeq` puts them all back to −1 and the master gain
  to `S.master`. `tfxChain` now sends **off slots too** (`on:false, slot:k`) so FXO can switch them on; nodes exist
  when `fxAny()`. Slot switching fades 5 ms (`s.g`); an off Flex is still fed (`feed`).
- **Effects** (original, JS, in the TFX Blob: `clkSpb`, `FlexCore` id 95, `StepGateCore` 94, `RetrigCore` 93; TFX keys
  `flex`, `sgate`, `retrig`; not in `tools/fxgen.py`, hand-added like `degrade`). Not on pads or in …chains yet.
- **Swarm engine** (`SwarmCore`, ENGINE block, rendered like Modal; `XENG.swarm`, `XUI.swarm`, 12 presets): ≤48
  polyBLEP voices, drift paths at control rate (32 samples), scatter intervals, bloom, shimmer, TPT low-pass. ~0.5 s for
  a 48-voice 6 s note in Node.
- **Pitch reverb fix:** shimmer loop gain was > 1 at large Size (the reverb is ~2.4× at its low resonances), so it
  self-oscillated; now Size tops at fb 0.94 and the shimmer loop is scaled 0.38 (measured to decay at every setting),
  plus a slow limiter on the loop.
- **Skins:** terminal skins' cursor is `--mxcur` (default the accent `--mxpoly`), never the playhead's `--mx`; cool
  cursors on the single-colour ones, lavender `#9a63ff` on Simple grey, which is now the first-run default. Monotone blue.
- **Settings:** Audio (+ **Reset audio engine**, `resetAudio()` → `pl.dispose()` and a fresh graph), Project (Song,
  Input, Render & export with the `pExport` panel, Mastering with `pMaster` + LIMITER/MASTER rows), Screen
  (`buildScreen`: the Screen rows + `pSkin`), MIDI, Kit, App. `INNER_SECS` are re-parented on each open.
- **FX target follows** the edited track (`draw()` sets `cFxT` on Song/Sect/Phrase). **LPB default 8** (new songs).
- **Mutable:** `git clone --depth 1 https://github.com/pichenettes/eurorack`, then
  `git -C eurorack submodule update --init stmlib`. Use the current upstream: older checkouts lack Beads.
- **DaisySP:** `git clone https://github.com/electro-smith/DaisySP`.
- **Airwindows:** a sparse checkout of `airwindows/airwindows` with the plugin folders listed in `build.sh`.
- **Cloud Seed:** `git clone https://github.com/GhostNoteAudio/CloudSeedCore`.
- **Toolchain:** wasi-sdk 25.

```
E=eurorack D=DaisySP A=airwindows/plugins/LinuxVST/src C=CloudSeedCore WASI_SDK=wasi-sdk-25.0-x86_64-linux sh dsp/tfx/build.sh
```

Then copy `dsp/tfx/tfx.wasm` to `chains/dsp/` and `waves/dsp/`, put its base64 into the `tfxWasm` script tag
in `index.html`, and delete the build output from `dsp/tfx/`. **Check the old effects still match**: render
ids 0–10 with the old and new wasm and compare (this session they were bit-identical). Then bump the
caches.

## 14 Oct (afternoon): the sequencer never renders a pad mid-song

- **Risk found:** `pump()` books notes from a 25 ms timer on the UI thread. A note on a pad with no usable
  render (new pad, first moments after opening, sample-rate change) made `ensureLive()` fall back to `ensure()`,
  rendering on the UI thread: measured 588 ms for a synth slot at 4× throttle, longer than the look-ahead, so
  notes came late. Chopped/synced instruments did the same through `ensure()` inside `makeSeq`.
- **Fix:** `ensurePlay(i)` (next to `ensureLive`): while `tp.on`, a pad the Worker can render returns `null` and
  goes to `bgRender`; the note is silent until the render lands (the player already skips a null buffer).
  `rtBuf`/`rtBufPlain` and the live `makeSeq` (`o.live`, set by `tStart`) use it. Offline renders, exports,
  `tStart`'s own pass and previews while stopped still use `ensure`/`ensureLive`. SoundFont voices, …waves without
  the Worker's copy, or no Worker at all: render here as before.
- **Look-ahead** is now 250 ms on every device (was 150 ms on a computer). Tracker edits and mutes reach notes
  up to ~100 ms later; pads, keys, previews and STOP are immediate.
- **Verified headless (4× throttle):** `ensurePlay` on an unrendered synth slot returns at once (0 ms) and the
  Worker's render lands in the background; the old path took 588 ms. Not yet heard on the PC or the Fold.
- **Later (the proper fix):** move the sequencer and note playback into an AudioWorklet, so nothing on the UI
  thread can delay a note. That is also the shape a native (C++) engine needs.
- Cache `thunderplus-v90`.

## 14 Oct (late afternoon): MIDI out, clock out, clock in

- **Where:** the `/* ---------- MIDI out and MIDI clock ---------- */` block right after the MIDI input code.
- **Instruments:** `I.mo` (0 PAD, 1 MIDI, 2 PAD+MIDI) and `I.mch` (0–15), on INST as OUTPUT and MIDI CH. Note events
  from `makeSeq` now carry `ii`, `mn` (tracker note + 12, so C-4 = 60) and `vel` (volume column / 255; retrigs fade it).
- **Sending:** `pump()` passes every event through `moEv()` first (it returns true for MIDI-only notes, which the
  player then skips). Mono tracks end the previous MIDI note at the next note; poly tracks keep 4; OFF, KIL and
  muted blocks end notes; a note with no end is held until one of those. `msend()` timestamps with `a2p()`: the audio
  clock mapped to `performance.now()` at the moment of hearing (`getOutputTimestamp`, offset smoothed in `amap`,
  since raw calls jitter ±2 ms). Gear delay (`midi.lat`) sends notes earlier, not the clock.
- **Stop / changes:** `moStop()` (in `tStop`) = `moRelease()` (note-offs for everything, including notes booked
  ahead; `clear()` where the browser has it; CC 123 on used channels) + MIDI Stop. Changing OUTPUT/MIDI CH while
  playing only releases notes, so the clock keeps running.
- **Clock out:** `moClock()` at the end of each `pump()` sends ticks up to `seq.time` from the last `'beat'` event,
  at the current tempo (straight; swing moves notes only). Start goes out on the beat-0 event.
- **Clock in:** `clkIn()`: Start/Continue restart PLAY via `playCtx()`, Stop stops it. Tempo is the mean over the
  last 48 clocks; every sixteenth `setTempoLive()` sets `tp.seq.bpm` (exact) and `S.bpm` (rounded). Phase: the
  device's beat (clocks since Start / 24) against ours at the moment that clock is heard; tempo is nudged by err/2,
  at most ±8% while |err| > 0.05 beat, ±3% after. Song Position Pointer is ignored (Continue starts like Start).
- **Exports:** `pass()` skips MIDI-only notes. Settings (port id, clock on/off, delay) live in `thunderplus-midi-v1`
  on the device, not in the song; OUTPUT and MIDI CH are in the song.
- **Verified headless with a fake Web MIDI device:** notes on the right channel and velocity, booked 100–390 ms ahead;
  clock intervals 20.83–21.36 ms at 120 BPM (20.83 expected); Start first, Stop and All Notes Off at STOP. Clock in
  at 100 BPM: tempo 100, phase error 0.17 beat at Start, under 0.01 beat after about 8 beats. Not yet tried with real
  gear (MPC One, Force).
- **Not sent yet:** pitch bend for slides/PBN, arps, CCs; Song Position Pointer.

## 14 Oct (evening): instrument list

- Settings → INST LIST (`ck: 'Show instrument list'`), **off by default**, stored in `thunderplus-instlist-v1`
  (`inlOn`, `setInl`). `instList()` runs at the end of the tracker `draw()` and only rebuilds when the names change.
- One row per instrument: its number and its pad's full name (`MIDI ch` for MIDI-only, `+MIDI` for PAD+MIDI),
  current instrument highlighted, long names cut with an ellipsis. A tap sets `cInst` and opens INST.
- Where: `#tInlSideW` in the side panel when the panel is showing; otherwise `#tInlPad` between the d-pad and the
  keys in PAD mode (hidden under 560 px wide, where there's no room). Never both at once.

## 14 Oct (evening): Simple rainbow skin

- `rainbow` in `SKINS`, base `term`, so it is Simple grey's look (white, outlined keys, solid PAD keys).
- Colour by position: `--ci` = an element's place among its siblings (nth-child, 12 steps) for tracker spans,
  buttons, pads, meters, keypad keys and Arrange blocks; `--ri` = the tracker line (12 steps). Hue
  `(ci + ri) × 30°` in OKLCH at one lightness. Buttons get their own `--mx`/`--mxdim`/`--mxsoft`, which the term
  rules already use, so outlines, text and solid fills all follow. Cursor and playhead cells keep their colours.
- **Simple rainbow dynamic** (`rainbowdyn`): the same CSS, plus `rbWheel()` (called from `applySkin`) setting
  `filter: hue-rotate()` on `<html>` ten times a second, 0.3° a step, once round every 120 s. Not a CSS animation:
  on the root that recalculated and repainted every frame (60 recalcs/s); this is 10/s, about 8 ms of main thread a
  second on a desktop. Paused while hidden, off with reduced motion. Fixed overlays (Help, toasts) are unaffected.

## 15 Oct (afternoon): review risk fixes, one commit each on main

After the eight bugs below were merged (PR #1), the review's risk items went in as separate commits. Cache is now
`thunderplus-v118`; `chains-worklet.js?v=20`, `waves-worklet.js?v=15`, `master-worklet.js?v=3`.

- **Effects can't be hung or silenced by a bad sample** (93d3ae3): the wasm only checks its output, and a NaN/∞ on the
  way in made the tape delay's `tfx_process` never return (frozen audio thread) and left twenty effects silent. The
  three JS call paths (track-FX worklet, …chains/…waves `effect()`, pad-FX `applyFx`) zero non-finite or |x| ≥ 8 input
  samples, and when an effect's output is no longer finite it is freed and made again with its settings (`s.t`/`s.p`
  kept on the slot). Checked in Node against the real wasm.
- **Replaced worklet nodes are released** (6ee130b): a processor that keeps returning `true` can never be collected, so
  each EQ-band/mastering change leaked a master chain (its stale meters flickered over the new one's), each chains
  on/off an engine node, each engine switch a bass/vox/acid/mach node. `{dispose:true}` / `op:'dispose'` make
  `process()` return `false`; `mstDrop()`, the chains rewire and `bassDrop()` disconnect the old nodes.
- **Transport** (837caca): the 25 ms pump ticks from a Worker (`pumpTimer`), so a MIDI-only song with the screen off or
  a muted master no longer stalls when the page timer is throttled; `audioInterrupted()` resumes a suspended/interrupted
  context on `statechange` and when the app returns, and stops the transport with a toast if it stays stopped 2.5 s.
  `pump()` already re-anchors the timeline when more than 0.5 s behind (from the dialog fix).
- **MIDI clock** (020b76b): ticks are booked ≤ 80 ms ahead (not the 250 ms look-ahead) and Stop is timestamped after the
  last booked tick (`mx.clk.last`), since Chrome can't take booked messages back. Fake-port test: none after Stop.
- **Samples and tabs** (67be903): `smpTidy` counts refs in the saved kit/song and skips entirely while another tab answers
  on `BroadcastChannel('thunderplus-tabs')`; a `storage` event from another tab toasts once. `smpGet` marks `own`, so
  samples borrowed from ...Thunder's store are copied into ours by `save()`; `thunderTidy()` removes `thunder-kit-v1` /
  `thunder-song-v1` once the import answer is yes/auto and everything is saved here with every sample in our store.
- **First PLAY and Space** (646904e): `tStart` sends un-cached Worker-capable pads to `bgRender` (3.7 s → 82 ms at 4×);
  `ensureLive` keeps a render at another sample rate while the Worker re-renders. Space reverb's Size is capped at 0.99
  (`SPACE_SIZE_MAX`, in the ENGINE block): at exactly 1 it never decays (5× the input, every Decay); 0.99 is fine.
- **Desktop/CI** (2ce6a3b): `will-navigate` blocks all navigation (dropped files), single-instance `return`, no `media`
  permission, page ignores drag/drop; workflow `contents: read` with `write` only on `release`; release notes explain
  SmartScreen / Gatekeeper; exact dependency versions. **Still to do for a real release:** commit a `package-lock.json`
  from the first real `npm install` and switch the workflow to `npm ci`; sign/notarise so electron-updater verifies.
- **Performance pass (15 Oct, afternoon), one commit each:** `draw()` measures layout only when a signature changed
  (bd93010: Song 19.7 → 4.6 ms a draw on the phone at 4×); Sect/Phrase light the playhead in place (09fecf1: Phrase
  22 → 5.2 ms); one `scheduleRefit()` for every shape change, grip drag ≤ 8 fps, frame loop at 10 Hz when idle
  (ddd1354); the two wasm modules are fetched from `waves/dsp/` instead of base64 in the page (index.html 3 MB → 1 MB;
  `tfxLoad` promise gates `tfxReady`/`chainsReady`; the service worker precaches with `cache:'reload'`).
- **Performance pass, part 2 (15 Oct, late afternoon), one commit each:** synth ↔ tracker is one layout pass with a
  cached character width and a per-render waveform envelope (5887b1d: 807 → 559 ms of main thread per round trip at
  4×, the rest is the browser laying out the shown page); no per-block garbage in the modular engines, the track-FX
  worklet and the Plaits/Elements voices (6020c92: a Plaits patch 123 → 28 scavenges / 40 000 blocks); released …waves
  voices sleep once their exit outputs (`m.exits`, set in `compile()`) have been under −100 dB for 0.25 s, and track-FX
  nodes sleep during playback unless the chain has a tempo-locked slot (cc5e5d4); the dynamic rainbow turns `--rbo`
  instead of filtering `<html>`, and ±1e-18 goes into every wasm effect against denormals (05cb255: Ladder on silence
  710 → 40 µs a block on x86; Clouds keeps a 3× idle cost from its own buffers).
- **The last of the review (15 Oct, evening):** Suppressor frame staged over the hop's four blocks, latency 2560
  (a9b4fc6: output bit-identical, p90 block cost 631 → 203 µs); load shedding steps back up after 8 s of calm and
  …waves' all-notes-off fires at most once per 10 s (1a3c7c4); …chains full telemetry only while its screen shows,
  'load' mode otherwise (12806cc, 28.5 → 3.5 msgs/s), which also found and fixed a crash when leaving …chains/…waves
  before the editor had started; cover-screen top bar on one row, Settings sheet under the keyboard, overscroll off
  (847a1bb). Not done: creating module states "off the audio thread" — a worklet's message handler runs on the
  rendering thread, so there is nowhere else to do it.
- **Hint line wraps** (15 Oct, evening): the tracker hint was one line with an ellipsis, cut off on every phone screen
  (it is how the PAD gestures are discovered). Now two lines (`.thint`, `-webkit-line-clamp:2`); a longer one gets
  `.more` and opens fully on a tap (`.open`, with a refit). Phone grid font and row count unchanged; desktop ~5 %
  smaller text on a 900 px window.
- **Owner's take on the UI suggestions (15 Oct):** the COMMANDS row and the two bottom play keys stay as they are — on
  touch, single-tap CLN↓/CUT/PST and pattern-vs-song play under the thumb are the point. The top "▷ Song" button stays
  too (the only play control when the key block is hidden; useful on desktop). Later, maybe: a PAD KEYS toggle in
  Settings → Screen (COMMANDS/KEYPAD/BOTTOM/SIDE already have one; the d-pad block in PAD mode doesn't), and a
  swipe-down to hide the lower block — with the caveat that it removes no-look navigation, so not a priority.
- **Preset browser (15 Oct, evening):** `presetSheet(kind)` — kinds `voice`, `layer` (the song's presets + factory
  groups, Recent, search, ▶ preview via `renderAsync` on a copy, ✕ delete, Save/Import) and `bass` / `blip` / `vox` /
  `x` (the engine cards' own lists; preview runs the card's preset function on a throwaway copy of the pad,
  `onVoiceCopy`). **To put the engine cards back on their native dropdowns set `ENGINE_PRESET_SHEET = false`** (one
  line next to `PRK`): the `<select>`s are still filled and wired; while it is true each one is hidden behind a
  `#…PreBtn` button (`.engpre`) that opens the sheet on click. `vPre` / `lPre` are buttons too; `fillPresetSel` is
  unused and can go once the sheet is settled.
- **Engine preset menus closed at once on touch (16 Oct; cache v136):** they opened on pointerdown over the select, so
  the tap's click landed on whichever preset row of the new sheet was under the finger (Piano / EP / Organ / Table
  lists reach the card header): it took that preset and closed. Now buttons that open on click, and both sheets
  ignore taps in their first 350 ms (`early()`). Checked with real taps at 412 and 884 wide. Cover-screen (≤480 px)
  engine card headers are a grid: name + full description, Random / Init beside them, Presets full width below.
  The engine card's footnote (`#xSecs>.note`) spans the grid with the sections' 12 px inset and a hairline above.
- **Three more rendered engines (15 Oct, night; cache v130):** `TableCore` (wavetable: four 8-frame tables drawn in
  the time domain, taken apart once with a DFT and rebuilt band-limited per note; Position/Warp, position envelope +
  LFO, unison, sub, noise, SVF low-pass with envelope, ADSR), `BreathCore` (coloured noise → grains → a bank of up to
  8 resonant band-passes with Spacing/Metal/Ring → HP/LP with envelope, Sweep for risers, Wind random walk; hats,
  shakers, cymbals, risers, wind, static) and `StringsCore` (plucked-string loops with a damping low-pass and a
  fractional tap — the tap is pulled in by the low-pass's delay so tuning holds —, pick/strike-point excitation,
  chord voicings strummed up or down, 4-mode body sized mandolin→cello, release damping). Same shape as the keys
  engines: `XENG` / `renderX` / `XUI` (`table`, `breath`, `strings`), engine grid is now an exact 4×4, factory
  voice groups "Table" / "Breath" / "Strings". All original code. Checked in Node (no NaN, pitch within ~1 %,
  20–230 ms a render) and headless (cards build, every preset renders, sheet lists them). Not heard yet.
- **Six more engines (15 Oct, late; cache v131):** `FmDrumCore` (3-op FM percussion, three algorithms, pitch/index
  envelopes, feedback, noise, click, curve), `WindCore` (blown/bowed waveguides: Reed = single reed on a half-length
  bore with the reed curve 0.7 − slope·Δp; Jet = jet delay + bore with the x(x²−1) jet curve, bore at the full period
  and a dark bore so it sits on its fundamental; Bow = bridge/neck delays with the (|Δv·slope|+0.75)⁻⁴ friction curve.
  Each model's controls are mapped into the narrow region where it oscillates (reed: pressure·slope ≈ 0.22–0.29;
  jet: pressure·gain ≈ 1.05–1.15 with jet ratio 0.38–0.48; bow: velocity·slope ≈ 0.48–0.58) — found by sweeping in
  Node, see the matrix test — and a self-trim runs the loop for 0.5 s, autocorrelates near the asked period (±16 %
  only, so an octave jump is left alone) and scales the delays), `PhaseCore` (CZ-style phase distortion, 8 waves incl.
  three windowed-resonance ones, DCW ADSR, two oscillators, ring, 2× oversampled), `PartialsCore` (up to 256
  harmonics on the KeysUtil bank; tilt/odd-even/formant/stretch, spectrum B morph, per-partial decay tilt, brightness
  envelope, drift), `SkinCore` (circular-membrane modes at the Bessel zeros, weighted by J_m(z·r) at the strike point,
  mallet hardness roll-off, tension pitch bend, damping/muffle, a shell resonance), `ChipCore` (frame-rate 60 fps
  8-bit voice: pulse duties + sweep, 4-bit triangle, 15-bit LFSR long/short, arp chords, slide, stepped vibrato,
  NES-style period quantisation). Engine grid is 22 cells now (room for two more makes 6×4). Same checks as the
  previous three. Original code throughout (the physical models follow the textbook waveguide structures, written
  from scratch).
- **Engine picker is a sheet (15 Oct, late; cache v133):** `engineSheet()` — groups (`ENGINE_GROUPS`: Layers, Drums,
  Keys, Synths, Strings & wind, Voice & bass, Fonts) with a line about each engine, search, the current one marked;
  the pad's engine row shows one button (`#vEngBtn`, "Strings ▾"). **Set `ENGINE_SHEET = false` to get the 4-column
  grid of engine buttons back** (the `makeSeg` code is still there). A new engine goes in `ENGINE_GROUPS` as well as
  `XENG`/`XUI`. The preset and engine sheets are now centred on every screen size (same gap above and below) instead
  of a bottom sheet on phones.
- **Voice card in groups (15 Oct, late; cache v135):** `#voiceCard` has four `.secgrp` blocks with a small caps
  heading — Voice (pitch, low-pass, amp, play), Envelopes (Envelope 1/2/3, each its own section), LFOs & modulation
  (LFOs, Sequence, Matrix), Tuning (song, two columns C–F / F♯–B) — hosts `voiceSecs`, `voiceEnvSecs`,
  `voiceModSecs`, `voiceTuneSecs`. Hairlines rule the sections off inside each group. Tuning is song-wide, not per
  pad; it lives at the bottom for now, Settings → Song would be its proper home if the card gets crowded.
- **Nothing from the review is left.** Next: hear it on the Fold and the PC (nothing has been checked by ear), then the
  feature list below.

## 15 Oct: review fixes (branch fix/review-bugs, merged as PR #1)

A code review (audio, storage, UI, DSP) found these; all fixed and checked headless. Cache `thunderplus-v98`;
`chains-worklet.js?v=17`, `waves-worklet.js?v=13`.

- **SoundFont pads were silent:** in `sfNote` the `f.connect(pn); pn.connect(bus)` had been swallowed by the "+6 dB"
  comment appended to the same line, so no SoundFont voice reached a track. One-line fix.
- **VCF (…chains and …waves, outside the shared block, so edited in both worklets):** the old core mixed a Chamberlin
  `hp` with the TPT `2·v − ic` update, doubling the integrator step; it ran away above cutoff ≈ 0.45 at the default
  resonance (5 000× full scale, resetting 200×/s). Now the TPT SVF Blades uses, same `q = 1 − res·0.95` mapping; checked
  at default, min and max cutoff/res in all three modes: peak ≤ 0.93 on a 0.5 input, no NaN.
- **Storage full was silent:** `storeWrite()` (before `save()`) is the one path for the song and kit writes. A failed
  write shows a red banner (`#storeB`, `.updb.storeb`) that exports the project on tap; it clears when a write succeeds.
  `navigator.storage.persist()` is requested at start-up (next to `hadSW`) so Android doesn't evict IndexedDB.
- **Imported presets kept base64 audio inside the song** (3.9 MB per 30 s sample in every autosave and undo step; two
  imports filled localStorage). `presetsByRef()` stores the audio in IndexedDB and keeps `{ref}`, like in-app presets;
  used by Import presets, by project files that carry preset audio, and once at start-up for songs saved before this.
  `smpHeld` (Map id → sample, every sample seen this session) backs `smpById`, `fullSmp` and undo. `presetSmpFill()`
  (awaited by Save/Share project and Export presets) fetches preset samples no longer on a pad, and `projBlob()` now
  embeds preset audio (`fullPresets()`), so presets travel between devices.
- **Kit undo by reference:** `vSnap(v)` = `voiceToJSON(v, 'ref')` with the sample objects parked in `smpHeld`; restore
  refills from there (else `resolveSamples()`). Was 3.7 MB and ~100 ms per settled tweak with a 30 s sample.
- **No more `prompt()`/`confirm()`:** `askText(msg, default, max)` and `askYes(msg, okLabel)` (`askSheet`, after
  `toast()`, CSS `.ask`) are promise-based in-page sheets: Electron has no `prompt()` (six features did nothing in the
  desktop build), and a native dialog froze the 25 ms pump so every missed note played at once when it closed. All ten
  call sites are now async. `pump()` also re-anchors the sequencer (`s.base`, `s.time`, `s.btT`) when it finds itself
  more than 0.5 s behind the audio clock (a frozen tab, a system dialog), so the song continues instead of bursting.
- **…waves pads rendered cut off on slow devices:** `wavesPre` now passes `costCeiling: Infinity`; the engine's wall-clock
  load shedding (cut voices → draft → all notes off) belongs to the live editor, not an offline render.
- **A tap that closed Settings / File / Phrases also pressed what was under it** (d-pad, pads) on wide screens:
  `swallowTap(e)` in the three capture-phase closers stops the pointerdown and the pointerup/click of that tap
  (propagation only — cancelling touchend would cancel the click). Checked with real taps at 884×1104.

## How to validate (there is no test suite)

1. **Syntax:** `node --check` the worklets; for the single-file apps, extract each inline `<script>` and
   `node --check` it (`type="module"` ones as `.mjs`).
2. **…chains params match:** eval the worklet (`(0,eval)(src)` exposes `globalThis.ChainsDSP`) and the
   editor's `const MODULES = {…}` block, then compare every module's `params`. The only expected
   "mismatch" is `track`'s max, which the editor writes as `CHAN_COUNT`.
3. **Effects in Node:** instantiate `tfx.wasm` with a stub WASI, `tfx_new(id, 48000)`, write 12 params at
   `tfx_params(h)`, fill `tfx_l()`/`tfx_r()` and call `tfx_process(h, 128)`. Check for sound, no NaNs, CPU
   and `memory.buffer` growth. Drive them through the worklet's own maps (`ChainsDSP.MODULES[k].process`
   after `wasmInit({tfxBytes})`) at default, min, max and random settings: raw 0–1 params give false alarms
   (Rings' voices, for one, must be 1/2/4).
4. **In the page:** headless Chromium (Playwright is installed) with a copy of `index.html` that adds
   `window.__T = {…}` before the main script's closing `})();`. That gives you `S`, `FX`, `freshFx`,
   `applyFx`, `pass`, `makeSeq`, `applyChop` and so on. Never commit that copy.
5. **Worker matches:** in that copy, compare `renderVoice(v, sr, null, hold)` with
   `(await renderAsync(v, sr, hold)).data` for every TYPES oscillator and FX key (they should be identical).
   For responsiveness, use CDP `Emulation.setCPUThrottlingRate` 4 and a `longtask` PerformanceObserver.

## Open items / where to pick up

1. **Hear it on the Fold.** Everything above is verified headless, not by ear on a phone. Worth checking:
   - CPU with Space reverb and Cross-mod on several tracks.
   - The trance gate landing on the downbeat.
   - STRETCH and LOOP on a real break.
   - Touch on the new INST rows.
2. **Long synth renders:** a synth slot renders its whole Max len (6–8 s, up to 30 s) each time. It's now in
   the background, but a phone still takes a second or more before an edit is heard. Rendering in pieces
   (the first second first) would shorten that.
3. **…waves editor** doesn't list the new effects (the shared wasm already has them). Adding them means
   mirroring the …chains editor entries in `waves/index.html` and `waves/waves-worklet.js`, which come from
   the separate `matthettich/waves` repo.
4. **Naming:** the older effects/oscillators still use product names (Plaits, Clouds, Rings, Elements, Beads,
   FM Genesis, SoundBlaster). Rename them before any commercial release, keeping "based on" credits.
5. **More effect ideas** (permissive only):
   - More Airwindows (hundreds, all MIT).
   - A spectral freeze.
   - A granular delay.
   - An envelope filter with sidechain.
6. **Still left from the earlier mobile pass:** incremental playhead painting in `draw()`/`drawArr()`
   (`draw()` costs ~13 ms at 4× even when nothing changed, because it rebuilds the grid string to compare).
7. **On the phone:** the owner should install ...Thunder Plus from its page and remove the old ...Thunder icon
   (the old one now just forwards). Check that the offer to bring Thunder's work over appears there.

### Standard DAW features still missing (suggested 14 Oct, in rough priority order)

Already there, for reference: undo/redo, stem export, sidechain, FX automation from tracker commands, sample
recording, resampling, MIDI in/out and clock.

1. **Metronome and count-in.** No click exists yet. Also useful for checking clock-in lock against the MPC.
   About half a day.
2. **MIDI file export and import.** Export a song as `.mid` (to finish in a DAW or load on the MPC/Force);
   import MIDI drum patterns or melodies into phrases. About a day each way.
3. **The rest of MIDI out:** pitch bend for slides/PSL and PBN, arps sent as notes, CC automation for hardware
   synth knobs, and Song Position Pointer (so Continue resumes where the MPC is instead of restarting). About a day.
4. **Humanize.** Small random timing and velocity changes per track (GRV and swing exist; random variation
   doesn't). A few hours.
5. **Named song markers/sections**, to jump around a long arrangement. A few hours.

Deliberately left for later:
- **Audio tracks** (recording vocals or guitar across the song): turns Thunder into a different kind of app;
  better with the native port.
- **Time signatures other than 4/4:** a deep change for a tracker; odd meters already work through phrase length.
- **Plugin delay compensation and an automation-lane editor:** only matter once VSTs exist.

## Conventions worth knowing

- **Ports/cables:** audio = teal, `cv`/modulation = purple dashed, `gate` = pink dashed. Any slider
  has a purple modulation jack; a cable into `p:<param>` modulates it.
- **Adding a …chains module:**
  1. Define it in the worklet and register it in `MODULES`.
  2. Mirror it in the editor's `MODULES` table with identical `params`, plus `ui`.
  3. Add a `PALETTE` entry and help text in `ABOUT`.
  4. Bump `chains-worklet.js?v=` and the `sw.js` cache.
  5. Use `fx:true` only for `tfx.wasm` effects.
- **`README.md`:** the top part is Thunder Plus's; the rest is …Thunder's (the base app).
- **Two engines:** `waves/waves-worklet.js` (from the waves repo) and `chains/chains-worklet.js`.
- **Mobile vs Desktop:** anything that should differ uses `isMobile()` in the page; there is no second build.
- **Engine block:** code that renders pads goes between the `===ENGINE===` markers, with no DOM, `window` or
  `localStorage` at top level, because the render Worker runs that block as-is.

## Bass engine (13 Oct, afternoon)

- **What:** synth slots can set `V.engine = 'bass'` (Synth → Engine). Settings are in `V.bass` (`normBass`); the
  voice's `tune`, `drive` and `level` are folded in by `bassParams(V)`.
- **DSP:** `class BassCore` in the ENGINE block: additive (≤64 partials, sine table), spectral 24 dB resonant
  low-pass, two vowel formants, comb, Reese second set, sub, tempo-synced wobble (`BASS_DIV`), ADSR, glide.
  Self-contained on purpose: the live AudioWorklet is a Blob built from `BassCore.toString() + BASS_PROC`.
- **Live/offline:** `makePlayer` makes one `thunder-bass` node per track + pad (`bassNode`), and every note is
  AudioParam automation (`rate`, `gate`, `trig` counter, `leg`, `bpm`, `wob`), so offline renders are exact.
  `pass()` awaits `bassReady(oc)` when the song uses a bass pad. Pads, the waveform and the Worker use
  `renderBass()` (one held note).
- **Tracker:** `WOB` command (last in `FXC`): 01–0D pick `BASS_DIV`, 00 = the voice's rate. Mixer's master row
  has a MASTERING cell (`cs.mixer.mx === 1`).

## Desktop (Electron) build (13 Oct, afternoon)

- `electron/`: `main.js` (app:// protocol, permissions, single instance), `copy-app.js` (copies the app and
  fails if a file index.html loads is missing; add new runtime files to `PARTS`), `package.json` (Electron 44,
  electron-builder 26, unsigned, `--publish never`; product name "Thunder Plus" without the dots so the .app
  isn't a hidden file on macOS).
- `.github/workflows/desktop.yml` builds Windows/Mac/Linux on demand or on a `v*` tag.
- In the page, `DESKTOP_APP` (protocol `app:`) turns off the web update check and shows a note under App.
- Not built or run in the cloud session (no npm registry access there): first real run is the owner's.
- Auto-update: `electron/updater.js` (electron-updater, GitHub provider `matthettich/thunder-plus`). The workflow's
  `release` job (tags `vX.Y.Z` only) runs `gh release create` with every platform's files incl. `latest*.yml`.
