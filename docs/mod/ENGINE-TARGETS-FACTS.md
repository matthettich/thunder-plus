# Live engines: what a MOD / CUT / RES / ATK / DEC / SUS / REL target would reach (facts only)

Read-only survey, 10 Nov 2026. Code read: `origin/beta` at d378421 (fetched; `git diff --stat` of `index.html`, `core/core-processor.js`,
`pump/synth/`, `waves/waves-worklet.js` against it is empty, so the working tree on `lead/build-553` is the same for these files).
Line numbers are `index.html` unless a file is named. "not checked" means exactly that.

Vocabulary used below: the QUANTUM is the 128-sample block a worklet's `process()` gets (2.7 ms at 48 kHz). A BLOCK is the engine's own
sub-block inside `render()`: bass 32 samples (`BassCore.BLK`, 4826), vox / acid / mach 16 (`this.BLK = 16`).

## 0. Which engines are live (the "xeng worklet template")

- `isLivePad` (21474): `isSyn(i) && (isBassV || isVoxV || liveX(v))`; `liveX = v => XENG[v.engine] && XENG[v.engine].live` (21475).
- Worklet sources: `BASS_PROC` (21283, `thunder-bass`), `VOX_PROC` (21321, `thunder-vox`), `XPROC(name, core)` (21358: a template, used for
  `acid` and `mach` only: `XPROC('acid','AcidCore')`, `XPROC('mach','MachCore')`, built into one Blob with BassCore / VoxCore / AcidCore /
  MachCore `.toString()` in `bassReady` 21442).
- `XENG` entries with `live:true`: `acid` (7889), `mach` (7890), `wavesl` (7913, a patch player, section 7 below), and the pump live twins
  (`PsynCore.xeng()`, 7917; worklet `pump/synth/pump-synth-worklet.js`, module name `thunder-psyn`).
- Blip (`isBlipV`, 5461) is NOT live: render only (`renderBlip` via `renderVoice`). Every other `XENG` entry has `live:false`.
- Page-side nodes are made by `bassNode` (20512; bass, vox, acid, mach), `psNode` (20536), `wlNode` (20554), all in `makePlayer` (20373).

## 1. bass (`BassCore` 4825, `BassProc` 21283)

| # | Fact |
|---|---|
| 1 params | Names / defaults (`BassCore.defaults` 4841): `cut:400` Hz, `res:0.35`, `fenv:2` (octaves, UI -6..+6), `fdec:250` ms (UI 5..3000 log), `atk:2` ms (UI 0.5..2000 log), `dec:300` ms (1..4000), `sus:1` (0..1 level), `rel:80` ms (1..4000), `key:0.5` (key follow), also `wCut:0` (wobble to cutoff, oct), `glide`, `legato`. UI ranges: rows at 9070-9104 (`cut` at 9078: min 30 max 18000 log). `normBass` (4989) copies every default key that is a finite number; `bassParams(V)` (4990) adds `tune`, `drive`, `level` from the voice. |
| 1 curves | Cutoff: linear Hz in the param; the engine applies octaves: `fc = min(nyq*0.98, max(20, p.cut * 2^(p.fenv*this.fe + p.wCut*u) * (f/65.406)^p.key))` (4923). Resonance: `Q = 0.6 + p.res*p.res*11` (4925), so res is squared. Filter env: `this.fe` starts at 1 on each non-legato note-on (`on()` 4871) and decays exponentially `this.fe *= Math.exp(-dt*1000*4.6/Math.max(1, p.fdec))` (4910); there is no filter attack and no filter sustain (fe decays to 0). Amp env: stage 1 attack linear `e1 = e0 + dt*1000/Math.max(0.5,p.atk)` (4906); stage 2/3 decay to sustain `e1 = p.sus + (e0 - p.sus)*Math.exp(-dt*1000*4.6/Math.max(1,p.dec))` (4907); stage 4 release `e1 = e0*Math.exp(-dt*1000*4.6/Math.max(1,p.rel))` (4908). |
| 2 when read | PER BLOCK (32 samples), in `block()`: every `p.*` above is read at the start of each block (4890-4960). So a changed `cut`, `res`, `fenv`, `fdec`, `atk`, `dec`, `sus`, `rel` during a held note is heard from the next block; `atk` only matters while stage 1 runs, `dec`/`sus` while in stage 2/3 (sus is re-read every block, so the level glides toward a new sustain), `rel` from the moment stage 4 starts and while it runs. NOT latched at note-on. Partial amplitudes slide linearly from last block's to this block's values (`dA = (s1-s0)*inv`, ~4955), so spectral changes are interpolated inside a block; `res` / `cut` steps are not zippered by more than that. |
| 3 how a change arrives | `{set: obj}` on the node's port: `this.port.onmessage = e => { const d = e.data \|\| {}; if (d.set) this.c.set(d.set); ... }` (in `BASS_PROC`). `BassCore.set(o)` MERGES: `Object.assign(BassCore.defaults(), this.p \|\| {}, o \|\| {})` (4847), re-rounds `parts`, rebuilds the 64-partial base spectrum and `norm` (cheap). So a partial object `{set:{cut:900}}` works. Message is handled between quanta, so it takes effect at the first block of the next quantum. No smoothing in the message path. Page sender: `bassPush(i)` (21477) posts `{set: bassParams(V)}` (the whole object) to every `bassLive` node with `b.pad === i && b.kind === kind`; called from `changed()` (8688) on every sound edit, and for every voice after a kit load (13488). |
| 4 AudioParams | `rate` (a-rate, default 1, 0..256), `gate` (a), `trig` (a), `leg` (a), `bpm` (k-rate, 10..999), `wob` (k-rate, 0..64). Declared in `BASS_PROC.parameterDescriptors`. `process` reads trig / gate per sample (scan `for i<n`, splits `c.render(L,R,s,i,rate[s])` at each edge) so note-on / off are SAMPLE-ACCURATE; `rate` is read ONCE per segment (`rate.length > 1 ? rate[s] : rate[0]` passed as a scalar to `render`), i.e. about once per quantum, so glides / bends step at quantum rate; `bpm` / `wob` read `[0]`. Core override: first line of `process`: `if (this.__ov && globalThis.__tcOv) P = globalThis.__tcOv(this, P, outs[0][0].length);` and last `if (this.__ov && globalThis.__tcPost) globalThis.__tcPost(this, outs[0]);`. Page player (when core not used): `bassNote` (20589): `b.gate.setValueAtTime(1,t)`, `b.trig.setValueAtTime(++b.n,t)`, `b.rate.cancelAndHoldAtTime(t)` + `setValueAtTime` / `linearRampToValueAtTime(ev.rate, t+ev.slide.dur)`, `b.wob.setValueAtTime(ev.wob,t)`, level / pan on the page's own `GainNode` / `StereoPannerNode` after the node (`b.g.gain.setValueAtTime(gain,t)`, `b.pn.pan...`); note off: `kill` (20623) `b.gate.setValueAtTime(0,t)` (bass release is the engine's own). Tempo: `tempoTo` (20689) `b.bpm.setValueAtTime(ev.bpm, ...)` for every node with `bpm`. |
| 5 voices | MONO: one `BassCore` per node, one node per track and pad (`bassN` key `t:pad:kind`, 20510-20523). A poly track's chord goes to the same node (mono takeover in `startNote`, 20813-20819). Params are the node's single object. |
| 6 rendered twin | `renderBass(V, sr, hold)` (4994): `new BassCore(sr); c.set(bassParams(V)); c.on(false); c.render(...,1); c.off(); c.render(...)` at rate 1 (the pad's own pitch), the same param object and the same class; the rendered pad is then re-pitched by `playbackRate`, so the filter's key-follow and the pitch-relative cutoff differ from live (live passes the note's `rate` into `block`; the render is made at C-4 and shifted). `renderVoice` (4658) calls it. |

## 2. vox (`VoxCore` 5012, `VoxProc` 21321)

| # | Fact |
|---|---|
| 1 params | No cutoff / resonance / filter envelope / decay / sustain. The nearest: `bright` (0..1, default 0.55) = a one-pole low-pass `kb = 1 - Math.exp(-2*Math.PI*(1200*Math.pow(10,p.bright))/sr)` (5306), i.e. 1.2 kHz .. 12 kHz exponential, no resonance; `atk:12` ms (UI 0.5..400 log), `rel:140` ms (1..3000 log); `form` (-1..1 formant shift, `fs = 2^(form*0.35)`), `bw` (softness / formant bandwidth), `cons` (diction speed), `clar`, `voice`, `breath`. Defaults at `VoxCore.defaults` (5033). |
| 1 curves | Amp envelope is a one-pole toward a target: `er = this.envT > this.env ? 1 - Math.exp(-dt*1000/Math.max(0.5,p.atk)) : 1 - Math.exp(-dt*1000*4.6/Math.max(1,p.rel)); this.env += (this.envT - this.env)*er` (5297-5298). `envT` is 1 while a note is held, 0 after off (the vowel is held, there is no sustain level or decay parameter). |
| 2 when read | PER BLOCK (16 samples) in `block()` (5280): `atk`, `rel`, `bright`, `form`, `bw`, `cons` re-read every block. `atk` is read whenever `envT > env` (attack and every re-rise), `rel` whenever falling. Not latched. |
| 3 how a change arrives | Same handler as bass: `if (d.set) this.c.set(d.set)`; `VoxCore.set` merges (`Object.assign(VoxCore.defaults(), this.p||{}, o||{})` 5038) and re-parses lyrics only if `txt` changed. Page sender `bassPush` with `voxParams(V)` (5383). |
| 4 AudioParams | `rate`, `gate`, `trig`, `leg` (all a-rate), `syl` (a-rate, the syllable number), `same` (a-rate, a slide keeps its syllable). No `bpm` / `wob`. Same edge-scan structure as bass (`vxAt(A,i)`), `rate` per segment. Same `__tcOv` / `__tcPost` hooks. Page: `bassNote` sets `b.syl.setValueAtTime(k,t)`, `b.same.setValueAtTime(leg,t)`. Core: `liveStart` sets `syl`, `same` (core-processor.js 639). |
| 5 voices | Mono per node, but a POLY track uses FOUR nodes per pad (`polyVox` 20798: `bassNode(track,pad,j+1)` for j 0..3, keys `t:pad:vox:1..4`); each node has its own `VoxCore` and its own copy of the params (`bassPush` posts the same object to all four, since all are in `bassLive` with the same `pad`). Per-voice state, shared values. Core twin: `liveNote` poly branch (core-processor.js 609). |
| 6 rendered twin | `renderVox(V, sr)` (5391): `new VoxCore(sr); c.set(voxParams(V))`, the whole lyric sung at pad pitch; same class and param object. |

## 3. acid (`AcidCore` 5592, `XPROC('acid')`)

| # | Fact |
|---|---|
| 1 params | `AcidCore.defaults` (5598): `cut:380` Hz (UI 40..6000 log), `res:0.72` (UI 0..1.05), `envAmt:0.6` (UI 0..1; filter env amount), `dec:300` ms (UI 30..3000 log; this is the FILTER-envelope decay), `accent:0.6`, `slide:65`, `ampDec:900` ms (UI 1..4000 log; AMP decay), `sus:0.8` (UI 0..1), `rel:12` ms (UI 1..1000 log), `key:0.25`, `sat:0.25`. There is NO amp attack parameter: attack is a fixed 2 ms ramp `e1 = e0 + dt*1000/2` (5621). So generic "attack" has no acid target; generic "decay" is `ampDec` (amp) and `dec` is the filter decay. UI: 11690-11703. |
| 1 curves | `fc = Math.min(sr*0.42, Math.max(25, p.cut * Math.pow(2, p.envAmt*5*this.fe + acc*3.2*this.accC + p.key*(this.lf-7.03))))` (5631): cut in Hz, env amount scaled to 0..5 octaves. Filter: 4-pole ladder, `g = 1 - exp(-2*PI*fc/sr)`, `k = p.res*4.1` feedback (5632); resonance is linear 0..4.1 (self-oscillates at ~1). Filter env: `this.fe *= Math.exp(-dt*1000*4.6/Math.max(10, fd))` with `fd = this.accOn ? Math.min(p.dec,200) : p.dec` (5626-5627); no filter sustain. Amp: stage 2 `e1 = p.sus + (e0-p.sus)*Math.exp(-dt*1000*4.6/Math.max(1,p.ampDec))` (5622, only stage 2, no stage 3); stage 4 `e0*exp(-dt*1000*4.6/max(1,p.rel))` (5623). |
| 2 when read | PER BLOCK (16 samples): all of `cut`, `res`, `envAmt`, `dec`, `ampDec`, `sus`, `rel` read in `block()` (5611-5640). `accent` / `accOn` is latched at note-on (`on(leg, acc)` 5605: `this.accOn = acc ? 1 : 0`) and changes the filter-decay time for that note. |
| 3 how a change arrives | `XPROC` handler, same as bass: `if (d.set) this.c.set(d.set)`; `AcidCore.set` (5601) merges `Object.assign(defaults, this.p||{}, o||{})` and replaces non-finite values by defaults. Page sender `bassPush` with `xParams(V)` (7924: `normX` + tune, drive, level). |
| 4 AudioParams | `rate`, `gate`, `trig`, `leg`, `acc` (all a-rate; `acc` 0..1). `XPROC.process` uses `at(P.rate, s)` once per segment; edges per sample. Hooks `__tcOv` / `__tcPost` identical. Page: `bassNote` `b.acc.setValueAtTime(ev.acc?1:0,t)`. Core: `liveStart` 640 `P('acc').set(ev.acc?1:0,t)` for non-bass non-vox non-ps. |
| 5 voices | Mono, one node per track and pad. |
| 6 rendered twin | `renderX` (7925) `k === 'acid'` -> `renderLiveCore(AcidCore, P, sr, hold \|\| min(V.len\|\|1, 1.2), false)` (5760): same class, same `xParams`, one note rate 1. |

## 4. mach (`MachCore` 5654, `XPROC('mach')`)

| # | Fact |
|---|---|
| 1 params | `MachCore.defaults` (5659): `hp:30` Hz (UI 20..4000), `lp:3500` Hz (UI 60..16000 log; THE cutoff), `res:0.25` (UI 0..1), `fenv:0.35` (UI 0..1; filter env amount), `fdec:350` ms (UI 10..3000), `atk:3` ms (UI 0.5..2000 log), `dec:500` ms (UI 1..4000), `sus:0.7` (UI 0..1), `rel:220` ms (UI 1..4000), plus `a`, `b`, `c` (machine controls), `lfoRate`, `lfoDepth`, `lfoDest` (0 pitch, 1 low-pass, 2 A, 3 level; the engine's own sine LFO). UI: 11705-11716. |
| 1 curves | `lpF = Math.max(30, Math.min(sr*0.45, p.lp * Math.pow(2, p.fenv*6*this.fe + (dst === 1 ? lv0*3 : 0))))`; `q = 0.6 + p.res*p.res*14` (5696); SVF low-pass `gL = gof(lpF)`, `rL = 1/q`; separate high-pass `gH = gof(p.hp)`. So env amount is 0..6 octaves (UI 0..1), res squared. Filter env: `this.fe *= Math.exp(-dt*1000*4.6/Math.max(5,p.fdec))` (5695), no filter sustain. Amp: stage 1 linear attack `Math.max(0.5,p.atk)` (5691), stage 2 `p.sus + (e0-p.sus)*exp(-dt*1000*4.6/max(1,p.dec))` (5692), stage 4 release (5693). |
| 2 when read | PER BLOCK (16 samples), all of them in `block()` (5683-5700). Not latched. |
| 3 how a change arrives | As acid (`MachCore.set` 5660 merges, finite-checks). |
| 4 AudioParams | As acid (`rate`, `gate`, `trig`, `leg`, `acc`; mach ignores `acc`: `c.on(leg, acc)` -> `MachCore.on(leg)` takes one arg). |
| 5 voices | Mono. |
| 6 rendered twin | `renderX` `k === 'mach'` -> `renderLiveCore(MachCore, P, sr, hold \|\| min(V.len\|\|2, 2))` (7929). Same class / params. |

## 5. pump live synths (`pump/synth/pump-synth-worklet.js` `PsynProc`, `pump/synth/pump-synth-engine.js`)

Sound comes from `pump-synth.wasm` (the plugin DSP); its C++ source is not in this repo (`pump/` holds only `pump-meta.js`, `pump.wasm`,
`synth/`, `ui/`), so what the WASM does with a committed value (smoothing, retrigger) is NOT CHECKED.

Common facts (all 13 live types):

| # | Fact |
|---|---|
| 1 param object | `xParams(V)`: keys are the plugin's own symbols from `pump-synth-meta.js` (generated from the plugins' `Params.hpp`), values in the plugin's native units (cutoff in Hz, res and filter_env in %, times in ms, sustain in %). Aliases: `AL = {drive:'o_drive', tune:'o_tune', level:'o_level'}` (engine.js, `al()`). A live twin keeps the same values as its render twin. The node is built with `syms` = the type's symbol list in order (`PsynCore.liveSpec` engine.js 229), `fixed` = values held whatever the pad says (`LIVE_FIXED`: `bend_range:24` for pmonol / ppolyl / pglossl, `bend:12` for pcoill; `FIXED`: `seq_mode:0` x0x, `mode:0` life). |
| 2 when read | NOT per sample. `values(arr)` writes `this.p[sym]` for each symbol into the WASM's stage buffer, then `psyn_commit(h)`: `if (this.dirty){ const v = new Float32Array(X.memory.buffer, X.psyn_values(this.h), this.syms.length); this.values(v); X.psyn_commit(this.h); this.dirty = false; }` (worklet, in `process`, before the lane scan). So a changed value is committed at the start of the next quantum (128 samples); what the WASM does at commit (does the filter use it at once, is a held note's envelope re-evaluated) is not checked. The note's own envelope stage timing lives in the WASM (not checked). |
| 3 how a change arrives | `{set: wholeObject}`: `this.port.onmessage = e => { const d = e.data \|\| {}; if (d.set){ this.p = d.set; this.dirty = true; } ...` (REPLACES `this.p`, no merge). `values()` skips non-finite entries (`if (typeof v === 'number' && isFinite(v)) arr[i] = v`), so symbols absent from a partial object keep their previous staged value; but `semis()` reads `this.p.tune`, and `process` reads `this.p.level` / `this.p.drive` every quantum, so a partial `set` would zero those. No smoothing in the worklet. Page sender: `bassPush` (21477) with `xParams(V)` to each `bassLive` entry of that pad and kind (a psNode's four lanes are all in `bassLive`, so the node gets four identical messages). |
| 4 AudioParams | Per lane j = 0..3: `r<j>` (rate, a-rate), `g<j>` (gate), `t<j>` (note counter), `l<j>` (legato), `v<j>` (velocity 1..127, default 100), all a-rate; plus one `bpm` (k-rate; WOB's wobble tempo, `X.psyn_tempo`). Per-sample edge scan per lane (note-on / off to `X.psyn_midi(h, i, status, ...)` at sample offset `i`: sample-accurate). Pitch between notes is MIDI pitch bend from the last-played lane every 32 samples (`for (let i = bfrom; i < n; i += 32) this.bend(X, i, rr[i], bl.note)`), one bend per instrument. Hooks `__tcOv` / `__tcPost` present (worklet lines in `process`). Core names via `pn(L, lane, k)` (core-processor.js 605) `{rate:'r', gate:'g', trig:'t', leg:'l', vel:'v'}[k] + lane`. Page: `bassNote` uses `b.rate` / `b.gate` ... which are the lane's AudioParams `P('r'+j)` etc. (psNode 20546). |
| 5 voices | ONE node and ONE WASM instance per track and pad; four LANES = four MIDI note slots into that one instance (mono track uses lane 0, poly track up to four, `polyPs` 20573). The plugin's parameters are ONE set per instance: all lanes / chord voices share cutoff, envelopes etc. |
| 6 rendered twin | `PsynCore.render(tw, P, sr, hold, V, bpm)` (engine.js 257): a fresh WASM instance, `st[i] = P[al(sym)]` or the symbol default, note 60 + round(tune), held `hold`, rendered; XENG `r` (engine.js `xeng()` 233) also serves as the live engine's safety-net render (`psFallback` 21423, `aset.psLive === 'render'`). Same param object, same WASM; the render is pitched by `playbackRate`, live by MIDI note + bend. |

Per-type symbols (from `SECS` in engine.js 89- and `pump-synth-meta.js`, ranges `min..max` with the meta's default and scale):

| live key (twin) | cutoff | resonance | filter-env amount | filter-env times | amp env | notes |
|---|---|---|---|---|---|---|
| `pmonol` (mono) | `cutoff` 20..20000 Hz def 800 log | `res` 0..100 % def 20 | `filter_env` -100..100 % def 45 | `f_a` .5..10000 ms def 2, `f_d` 1..10000 def 350, `f_s` 0..100 % def 20, `f_r` 1..15000 def 250 | `a_a` .5..10000 def 2, `a_d` 1..10000 def 600, `a_s` 0..100 % def 85, `a_r` 1..15000 def 150 | also `filter_type`, `filter_mode`, `vel_filter`, `lfo_filter` |
| `ppolyl` (poly) | `cutoff` 20..20000 def 2200 | `res` 0..100 def 15 | `filter_env` -100..100 def 35 | `f_a` def 5, `f_d` 700, `f_s` 30, `f_r` 500 (same ranges) | `a_a` 5, `a_d` 800, `a_s` 80, `a_r` 600 | also `hpf`, `pm_filter`, `lfo_filter`; trim 1.5 |
| `porbitl` (orbit) | `cutoff` 30..18000 def 6000 | `reso` 0..100 def 15 | `env_cut` -100..100 def 0 | MOD env: `mod_attack` .5..5000 def 5, `mod_decay` 1..10000 def 400, `mod_sustain` 0..100 def 0, `mod_release` 1..10000 def 300 | `attack` .5..5000 def 4, `decay` 1..10000 def 600, `sustain` 0..100 def 80, `release` 1..10000 def 300 | `drift_cut` also; bend range fixed +-2 |
| `pglossl` (gloss) | `cutoff` 30..20000 def 6000 | `res` 0..100 def 20 | `filter_env` -100..100 def 30 | `filter_decay` 5..3000 ms def 250 (attack / sustain / release of the filter env: none) | `attack` .5..3000 def 1, `decay` 5..5000 def 400, `sustain` 0..100 def 40, `release` 2..5000 def 120 | `filter_type` |
| `pwobl` (wob) | `cutoff` 20..16000 def 400 | `res` 0..100 def 35 | `filter_env` -4..8 (octaves) def 2 | `filter_decay` 5..5000 def 250 | `attack` .5..5000 def 2, `decay` 1..10000 def 300, `sustain` 0..100 def 100, `release` 1..10000 def 80 | `wob_cut` -6..6 oct (wobble to cutoff); `bpm` AudioParam |
| `pfoldl` (fold) | none (west-coast: low-pass gate: `gate_open` 0..100 def 0, `vactrol` 0..100 def 40, `gate_reso` 0..100 def 0) | `gate_reso` | none | function generator: `rise` .2..5000 ms def 1, `fall` 1..10000 def 150 | none (no ADSR) | not a filter / ADSR engine |
| `pcoill` (coil) | none | none | none | none | springs: `decay` .1..15 s def 2, `release` .02..8 s def .4 | `tone` -100..100, `excite`; no filter / ADSR |
| `ptractl` (tract) | none | none | none | none | `attack` 1..8000 ms def 400, `decay` 1..10000 def 800, `sustain` 0..100 def 100, `release` 1..12000 def 1200 | `bright`, `vowel` etc. are the voice, no filter |
| `pfreezel` (freeze) | none (spectral `low`, `high`, `tilt` params exist; ranges not checked) | none | none | none | `attack` 1..8000 def 300, `decay` 1..10000 def 800, `sustain` 0..100 def 100, `release` 1..12000 def 1500 | |
| `pstrikel`, `pboingl`, `px0xl`, `plifel` | KITS: no single filter or envelope. Per-drum params: `<drum>_decay` (strike / boing: ms, 10..6000 / 10..5000; x0x: 0..100 %; life: `t1..t8_decay` 5..2000 ms), `<drum>_tone` (0..100), strike `<drum>_char_a`, boing `tank_*`, life `fx_filter` -100..100, `fx_decay`; kit-wide `decay` (life, 25..400 % log). The note picks the drum (`whiteKey`, C-4 = first drum; `nb:48`), so a generic target would have to say which drum | |

Curves in the pad: the pad stores plugin units directly (no 0..1 mapping in the page); log scales exist only in the UI row (`rowOf`, engine.js 141: `scale:'log'` -> `c:'log'`), not in the stored value.

## 6. waves live (`wavesl`, `waves/waves-worklet.js` `thunder-wlive`, `wlNode` 20554)

A patch player: the pad's params are only `{bus:1, vel:100}` (7913); the sound is the song's waves patch `WV.doc` (loaded by `{doc}` message,
`wlDoc` 20569). Port handler `if (d.set){ this.p = d.set; this.e.setOnly(this.busOf()); }` (waves-worklet.js 4937). Four lanes with the same
`r/g/t/l/v + bpm` AudioParams as pump, `__tcOv` / `__tcPost` hooks (4944). No filter / envelope params on the pad: any target here would be a
patch parameter, which is waves work (not checked further).

## 7. The tracker commands today

(7) Parse (`makeSeq` 19961+; the page and the core run the SAME `makeSeq`, copied into the core by `tools/make-core.mjs`, core-processor.js line 2):
- Track state set when a row has the command (20198-20202), values are the raw command byte 00..FF:
  `if (fx.CUT != null) T.cut = fx.CUT >= 255 ? null : fx.CUT;`, `if (fx.RES != null) T.res = fx.RES;`, `if (fx.DEC != null) T.dec = fx.DEC >= 255 ? null : fx.DEC;`,
  `if (fx.ATK != null) T.cAtk = ...`, `if (fx.REL != null) T.cRel = ...` (FF = the sound's own). All sticky "from here on". There is NO `SUS` command
  (grep of command names: `CUT RES DEC FDI TRM APN ... ATK REL`, 17254, 24796).
- Scaling helpers: `cutHz = v => 30 * Math.pow(18000/30, clamp(v,0,255)/255)` (10673, 30 Hz..18 kHz exponential);
  `decSec = v => 0.008 * Math.pow(4/0.008, clamp(v,0,254)/254)` (10674, 8 ms..4 s exponential, used by DEC, ATK, REL); RES: `clamp(res,0,255)/255` (a 0..1).
- Event fields (note event, 20279): `tw: pc != null || pdc != null ? {cut:pc, res:prs, dec:pdc} : null`, `...(T.cRel != null ? {rlo: decSec(T.cRel)} : {})`,
  `fdi: fx.FDI != null ? fx.FDI*tick : T.cAtk != null ? decSec(T.cAtk) : 0` (ATK becomes the note's fade-in `fdi`, seconds). `pc` / `pdc` / `prs` come from the track
  command or, when none, from the MOD / Perform override `PF.pno.get(t)` (20261-20262). A `tw` exists only when CUT or DEC is set (RES alone does nothing).
  (Name clash: `ev.cut` / `ev.rel` on a note are KIL / OFF times in seconds, unrelated to CUT / REL.)

(7a) Rendered pads: page `startNote` (20820 `varBuf(ev, ac)`) takes `varBuf(ev, ac)` (`varBuf` 10698: `shapeData` 10675: a 2-pole low-pass at `cutHz(tw.cut)` with `k = 2 - 2*min(res/255*0.97, 0.97)`, and an exponential fade of time constant `decSec(tw.dec)`, cut at 9.3 time constants, on the rendered buffer, cached by `key:c:q:d`).
  Core: `Rd` (core-processor.js 163-190) does the same filter / decay sample by sample as the note reaches them (`mine(ev)` 484-490: `if (ev.vk || ev.tw){ const r = varRec(ev); ...}`;
  reversed + shaped is left to the page). ATK -> `fdi` fade-in gain ramp (page 20837, core 515); REL -> `rlo` = the synth note's release time constant in `kill` (page `relTc` 20376, core `relTc` 449).

(7b) Live pads on the page: `bassNote` (20589) starts with `liveCut(b, t, ev.tw)` (20582):
```
const on = !!(tw && tw.cut != null); let f = b.node.__lp; if (!on && !f) return;
if (!f){ f = ac.createBiquadFilter(); f.type = 'lowpass'; f.frequency.value = 20000; ... b.node.connect(f); f.connect(b.g); b.node.__lp = f; }
f.frequency.cancelScheduledValues(t); f.Q.cancelScheduledValues(t);
f.frequency.setValueAtTime(on ? Math.min(cutHz(tw.cut), ac.sampleRate*0.45) : 20000, t);
f.Q.setValueAtTime(on ? 0.7 + clamp(tw.res|0,0,255)/255*14 : 0.7, t);
```
  So CUT / RES are a biquad low-pass AFTER the engine, per note, stepped at note start only, created the first time a note has CUT, and set back to 20 kHz when a note has no `tw.cut`.
  ATK, REL, DEC are NOT applied to live notes on the page (bassNote ignores `ev.fdi`, `ev.rlo`, `tw.dec`; its `v.rel` is 0; the comment at 20580 says so).

(7c) Live pads when the core plays: `process1` calls `this.mine(ev)` first (false for live pads, 484 `if (!k || k.live) return false`), then `liveNote(ev)` (574) -> `liveStart` (632). `liveStart`
  never reads `ev.tw`, `ev.fdi`, `ev.rlo`, `ev.trm`, `ev.apn`: grep of the core for `tw` finds only `Rd` / `varRec` / `startNote` (lines 165-207, 487, 523). There is no `liveCut` in the core. So with the core playing, CUT / RES / DEC / ATK / REL on a live pad do NOTHING (they only reach the page if `lnode` returns null and the note falls through to `take(ev)`, 575, i.e. the node isn't registered yet).

## 8. MOD targets `cut:t`, `res:t`, `dec:t` today

- Target ids: `tgtList` (19898-19901) offers `cut:t`, `res:t`, `dec:t` as 'CUT · pad filter', 'RES · pad resonance', 'DEC · pad decay'; `tgtOwn` (19889): res 0, cut / dec 255 ("open").
- `tgtEvs(id, v, at, pno)` (19904): `if (g.k === 'cut' || g.k === 'res' || g.k === 'dec'){ const o = pno.get(g.t) || {}; if (own) delete o[g.k]; else o[g.k] = v; pno.set(g.t, o); return []; }` — it returns NO event: it only writes the sequencer's note-override map `PF.pno` (`perfLive.pno` live; a local `Map` in renders).
- LFO sampling: `lfoEvs` (20131-20137): for a filter target `n = 1` sample per step (at the step's start `tt = t0`), value `v = c + sum(lfoAt(sh, phase, cyc) * dp/2)` clamped to 0..255 (`c` = `PF.sent` value else `tgtOwn`), where `PLFO_RATES = [1/8,1/4,1/2,1,2,4,8,16,32]` cycles per beat (19838). Scenes / macros / locks (`perfResolve` 19896, `LK1-4` 20231) go through the same `tgtEvs`.
- Reaching a note: in the note-event builder (20261): `pc = T.cut != null ? T.cut : po && po.cut != null && po.cut < 255 ? po.cut : null` (same for `pdc`; `prs = T.cut != null ? T.res|0 : po && po.res != null ? po.res : T.res|0`), then `tw:{cut:pc, res:prs, dec:pdc}`. Range mapping afterwards is exactly the command's: 00..FF -> `cutHz` / `decSec` / res/255. Only notes that START after the value changed see it; a held note never hears a later LFO value. LFOs run only for tracks with a cell on the row or idle tracks (HANDOVER 716, 735).

## 9. Track -> pad -> live node

- Page: `bassN` Map (20510) keyed `track + ':' + pad + ':' + kind + (j ? ':' + j : '')` (20513), kind = `'vox'` | `V.engine` (`'acid'`, `'mach'`, `'wavesl'`, `'pmonol'`, ...) | `'bass'`. Entry: `{node, g, pn, pad, kind, rate, gate, trig, leg, [acc | syl, same | bpm, wob], n, lastOn, cur}`; pump / waves entry is the lane 0 object with `.lanes[0..3]` and `.node`. All entries are also in the Set `bassLive` (21391: `let bassURL = null; const bassLoad = new WeakMap(), bassOK = new WeakSet(), bassLive = new Set()`) when `live` (the realtime context); `bassPush` and the `liveWarm` / `rt.pl.warm` pre-creation use these. Pad of a track at event time: `ev.pad` (from the note's instrument `I.pad`, or `voiceForTrack(t)` for a voice track, 20277), then `startNote` (20809) -> `bassNode(ev.track, ev.pad)`.
- Core: the nodes register themselves on the audio thread: processorOptions `cid:live ? key : null` (bassNode 20516) and in the constructor `(globalThis.__tcNodes ...).set(__op.cid, this)` where the value is the PROCESSOR object itself (the DSP instance). The core finds it with `lnode(t, pad, j)` (core-processor.js 596): `key = t + ':' + pad + ':' + lk.kind + (j ? ':' + j : '')`, `proc = tcNodes().get(key)`, `lk = this.kv[pad].lk` = `{kind, vox, ps, vel}` sent by the page in `coreKitV` (22097-22100). An offline context (`live` false) has `cid` null: the core never drives it.
- Reaching the DSP state directly: `__tcNodes.get(key)` is the `BassProc` (`.c` is the `BassCore`), `VoxProc` (`.c`), `XP_acid` / `XP_mach` (`.c`), `PsynProc` (`.p`, `.dirty`), so the audio thread can set `proc.c.p.cut = ...` without a message.

## 10. Offline song renders with live engines

- Worklets ARE instantiated in the `OfflineAudioContext`: `if (evs.some(e => e.type === 'note' && isLivePad(e.pad))) await bassReady(oc);` (22715) then `const pl = makePlayer(oc, out, getB, false); ... for (const e of evs) pl.play(e);` (22717, 22731). So `startNote` -> `bassNode` -> `new AudioWorkletNode(oc, 'thunder-bass'...)` and `bassNote` sets the same AudioParam automation (and `liveCut`'s biquad) in the offline graph. Pump / waves live nodes also run live in an export ("An export always plays them live", comment above `psFallback` 21423; `psNode` skips the safety net when `ac !== ctx`).
- The core is not used offline (`cid` null; the core only exists in the realtime path), and `makeSeq` for the render runs with `o.live` false.
- A modulation of a held note today: nothing. CUT / RES via `tw` is a note-start biquad (same as live-on-page); LFO samples are 1 per step for `cut` / `res` / `dec` (2 per step for the other targets in a render, `n = PL ? min(4, tks) : 2`, 20134). There is no per-block path to a live engine's own filter or envelope in a render or live.

## 11. Anything half-built

- Grep for `eng:`, `engTarget`, `liveParam`, `ov.m`, `per-engine map` in `index.html` / `core-processor.js`: NO hits (the `pmod` hits are the unrelated table-synth `pmod` param, 6221 / 11818). Nothing has been started.
- HANDOVER "Feature priority", item 2 (562-570): "Tracker commands that control the live instruments (owner, 8 Nov: 'I would love commands that can control the live instruments'). Today CUT / RES reach live engines only as a low-pass on their output (`liveCut`), and ATK / REL / DEC work on rendered sounds only. Wanted: commands driving each live engine's own filter and envelope (…pump live synths, …waves live, bass, vox); each engine's parameters differ, so it needs a per-engine map." Item 1 (same list): MOD "live engines' own filter and envelope via item 2's per-engine map, renders matching live, smoothing where steps are audible". HANDOVER 735: "engine targets do not exist; `cut` / `res` / `dec` targets only reach notes starting on that row".
- A timed-message mechanism exists but only for FX worklets: core `tcAt(proc, at, msg)` (core-processor.js 212) fills `proc.__q`; the FX worklet drains it at the start of each block into its own `port.onmessage` (index.html 18786: `if (this.__q && this.__q.length){ ... while (q.length && q[0][0] <= due) this.port.onmessage({data:q.shift()[1]}); }`). The live engine templates (BASS_PROC, VOX_PROC, XPROC, PsynProc) do NOT drain `__q` (only one `__q` read in `index.html`).

## Smallest common mechanism the code suggests

What every live node already has:
1. `__tcOv` (core-processor.js 117): for each parameter `k` in the node's declared `parameterDescriptors` (`for (const k in P)`), if the core has an `Auto` timeline in `ov.p`, it replaces `P[k]` with either `[constant]` (when `A.constFrom` is constant: cheap) or a per-sample Float32Array (`A.at(t)`, sample-accurate, supports `set` / `lin` / `target` / `curve`). Parameters the node does not declare are never reached (the loop is over `P`'s keys).
2. On the page, the same names are real AudioParams (`node.parameters.get(k)`), driven by `setValueAtTime` / `linearRampToValueAtTime`, so live-on-page and OfflineAudioContext take the same route, and an a-rate AudioParam arrives as `length 128` or `length 1` in `process`.
3. `{set: obj}` + `bassPush`: a per-pad whole-object message, merged by the three JS cores (so partial objects work there) and replaced (not merged) in `PsynProc` / waves.
So the existing carriers for "a per-block offset on a held note" are AudioParams: add one declared AudioParam per generic target (cutoff, resonance, env amount, attack, decay, sustain, release; k-rate is enough since every engine reads its params once per 16 / 32-sample block or per 128-sample quantum), added to each template's `parameterDescriptors`, and read as `base + offset` where the engine reads `p.cut` etc. Then the page drives them with AudioParam automation and the core drives them through `ov.p` with no new protocol. A k-rate param read as `P.x[0]` gets the block's FIRST sample only when overridden by a non-constant (see `P.wob[0]` in BASS_PROC), which is quantum resolution.

What differs per engine (the part a "per-engine map" must hold):
- Param names: bass `cut/res/fenv/fdec/atk/dec/sus/rel`; mach `lp/res/fenv/fdec/atk/dec/sus/rel`; acid `cut/res/envAmt/dec(filter)/ -/ampDec/sus/rel` (no attack); vox only `bright`, `atk`, `rel`; pump per the table in section 5 (`cutoff|res|reso|filter_env|env_cut|f_*|filter_decay|a_*|attack...`), kits and fold / coil / tract / freeze have no cutoff, waves has none.
- Units / curves: Hz (bass cut, acid cut, mach lp, pump cutoff) vs 0..1 (bright, res); env amount in octaves (bass `fenv` x1, mach `fenv` x6, acid `envAmt` x5, pump `filter_env` in % (wob: octaves)); times in ms everywhere (pump up to 15 s); res is squared in bass / mach, linear in acid (`k = res*4.1`), % in pump.
- Where it is read: JS engines per block (32 bass, 16 vox / acid / mach); pump per quantum via `psyn_commit` (and WASM-internal behaviour unknown). Pump takes a whole-array commit (the stage buffer, `values()`), so a per-block offset there is "recompute the symbols, `psyn_commit`" in the worklet, not an AudioParam read inside a DSP loop.
- Voices: mono JS engines have one param set; vox poly = four nodes (per-node params, pad value pushed to all four); pump = one param set shared by up to four lanes.
- Two-path precision: the AudioParams in JS engines are applied from the first quantum in which they are non-constant at sample precision (`Auto.at(t)`), but the engines then sample them once per block (16 / 32 samples) at most; filter-env `fe` is a state decaying per block that is separate from the base params, so an offset on `cut` / `fenv` composes with it, while an offset on `atk` / `dec` / `sus` / `rel` only affects the stage currently running (section 1 row 2).

## Not checked

- What `pump-synth.wasm` does with a committed parameter on a held note (smoothing, envelope re-evaluation, retrigger); WASM C++ source is not in the repo.
- Ranges of freeze's `low` / `high` / `tilt`, fold's other params, and ORBIT's cutoff `skew:0` meaning in the meta.
- `waves/waves-worklet.js` internals beyond the port handler and parameter hooks; whether patch parameters could be targets.
- `tools/make-core.mjs` copy rules (which `index.html` functions are copied into the core; assumed `makeSeq` and what it reads).
- Core `ov.until` / `__tcOv` release timing in detail, CPU cost of per-sample `Auto.at` for extra a-rate params on 8+ nodes, behaviour on the phone safety net (`psFallback`) for new params.
- Exact UI row min / max for bass `sus`, `glide` etc. beyond those quoted; vox `bright` UI (0..1).
- Offline render parity of the page player vs the core player for live notes (assumed identical AudioParam events; not diffed).
