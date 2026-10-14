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
- **Cache-busters now:** `chains-worklet.js?v=12`, `waves-worklet.js?v=7`, `master-worklet.js?v=2` (index.html, the editor
  pages, sw.js). Cache is `thunderplus-v73`.

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

## Running it locally

No build step; serve the repo root over HTTP (the editors load their worklet by `fetch`, which
`file://` blocks):

```
cd thunder-plus
python3 -m http.server 8734 --bind 127.0.0.1
```

Then open `index.html`, `chains/index.html` and `waves/index.html`. On their own the two editors log a 404
for a `sw.js` beside them; that is expected (the service worker is the app's, at the root).

## Rebuilding the effects wasm (`dsp/tfx/build.sh`)

Sources (all MIT):
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
