# PFX notes: every pad effect in the core's pad render (builder; branch n1/pfx-pad-effects)

## Status (updated at every push)
- 24 Nov, first push: SURVEY ONLY (this file). Code starts next. Nothing built yet.

## What the page does (index.html, ENGINE block)
- `renderVoice` -> `renderLayersOnly` (layers) / `padFx` (Blip and every X engine that is not `live`: `engFx(V)`). Bass, Voice, Acid and Machine return before any pad effect: they are DRY on the page.
- Order: the voice is rendered (N samples), then `ext = Float32Array(min(N + round(V.tail * sr), sr * (SMP_MAX + 12)))` holds it with zeros after; then for each `f of V.fx` with `f.on && FX[f.type]`: `buf = applyFx(f, buf, sr)` IN LIST ORDER (offline, whole buffer, one effect after the other, mono in / mono out). Then trim (`end` while |x| < 1e-4) and a 2 ms fade.
- Tail: only the voice's own `tail` (default 2 s) gives room, PLUS the two pitch effects (pdelay / preverb) which lengthen the buffer themselves (`N = a.length + tail`). Every other effect keeps the length (the library effects ring out only inside `tail`).
- Dry/wet: each effect's own `mix` (or `wet`, `blend`). The three reverbs `reverb`, `space`, `room` are run fully wet in the wasm and blended by `rvMix(mix)` in `txRun` (`out = a * dry + wet * w`, REVERB MIX).
- A value a saved voice lacks is `undefined` on the page (no defaults are filled at load; `freshFx` only fills a NEW effect): it reaches the maps as NaN.
- `fxReady(f)` false (library not loaded) = the sound passes. The native core has the libraries, so native = "ready".
- A new effect row is added by the UI to at most 6 per pad (`v.fx.length >= 6`); the core's EngVoice holds 8.

## The pad effect types the page offers (FX table, 4392-4528, and MODFX with `pad:true`) = 66
| class | types | how the page runs it | native piece to reuse |
|---|---|---|---|
| JS cores (4) | degrade, reson, pdelay, preverb | DegradeCore / ResonCore / PitchFxCore in 128 blocks | DONE before PFX (`pad_degrade`, `pad_reson`, `pad_pitchfx`, kinds Degrade / Reson / PitchDelay / PitchReverb). Not touched. |
| tfx.wasm, `tx:` id (28) | eq 6, comp 7, reverb 8*, delay 9, beads 10, flanger 11, phaser 12, tremolo 13, odrive 14, folder 15, autowah 16, pshift 17, ladder 18, xmod 19, space 20*, lofi 21, drive 22, air 23, room 24*, comb 25, transient 26, tgate 27, ott 28, fshift 29, vinyl 30, dub 31, cab 32, tube 33 (* = REVERB MIX blend) | `txRun`: `tfx_new(id, sr)`, `p[12]` = the pad's own `map(f, sr)` (REAL values, not the track's bytes), 128-frame blocks, L = R = sample (+-1e-18 alternating dither, |x| >= 8 or NaN -> 0), out = (L + R) / 2 | the same `Fx` objects the track host makes (`make_fx` in fxhost.cpp, ids 0..33): REUSED, with the PAD maps written in C++ (the track `fx_map` takes bytes, not these). |
| thunder-dsp.wasm (6) | density, tape, chorus, crush, rings, clouds | `chunked` / resample around `aw_density`, `aw_totape`, `ds_chorus`, `ds_decimate`, `rx_*`, `cl_*` | density, tape: same Airwindows classes the tfx ids 2 / 3 wrap (same parameter order, see below): through `make_fx`. chorus, crush: DaisySP Chorus / Decimator, written as in w_fx.cc (the tfx wrappers differ: 1.4 vs 1.6 gain, per-side decimators). rings, clouds: NOT in the core (thunder_dsp_w conflicts with thunder_dsp_pe in one program): see the later section. |
| MODFX modules (28) | spring, plate, lverb, ensemble, dimension, bbd, harm, flex, sgate, retrig, spectral, stretch, convolve, autotune, vowel, rotary, gdelay, reverse, sdelay, filter, cut, compressor, buscomp, glue, ngate, limiter, ringmod, envfilt | `modFxRun`: the shared module made with `create(sr)` (seeds from the engine's counter), params = the pad's own values by module param name (non-finite -> module default), 128-frame blocks, both inputs the mono sample, out = (outL + outR) / 2 (a value outside +-8 -> 0); a module with `latency` run fully wet (mix null or >= 0.999) is pulled back by it; tempo = songBpm(), playing, startN 1 | `modfx::find(key)->make(sr, seeds)` + `run`: REUSED (the FX1 / U31 modules). |
So 66 = 4 + 28 + 6 + 28.

## Who uses what (census)
- The repo has ONE song (sounds/ELECTRO.thunder). `census.mjs` does not list pad effects; read directly: 40 voices, two with pad effects:
  - voice 17 (layers): `crush` (Bitcrush: smooth false, down 0.2, bits 10, mix 0.4). Applied by the page.
  - voice 10 (engine `mach`): `spring` (decay .6 tone .5 chirp .6 length .5 drip .4 mix .35). **The page does NOT apply it: Machine is a `live` engine, `engFx` is false.** So for ELECTRO the right native result is "dry", which the core already does (anyFxOn is false for Acid / Mach / Bass / Vox). The songfit fallback check that flagged "spring" is a false positive for this voice; it should apply the page's `engFx` rule (only layers, Blip and non-live X engines carry pad effects).
- Order of work (by use, then by cost): (1) crush (the only effect ELECTRO actually uses), (2) the generic offline MODFX path (spring + 27 more, one code path, the modules exist), (3) the generic tfx path (28 effects, one code path + the pad maps), (4) density / tape / chorus, (5) rings / clouds.

## Plan
1. `engine/padfx.{h,cpp}`: one function `pad_fx_run(const EngFx&, in, n, sr, bpm, out)` for the new kind `EngFx::Kind::Lib` (the four existing kinds stay as they are). The page's value rows are parsed into EngFx (NaN for a missing key).
2. Both render loops (engine.cpp layers, xeng_voice.cpp X engines) call it; `EngRenderOpts::libFx` (default true) turns the library effects off so the old voice goldens (made by a Node without the wasm: `room` passes the sound) keep their meaning.
3. Goldens: `tools/padfx-dump.mjs` runs the page's own `applyFx` / `padFx` (acorn cut, tfx.wasm, thunder-dsp.wasm, WavesDSP) for each hosted type -> `tests/golden/padfx_*`; `tests/test_padfx.cpp` compares (gate -100 dB RMS for library effects, exact where exact).
