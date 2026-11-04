# Sweep scripts (headless Chrome)

Not part of `npm test`: they drive the whole app in headless Chrome through its own functions (`?seqtest=1`, `__seqTest.ev`)
and take a long time (on a 2-CPU machine shared with other work: engines ~1.5 h, effects ~1 h). Each prints a line per check
as it goes (PASS / FAIL / NOTE / SKIP), then the rows that are not PASS and a count. Exit code 1 if anything FAILs.
Needs Chrome and playwright (`CHROME=…`, `PLAYWRIGHT=…/playwright/index.mjs` if they aren't found). `lib.mjs` is the
shared harness (static server, page, error capture).

| Script | What it checks |
|---|---|
| `node tests/sweep/engines.mjs` | every engine in the picker on a drum pad and a synth slot (page thread and render Worker), every factory preset, every parameter row at min and max; live engines through the offline mix beside their render; the pad preset sheet's factory voices, layer presets and every layer type; the 30 AKWF starter tables and imported WAVs in Table; SoundFont GM programs; Resynth and Freeze with a test sample. `--only=k1,k2`, `--from=k` (resume), `--twins` (also the …pump live twins' presets and rows, which render with their twin's code) |
| `node tests/sweep/effects.mjs` | every track / master effect (TFX, …pump included) fresh, every parameter at min and max (…pump EQ/BAND: band 1 for the per-band rows), every factory preset, through the app's own effect worklet offline; every pad effect (FX) with its rows; the master chain (default …pump chain, older S.mst chain); the FX screen, FX bar, panel, …pump windows and preset pickers for every effect; the …chains and …waves pages in their frames with every module added and removed. `--only=track,pad,master,ui,frames`, `--fx=k1,k2` |

What the results mean:
- **FAIL**: an exception, non-finite samples, a crash or reset in the worklet that isn't explained by gain, silence where a
  sound is expected, a page error.
- **NOTE**: a peak over full scale or silence at an extreme of a control (an Output or Makeup at max, a Level at min…), or
  a peak over 1 from a preset. A pad or track is not the output: the master and its limiter follow. Gain that only blows
  past the worklet's ±8 guard on the loud test signal is re-checked at −40 dB (track FX) / −20 dB (pad FX) and is a NOTE
  when it scales down with the input.
- **SKIP**: deliberately not run (see the row).

Known limit of the harness: Chrome keeps each offline render's worklet wasm memories until it tears that thread down, and
in a sandbox with a small address space only ~100–125 wasm memories fit in one page. `effects.mjs` opens a fresh page per
batch of 32 effect nodes for that reason. Desktop Chrome has far more room; in the live engine the audio thread frees
dropped effect nodes itself (150 add/remove cycles checked).
