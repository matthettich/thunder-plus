# Sweep scripts (headless Chrome)

Written for the full test sweep of 4 Nov 2026 (report: [`docs/TEST-SWEEP.md`](../../docs/TEST-SWEEP.md)). They are **not**
part of `npm test`. They drive the whole app in headless Chrome, either through its own functions (`?seqtest=1`,
`__seqTest.ev`) or with real mouse and keyboard events, so they take minutes to hours. Run one after a big change in its
area.

Each script prints a line per check (PASS / FAIL / NOTE / SKIP), then the rows that are not PASS and a count. It exits
with code 1 if anything FAILs. Each one starts its own static server and its own Chrome. They need Chrome and playwright:
set `CHROME=…` and `PLAYWRIGHT=…/playwright/index.mjs` if they aren't found. Run one at a time, because two browsers at once
make the timing checks unreliable on a small machine.

| Script | Time* | What it checks |
|---|---|---|
| `node tests/sweep/engines.mjs` | ~1.5 h | Every engine in the picker on a drum pad and a synth slot (page thread and render Worker), every factory preset, and every parameter row at min and max. Live engines through the offline mix beside their render. The pad preset sheet's factory voices, layer presets and every layer type. The 30 AKWF starter tables and imported WAVs in Table. SoundFont GM programs. Resynth and Freeze with a test sample. Options: `--only=k1,k2`, `--from=k` (resume), `--twins` (also the …pump live twins' presets and rows). |
| `node tests/sweep/effects.mjs` | ~1 h | Every track and master effect (TFX, …pump included), fresh, with every parameter at min and max and every factory preset, through the app's own effect worklet offline. Every pad effect with its rows. The master chain. The FX screen, FX bar, panel, …pump windows and preset pickers for every effect. The …chains and …waves pages with every module added and removed. Options: `--only=track,pad,master,ui,frames`, `--fx=k1,k2`. |
| `node tests/sweep/commands.mjs` | ~6 min | Every tracker command (`FXC`) with 00, a mid value, FF and its documented special values, on mono, poly and master tracks. Played in sim mode and on the audio-thread core, with events checked against the docs; the two sequencers must agree. Old `FXC_V1` files, and a live play with commands sprinkled in. |
| `node tests/sweep/editing.mjs` | ~27 min | About 50 Song / Arrange / Phrase / Mixer edits. After each one, the song must stay consistent, and Undo and Redo must restore the exact song. |
| `node tests/sweep/files.mjs` | ~15 min | Bundled songs load and play. Save → load round trip, Open, old formats, presets, kit, autosave restore after a reload. Render song WAV, stems, phrase to slot, Cancel. Transport, FOLLOW and the Arrange paging rule. Use `SWEEP_ONLY=files,render,transport` to run only some sections. |
| `node tests/sweep/menus.mjs` | 6–10 min | Every top-bar menu item and every right-click menu on every screen, with the real mouse. Options: `--only=file,edit,view,aud,set,tabs,ctx`, `--screens=…`. |
| `node tests/sweep/keys.mjs` | 3–5 min | Every documented shortcut on the screen it belongs to, checked against the app's state. |
| `node tests/sweep/mouse.mjs` | ~5 min | The one rule for value controls on every kind of control (double-click reset, right-click Reset / Type / Learn, Shift ×0.1, Ctrl ×4, wheel only after the pointer rests), Alt-drag copy and track-number clicks. |
| `node tests/sweep/layout.mjs` | ~20 min | Every screen at desktop and Galaxy Z Fold 6 sizes (folded and unfolded, Mobile setup), and every skin with its contrast. Screenshots go to `tests/sweep/out/` (gitignored). Options: `--sizes`, `--skins`. |

\* Measured on a quiet 2-CPU Linux machine. A desktop is faster.

Helpers: `lib.mjs` (engines, effects), `seq-lib.mjs` (commands, editing, files) and `ui-lib.mjs` (menus, keys, mouse, layout).
They were written in parallel, which is why there are three of them.

## What the results mean

- **FAIL**: an exception, non-finite samples, a crash or reset in the worklet that isn't explained by gain, silence where a
  sound is expected, a page error, an undo that doesn't restore the exact song, or a control that does nothing.
- **NOTE**: worth a look but not a bug. For example, a peak over full scale or silence at an extreme of a control (Output or
  Makeup at max, Level at min), or a peak over 1 from a preset; a pad or track is not the output, because the master and its
  limiter follow. Gain that blows past the worklet's ±8 guard on the loud test signal is re-checked at −40 dB (track FX) or
  −20 dB (pad FX), and is a NOTE when it scales down with the input. Low skin contrast is also a NOTE.
- **SLOW**: a timing check (the wheel-rest delay) that a loaded machine couldn't measure. Rerun it on a quiet machine.
- **SKIP**: deliberately not run (see that row).

## Known limit of the harness

Chrome keeps each offline render's worklet wasm memories until it tears that thread down. In a sandbox with a small address
space, only about 100–125 wasm memories fit in one page, so `effects.mjs` opens a fresh page for each batch of 32 effect
nodes. Desktop Chrome has far more room. In the live engine the audio thread frees dropped effect nodes itself (150 add and
remove cycles checked).
