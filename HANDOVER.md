# Thunder Plus — handover notes

Pick-up notes for continuing this work in a later session. Read this together with
[`README.md`](README.md) (the app's own docs). Keep this file up to date at the end of a
work session.

## Where things stand

- **Repo:** `https://github.com/matthettich/thunder-plus` · branch `main`.
- **HEAD:** `b0a2e1d` — docs and handover commit; original Vocoder implementation is `5a17b51`.
- **Current uncommitted changes (do not commit without asking):** the Spectral Vocoder mix/alignment
  fix, cache-buster changes to `?v=5`, service-worker cache bump, and updates to this handover file.
  The downstream `../Thunder-` repo has the matching worklet/index/cache changes uncommitted.
- **Prior docs/rebuild commits (local, not pushed):** `thunder-plus` docs commit `b0a2e1d`; `../Thunder-`
  rebuild commit `eba1dd5` (Thunder- branch `main`, based on `6c4dc71`).
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
| `index.html` | `chainsReady()` 3358 | Current cache-buster `?v=5`. |
| `chains/index.html` | `ChainsBridge.standalone()` 688 | Current cache-buster `?v=5`. |
| `sw.js` | 3, 7 | Current cache name `thunderplus-v29`; precaches `chains-worklet.js?v=5`. |
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

**Validated:** syntax checks passed on both worklets, service workers, Electron entry points, the build script,
and inline app scripts. DSP harness confirmed every mode is non-silent, `inR` fallback, `mix=0` dry pass,
out-of-range mode clamping, and draft band capping. Spectral overlap-add regression: identical 187.5 Hz
carrier/modulator remains RMS 0.707 with correlation 1.0 across Mix 0, .25, .5, .75 and 1. All three Plus
pages and rebuilt Thunder- loaded in headless Chrome with clean console output. The engine-graph test
(`trackIn → vocoder → chainOut`) is non-silent. Not verified with live AudioWorklet audio in a real browser.

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

1. **Existing commits are local, not pushed:** Thunder Plus has `b0a2e1d` (README/HANDOVER docs), and
   `../Thunder-` has `eba1dd5` (rebuild from Plus `5a17b51`, based on Thunder- `6c4dc71`). The rebuild
   contains all Plus changes since `63dcaae`, not just the Vocoder. The build is reproducible.
2. **Current DSP fix is uncommitted in both repos.** In `vocSpectral`, dry now has the same 128-frame
   latency as wet, and the Hann overlap-add is normalized per sample. Before the fix, identical
   187.5 Hz carrier/modulator fell from RMS 0.707 dry to 0.083 at mix=.5; with the fix the RMS stays
   0.707 and correlation is 1.0 at Mix 0, .25, .5, .75 and 1. Worklet references are `?v=5`, with
   caches `thunderplus-v29` and `thunder-v54`; `../Thunder-`'s worklet is byte-identical to Plus.
   These changes should be reviewed and committed when authorized; nothing has been pushed.
3. **README wording — done.** The …chains bullet now says "eleven effects" (EQ, Comp, Saturation,
   Tape, Chorus, Crush, Delay, Reverb, Rings, Clouds, Beads; the Vocoder is called out separately).
4. **`normalizeChainsImport`** (`index.html` ~3150) has a `keep` set used when opening a …waves patch
   as a …chains patch. The Vocoder is intentionally **not** in it (a …waves patch can't contain one).
5. **Additional bugs found + fixed (all uncommitted).**
   - **`sw.js` (both repos):** the navigate branch stored every page under the constant key
     `./index.html`, so opening the …chains/…waves editor overwrote the cached app page (offline root
     reload would then serve the editor), and it cached non-`ok` responses over good ones. Now keyed by
     the request and gated on `res.ok`. Behaviour verified in a mocked Cache/fetch harness.
   - **`Thunder-/sw.js`:** `activate` deleted *every* cache on the origin (`k !== CACHE`); scoped to
     `k.startsWith('thunder-')` like Plus's `thunderplus-` scope.
   - **`Thunder-` hidden editors:** `build-thunder.py` stripped the Waves/Chains seg but left
     `openWaves`/`openChains`/`focusChainHit` reachable, so turning CHN on for an already-chained track
     jumped to a `chains/index.html` iframe that isn't shipped (404). The build now makes
     `openWaves`/`openChains` no-ops and drops the auto-focus call; verified the paths are unreachable.
   - **`electron/main.js` (both):** `app://thunder` (no trailing slash) 404'd; now maps `''` to
     `index.html` too.
   - **`waves/index.html`:** the editor loaded `waves-worklet.js?v=2` while the app and SW use `?v=4`,
     so the worklet was fetched under a key the precache doesn't cover (editor broken offline on a
     fresh install). Now `?v=4`.
   - **`tools/build-thunder.py`:** now syncs the two engine `?v=` strings into `../Thunder-/sw.js` and
     bumps its `thunder-vNN` cache name when they change (idempotent when already matching), instead
     of leaving Thunder-'s sw.js silently stale after a worklet bump.
6. **Hear it for real.** Load `chains/index.html`, add a **Vocoder**, patch Track In → In, put a
   synth/loop into In R as the modulator, and sweep Mode/Resonance. Confirm CPU headroom on a phone.
   Verified this session: every mode non-silent, `inR` fallback works, `mix=0` is dry-exact, invalid
   modes clamp, draft caps banks to 16, and `trackIn → vocoder → chainOut` is non-silent. Plus's root,
   chains and waves pages and the rebuilt Thunder- page all rendered in headless Chrome with clean
   consoles; syntax/cache checks pass. A local Node benchmark on this machine measured 64-band full
   mode at ~1.3% of one CPU core. Live AudioWorklet playback still needs a real browser.
7. **Mobile performance pass (uncommitted, no worklet change).** Aimed at Fold-class input lag / minor
   hangs:
   - **Tracker meters** (`index.html` `frame()`): the analyser scan ran every display frame for the whole
     life of the tracker view (up to 2.2M sample reads/s at 120Hz with 32 tracks). It now runs ~30×/s and
     stops entirely once nothing has sounded for 1.5 s (`audioWake`, bumped by `flash`/`pump`/`preview`),
     with the decay made time-based so the fall-off is unchanged.
   - **Graph editors** (`waves/index.html`, `chains/index.html` `loop()`): cables were re-measured at
     60–120 fps forever. Now they redraw every frame only while a drag, auto-pan or pan/zoom is live
     (`sceneHot`, marked by `applyPan`/`syncCableEls`/`setCollapsed`/drag) and otherwise on a slow
     safety pass; the …chains host meter/CPU labels repaint at their telemetry rate, not per frame.
   - **Drag redraws**: module drags (`place()`), the editor `pointermove` and the tracker grid
     edit-drag + scroll now coalesce to one `requestAnimationFrame`, instead of one full `draw()` per
     raw pointer event (120/s on a Fold). The edit-drag flushes its final step on pointerup.
   - **Left as follow-ups (bigger/riskier):** moving `renderVoice` (up to 12 s of audio, runs on the UI
     thread after a 45 ms debounce) to a Worker; incremental playhead painting so `draw()`/`drawArr()`
     stop rebuilding the full grid/arrange strings each playback step.

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
