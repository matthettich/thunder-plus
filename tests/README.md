# Tests

No dependencies and no build: `npm test` (Node 20+). It loads the app's own files into a Node `vm` with no browser and no audio
device, so it runs in about 15 seconds.

| File | What it covers |
|---|---|
| `chains.test.js` | every chains module through the real engine, plus the `ChainsProcessor` / `ThunderChains` wrappers and the output limiter |
| `waves.test.js` | every waves module in its real voice chain, plus `WavesProcessor` and the output limiter |
| `master.test.js` | the mastering chain (suppressor, compressor, limiter), including what each stage must *do* |
| `hangs.test.js` | renders that once froze the audio thread, run in a child process with a time limit |
| `app-logic.test.js` | logic cut out of the HTML/JS: logger, PatchStore dirty flag, sample prune, `esc()`, phrase/pattern sizing and layout, CSP header, service-worker file list, shared-block sync |
| `updater.test.js` | the desktop updater's SHA-256 check |
| `pump-synth.test.js` | the …pump synth engines (MONO, POLY, COIL from `pump/synth/`): every factory preset renders finite and below full scale, hold / release / Tune, golden output; the live worklet's notes as MIDI (retrigger, legato, bend, chords), live = render for the same note, sleeping in silence |
| `usability.test.js` | the usability audit's Phase 1 fixes (`docs/USABILITY-AUDIT.md`): toast timing and queue, wheel step accumulation (host sliders and pump windows), AltGr, F8 panic, row insert/remove inside a phrase's LEN, Delete on a selection, and source guards for KEYS-mode modifiers, Synth-page keys, dialogs over Settings, unsaved-change marks, Open's fresh undo, leaving the page, and the key docs |

## Golden hashes

Audio tests hash the exact float bits of what a module renders and compare to `tests/golden/*.json`. A refactor that changes any
output, even by one bit, fails. **A golden hash says "unchanged", never "correct"**: it cannot tell you a change sounds worse, and
modules that need a gate or clock are rendered without one (so for those it proves "does not crash, no NaN, idle output unchanged").
Listening is still the only test of how it sounds.

When a change to the sound is intended, review it, then accept it with `npm run test:golden:update` and commit the golden diff.
Never update goldens to make a failing run pass without knowing why it changed.

## Things worth knowing

- `chains/` is the source of truth for the **shared** blocks; `tools/sync-shared.py` copies them into `waves/`. A test fails if
  the two differ. Edit `chains/`, then `npm run sync`.
- The worklet files wrap everything in one closure. `tests/helpers.js` `load(ctx, file, [names])` publishes internals by editing the
  text it runs in memory; the files on disk are never touched.
- The waves oscillator uses `Math.random()` for its start phase, which the engine's `seed` does not control. The test scope
  replaces `Math.random` with a deterministic one (`freezeClock` in `helpers.js`), so renders repeat.
- The engine sheds to "draft" quality when a block runs slowly, which changes the DSP. Renders that detect this throw rather than
  record a hash, so a very slow machine fails loudly instead of corrupting a golden.
- macOS has no `timeout` command; use `perl -e 'alarm N; exec @ARGV' node --test ...` to bound a run.
