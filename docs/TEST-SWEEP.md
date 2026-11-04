# Full test sweep (4 Nov 2026, builds 421 → 427)

Owner (3 Nov, night): "go through and test every feature and command… fix the bugs. I don't want anything we made
deliberately function a certain way changed… report any clunky or awkward behaviour but don't fix those unless they are an
obvious bug. Document everything."

The sweep ran on `beta` from build 421 (`eba0f5c`), in headless Chrome and Node. Its scripts are in
[`tests/sweep/`](../tests/sweep/README.md) and can be rerun. It found and fixed ten bugs (builds 422–425). After every build,
`npm test` passed (127, 1 skipped) and `tools/seq-golden.mjs` matched on both the main thread and `--core`. No golden output
changed. Deliberate behaviour was checked against HANDOVER.md, the README and the owner notes in the code before each fix.

## Bugs fixed

| # | Symptom | Cause → fix | Build · commit |
|---|---|---|---|
| 1 | Reopening the app lost the song. Perform scenes A/B came back empty, parts shrank to 1, and a song with a track LFO threw, so the starter beat opened and was autosaved over the song. | The saved song is read at start-up, before `PERF_SCENES` / `PERF_MACROS` / `PERF_PARTS` / `PLFO_RATES` / `PLFO_SHAPES` were assigned (`var` hoisting only declares them). Fix: the same values are now set before the song is read, and the originals stay for `perform.test.js`. `core/seq-core.js` was regenerated (declaration order only). | 424 · `3124c8e` |
| 2 | PLAY on a phrase or sect (Shift+Space, F6, Phrase PLAY) played nothing on the audio-thread sequencer. | `coreStart` didn't pass `track` / `phrase` / `sect`. Fix: they're now passed. | 423 · `8c25716` |
| 3 | Blip ran away to about 8× full scale with Filter near HP 8 kHz or a nearly open LP and low Resonance (pads, and the …chains / …waves module). | The Chamberlin SVF needs f² + 2fq < 4. Fix: f is capped just under that limit; settings already inside it are unchanged. | 422 · `9207f98` |
| 4 | Right-click on an Arrange track number (Song screen) soloed it and also opened the Edit menu. | The solo redraw detached the target, so the event reached `cmdCtx`. Fix: `stopPropagation`. | 423 · `8f20e63` |
| 5 | Pattern view: the open track's own band ignored right-click solo, Alt-click mute and double-click name. | `.pvhc` had no `pvMap` entry. Fix: `pvBandT` returns `cTrack` for it. | 423 · `ff165f4` |
| 6 | The wheel on a Mixer VOL/OUT bar stopped after 2–3 notches. | The 3 px bar moves away from a still pointer (and OUT is rebuilt each redraw). Fix: focus holds while the pointer stays still. | 423 · `40d304c` |
| 7 | Render/export rate, bits, normalise and "+ note" were reset on every reload. | They were kept in memory only. Fix: they're stored in `thunderplus-export-v1`. | 423 · `4eb6726` |
| 8 | Alt+S (Song settings) did nothing on the Synth page, against the 3 Nov "from any screen" decision. | Tracker-only key. Fix: added to the Synth-page key handler. | 423 · `0b5f169` |
| 9 | …pump value boxes (`Field`) and TUNE note sliders jumped when Shift was pressed mid-drag, and ignored Ctrl/⌘ (TUNE fine was ×0.15). | They measured from the press. Fix: they now follow the one rule (`dragMul`): Shift ×0.1, Ctrl/⌘ ×4. | 423 · `80fa4fd` |
| 10 | The strip PAN slider kept its old value after a drag, so after a track change, Undo or the Mixer, the next drag or wheel step jumped back. Found in the re-check run after the merge. | `stPan` kept focus and `deckStrip` skipped it while focused → it's now skipped only while `:active`. | 425 · `ed62264` |

## Owner follow-up (4 Nov, morning): open items and clunky spots changed (builds 426–427)

The owner read the report and asked for the HOP docs to follow the code, for all the found bugs to be fixed, and for every
clunky spot to be changed. Duplicate track should add a track of its kind ("ok if it created a mono and poly track").

| Item | What changed | Build |
|---|---|---|
| `HOP` code vs docs | Kept the code (whole value = row, 00–1F; needed for 32-row patterns); hint, Help and README say so | 426 |
| Fold cover top bar | At ≤ 520 px in the Mobile setup the screen tabs shrink and scroll inside themselves, so ⚙ and CPU stay on screen | 427 |
| Effect node that can't start | `processorerror` on the track-effect node toasts once per audio context (render or live) | 427 |
| Ctrl+D listed twice in Edit ▾ | The shortcut shows only on the duplicate it does on this screen | 427 |
| Effects cut out at high gain | …pump effects and modules: a finite output up to ±64 is clipped at ±8 (`hotClip`) instead of restarting the effect; only NaN, infinity or ±64+ restarts it. The track-input guard clips instead of zeroing too. The built-in (wasm) effects' own guard in `dsp/tfx/tfx.cc` still zeroes samples past ±8: changing it needs a wasm rebuild | 427 |
| Song Shift+Del hint | Says "clear, step down" (one column on the Song is the 21 Oct design) | 427 |
| Song hex entry | One or two digits are placed after a 1.2 s pause (5 → 005) | 427 |
| Acid line… | The phrase goes into the song only when GENERATE writes it | 427 |
| Right-click on Mixer / FX / Inst / Project | Cut / Copy / Paste only on Song, Sect and Phrase (Edit ▾ greys them elsewhere) | 427 |
| Alt+▲ from the Mixer | Help says the moves are one step at a time (the screen grid is as designed) | 427 |
| ⚙ − / + steppers | Grey out at the ends of their range | 427 |
| `GRV` / `GGR` | Groove the row they are on too (pre-scan of the rows due, as TIC does); `core/seq-core.js` regenerated; goldens unchanged | 427 |
| Save picker failing | Falls back to a download | 427 |
| Duplicate track with no free track | TRACKS goes up by two (a mono and a poly), the copy goes in the new one; one undo step | 427 |
| ELECTRO FLX message | `sounds/ELECTRO.thunder` stored with FLX 10 written and `flx256` (it was already shown once per song name; the file no longer needs it). `?v=2` | 427 |
| First-run box on phones | Sits under the top bar, clear of the pad | 427 |
| Skins | `--mx` / `--mx2` darkened to ≥ 4.5:1 on Mustard, Complement, LCD green, Grey white, Miami, Citrus, Sunset; empty cells mixed 25 % toward the ink on every skin | 427 |
| MENU-AUDIT.md | Status line: all built | 427 |

Left as they are, with the reason:
- **Shift-wheel ratios.** Faders and FX bar values step 4 a notch and Shift 1, which is already the smallest step those
  0–255 values have (×0.25). The tracker's Alt+Shift-drag stays at 48 px a step (×0.33); 160 px a step would make it hard
  to use.
- **Presets over full scale** (Voice Laser choir, pVOX Talkbox lead, pSWIRL Metal comb, Phaser Deep 8-pole…). These are
  levels inside the mix in floating point; the master limiter follows, and changing them would change how they sound.
- **ELECTRO sect F3** is the bus track's phrase: a bus has no notes, so an empty phrase is right.

## Found, not fixed (owner decisions)

- **`HOP` vs its docs.** README, Help and the hint (`v & 15`) say the low digit is the start row. The code uses the whole
  value (`Math.min(fx.HOP, PROWS - 1)`, around index.html:18022). So `HOP 18` on a 16-row phrase starts at row 00, not row 8.
  Changing the code would change how saved songs play.
- **Fold cover (360 / 412 px), Mobile setup, with the …waves/…chains editors on:** the top bar is 459–477 px wide. ⚙ and CPU
  go off-screen, the page scrolls sideways and the Settings menu is cut off. With the editors off (the default) it fits.
- **Silent dry render if an effect node fails.** Chrome frees an offline render's worklet wasm memory lazily. In the test
  sandbox, about 100 effect nodes across offline renders exhausted it, so new nodes failed and the track rendered dry with no
  toast. This wasn't seen outside the sandbox. A `processorerror` toast during renders would be a cheap safety net.
- **Edit ▾ lists both "Duplicate the pattern" and "Duplicate the track" with Ctrl+D** (one greyed out). The key itself acts by
  context, as decided on 20 Oct.
- **Shift on the wheel isn't one ratio.** It's ×0.1 on sliders, ×0.25 on faders and FX bar values, and ×0.33 for tracker
  Alt+Shift-drag. The fader steps were chosen on 3 Nov.
- **Presets over full scale before the master:** Voice "Laser choir" 1.35 and "Buzz box" 1.10, Blip "Click" 1.03, pVOX
  "Talkbox lead" 4.0, pSWIRL "Metal comb" 3.5, Phaser "Deep 8-pole" 2.7 (Phaser default about 1.2).
- **ELECTRO sect F3 → a null phrase** (plays empty, harmless).

## Clunky or awkward (reported only)

- **Effects at extreme gain.** A hot signal through high Output or Makeup trips the worklet's ±8 guard, and the effect silently
  resets: you hear dropouts, not clipping.
- **Song screen keys.** The hint says "Shift+Del line" but it clears one cell. Block hex entry is 3 digits; 2 digits just waits.
- **Acid line…** writes an empty phrase (plus an undo step) before Generate.
- **Right-click on Mixer / FX / Inst / Project** lists Cut, Copy and Paste that only say "Copy works in Song, Sect and Phrase".
  Clear often does nothing.
- **Alt+▲ from the Mixer** goes to the middle screen (the screen grid), while Help says "▲ Project".
- **⚙ render −/+ buttons and segments** give no feedback.
- **`GRV` / `GGR`** take effect from the next step.
- **Save** with a failing save picker says "Couldn't save there" and has no download fallback.
- **Duplicate track** with no free mono track has no way to choose a target.
- **The ELECTRO FLX message** shows on every load.
- **The first-run box on the cover screen** slides under a tap.
- **Low-contrast skins:** mustard and complement (~2.6:1), lcdgreen 3.3, greywhite 3.45, miami / citrus / sunset 3.6–3.9. The
  Phrase grid's empty-cell dashes are about 1:1 on most skins.
- **`docs/MENU-AUDIT.md`** still lists proposals 2–5 as not built.

## Re-check after the merge

On merged `beta` (build 425): `commands.mjs` 185/185, `editing.mjs` 61/61, `files.mjs` 29/29, `mouse.mjs` 96/96 (after fix 10), `effects.mjs --only=ui,master` 176/176 (every effect UI and …pump window after the
kit.js / tune.js fix). In
`keys.mjs`, 139/140 pass. The one failure, "Ctrl+D on Phrase", reads the toast while an earlier toast is still showing; the
duplicate itself and its "No empty mono track" refusal pass in `editing.mjs`. In `menus.mjs`, 573/576 pass. The 3 failures were
also there before the sweep and are harness gaps, not app bugs: a "Phrases…" item that playwright reports as covered, and two
selectors (Synth layer strip, side panel map) that need updating.

## Coverage

58 engines (pad, slot, Worker), about 650 engine presets, about 1,900 parameter renders, live vs rendered; 43 layer types,
about 700 voice presets, AKWF tables and WAV import, SoundFont, Resynth and Freeze. 98 track/master effects (25 …pump) with
2,204 parameter renders and 1,056 presets; 65 pad effects; the master chain; every effect's UI; 121 …chains and 107 …waves
modules. 75 tracker commands (185 cases, main thread = core); about 50 edits with exact undo and redo; files, autosave,
renders and transport. Every menu (about 330 items), the right-click menus, 140 key checks, 89 mouse-rule checks, 63 skins,
and 8 window sizes. The memory test (200 renders, 150 effect add/remove cycles, 8 identical exports) showed no leak.

Skipped for time: Sect-screen editing, Ctrl-click picked-cell paste, MIDI file export/import, LXR kit import, …chains export,
Restore backup, Open recent, Share, and per-command event checks in live mode.

Not testable headless: listening and sound quality, MIDI hardware, Ableton Link, real-time timing under load, touch and the
on-screen keyboard on the Fold 6, the Electron app (native dialogs, updater), and the owner's own songs.
