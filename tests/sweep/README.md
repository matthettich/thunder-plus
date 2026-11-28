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
| `node tests/sweep/resize-playing.mjs` | ~5 min per skin | The window resized in steps (wide to narrow and back, short heights) while ELECTRO plays, on Phrase, Pattern view, the master phrase (SIGNAL chain), Song, Mixer, Sect: long tasks during the drag, the stage over the grid's text mid-drag (`--sample=1`) and settled, a stale layout (the same after a forced refit). Options: `--skins=`, `--screens=`, `--steps=`, `--w0=`, `--w1=`, `--h0=`. |
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

## Soak test (`soak.mjs`)

`node tests/sweep/soak.mjs --minutes=10 --seq=page|core [--song=sounds/ELECTRO.thunder] [--phone] [--only=stopplay,tour,panels,reload]`
plays the song in a loop in real time (not offline) and, every 5 s, samples: the JS heap (after a forced GC every 30 s, over CDP),
DOM nodes, event listeners and documents (CDP `Performance.getMetrics` / `Memory.getDOMCounters`), renderer and browser RSS (from
`/proc`), live AudioNode / AudioWorkletNode / AudioBuffer wrappers (`Runtime.queryObjects`, at the 30 s mark), the app's own counts
(`rt.pl.aliveN()`, `bassLive`, `padSrcs`, `VAR.map` / `VAR.bytes`, `tp.q`, `MOB.q`, `MOLED`, `PB.log`, undo, localStorage size, live
intervals and pending timeouts in the page and in the waves / chains frames) and the audio health (`ctx.currentTime` against the wall
clock, `playbackStats`, the app's dropout counter, the CPU meter, `PB.late` / `PB.drop` / `PB.skips`, the page ticks' lead, moved-on
and held counts, long tasks over 50 ms, rAF rate). Every sample also records `os.loadavg()`.

It does what a user does, on a schedule: STOP then PLAY every 60 s, a tour of all screens, Synth, Waves and Chains every 2 min,
Settings, the …pump window and the waves / chains frames every 3 min, and a reload of the song (`__seqTest.load`, the `setSong` path)
at half time. After the run it fits a line to each series (per minute, after a 2-minute warm-up, `--warm=N`) and prints PASS / FAIL /
NOTE: a count FAILs when its slope is over a stated bound **and** it is still rising in the second half of the run (a step is not
growth); DOM nodes and listeners are judged on the lowest value of each minute, because a screen's DOM comes and goes. Audio-health
series are timing, so they are only ever a NOTE. Raw samples go to `tests/sweep/out/soak-<seq>-<timestamp>.json` (gitignored); score a
file again with `--rescore=<file>`. `--phone` is touch + Mobile setup + a 412 x 915 window + a 4x CPU throttle: it is not a phone's
audio stack. Run it on a quiet machine, one browser at a time; on a shared one read the trends, not the numbers. Report:
[`docs/audits/2026-11-10-soak.md`](../../docs/audits/2026-11-10-soak.md).

## Redraw churn (`redraw-churn.mjs`)

`node tests/sweep/redraw-churn.mjs [--secs=5] [--screens=song,phrase,mixer,fx,inst,project] [--top=12] [--json]` plays `sounds/ELECTRO.thunder` in
headless Chrome and, per tracker screen, counts the DOM writes the page makes in a few seconds (a MutationObserver on the page) and how many of
them leave the value as it was (a text node replaced by the same text, an attribute set to what it was). The redraw paths that run while a song
plays should write only what changed (`setText`, 10 Nov; `tests/redraw-churn.test.js`). About 1 minute for six screens. After the change the
same-value text / node writes are 0 on every screen (before: 1,100 to 2,100 in 5 s); same-value attribute writes (`hidden`, `title`, `aria-*`)
remain and are not counted as garbage (an identical attribute is not stored again).

## render-vs-live.mjs (14 Nov, "renders should sound like the playback")

`node tests/sweep/render-vs-live.mjs --song=/path/to/song.thunder --from=72 --rows=4 --tracks=0,2,13,9 --full` renders a stretch of a
song offline (the app's own `renderOut`) and captures the same stretch played live by the audio-thread sequencer, track by track, and
prints PASS / FAIL for level, left and right level and balance (stereo), octave spectrum, timing (INCONCLUSIVE when the envelope has no timing information; exit 2 if every row is, or if nothing was compared) and envelope (`ri-lib.mjs` holds the session, alignment and band maths). It warms
the engine up with a PLAY / STOP first and retries when the core skipped; it needs a quiet machine (a starved capture reads as a
difference). Waveform correlation is NOT a pass criterion: the synth engines' oscillator phase depends on when the note starts.

## render-stereo.mjs and render-many.mjs (15 Nov, stereo pads in renders; renders silent after about 32)

`node tests/sweep/render-stereo.mjs [--live]` loads a stereo WAV (left 440 Hz sine, right noise) into pad 2 through the app's own loader and
renders one note with `renderOut`: PASS when the render's left / right correlation is near 0 (it was 1: the render handed the player only
the left channel) and a mono control sample still renders identically on both channels. `--live` adds the audio-thread and page
sequencers played live (about 25 s each; the table then reads render / live core / live page). About 30 s without `--live`.

`node tests/sweep/render-many.mjs [--n=40] [--stems] [--fx=0] [--exhaust]` renders one song `n` times in ONE page (six tracks, each with a
plate reverb, the safety limiter on) and checks that every render has sound. A render that writes a silent file always FAILs. Cause of the old
failure: the browser allows about 100 WebAssembly memories in a page and Chrome never frees an offline render's audio thread, so each effect
node of each render used one for good (before the fix: 17 renders with sound, then silent files). Effect nodes of one render now share
instances; when the browser really is out of room the render stops with "Render failed: An effect couldn't start in this render…", nothing
written. `--exhaust` runs 3 x `n` and only checks that no silent file was ever written and every refusal came with a message. About 1.5 s
per render.
