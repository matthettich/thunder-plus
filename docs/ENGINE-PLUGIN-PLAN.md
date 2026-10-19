# The synth engine as a plugin (VST3 / AU / CLAP) and mobile apps: a plan

Status: **plan only, nothing built yet** (written 19 Oct 2026). The owner wants to pick this up once Thunder Plus
itself is well tuned. This doc is the starting point for that work; `HANDOVER.md` points here.

## What the owner wants (19 Oct 2026)

- A **VST** of the Thunder Plus synth engine, first of all to **produce a whole album with it in a DAW** (Ableton
  Live on Windows and Mac), while Thunder Plus itself is still being worked on. Later Thunder Plus should be able
  to do that job on its own.
- **One plugin instance = one playable sound**, based on the engine picked:
  - a **melodic** engine plays as a polyphonic synth from MIDI notes;
  - a **drum** engine is a **kit** (pads on MIDI notes).
- Later: an **Android** app and possibly an **iOS** app of the synth engine.
- The main worry: **parity**. The plugin and Thunder Plus must sound the same, and keep sounding the same as the
  engine grows, without double work.
- Thunder Plus may be sold one day, so everything must stay permissively licensed (see Licensing).

## The core idea: one engine, two builds

Thunder Plus already does this for part of its sound. Plaits, Rings, Clouds, Elements, the Airwindows effects and
DaisySP are C++ files in `dsp/` (built by `dsp/build.sh`, `dsp/tfx/build.sh` and `dsp/mods/`, with wasi-sdk/clang)
compiled to the `.wasm` the app loads. The plan extends that to the rest of the engine:

1. Port Thunder's own engine code from JavaScript to C++ in `dsp/` (a new `dsp/engine/`).
2. Compile it to WebAssembly for Thunder Plus, which then renders with it instead of the JS.
3. Compile **the same files** natively into the plugin (and the mobile apps).

From then on the C++ is the only engine. A new engine or a fix goes in once and every build gets it. Engine
work becomes "edit C++ and rebuild the wasm"; a GitHub Action can rebuild and commit the `.wasm` on every push so
nobody has to run a toolchain by hand. The app itself keeps no build step for everything else.

A likely bonus: wasm runs DSP faster than JS, so pad renders (HANDOVER open item 2, the slow re-render of long
synth slots on the phone) and the live engines get lighter.

## What "the engine" is today (index.html)

Everything below lives between `/* ===ENGINE START=== */` and `/* ===ENGINE END=== */` (about 4,400 lines, plain
sample-by-sample JS with no Web Audio nodes; the same block runs in the pad-render Worker and in
`tools/drums/render.mjs`), unless noted.

- **Voices** (`mkVoice`, `renderVoice(V, sr, stages, hold)`): up to N **layers** (`renderLayer`), each an
  oscillator **type** with its own envelopes, plus a voice-wide filter and amp (`voiceFilter`, `voiceAmp`), drive and
  level, then the pad effects (`padFx`, `FX`, `freshFx`).
- **Layer oscillator types** (`TYPES`, 44, in families `FAMS`):
  - Standard: sine, tri, saw, square, pulse, wavetable, supersaw, pd, chip, click
  - FM: fm2, fmfb, fmmetal, fmcross, fm4, fm6, genesis, sb, arcade
  - Additive: addsaw, addodd, membrane, bell, bar, chord, formant, karplus, vosim
  - Noise: white, pink, brown, blue, metal, lfsr, dust, grit, chaos
  - Mutable Instruments: plaits, elements (already C++/wasm: `plaitsPre`, `elementsPre`)
  - Sample: sample, grains, pwm
  - …waves: a whole …waves patch rendered offline (`wavesPre`, through `WavesEngine` in
    `waves/waves-worklet.js`).
- **Whole-voice engines** (`XENG`, 23): acid and mach (**live**, real-time worklets), and tone, modal, swarm, drift,
  piano, ep, organ, table, breath, strings, fmdrum, wind, phase, partials, skin, chip, vector, scanned, resynth,
  bytebeat, sf (SoundFont GM, sample-based).
- **Other engines**: bass (`renderBass`, live worklet `thunder-bass`), vox (`VoxCore`, live worklet `thunder-vox`),
  blip (Blip drum).
- **Drum kits**: the classic-machine and acoustic kits (`drumKitGroups()`), built from the above.
- **Files**: a kit is `kitToJSON()` → `{app:'thunder', v:1, name, voices:[...], waves}`; one voice is
  `voiceToJSON()`. The plugin should read and write these, so a sound made in Thunder Plus opens in the plugin and
  the other way round.

## The big design decision: rendered notes or live notes

Today most pads are **rendered once** (at the pad's pitch, for its length) and **played back like a sample**,
re-pitched per note with `playbackRate` (the Koala / SunVox one-shot model). Only bass, vox, acid and mach run live.

- **Rendered (Thunder mode):** the plugin does the same: render in the background when a parameter changes, play the
  render through a small sampler (pitch, velocity, release on note-off). **Identical to Thunder Plus**, light on CPU.
  Automating a parameter from the DAW means a re-render (a short delay before it's heard, so not smooth sweeps).
- **Live:** run each engine per note at the note's own pitch in real time. A "real synth" (key tracking, smooth
  automation, true note length), but it **won't sound identical** to Thunder Plus's re-pitched renders (higher
  notes are no longer shorter and brighter the way a re-pitched one-shot is).

Suggested: **rendered by default** (parity, the album sounds like Thunder), with **live as an option** per engine
where the engine supports it, added later. Whatever is picked, Thunder Plus and the plugin should offer the same
modes so they stay in step.

## Plugin shape

- One instance = one Thunder **voice** (melodic) or one **kit** (drums). The engine picker decides which.
- Melodic: polyphonic (voice count setting), pitch bend, mod wheel, velocity, sustain pedal; glide for the mono
  engines (acid, bass).
- Drums: pads on MIDI notes (General MIDI layout by default: kick 36, snare 38, closed hat 42…, plus a "chromatic
  from C1" option), per-pad outputs later if Live needs them.
- Every control is a host parameter, so it can be automated in Live.
- State: the voice / kit JSON (with any samples embedded) saved in the DAW project; Load / Save of Thunder files.
- UI: the plugin frameworks can show a **web view**, so the plugin's screen can reuse the Synth screen's HTML/CSS
  (a slimmed-down copy), talking to the C++ engine through a small parameter bridge. That's UI parity as well as
  sound parity, and the skins carry over.

## Architecture

```
dsp/engine/            C++ port of the ENGINE block (voices, layers, XENG cores, bass, vox, blip, pad FX)
dsp/ (existing)        Plaits, Rings, Clouds, Elements, Airwindows, DaisySP, …chains/…waves modules
   │
   ├── wasm build  ───► Thunder Plus (index.html renders with it; the JS stays only as a fallback until parity)
   └── native build ──► plugin/   (iPlug2 project: VST3, AU, CLAP, standalone; AUv3 for iOS)
                        android/  (Kotlin shell + Oboe audio + the same web-view UI)
```

- **Framework: iPlug2** (permissive licence, C++). It builds VST3, AU and CLAP plugins and a standalone app for
  Windows and macOS, and **AUv3 for iOS** from the same project, and it has a web-view UI option. (JUCE would also
  work but needs a paid licence, or the AGPL, for a closed-source product.)
- **Builds on GitHub Actions**, like the desktop app: Windows VST3 first, then Mac (VST3 + AU, universal).
- …waves voices (the `wv` layer type) need the …waves engine in C++ too; it's big (the …chains/…waves engine is
  JS around the shared wasm). Leave it out of the first plugin, or render …waves layers to a sample when a sound
  is loaded.

## Making sure they sound the same (parity tests)

- `tools/drums/render.mjs` already renders voices offline in Node with the app's own ENGINE block. Extend it to
  render every voice twice, with the JS and with the C++ wasm, and compare (peak and RMS difference per voice, a
  spectral distance, and a "max sample difference" figure).
- Bit-exact is not realistic (JS `Math.sin/exp/pow` vs C++ libm, `-ffast-math`, float vs double), so aim for "below
  audibility": for example a difference at least 80 dB below the signal. Engines that use the shared `SIN` table or
  the seeded `rng()` should match very closely, since the same tables and seeds carry over.
- Make it a CI check: the wasm build fails if any factory voice drifts past the threshold. Then parity is
  enforced by the build, not by memory.
- Thunder Plus switches each engine from JS to C++ only once it passes, so nothing anyone has made changes sound.

## Milestones

1. **Pipeline proof.** `dsp/engine/` with one melodic engine (say `piano` or `fm2` layers) and one drum kit voice
   path; wasm build; Thunder Plus renders them with it behind a switch; parity test passing; a Windows VST3 from
   GitHub that loads in Live and plays both.
2. **Port the rest of the ENGINE block** (layer types, XENG cores, pad FX), engine by engine with the parity test
   as the gate; switch Thunder Plus over engine by engine; then remove the JS copies.
3. **Live engines** (bass, vox, acid, mach) into the C++ core; the app's worklets load them from wasm.
4. **Plugin UI** in a web view from the Synth screen; presets and kit/voice files; Mac build (VST3 + AU).
5. **iOS**: AUv3 + standalone from the iPlug2 project.
6. **Android**: standalone app (Kotlin + Oboe + web-view UI).

## Mobile and stores

- **iOS**: an AUv3 runs inside AUM, GarageBand, Logic for iPad and Cubasis, plus a standalone app. Needs the
  **Apple Developer Program** ($99 a year as of Oct 2026) and Xcode on a Mac (the owner has MacBooks); GitHub's Mac
  runners can build it, but signing uses the owner's certificates.
- The same Apple membership also lets the **Mac desktop app and Mac plugins be signed and notarized**: no
  "unidentified developer" warning, and the Mac desktop app could then update itself the way Windows does.
- **Android**: Google Play account ($25 once as of Oct 2026). New personal accounts must run a **closed test with
  at least 12 testers for 14 days in a row** before going public, and Google verifies ID, so open the account early.
  A quicker first Play Store presence: wrap the existing web app (it already runs on the Fold) while the native
  app is built.

## Licensing and naming

- Engine code: Thunder's own, plus MIT projects (Mutable Instruments, Airwindows, DaisySP; see
  `THIRD_PARTY_NOTICES.md`). The notices must ship with the plugin and apps too.
- **VST3 SDK is MIT** since VST 3.8 (Oct 2025). CLAP is MIT. iPlug2 is permissive. No GPL code anywhere (DaisySP's
  separate LGPL modules stay out, as now).
- HANDOVER open item 4 applies doubly here: rename the engines that use product names (Plaits, Clouds, Rings,
  Elements, Beads, FM Genesis, SoundBlaster) before anything is sold, keeping "based on" credits.

## Open questions for the owner

- Rendered vs live by default (see above). Suggested: rendered.
- Which engines matter most for the album? Port those first.
- Plugin name (…Thunder Synth?) and whether it is sold, free, or a companion to Thunder Plus.
- One stereo out per instance, or per-pad outputs for kits.
