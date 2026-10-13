# Thunder Plus — handover notes

Pick-up notes for continuing this work in a later session. Read this together with
[`README.md`](README.md) (the app's own docs) and [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).
Keep this file up to date at the end of a work session.

## Where things stand (12 Oct 2026, evening)

- **Repos:** `matthettich/thunder-plus` (this one) and `matthettich/Thunder-` (built from this one), both
  branch `main`, both pushed. Nothing uncommitted.
- **Commits this session** (Plus, oldest first):
  - `2dcda77`: 14 new effects, 6 oscillators, tempo-synced modulation, …chains Steps/Clock/Repeat, and the
    song phrase-number fix.
  - `eebb19e`: 9 more effects (comb, transient, trance gate, multiband, frequency shifter, vinyl, dub, cab, tube).
  - The **break-chopping** commit on top (slices, sync, stretch), plus this file.
  - Thunder- has a matching rebuild after each of them.
- **App:** no build step. `index.html` is the whole tracker/synth app; `chains/index.html` and
  `waves/index.html` are the two modular patch editors loaded in frames.
- **Cache-busters now:** `chains-worklet.js?v=7` (index.html, chains/index.html, sw.js). Caches are
  `thunderplus-v36`; `build-thunder.py` bumps Thunder-'s `thunder-vNN` itself.

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
caches and rebuild Thunder-.

## How to validate (there is no test suite)

1. **Syntax:** `node --check` the worklets; for the single-file apps, extract each inline `<script>` and
   `node --check` it (`type="module"` ones as `.mjs`).
2. **…chains params match:** eval the worklet (`(0,eval)(src)` exposes `globalThis.ChainsDSP`) and the
   editor's `const MODULES = {…}` block, then compare every module's `params`. The only expected
   "mismatch" is `track`'s max, which the editor writes as `CHAN_COUNT`.
3. **Effects in Node:** instantiate `tfx.wasm` with a stub WASI, `tfx_new(id, 48000)`, write 12 params at
   `tfx_params(h)`, fill `tfx_l()`/`tfx_r()` and call `tfx_process(h, 128)`. Check for sound, no NaNs, CPU
   and `memory.buffer` growth.
4. **In the page:** headless Chromium (Playwright is installed) with a copy of `index.html` that adds
   `window.__T = {…}` before the main script's closing `})();`. That gives you `S`, `FX`, `freshFx`,
   `applyFx`, `pass`, `makeSeq`, `applyChop` and so on. Never commit that copy.

## Open items / where to pick up

1. **Hear it on the Fold.** Everything above is verified headless, not by ear on a phone. Worth checking:
   - CPU with Space reverb and Cross-mod on several tracks.
   - The trance gate landing on the downbeat.
   - STRETCH and LOOP on a real break.
   - Touch on the new INST rows.
2. **Long pads re-render on the UI thread:** a 30 s pad takes a moment after each edit on a phone. Moving
   `renderVoice` to a Worker (item 6) would fix it.
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
6. **Still left from the earlier mobile pass:**
   - Moving `renderVoice` to a Worker.
   - Incremental playhead painting in `draw()`/`drawArr()`.

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
- **`tools/build-thunder.py`** writes ../Thunder-'s `index.html`, `electron/main.js`, the two engines and its
  `sw.js` versions. It fails loudly when a string it replaces has moved; update the script then.
