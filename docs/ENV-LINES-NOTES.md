IN THE MIDDLE OF: Phase 1 done (this file, line numbers are for `index.html` at beta ba32739, build 647); starting Phase 2 (build 658: envelope times in lines, MS | BARS+LINES).

# Envelope times in lines: findings and design (19 Nov, branch `beta-env-lines`)

Owner's request: choose ms or lines for an envelope length so a noise swell fits a pattern. Follow-ups: 4 bars is the must (8 bars if cheap), and the length is ENTERED and SHOWN as bars + lines (a bar = 4 beats x the song's LPB lines; steps: `++` / `--` one bar, `+` / `-` one line).

## 1. The voice / layer model and every envelope

A kit voice is `mkVoice` (:5130): `V.layers` (8 layers, `freshLayer` :5110), voice rows `VP` (:8489: level, tune, drive, `gate` 5..4000 ms log, `len` "Max len" 0.1..300 s sq), pad effects `V.fx`, plus the voice-wide blocks. Voices may instead be an engine (`V.engine`: bass, vox, blip, `isXEngV` the X engines); those have their own envelopes (`BU.atk..rel` :9686, engine cards) and are NOT layers: out of scope here.

Layer types (`TYPE` / `TYPES`): sine ... noise (`white`, `pink`, `brown`, `blue` through `makeOsc` kind 0..3, :4296), `metal`, `lfsr`, `dust`, `grit`, `plaits`, `elements`, `waves` (these three pre-render through the wasm DSP, :4415), `pwm`, `sample`, `grains`, `wavetable`, `fm2`, `chord`, `karplus` ... All of them go through ONE per-layer amp envelope in `renderLayer` (:4384), so one change covers every type.

Envelopes (all times in ms, all stored as plain numbers on the layer / voice object):

| Envelope | Fields | Slider range / curve (`P` :8438) | Drawn / edited | Consumed |
|---|---|---|---|---|
| Layer AMP, mode `L.mode` AHD (drums) or ADSR (synth slots) | `atk` 0.1..2000 log, `hold` 0..300000 sq, `dec` 1..6000 log, `sus` 0..1, `rel` 1..6000 log | `makeRow(P[k], k, Lc, ...)` rows in the Amp section (:9648..9654); AHD hides sus / rel, ADSR hides hold; no envelope graph, just rows | rows via `makeRow` (:9213: slider 0..1000 mapped by `toPos` / `fromPos` :8526, readout `def.f`, double-click types a value) | `renderLayer` :4390 `env1` (AHD: atk, hold, dec; ADSR: lv(t) while t < gate, then release `rel`); `layerLen` :4372 and `voiceLen` :4377 set the buffer length; `relOf` :4993 reads `L.rel` for the note-off fade (page player and audio-thread core) |
| Layer FILTER env | `fAtk` 0.1..2000, `fDec` 1..6000 (amount `lpEnv`, `hpEnv`) | Filter section (:9658) | rows | `renderLayer` `fenv` :4405 |
| Layer PITCH env | `pDec` 1..2000 log (`pAmt`) | Pitch section | rows | `renderLayer` `ptau` :4388 |
| Pad (voice-wide) AMP | `gAtk gHold gDec gSus gRel`, `gMode`, `gAmpOn` | same `P.atk..rel` defs (:14205) | Pad amp section, `buildVoiceGlobals` :14192 | `voiceAmp` :5016 |
| Pad FILTER / PITCH | `gFAtk gFDec` 0.1..2000 / 1..6000, `gPDec` | `VGP` :8498 | same section | `voiceFilter` :5002, `renderLayer` `gPtau` |
| Mod envelopes | `env1A..env3R` | :8516 | MOD section | `voiceMod` :5112 |
| Pad Gate / Max len | `gate`, `len` | `VP` | Pad row | see section 2 |

JS render cores are the only place a layer is rendered: `renderVoice` :5042 -> `renderLayersOnly` :5057 -> `renderLayer`. Plaits / Elements / Waves layers use wasm (PL / TX) only for the oscillator pre-render; their ENVELOPE is still the JS `env1`. Nothing in the layer path is C++ except through the native port (section 4).

## 2. How a sound exists in time; the longest note

Every sound the players make is ONE pre-rendered mono (or stereo with a stereo sample) Float32 buffer per pad / slot, made by `renderVoice(V, sr, stages, hold)`:
- Drum pad (`isSyn(i)` false): `hold = 0`; length = `voiceLen(V)` = the longest layer (`delay + atk+hold+dec`, AHD) capped at `V.len` (default 4 s via `mkVoice`, UI up to 300 s).
- Synth slot (`isSyn` true): `bgRender` / `ensure` pass `hold = v.len` (:8859, :8931; default 6 s); `renderLayersOnly` :5058 makes `gate = len = hold`, so the ADSR sustains for the WHOLE buffer, then the buffer ends. A note-off makes the player fade the voice over `relOf(V)` (:9046, :22956, core `rel`).
- So the longest note a slot can sound = the buffer = `V.len` (up to 300 s, `SMP_MAX` 300 :5040 for samples). A swell whose attack is longer than `V.len` is cut off. That is the only length cap on layer voices; the players (page player: AudioBufferSourceNode, :22940; audio-thread core, `core/core-processor.js`; offline render :25253 / :25365) play a buffer to its end and have no other cap. `kRdRing` / `RD_RING` (core-processor.js :207) is only the 16 K-sample ring for CUT / RES / DEC shaping of a tracker note, never the buffer. `coreBuf` :24480 transfers the whole buffer to the core. The variation cache (`VARBUF_MAX_BYTES` :11773: 64 MB phone, 256 MB computer) holds MUT / GEN renders: one 64 s mono buffer is 12 MB.
- Memory at 48 kHz, float32: mono 192 KB per second: 15 s 2.9 MB, 30 s 5.8 MB, 64 s 12.3 MB. Stereo (a stereo-sample layer) doubles it.
- Render time (measured now, Node, this machine, pink noise, ADSR, one layer, nothing else): about 140 to 230 ms per second of sound (6 s: 1.0 to 1.7 s; 21 s: 2.8 to 3.9 s; 64 s: 8.5 to 9.8 s). That is far too slow for a phone. The profile: 55 percent is `voiceMod` (:5112), which runs per 16-sample block and builds an object even when no mod route is used, plus 10 percent garbage collection. A fast path for "no route has an amount" gives the same numbers and is part of Phase 2. All pad renders run in the render Worker (`bgRender` :8854), so the UI never waits, but a long swell must still land in a few seconds.

Players (tap, keys, tracker note with a long gate, page sequencer, audio-thread core, offline export): all play the same buffer; a note shorter than the swell ends it early on a synth slot (note-off fade), a drum pad plays the whole buffer. No player needs a change for a longer buffer.

## 3. Tempo

Seconds = `lines / lpb * 60 / bpm`. Conversion belongs at render time in `renderVoice` (a pre-pass that turns every lines-mode envelope into ms and lengthens `len` / `hold` to fit), from `songBpm()` (:5116) and a new `songLpb()` next to it. The Worker gets `lpb` as well as `bpm` (its glue sets `S = {bpm}` :8798; `renderAsync` :8838 posts `bpm`). The cache: `padBpm(i)` (:8849) returns the song tempo for pads that follow it (synced LFOs...) else 0; `stale()` compares it with the cache entry, `bgRender` / `ensure` re-render on a difference. A pad with a lines-mode envelope makes it return the tempo AND the LPB (`bpm + lpb / 100`, for a pad that does not use lines the old value exactly), so a BPM or LPB change re-renders exactly as a synced LFO does. The stretch cache (`stKey` :23243) is separate and unaffected. MPC-style variation renders key on `padRev`, which every install bumps (:11736).
- Phrase with its own LPB (`S.plpb`) and TPO / tempo commands mid-song: not followed. The envelope is rendered once with the SONG'S BPM and LPB settings (Project tempo, LPB). The row says so.
- Old songs (no unit field = ms): byte-identical render (test), and `padBpm` unchanged.

## 4. Native port

Read from `origin/native` (`native/core/src/engine/`): `EngLayer` (engine.h :68) holds `atk hold dec sus rel` as ms doubles, `EngVoice` `gate len`, `EngRenderOpts` has `hold` and `bpm` (engine.h :107-108) but no lpb; `engine_json.cpp` :17 reads the layer fields by name; `eng_layers_len` / `planOf` size the buffer from `voiceLen(V, gate, len)`.
The design keeps the ENGINE in ms: the cores and the C++ engines do not change. What the native HOST must do (nothing in `native/` is touched by this PR):
1. Read the new optional voice JSON fields: layer `aU` (`'l'` = lines), `atkL holdL decL relL`; layer `fU`, `fAtkL fDecL`; voice `gU`, `gAtkL gHoldL gDecL gRelL`; absent = ms.
2. Before `eng_render_voice`, for each field set in lines write the ms (`lines / lpb * 60 / bpm * 1000`, each stage clamped to 64 s) into `atk` etc.; take `lpb` and `bpm` from the song (add `lpb` to `EngRenderOpts` or convert in the host).
3. Lengthen `V.len` and `opts.hold` exactly as `envLinesFit` does (the JS function is the reference): `need = max over lines layers of delay + stages (+ one bar sustain for ADSR)`, `len = min(300, max(len, need))`, `hold = hold ? min(300, max(hold, need)) : 0`; the buffer bound (`eng_render_bound`) already follows `len`.
4. Re-render the pad when bpm OR lpb changes if the voice has a lines unit (the host's own `padBpm`).
5. The constant that matches the caps: `kSmpMax` (300 s) in `engine.cpp` :334; unchanged.

## 5. Choices for the lead (recommended answer first)

1. Unit per ENVELOPE (not layer / voice): layer AMP, layer FILTER env and pad AMP get it in this build; pitch, pad filter and mod envelopes later (the helper is generic). The noise layer is just a layer type: it works because the amp envelope is shared.
2. Range: 0 to 8 bars per stage (`32 * LPB` lines: 256 at LPB 8, 128 at 4, 512 at 16); one stage is clamped to 64 s; whole lines only (a line at LPB 8 and 131 BPM is 57 ms; halves would be below useful).
3. Row text: slider readout shows `4 bars`, `2 bars + 8`, `12 lines`; a line under the envelope rows says "Attack 4 bars = 7.33 s at 131 BPM, 8 lines a beat. The tempo is the song's setting when the pad was rendered."
4. Switching unit converts to the nearest equivalent (ms -> lines rounds to a whole line, lines -> ms is exact at the current tempo), so the sound does not jump (to within a line).
5. Storage: separate optional fields (`atkL` ...) plus the unit `aU: 'l'`; the ms fields stay as the equivalent at the last edit, so a build without this feature (or code that reads `atk` directly) still sees a sane ms value. Saving in ms mode writes nothing new.
6. Lengths: lines-mode envelope makes the buffer long enough for its stages plus one bar of sustain on ADSR (never more than 300 s, today's cap); ordinary pads untouched. Holding a note longer than that: raise Max len (existing row).
7. 8 bars: shipped at LPB 8 if the measured phone render time after the `voiceMod` fast path is acceptable (see PR text).

## 6. Tempo-driven re-render (verifier F2)
`lnTempo()`: the tempo a lines pad renders at follows the song's after 300 ms without a change (a 100 ms poll then calls `prerender`); while the song plays, a change under 0.5 percent of the tempo of the last render is ignored (LPB changes always count). `bgRender` already runs one render per pad and goes again at the latest tempo. The native host should do the same.
