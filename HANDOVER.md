# Thunder Plus — handover notes

Pick-up notes for continuing this work in a later session. Read this together with
[`README.md`](README.md) (the app's own docs). Keep this file up to date at the end of a
work session.

## Where things stand

- **Repo:** `https://github.com/matthettich/thunder-plus` · branch `main`.
- **HEAD:** `5a17b51` — *"Add a Vocoder …chains module: Talkbox, 4-64 band banks and spectral cross-synthesis"* (pushed).
- **Working tree (uncommitted, do not commit without asking):** `README.md` modified
  ("ten effects" → "eleven effects", plus earlier build-note/running-locally additions);
  `HANDOVER.md` untracked (this file).
- **App:** no build step. `index.html` is the whole tracker/synth app; `chains/index.html` and
  `waves/index.html` are the two modular patch editors loaded in frames.
- **Local dev server:** running (persistent) at **http://127.0.0.1:8734/** — see *Running it locally*.

## Just completed — the Vocoder …chains module (commit `5a17b51`)

A new audio module for the **…chains** editor. Carrier = **In** (left), modulator = **In R**
(right); with **In R** unpatched the modulator falls back to **In**.

| File | Where | What |
|---|---|---|
| `chains/chains-worklet.js` | `FFTN`/`fftInPlace` ~518, `Vocoder` 532–549, `vocBands` 551, `vocSpectral` 578, registered 596 | The DSP and the module definition. |
| `chains/index.html` | table 409, palette 1541, help 1838 | Editor mirror: params, UI labels, palette entry, help text. |
| `index.html` | `chainsReady()` 3358 | Cache-buster `?v=3` → `?v=4`. |
| `chains/index.html` | `ChainsBridge.standalone()` 688 | Cache-buster `?v=3` → `?v=4`. |
| `sw.js` | 3, 7 | Cache name `v27` → `v28`; precache `chains-worklet.js?v=4`. |
| `README.md` | 40 | Module list mentions the Vocoder. |

**Parameters** (worklet `params` must equal the editor table's, `[default, min, max, step]`):

- `mode` `[0,0,6,1]` — Talkbox, 4, 12, 16, 32, 64 bands, Spectral
- `res` `[.5,0,1,.01]` — band resonance / sharpness
- `atk` `[2,.1,100,.1]` ms · `rel` `[60,5,1000,1]` ms — band envelope followers
- `mix` `[1,0,1,.01]` · `level` `[1,0,2,.01]`

**DSP:** `vocBands()` is a bank of log-spaced TPT band-passes whose per-band envelope (attack/release
from the modulator) scales the carrier's band; Talkbox is 6 resonant bands with `tanh` drive.
`vocSpectral()` is a 256-point FFT cross-synthesis (carrier bins take the modulator's magnitude, 50 %
overlap-add). Blocks are 128 frames (`BLOCK`), matching `FFTH`. In `draft` quality the band count
caps at 16.

**Design points:** it is a **pure-JS** module (not a `tfx.wasm` effect), so it is declared with explicit
`scope`/`inputs`/`outputs` and **no `fx:true`** in the editor table — that avoids the "couldn't load the
effects code" note. It has no third-party code (a hand-written radix-2 FFT and biquads), so
`THIRD_PARTY_NOTICES.md` was **not** changed.

**Validated:** `node --check` on the worklet, `sw.js`, and all inline scripts; a Node DSP harness
(see below) confirmed every mode is non-silent, `inR` falls back to `in`, `mix=0` passes the dry
carrier exactly, draft caps bands, out-of-range modes clamp; an engine-graph test
(`trackIn → vocoder → chainOut`) is non-silent; headless Chrome renders the palette item and help.
Not yet verified with **live audio** in a browser (headless can't run the AudioWorklet).

## Running it locally

No build step; serve the repo root over HTTP (the editors load their worklet by `fetch`, which
`file://` blocks):

```
cd thunder-plus
python3 -m http.server 8734 --bind 127.0.0.1
```

Then open:

- **Main app:** http://127.0.0.1:8734/index.html
- **…chains editor (the Vocoder lives here):** http://127.0.0.1:8734/chains/index.html
- **…waves editor:** http://127.0.0.1:8734/waves/index.html

If a link stops working, the server process was stopped — restart it with the command above.

## How to validate (there is no test suite)

1. **Syntax:** `node --check` the worklets directly; for the single-file apps, extract each inline
   `<script>` and `node --check` it (a small Node script over `<script>…</script>` runs is enough).
2. **DSP harness** (works because the worklet exports itself on a non-AudioWorklet global):

   ```js
   (0,eval)(require('fs').readFileSync('chains/chains-worklet.js','utf8'));
   const { MODULES, ChainsEngine } = globalThis.ChainsDSP;   // test MODULES.xxx.process / ChainsEngine
   ```

3. **UI render:** headless Chrome `--headless=new --virtual-time-budget=6000 --dump-dom URL` and grep
   the DOM for the new palette item / help text.

## Open items / where to pick up

1. **…Thunder is rebuilt (2026-10-13 session, NOT committed there).** A fresh clone now lives at
   `../Thunder-` (sibling of this repo, as the build script expects). `python3 tools/build-thunder.py
   ../Thunder-` ran clean and `Thunder-/sw.js` was bumped (`thunder-v52` → `v53`,
   `chains-worklet.js?v=3` → `?v=4`). Its diff vs its HEAD (`6c4dc71`) is large because it lagged by
   several Plus commits, not just the Vocoder. **Still to do in `../Thunder-`:** review the diff,
   commit, and push (needs confirmation — never commit without asking).
2. **README wording — done.** The …chains bullet now says "eleven effects" (EQ, Comp, Saturation,
   Tape, Chorus, Crush, Delay, Reverb, Rings, Clouds, Beads; the Vocoder is called out separately).
3. **`normalizeChainsImport`** (`index.html` ~3150) has a `keep` set used when opening a …waves patch
   as a …chains patch. The Vocoder is intentionally **not** in it (a …waves patch can't contain one).
4. **Hear it for real.** Load `chains/index.html`, add a **Vocoder**, patch Track In → In, put a
   synth/loop into In R as the modulator, and sweep Mode/Resonance. Confirm CPU headroom on a phone.
   Re-verified headless this session (2026-10-13): all 7 modes non-silent (rms 0.13–0.44 after
   settling), `inR` falls back to `in`, `mix=0` passes dry exactly, `mode=99`/`-3` clamp, draft caps
   64→16 bands, `trackIn → vocoder → chainOut` graph non-silent in bands and spectral modes, and
   headless Chrome renders the palette item + help (Talkbox…Spectral choices, `?v=4` fetches in
   `index.html`/`chains/index.html`/Thunder rebuild). Still not verified with **live audio** in a
   real browser (headless can't run the AudioWorklet). **Harness note:** fake `io` objects need a
   `linked: new Set(...)` — `linked(io,'inR')` reads `io.linked.has`, not the presence of `io.in.inR`.
5. **Behaviour to know:** on bands >16, `draft` quality silently drops to 16 bands (load shedding).

## Checklist — adding another …chains module

1. **Worklet** (`chains/chains-worklet.js`): define `const Foo = { type, scope, cost, inputs, outputs,
   params, create, process }` and register it in the `MODULES` object. Reuse `clamp`, `linked(io,'port')`,
   and `ctx` (`sr`, `block`, `quality`, `transport`, per-track `trackInL/R`, `busL/R`, `scL/R`).
2. **Editor** (`chains/index.html`): add a matching entry to the `MODULES` table — `params` **must**
   match the worklet's `[default,min,max,step]`, plus `ui` labels (`l`), formatters (`f`) and dropdown
   `choices` (`c`). Add it to a `PALETTE` group and add help text to `ABOUT` in `helpDoc()`. Use
   `fx:true` only for `tfx.wasm` effects (it adds the wasm-missing note and default ports).
3. **Cache-bust:** bump `chains-worklet.js?v=` in `index.html` + `chains/index.html` and the `CACHE`
   name in `sw.js`.
4. **Validate** with the recipes above.

## Conventions worth knowing

- **Ports/cables:** audio = teal, `cv`/modulation = purple dashed, `gate` = pink dashed. Any slider
  has a purple modulation jack; a cable into `p:<param>` modulates it.
- **Two READMEs in one:** `README.md` lines 1–15 are Thunder Plus's; the rest is …Thunder's (the base
  app). Keep new Thunder-Plus-only notes in the top part.
- **Two engines:** `waves/waves-worklet.js` (from the waves repo) and `chains/chains-worklet.js`.

## Related docs

- [`README.md`](README.md) — user-facing docs (start here).
- [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) — third-party licences (unchanged by this work).
- [`tools/build-thunder.py`](tools/build-thunder.py) — the …Thunder build step.
