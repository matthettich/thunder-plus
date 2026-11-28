# Bridge today: every message and call that crosses a boundary (inventory, 10 Nov 2026)

Read-only inventory for the native port (`docs/NATIVE-PORT-STRATEGY.md`, `docs/NATIVE-DESKTOP-PLAN.md` section 5). Facts only: each
row comes from code read for this file. Line numbers are `index.html` lines at build 548 (they drift; search the quoted names).
"not checked" means not looked at, not "absent". Nothing was run; rates marked *derived* are computed from the code, not measured.

Files read: `core/core-processor.js` (all), `core/seq-core.js` (header and `makeSeq` event types only: it is generated), `electron/*.js`
(all), `io/README.md`, `master-worklet.js`, the message code of `chains/chains-worklet.js`, `waves/waves-worklet.js`,
`pump/synth/pump-synth-worklet.js`, the worklet sources embedded in `index.html`, both editor pages (`TW` / embed objects), `sw.js`.

## 0. The boundaries at a glance

```
index.html (page, main thread)
  |-- postMessage ---> core worklet 'thunder-core'  (sequencer + sampler, CORE_V 7)        [section 1]
  |-- postMessage ---> tfx / master / chains / bass / vox / acid / machine / psyn / wlive / rec / io-in / cpu worklets  [section 2]
  |-- AudioParam automation + audio-graph wiring (NOT messages) into the same worklets, and the core's audio outputs into the page's mixer  [1.4, 2.7]
  |-- postMessage ---> render Worker (pad renders) ; pump-timer Worker (25 ms tick)         [section 3]
  |-- window.thunderDesktop (Electron preload, contextBridge) ---> main.js ipcMain ---> link addon, updater, dialogs, fs  [4.1-4.4]
  |-- MessagePort (from main.js) <---> io-helper.js utility process <---> thunder_io.node (miniaudio + libremidi)       [4.5]
  |-- direct JS object access (same origin, no postMessage) <---> iframes chains/index.html, waves/index.html            [section 5]
  `-- browser APIs (Web MIDI, getUserMedia, setSinkId, wake lock, <audio>, File System Access, share, storage, SW)         [section 6]
```

Two facts that shape the bridge design (both verified in code):

1. **There is no SharedArrayBuffer, Atomics or cross-origin isolation anywhere** (`grep` over `index.html`, `core/`, `master-worklet.js`,
   chains / waves / pump worklets: 0 uses; `waves/waves-worklet.js` line 6 only mentions it in a comment). Every crossing is a structured-clone
   `postMessage`, with `ArrayBuffer` transfer only where listed.
2. **The mixer is not behind any message.** Faders, pan, mute, solo, trim, sends, routing and effect-chain wiring are direct calls on the
   page's Web Audio graph (`rt.pl.applyMix()` sets `GainNode.gain` / `StereoPanner.pan` with `setTargetAtTime`; `rt.pl.setFx`, `routeTrack`,
   `busSync`, `pdcSync`). The core worklet only sequences and plays rendered pads; its audio goes into the page's per-track buses. So "mute /
   fader" has no wire format today (section 1.5).

## 1. Page <-> audio-thread sequencer core (`core/core-processor.js`, built into `core/seq-core.js`)

Node: `new AudioWorkletNode(ctx, 'thunder-core', {numberOfInputs:0, numberOfOutputs:NT+4, outputChannelCount:[2 x NT, 1, NT, NT, 1]})`
(index.html 21838). Page side: `corePost(m, transfer)` (21875) and `coreMsg(d)` (22080-22110). Core side: `ThunderCore.msg()` (core-processor.js 227).
Versioning: `CORE_V = 7` in the core (line 27), `CORE_WANT = 7` in the page (index.html 21831); on `ready` a mismatch sets `core.ready=false` and the
page falls back to its own "Page sequencer". `CORE_REV = '1uvz725'` (21830) is a hash of `core/seq-core.js` used only to change the module URL
(`core/seq-core.js?v=BUILD-REV`) so the offline cache cannot serve an old file. `tools/make-core.mjs` copies 79 declarations (makeSeq and
what it reads) out of `index.html` into `core/seq-core.js`, then appends `core-processor.js`; `--check` fails when it is stale.
So **the sequencer's source of truth is still `makeSeq` in index.html (line 19873)**; the core runs the same JS.

### 1.1 Page -> core (`port.postMessage`)

23 `op` names exist in `msg()`. 17 are sent by the page in normal use; 6 are test hooks or have no sender.

| op | payload fields (types as seen) | how often | sender | receiver | notes |
|---|---|---|---|---|---|
| `load` | `S` (the whole song object, structured-cloned), `vars:{vtrk:bool, NSLOT:int, TFX:coreTfx()}` | once per core instance, first sync (`core.mir` empty) | `coreSyncAll` 21930 | `msg` case 'load' | **whole-song snapshot.** `coreTfx()` (33838) = `{[fxType]:{pump:bool, mod:bool, cv:array or null, ps:[{k,min,max}]}}` |
| `patch` | `ops: [ [key, null, value] ... or [key, index, item] or [key, 'len', n] or [key, 'del'] ]` | per PLAY (via `coreSyncAll`) and, while the core plays, at most every 100 ms after an edit (`coreSong`, 21940; timer says 100 ms, a comment says 40 ms) | `coreDiff()` 21917 builds it by JSON-comparing each top-level field and each item of arrays longer than 8 (phrases, chains, song rows, insts...) | case 'patch': applied in place to `S`; any op on key `insts` drops all variation buffers | **small song edit.** "Same paths as undo" claim in the plan is not what this is: it is a key/index diff, not the undo format |
| `vars` | `vars:{vtrk, NSLOT}` (TFX only in `load`) | each PLAY | `coreSyncAll` | case 'vars' | page settings the sequencer reads |
| `kit` | `kv: [ {choke:int, syn:bool, rel:number, live:bool, lk:{kind:'vox' or 'bass' or <engine>, vox:bool, ps:bool, vel:int} or null, sf:bool} or null ] x nslots` | each PLAY; while playing, re-sent when its JSON changes, checked at most every 200 ms (`coreKitCheck` 21878) | `coreKit` 21876 | case 'kit' (`this.kv`, `kit.voices`) | tells the core which pads it may play itself (rendered) vs which are live engines |
| `buf` | `pad:int, data:Float32Array (mono), data2:Float32Array or null (stereo right), sr:int`; or `data:null` to drop | per pad render install (`install()` 8447 -> `coreBuf` 21913), and for all pads in each `coreSyncAll` | `coreBuf` | case 'buf' -> `coreBufs` map | **bulk audio buffer.** A copy (`data.slice()`), the copy's `.buffer` is transferred; the page keeps its own |
| `vbuf` | `k:string ('ii:vk:pad'), pad:int, data:Float32Array, sr:int` | when a MUT / GEN variation render exists and a note asks for it (`coreVar` 21909) | page `coreMsg` rows loop | case 'vbuf' -> `varBufs` (core evicts oldest beyond 32e6 bytes) | bulk audio, transferred copy |
| `sf` | `pad:int, data:{P:{bank,prog,trans,velo,atk,rel,level,len}, font:{samples:[{root,corr,type,link}]}, pr:{zones}, smp:{[k]:{d:Float32Array, sr, ls, le}}}` or `data:null` | per SoundFont pad when its preset is decoded and when its signature changes (`coreSf` 21895) | `coreSf` | case 'sf' -> `sfPads` | bulk audio plus preset tables, transferred copies |
| `play` | `gen:int, mode:'song' or 'phrase' or ..., o:{row, loop, once, only, track, phrase, sect} (only keys not null), t0:number (audio time), rs:{ref,row,step} or undefined (RESUME), watch:bool` | per PLAY | `coreStart` 21942 | `play(d)` 291: builds `makeSeq`, replies `started` | `gen` is a generation counter; stale-gen rows are ignored page-side |
| `hold` | none | per STOP of a song (exact stop at the next pattern edge) | page 14856 | sets `this.hold` | answered by `held` |
| `stop` | `at:number (audio time)`, `rel:true` optional (hand-over: live-engine overrides released at `at`) | per STOP; hand-over to page sequencer; stray-rows cleanup | 14875, 22037, 22078, 22175 | case 'stop' | |
| `loop` | `l:{...} or null` | per loop-range change while playing | `tp.seq.setLoop` 21955 | `seq.setLoop` | **small command** |
| `bpm` | `v:number` | per tempo change while playing (setter on `tp.seq.bpm`, 21956) | page | `seq.bpm = v` | **small command.** Link and MIDI-clock-follow change tempo through the same setter |
| `perf` | `xf:number, mac:[numbers], fill:bool` | on perform-pane change and at PLAY | `perfPush` 19841, `perfFill` 28331, `coreStart` | `perfLive` | live-performance state (crossfade, macros) |
| `lxp` | `st:int` (live transpose) | on change and at PLAY | 15773, `coreSyncAll` | `lxp.st` | |
| `lead` | `s:number` (seconds of look-ahead, 0.05-2) | PLAY start 0.6, then 0.25; hidden page: steps up to 1.2 over ~1 s | `coreLead` 21934 | `this.LEAD` | |
| `ack` | none | **once per `rows` message received** | `coreMsg` 22094 | `this.ackT` | the WATCHDOG heartbeat (phones) |
| `die` | none | "Reset audio engine" | `coreDrop` 21873 | releases overrides, `dead=true` | |
| `song` | `S` | **no sender found** (the comment in core-processor.js says "no longer sent") | - | case 'song' | dead code on the page side |
| `vdrop` | `pad` | **no sender found** in index.html | - | `varDrop` | |
| `test` | `id, mode, max, opt:{row, loop}` | test hook (`tools/seq-golden.mjs --core`; index.html 33861) | test | runs `makeSeq` with a seeded `Math.random`, replies `test` | the golden event-stream mechanism |
| `stats` | `id` | test hook (33878) | test | replies `stats` | |
| `sig` | none | test hook (33872) | test | replies `sig` (`len`, `h`: a hash of JSON.stringify(S)) | |
| `dbg` | `noSampler:bool` | test hook (33876) | test | the page plays every note | |

### 1.2 Core -> page (`port.postMessage` from the audio thread)

| op | payload | how often | sender | receiver | notes |
|---|---|---|---|---|---|
| `ready` | `v:int (CORE_V)` | once, in the constructor | `ThunderCore` ctor | `coreMsg` 22084 | version check |
| `rows` | `gen, rows:[{t:audio time, evs:[event...], pos:[{row,step,...} per track], tks, line, bpm}], time, bpm, tks, base, btT, alive:bool, skips:int` | **one message per audio block in which at least one sequencer step falls inside `time < currentTime + LEAD`, up to 3 steps per message** (`process1` 643). Step = one tracker row. *Derived*: bpm x lpb / 60 rows per second (8/s at 120 BPM, LPB 4), batched into fewer messages when a block has several | `process1` 653 | `coreMsg` 22090-22110: pushes `tp.q` (playhead), plays page-side events, marks `e.core` | **event stream up.** Booked LEAD ahead (0.25 s visible, 0.6 s after PLAY, up to 1.2 s hidden). Each event carries its own audio time `t` |
| `end` | `gen, time` | once when the song runs out | 655 | `s._alive=false` | |
| `held` | `gen, t, pos` | once per exact STOP | 654 | `rcdHeldAt(t, pos)` | answer to `hold` |
| `started` | `gen, late:number (s), ms:int` | once per PLAY | `play()` 304 | `pbLog` only | PLAYBACK REPORT: how late the PLAY reached the audio thread |
| `self` | `gen, why:'deaf' or 'clock <ratio>', t` | when the WATCHDOG lets go | `watchdog` 631 | `coreFail` (page falls back to Page sequencer) | phones only (`watch:true`) |
| `error` | `where:op or '?', message:string` | on a throw in `msg` | ctor `onmessage` try/catch | `tpLog` | |
| `test` / `stats` / `sig` | see 1.1 | on request | | | test hooks. `stats` = `{id,nodes,n,late,worst,voices,lead,skips,lnodes,vs,ovEv,clk,ms,blocks}` |

There is **no meter, playhead, scope, CPU or xrun message from the core.** The playhead is derived page-side from `rows[].t` / `pos` and
`heardLat()`. Meters come from page-side `AnalyserNode`s (12 `createAnalyser` sites; the per-track pair at 20415 is created in a loop over
tracks) and from the master / tfx / chains worklets (section 2). The core's only health signals are `started.late`, `rows.skips`, `self`, `stats`.

Event types inside `rows[].evs` (from `makeSeq` and `ThunderCore.ev`): `note`, `off`, `mod`, `stp`, `tva`, `tvl`, `tpn`, `mvol`, `fxc`,
`fxreset`, `fxp`, `chc`, `tempo`, `beat`, `mpg`, `mpb`, `mat`, `mcc`. A `note` event carries about 40 fields (index.html seq-core.js 533): `t, track,
poly, col, pad, rate, gain, pan, ofs, rev, cut, rel, slide{from,dur}, vk, tw{cut,res,dec}, rlo, slc, fdi, trm, apn, wob, acc, ii, mn, vel, tick, vc`
plus per-note mods. The core marks each event it sounded itself with `core:true` (`rows` goes up with the flag set, so the page does its
bookkeeping but not the sound) and may set `ev.coreLive` (keys of live nodes it drove) and `ev.chnCore` (it already sent the tempo to chains).

### 1.3 What the core handles itself vs sends up (important for what the native core must own)

Sounded in the core (core-processor.js `ev()`): rendered-pad notes (`startNote`: own sampler, stereo, reverse, shaped CUT/RES/DEC via `Rd`,
slide, trem, auto-pan, arp/bend/vib), SoundFont notes (`sfNote`), live-engine notes by driving the engine's AudioParams (`liveNote`/`liveStart`),
`fxc`/`fxreset`/`fxp`/`chc` effect-switch events, `tempo` (to chains), `beat` (the tempo-locked effects' beat clock output), `tva`/`tvl`/`tpn`/`mvol`
(the mix control outputs, below). Sent up for the page to play: stretched (SYNC) notes, MUT/GEN notes whose variation the core does not hold,
non-rendered live engines it cannot find, all MIDI events (`mpg mpb mat mcc` and MIDI notes of insts with `mo===1`), effects whose map is not in `TFX_MAP`,
and every `pump` slot.

### 1.4 Audio crossing the core boundary (not messages)

`NT+4` outputs: outputs `0..NT-1` stereo per track, connected into `rt.pl.buses[t].inp` (`coreWireT` 21858, only for tracks the song uses); output `NT`
mono **beat clock** (a ramping signal, `beat+1` while playing, 0 stopped) connected to `rt.pl.clkBus`; outputs `NT+1` (per-track level multiplier),
`NT+2` (per-track pan offset), `NT+3` (master level offset) are control signals the page plugs into `b.cg.gain`, `b.p.pan` and `pl.out.gain` the first time a
`tva/tvl/tpn/mvol` event for that track arrives (`coreMixT` 21867). STOP unplugs them (`coreWire(false)`).
Plus a **second, in-scope coupling**: the core reaches into other processors in the same `AudioWorkletGlobalScope` through `globalThis.__tcNodes`
(a Map: `'fx:<track>'`, `'chn:<track>'`, `'<track>:<pad>:<kind>[:j]'` -> processor, registered from each node's `processorOptions.cid`), sets `proc.__ov`
(AudioParam override timelines read by `globalThis.__tcOv` / `__tcPost`), and queues timed messages on `proc.__q` (`tcAt`). A native core owns
all of that directly; it has no wire format.

### 1.5 Live changes: how each one travels today

| change | path | crosses a boundary as |
|---|---|---|
| tempo (while playing) | `tp.seq.bpm = v` | `{op:'bpm'}` to the core; separately `bpm` AudioParams on live nodes (`bpm.setValueAtTime`) and `chains-host` `transport` messages |
| loop range | `tp.seq.setLoop` | `{op:'loop'}` |
| mute / solo / fader / pan / trim / sends | `mixChanged()` 21558 -> `rt.pl.applyMix()` 20843 | **direct AudioParam writes in the page graph; no message** |
| song edit while playing | `saveS()` -> `coreSong()` | `{op:'patch'}`, 100 ms debounce |
| note preview, pads, keyboard | `rt.pl.play(e)` (page player) | no message; page voices (AudioBufferSourceNode) and live-engine params |
| effect parameters (UI) | page writes `tfx` chain | `chain` message to the tfx node (2.1) |
| effect parameters (pattern FXV) | `ThunderCore.ev` 'fxp' computes the slot map | `tcAt` timed `{pset}` inside the audio scope |

## 2. Page <-> every other worklet

All nodes are created with `processorOptions` (the first message, carried by the constructor) and then use `port.postMessage`. The wasm bytes
and compiled modules travel as `processorOptions`, once per `AudioContext` (flags `pumpSent`, `psSent`, `wlSent`). File sizes of the wasm on disk:
`chains/dsp/tfx.wasm` 674 KB (same in `waves/dsp/`), `pump/pump.wasm` 1.60 MB, `pump/synth/pump-synth.wasm` 921 KB, `waves/dsp/thunder-dsp.wasm` 836 KB,
`lxr/lxr.wasm` 151 KB.

### 2.1 `thunder-tfx` (track and master effect slots; source embedded in index.html, `ThunderFx` 18505-18795, node made by `tfxNode` 19120)

Node: 3 inputs (track, SOURCE taps, SENDS), 1 output. One node per track that has effects, plus the master (`cid` `'fx:<track>'`, master `'fx:'+MT`).
`processorOptions: {bytes:tfxBin(), chain, irs:irsOpt(ac), tapMap:keyTrackList(), pump:pumpBin() or null, cid}`.
AudioParams (k-rate unless noted): `bp` (a-rate), `sw0..sw7`, `flx`, `gat`, `rtg` (pattern effect switches; written by the page player and by the core's `__ov`).

| direction | message | payload | how often | sender | receiver |
|---|---|---|---|---|---|
| in | (the chain) | array of slot objects `[{slot,type,p:[12 numbers] or mp:{...}, pv, ...}]`, or `null` (dispose) | per effect edit (`setFx`: `b.fx.port.postMessage(chain)` 20916; master 21452) | page | `this.set(e.data)` |
| in | `{pset:{slot, p or mp or pv}}` | one slot's settings | per FXV pattern command, booked at its audio time (`atTime` -> `fxpTo`/`fxpMsg` 20585-20596) or, via the core, `tcAt` | page, or core (in-scope `__q`) | patches the chain copy |
| in | `{irs}` | song's impulse responses (typed arrays) | when IRs change (23175) | page | `globalThis.__thunderIR` |
| in | `{tapmap}` | list of track numbers | when taps change (20314) | page | `setTap` |
| in | `{tel:bool}` | | when FX screen opens/closes (27800) | page | enables `dyn` |
| in | `{lvl:bool}` | | FX bar on/off (27793) | page | enables `lvl` |
| in | `{pumpBytes}` | pump.wasm bytes (1.6 MB, ArrayBuffer, **copied**) | once per context, on `needPump` or when `pumpBin()` becomes ready (16896) | page | `globalThis.__pumpBytes` |
| in | `{ptel:{key,on,back}}` | PUMP telemetry subscribe | on subscribe / unsubscribe | page | `ptel` map |
| in | `{pmidi:[[slot, at(s), status, d1, d2]]}` | MIDI to pump creative effects | per booking pass (20675) | page | `pq` queue |
| in | `{phold:{key,kind,a,b,c,d}}` | | per hold action (18345) | page | `pump_hold` |
| out | `{load:number}` | every 0.5 s of audio time (~2/s per node) | worklet | `n.load` (smoothed 0.6/0.4) |
| out | `{lvl:[8 numbers]}` | every 12 blocks while `lvl` on: ~31/s at 48 kHz (*derived*) | | `n.lvl` |
| out | `{dyn:[[slot,key,[gr,in,out]]...]}`, `{pump:[...]}` | every 12 blocks while `tel` or `ptel` on (~31/s each) | | `fxDynIn`, `PumpFX._feed` |
| out | `{pst:[[slot,key,name,text]]}` | when a pump slot's own state text changes | | `PumpFX._pst` |
| out | `{plat:[[slot,key,latency]]}` | when pump latencies change | | `PumpFX._lat` |
| out | `{needPump:true}` | once | | page replies `{pumpBytes}` |
| out | `{failed:'<slot key>'}` | on a crashed effect (bypassed) | | `toastErr` |

### 2.2 `thunder-master` (`master-worklet.js`; node made at index.html 21399 `mstChain` and 21065 `safeWk`)

`processorOptions:{set, meters:bool}`. In: `{set:{sup:{on,lo,hi,...}, comp:{on,ratio,...}, lim:{on,...}}}` (per master-chain edit, `mstSync` 21427), `{resetPeak:true}` (28245),
`{dispose:true}`. Out: `{meter:{inPk, outPk, tp, inLu, outLu, load, comp, compIn, lim, sup:bands(48) or null}}` every `round(sampleRate/128/20)` blocks
(19 blocks at 48 kHz = about 20/s; `sup` is a 48-number array). Receiver: `rt.mst.wk.port.onmessage` (21422) sets `mstMeter`. The page also has a second
instance `safeWk` (21065, the safety limiter) with its own `processorOptions`.

### 2.3 `thunder-chains` (the ...chains patch graph; `chains/chains-worklet.js` `ThunderChains` 5025-5109; node made by `chainsNode` 18959)

One node per track that runs chains. 6 inputs (track, sidechain bus, SOURCE taps, SENDS, BUS feeds x2), 2 outputs (return, SENDS 8 channels).
`processorOptions:{cid, bytes:chnBin(), pump, track, patch, quality:'full', transport:{tempo,lpb,ticks,playing}, irs, busMap, tapMap}`.
AudioParams `b1..b15` (BYPASS switches, k-rate; driven by the page player or the core `__ov`).
Every message carries `target:'chains-host'`. The page queues messages in `n._q` until the node says `ready`; `n._send` JSON-clones `value`.

| direction | op | `value` | how often | sender | receiver |
|---|---|---|---|---|---|
| in | `patch` | whole patch doc (modules, cables, deck...) | per undo / new / import (`chainsReload`) | page | `loadPatch` |
| in | `add` | `{id,type,params,pos}` (op fields at top level) | per module add (`thunderChains.mod`) | editor frame -> parent | `addModule` |
| in | `remove` / `connect` / `disconnect` | `{id}` / `{a:[id,port], b:[id,port]}` | per edit | same | |
| in | `param` | `{id, name, value}` | **per slider move**, per node (`thunderChains.param` loops all buses) | same | `setParam` |
| in | `params` | `{list:[...]}` | batch | page | `setParams` |
| in | `quality` | `'full' or 'draft'`... | | | |
| in | `transport` | `{tempo, lpb, ticks, playing}` | on tempo change, PLAY (22479), and by the core at audio time via `tcAt` | page / core | `setTransport` |
| in | `telemetry` | `true` (screen showing) or `'load'` or false | on `ready` and screen enter / leave | page | |
| in | `tapmap` / `busmap` | `[track numbers]` | when sidechain / bus wiring changes | page | |
| in | `irs` | impulse responses | 23176 | page | `__thunderIR` |
| in | `pump` | pump.wasm bytes | late arrival (19081, "not _send: it would JSON-copy the bytes") | page | `__pumpBytes` |
| in | `dispose` | | | page | replies `disposed` |
| out | `ready` | `{protocol, sr, block, track, wasm:{tfx}}` | once | worklet | page flushes `n._q` |
| out | `meter` | `e.telemetry()` = `{load, mods, shows, jacks, byp, ...}` | every 12 blocks while telemetry `true` (~31/s **per node**, derived); every 96 blocks (~4/s) with `'load'` (`{load, light:true}`) | | `n.load`, `n.mods`, `n.onMeter` |
| out | `shed` | `{recovered?...}` | on quality shed / recover | | toast |
| out | `resample` | `{L:Float32Array, R:Float32Array, track, ...}` **transferred** (`[v.L.buffer, v.R.buffer]`) | when a chains Resample module finishes | | `resampleIn` |
| out | `error` | `{op, message}` | on throw (process errors every 256 blocks at most) | | `toastErr` |
| out | `disposed` | `{track}` | | | |

The standalone editor engine in the frames uses the same module (`'chains'` / `'waves'` processors, `TARGET`, `PROTOCOL` constants, ops `patch add remove connect
disconnect param params quality transport telemetry`, waves also `noteOn {pitch,vel}`, `noteOff {pitch}`, `panic`; out `ready meter shed error`). `PROTOCOL` value: not checked.

### 2.4 Live engines: `thunder-bass`, `thunder-vox`, `thunder-<acid/machine/...>` (`XPROC`), `thunder-psyn`, `thunder-wlive`

Created per track and pad (`bassNode` ~20429, `psNode` 20446, `wlNode` 20464). Registered in `__tcNodes` by `processorOptions.cid`.
Wired `node -> GainNode -> StereoPanner -> buses[t].inp`.

| node | `processorOptions` | AudioParams | in messages | out messages |
|---|---|---|---|---|
| bass, vox, acid / machine (source in index.html 21186-21291) | `{p: params, cid}` | bass: `rate, gate, trig, leg` (a-rate), `bpm, wob` (k); vox adds `syl, same`; acid/machine adds `acc` | `{set:params}`, `{dispose:true}` | `{load}` every 0.5 s audio time |
| psyn (`pump/synth/pump-synth-worklet.js`) | `{p, cid, ...liveSpec, bytes (once per ctx), file:{d,sr}}` | per lane j = 0..3: `r<j> g<j> t<j> l<j> v<j>` (a-rate), `bpm` (k) | `{set}`, `{file:{d:Float32Array or null, sr}}` (frozen sample), `{dispose}` | `{load}` |
| wlive (`waves/waves-worklet.js` `WavesLiveProc`) | `{p, cid, doc:patch, dspModule, tfxModule}` (compiled `WebAssembly.Module`s, once per ctx) | same lane params as psyn | `{set}`, `{doc}` (whole patch, per waves edit via `wlDoc` 20480), `{dispose}` | `{load}` |

Notes are not messages: they are AudioParam automation (`gate`, `trig` counter, `rate`, `leg`, ...) set by the page player (`bassNote`) or by the core
(`liveStart`, via `__ov`). A native engine needs a note-on / note-off / glide / accent / lane-pick command set; none exists today.

### 2.5 `thunder-rec` (recorder; `RCD_WORKLET` index.html 14713) and `thunder-io-in` (native input playback; `IOIN_WORKLET` 16733)

| node | direction | message | payload | how often |
|---|---|---|---|---|
| rec | in | `'go'` / `'stop'` (strings) | | per take start / stop (`rcdGo` 16826) |
| rec | out | `{pk:[l,r]}` | peak pair | every 6 blocks (~62/s at 48 kHz, derived) |
| rec | out | `{f0:int (currentFrame), ch:[Float32Array, Float32Array], done:bool}` | **transferred** buffers, chunk length `N = sampleRate` frames per channel (1 s: 192 KB per channel at 48 kHz) | once per second while recording, plus final |
| io-in | in | `{D:seconds}` | delay target | at open and when it grows (under-runs) |
| io-in | in | `{rec:bool}`, `{pick:[a,b]}`, `{reset:true}`, `{mreset:true}` | | per take / channel pick |
| io-in | in | `{f0, t, n, nch, data:Float32Array}` | interleaved samples of every input channel; `t` = audio time of the first frame (`p2a(hostNs/1e6 + IOH.clk)`); **transferred** | one per helper chunk (helper polls every 5 ms, up to 8 chunks per tick) |
| io-in | out | `{late, under, jumps, lastJ, A}` | health stats | every 150 blocks (~3/s) |

### 2.6 `thunder-cpu` (CPU probe, only where `ctx.renderCapacity` is missing; `CPU_PROBE` 8321)

Out: `{load:number or null}` about every 500 ms; page stores `cpu.probe`. No input.

### 2.7 Other non-message couplings between page and worklets

- The page's own `AnalyserNode`s read time-domain / frequency data synchronously (17 `getFloatTimeDomainData` / `getByte...` / `getFloatFrequencyData` call sites; not counted by call frequency).
- `ctx.renderCapacity` (`update` events with `averageLoad`, `peakLoad`, `underrunRatio`, every 0.5 s) is read from the browser, not a worklet (8355).
- `globalThis` shared state inside each audio-thread scope: `__tcNodes`, `__tcOv`, `__tcPost`, `__thunderIR`, `__pumpBytes`, `__pumpX`, `__wlMods`.

## 3. Page <-> render Worker (pad renders) and other Workers

Worker built from the page's own `===ENGINE START===` ... `===ENGINE END===` text plus glue, as a blob URL (`rwStart` 8396-8421). Created once; `PW.w`.

| direction | op | payload | how often | sender | receiver | notes |
|---|---|---|---|---|---|---|
| in | `init` | `pl:WebAssembly.Module` (dsp), `tx:Module` (effects), `lx`, `ps` modules or null, `wv:URL string` of the waves engine script | once | `rwStart` | worker glue `onmessage` | compiled modules are structured-cloned (not bytes) |
| in | `irs` | `v:` song impulse responses | at start if the song has IRs; on change | page | `self.__thunderIR` | |
| in | `render` | `id:int, v:voice (the whole pad definition, JSON-able), sr:int, hold:number, stages:bool, bpm:number, waves:patch doc or null` | per pad render (`renderAsync` 8436); one job per pad at a time | `bgRender` | `renderVoice(...)` | |
| out | `ready` | `wv, pl, tx, lx, ps: bool, errs:[string]` | once | worker | `w.onmessage` | |
| out | `done` | `id, data:Float32Array (mono), data2:Float32Array or null, st:[[fx index, peaks]]` | per render | worker | `PW.jobs` resolve -> `install(i, {...})` | `[d.buffer, d2.buffer]` **transferred** |
| out | `fail` | `id, err:string` | | | page falls back to rendering on the main thread (`ensure`) |

- **No cancel message exists.** The only back-pressure is "one job per pad at a time; an edit while it runs re-queues the newest state when it lands"
  (`padRev[]` discards results older than the latest). Worker failure -> `rwDown()` terminates it and rejects pending jobs; renders then run on the main thread.
- Typical sizes: **not measured.** A pad render is a mono Float32Array at `ctx.sampleRate` (4 bytes x frames: one second is 192 KB at 48 kHz); length depends on the pad's
  Max len / sample. The plan's "10-90 ms drum / 0.2-0.4 s synth on a computer" is a code comment (8399), not a measurement here.
- After install, the same buffer crosses a second time to the core as `buf` (a copy, section 1.1). So each pad render is: Worker -> page (transfer), page -> core (copy + transfer).
- **Song renders (WAV / stems / export) do not use the Worker.** `pass()` (22441) builds a whole second player graph in an `OfflineAudioContext(2, N + lat + limiterLat, sr)` on the **main thread**
  (`makePlayer(oc, ...)` with the page sequencer's events), splits progress with `oc.suspend(at)` / `resume()` about 50 slices (`passProg`), and cancels with `rprog.cancelP`
  (a Promise raced against `startRendering()`; the offline graph stays suspended). Output: two `Float32Array`s sliced to length `N` -> `wavN()` (16 or 24 bit PCM in a `Uint8Array`) -> `deliver()` (13546).
  There is no "render.song" message. Pad previews and sound renders use `renderAsync` or `ensure()` on the page.
- **Pump-timer Worker** (`pumpTimer` 21683): blob worker `onmessage = e => { clearInterval(t); if (e.data > 0) t = setInterval(() => postMessage(0), e.data) }`. In: number (25 = start, 0 = stop). Out: `0`, **every 25 ms (40/s)** while the page sequencer pumps. Calls `pump()` on the page. Used for the Page sequencer path (the core path has `corePump` only called from `rows`).

## 4. Page <-> desktop shell (Electron)

`electron/preload.js` exposes `window.thunderDesktop` with `contextBridge.exposeInMainWorld` (contextIsolation true, sandbox true; `backgroundThrottling:false`). The website has no `thunderDesktop`;
the page detects the shell by `window.thunderDesktop` (e.g. `DESK`, `DESK_APP`, `LINKAPI`, `IOAPI`, `deskUpdApi`; `seqHandHere()` returns false when `thunderDesktop.app` exists).
App content is served by `protocol.handle('app', ...)` from `app://thunder/` (standard, secure, fetch, CORS, stream), with a **report-only** CSP on HTML.
Permissions allowed (`ALLOW`): `midi`, `midiSysex`, `fileSystem`, `clipboard-sanitized-write`, `clipboard-read`, `speaker-selection`, and `media` for audio only (never camera).
Command-line switches: `autoplay-policy=no-user-gesture-required`, `disable-renderer-backgrounding`, `disable-background-timer-throttling`, `disable-backgrounding-occluded-windows`.

### 4.1 Files and dialogs (`thunderDesktop.files`)

| call | arguments | returns | ipcMain channel (main.js) | page caller | purpose |
|---|---|---|---|---|---|
| `open()` | none | `{path, name, text}` or `null` or `{error}` | `thunder:file-open` (handle) shows Open dialog, filter `.thunder` / `.json`; 192-197 | 22846 | open a project |
| `read(p)` | path string | `{path,name,text}` or `{error}` | `thunder:file-read`; only `.thunder` / `.json` | 22870, 22901 (recent, opened path) | read a recent / dropped file |
| `save(p, text)` | path, project JSON string | `{ok, path}` or `{error}` | `thunder:file-save`; writes `p + '.saving'` then renames | 22805 | save in place |
| `saveAs(name, text, near)` | suggested name, text, path to start near | `{ok,path}` or `null` | `thunder:file-saveas`; Save dialog, default dir = near's folder else Documents | 22807 | save as |
| `outPick(name, near)` | file name (`wav`, `zip`, `mid` only), near path | path string or `null` | `thunder:out-pick`; Save dialog, remembers last folder, else near, else Music | 13523 | pick where a render goes |
| `outWrite(p, data)` | path, `ArrayBuffer` (the page passes `await blob.arrayBuffer()`) | `{ok,path}` or `{error}` | `thunder:out-write`; only `.wav .zip .mid`; `fs.writeFile` | 13534 | write the render (whole file crosses IPC as one structured clone) |
| `outShow(p)` | path | `true` | `thunder:out-show` -> `shell.showItemInFolder` | 13535 | "Show" button |
| `pathOf(file)` | a dropped `File` | path string or `''` | none (runs in preload: `webUtils.getPathForFile`) | 15633 | drag-drop keeps the path |

Page-side state kept for these: `localStorage['thunderplus-projpath-v1']` (the open file's path, desktop only), `thunderplus-dirty-v1`.

### 4.2 App, window and OS (`thunderDesktop.app`)

| call / event | direction and channel | arguments / payload | purpose | page caller |
|---|---|---|---|---|
| `ready()` | page -> main, `thunder:page-ready` (send) | none | the page can take close-asks and an opened file; main then sends any pending `thunder:open-path` | 22902 |
| `onCloseAsk(cb)` | main -> page, `thunder:close-ask` | none | window close requested; the page answers within 3 s or the window closes anyway | 22900 |
| `closeAck()` | page -> main, `thunder:close-ack` | none | "I heard it" (cancels the 3 s timer) | 22900 |
| `closeReply(ok)` | page -> main, `thunder:close-reply` | bool | true: close (and quit if quitting); false: stay | 22900 |
| `onOpenPath(cb)` | main -> page, `thunder:open-path` | path string | a `.thunder` file double-clicked / second launch / macOS `open-file` | 22901 |
| `naturalScroll()` | page -> main, `thunder:natural-scroll` (handle) | none -> bool | macOS "Natural scrolling" setting (`defaults read -g com.apple.swipescrolldirection`); non-mac returns false | 15464 (on focus too) |
| `playing(on)` | page -> main, `thunder:playing` (send) | bool | App Nap guard: `powerSaveBlocker.start('prevent-app-suspension')` while the song plays | 21676 (from `songBtn`) |
| `reload()` | page -> main, `thunder:reload` (send) | none | reload this window (page `location.reload()` is blocked by `will-navigate`) | 33717 |

Also in main.js and not on `thunderDesktop`: single-instance lock, `second-instance` and `open-file` -> open-path, `will-navigate` blocks all navigation, `setWindowOpenHandler` sends https links to the browser, the window `close` handshake, `powerSaveBlocker`, system menu hidden on Windows/Linux.

### 4.3 Ableton Link (`thunderDesktop.link`; main process `electron/link-session.js` over the optional addon `electron/link/thunder_link.node`)

| call | channel | args -> return | purpose / rate |
|---|---|---|---|
| `available`, `why` | `thunder:link-avail` (**sendSync**, once at preload) | -> `{available, why}` | is the addon loaded |
| `enable(v, bpm)` | `thunder:link-enable` (handle) | bool, number -> bool | join / leave; sets our tempo first; starts a 20 ms timer in main |
| `sync(v)` | `thunder:link-sync` | bool -> bool | start/stop sync |
| `setTempo(bpm)` | `thunder:link-tempo` | number -> bool | on each local tempo change (`lkTempoOut` 16516) |
| `start(at, sync)` | `thunder:link-start` | Link time in microseconds, bool -> µs chosen | start at a Link time (default now + 100 ms) |
| `stop()` | `thunder:link-stop` | -> true | |
| `now()` | `thunder:link-now` | -> Link clock in µs | **page's clock ping: 4 per second while Link is on, plus 5 at enable** (`lkPing` 16463, interval 250 ms); the page keeps up to 240 pings and takes the one with least estimated error |
| `onState(cb)` | main -> page `thunder:link-state` | `{available, enabled, sync, peers, tempo, playing, playAt}` | on any change |
| `onTime(cb)` | main -> page `thunder:link-time` | `{beat, phase, tempo, at (µs, Link clock), hostNow (ms epoch)}` | **every 20 ms (50/s)** while enabled |

The page maps `at` to its audio clock with `lk.off` (performance.now ms minus Link µs / 1000) and `lkAm()` (performance ms minus audio ms). See section 7.

### 4.4 Updater (`thunderDesktop.updates`; `electron/updater.js` `setupUpdates` has no ipcMain handlers of its own; main.js wires them)

| call | channel | returns | purpose |
|---|---|---|---|
| `state()` | `thunder:upd-state` | `{version, avail, ready, notifyOnly, dl (0-1 or null), file}` | current state |
| `check()` | `thunder:upd-check` | bool | check now (`manual` flag: shows a message box with the result) |
| `act()` | `thunder:upd-act` | | restart into the update, or (Mac, portable .exe) download it and open it; also a "Restart now / Later" box and progress on the window |
| `on(cb)` | main -> page `thunder:upd` | state object, sent to every window on change | |

updater.js itself: electron-updater first, GitHub API fallback (`api.github.com/repos/matthettich/thunder-plus/releases`), downloads to Downloads as `name.part` then renames, SHA-256 check when GitHub gives one, checks 8 s after start and every 4 h, `dialog.showMessageBox`, `setProgressBar`. Platform behaviour beyond that: not checked.

### 4.5 Native I/O helper (`thunderDesktop.io`; `electron/io-helper.js` in a `utilityProcess`; addon `thunder_io.node`, API in `io/README.md`)

| call | channel | returns | purpose |
|---|---|---|---|
| `io.available` | constant `true` | | this app has the helper path |
| `io.start()` | `thunder:io-start` (handle) | `{ok}` or `{ok:false, why}` | forks the helper on first call (one restart allowed: `createRestartGate(2)`), makes a `MessageChannelMain`, sends `port1` to the helper (`{op:'port'}`) and `port2` to the page as `thunder:io-port`; preload forwards it as `window.postMessage({thunderIo:true}, '*', ports)` |
| `io.mic()` | `thunder:io-mic` | `{ok}` / `{ok:false, why}` | macOS microphone permission, asked once when an input opens |

Then everything flows over the MessagePort (page `iohAsk` 16578 adds an `id`; replies match on `id`; 5 s timeout, 30 s for `open`):

| page -> helper `op` | fields | reply `op` | notes |
|---|---|---|---|
| `devices` | | `devices {devices:[{id,inId,outId,name,backend,inputs,outputs,rates,isDefaultIn,isDefaultOut}]}` | audio device list |
| `open` | `dev:inId, rate, block` | `opened {ok, rate, channels, deviceRate, block, bufferFrames, latencyFrames, latencyMs, name}` or `{ok:false, why}` | then the helper polls every 5 ms (up to 8 reads per tick) |
| `close` | | `closed` | |
| `now` | | `now {ns}` (OS clock in ns) | 8 round trips at connect (`iohClock`), repeated about every 10-15 s |
| `midiPorts` | | `midiPorts {outs:[{id,name}], ins:[{id,name}]}` | page rescans every 3 s |
| `midiOpenOut` / `midiOpenIn` | `name` | `midiOpened {ok, port}` | |
| `midiSend` | `port, bytes:array, at:ns on the OS clock or 0` | none | **per MIDI message** (notes, CCs, clock): `nmPost` 16679; `at = nmNs(timestamp)` |
| `midiSendMany` | `list:[[port,bytes,at]]` | none | implemented in the helper; **no sender found in the page** |
| `midiClear` | `port` (0 = all) | none | on STOP / panic |
| `midiClose` | `port` | `midiClosed` if `id` given | |
| `midiStats` | | `midiStats {queued, late}` | late = sent more than 2 ms after its time |

| helper -> page `op` | fields | how often |
|---|---|---|
| `hello` | `available, version, midi` or `why` | once per port |
| `in` | `frame0, hostNs, channels, frames, data:Float32Array interleaved, overruns` | per audio chunk, copied by structured clone (a transfer list threw in Electron's `MessagePortMain`; comment: ~5 MB/s for 28 channels at 48 kHz) |
| `midiIn` | `list:[{port, bytes, ns}]` | batch every 2 ms while a MIDI input is open (only when data) |
| `closed` / `error` | `why` | on device loss / error |

The helper exposes input audio and MIDI only. **No audio output goes through it today**; output is still the browser's `AudioContext`.

## 5. Page <-> embedded editors (iframes)

Both editors are same-origin iframes (`<iframe src="waves/index.html">`, `chains/index.html`, `allow="midi; autoplay"` on waves). They talk to the page by **direct property access**, not postMessage:
`window.parent.thunderWaves` / `window.parent.thunderChains` (host object, page -> frame direction is `contentWindow.wavesEmbed` / `chainsEmbed`) and `window.parent.tpLog`.
Each editor also owns an **independent `AudioContext`** (`new AudioContext({latencyHint:'interactive'})`: waves/index.html 1856, chains/index.html 1939) with its own `'waves'` / `'chains'` worklet
and its own Web MIDI use (`requestMIDIAccess`: 2 sites in each editor, not read further). Inside Thunder, the page's own per-track `thunder-chains` nodes make the song's sound; the editor's context is for auditioning while editing.
The page injects a skin stylesheet into the frame's document (`skinFrame` 14245: `contentDocument`).

### 5.1 Frame -> page: `window.thunderWaves` (index.html 16952) and `window.thunderChains` (17014)

| object.method | arguments -> return | purpose |
|---|---|---|
| `thunderWaves.viewKey(code)` / `thunderChains.viewKey(code)` | key code | view-switch keys from inside the editor |
| `.appKey(e)` | `{key, code, shiftKey, ...}` | transport / file / help keys forwarded to the app |
| `thunderWaves.load()` / `thunderChains.load()` | -> deep copy of the patch or `null` | patch lives in the kit (`kit.waves`) / the song (`S.chainPatch`) |
| `.save(doc)` | patch doc | editor autosave hand-off: undo step if changed, `saveS`, push to live engines |
| `.undo(dir)`, `.canUndo()`, `.canRedo()` | | the app's single undo history |
| `thunderWaves.close()` / `thunderChains.close()` | | leave the screen |
| `thunderChains.chainOn()` | -> `[bool x NT]` | which tracks' chains are on |
| `thunderChains.tracks` (getter) | -> NT | track count for pickers |
| `thunderChains.param(id, name, value)` | | one slider: patch update and `chains-host param` message to every track node |
| `thunderChains.mod(op)` | `{op:'add' or 'remove' or 'connect' or 'disconnect', ...}` | one structural edit pushed to every live node |
| `thunderChains.reload(doc)` | patch | whole patch replace (undo / new / import) |
| `thunderChains.deck()` | | bottom-panel controls changed |
| `thunderChains.play()` / `.playing()` | | Space toggles the song from the editor |

### 5.2 Page -> frame: `contentWindow.wavesEmbed` (waves/index.html 2975) and `chainsEmbed` (chains/index.html 3419; `wavesEmbed` is an alias of it in the chains page)

Methods seen: `pause()`, `resume()`, `setDoc(doc)`, `settings()`, `file.{new, open, save, saveAs, undo, redo, canUndo, canRedo, exportCustom, importCustom}`,
`patchSettings()` -> `{quality, tempo, tips, midi, midiDevices}`, `setPatchSetting(key, value)` (`quality`, `tempo`, `tips`, `midi`). chains only: `chainOnChanged()`, `backups()`, `restoreBackup(i)`,
`play()`, `playing()`, `fit()`, `focusTrack(n)`, `setParam(id, key, v)`, `deckSet(list)`, `fmt(id, key, v)`, `reveal(id)`, and
**`meters(payload)`**: `{peaks:[NT], peaksIn, load, mods, shows, jacks, byp}` pushed by the page from `index.html` 31158 at the meter rate while the chains screen is open (the page's UI timer, not measured here).
The waves editor's `wavesEmbed` list was read only to line 3000 of the file; later methods: not checked.

## 6. What the page asks the browser for (what a native shell must replace)

| need | API and where | detail |
|---|---|---|
| MIDI in / out | `navigator.requestMIDIAccess()` (15866, also 16702 to get names); on the desktop app the native helper replaces it (`midiEngine:'native'` default) | out: `output.send(bytes, timestampMs)` with a **future `performance.now()`-based timestamp**; `clear()` is not available in Chrome so the page keeps its own OUTBOX (`MOB_AHEAD 60 ms`, flush every 15 ms) and a hidden-page ledger of sent note-ons so STOP can send note-offs. in: `onmidimessage(e)` using `e.data` and `e.timeStamp` |
| audio input | `navigator.mediaDevices.getUserMedia({audio:{deviceId:{exact}...}})` (14767-14771), `enumerateDevices()` (14723) | recorder input; the desktop app prefers the helper |
| audio output | `new AudioContext({latencyHint, sampleRate})` (8352); `ctx.setSinkId(id)` (8355, 14041); `enumerateDevices()` for `audiooutput` (14025); `getUserMedia({audio:true})` once to get device labels (14049) | latency hint from `aset.lat` (`'balanced'` default on desktop and mobile, `'interactive'` otherwise) |
| wake lock | `navigator.wakeLock.request('screen')` (22389), setting `thunderplus-wakelock-v1` | only while playing, only where the page sequencer is the host (`seqHandHere()`), re-requested on visibility change |
| background play | hidden `<audio>` loop of an inaudible clip (`bgClip()`, 22368) + `navigator.mediaSession` (22355), setting `thunderplus-bgplay-v1` | Android / phones: keeps the browser from killing the tab |
| hidden / visible | `document.visibilitychange` listeners (many: core lead, MIDI outbox flush, wake lock, playback report) | |
| file save | `showSaveFilePicker` (13524, 22815; not on Android); desktop app uses `thunderDesktop.files`; else `<a download>` (13540) | |
| file open | `showOpenFilePicker` (22847; not Android), `<input type=file>`, drag-and-drop (`DESK.pathOf` for the path), PWA `launchQueue.setConsumer` (33788) and manifest `file_handlers` + `share_target` (manifest lines 18 and 35) | |
| share | `navigator.share({files})` (13593, 22693, 22717) when `canShare` | phone-to-desktop song hand-off |
| sample folder | `showDirectoryPicker({id:'thunder-samples', mode:'read'})` (14582); handle kept in IndexedDB `thunderplus-browser` | desktop Chrome only |
| clipboard | `navigator.clipboard` (1 site) | |
| storage | `navigator.storage.persist()` (33714), `estimate()`, `persisted()` (13652) | |
| tabs | `BroadcastChannel('thunderplus-tabs')` (13397): ping / pong peer count | blocks sample cleanup while another tab is open |
| service worker | `sw.js` (cache `thunderplus-v548`): precaches app files, page navigations network-first, others cache-first; **POST `/share-target/`** stores the shared project in cache `thunder-share` (`__thunder-share-project`, header `x-name`) and redirects to `./?shared=1` (page picks it up at 33804) | not used in Electron (app:// scheme); the editors register `./sw.js` only when not framed |
| timers | blob Worker pump tick (25 ms) | section 3 |
| high-res clock | `performance.now()`, `ctx.getOutputTimestamp()`, `ctx.baseLatency`, `ctx.outputLatency`, `ctx.renderCapacity` | section 7 |

### 6.1 Storage that holds user work

`localStorage` (all in try / catch; `storeWrite` shows a "Can't save: storage is full" banner on failure):

| key | holds |
|---|---|
| `thunderplus-song-v1` (`TK`) | the current song, JSON of `songOut(S)` (written by `flushS` / `saveS`). Legacy read: `thunder-song-v1` |
| `thunderplus-kit-v1` (`KEY`) | the current kit: all pad / voice definitions with samples **by reference** (`kitToJSON('ref')`); older kits had audio inline (`"b64"`) and are migrated. Legacy: `thunder-kit-v1` |
| `thunderplus-default-song-v1` | the "default song" new songs start from (`songOut` JSON) |
| `thunderplus-fxuser-v1` | the user's saved effect presets |
| `thunderplus-favs-v1` | favourite presets / items per list |
| `thunderplus-preset-recent-v2` | recently used presets |
| `thunderplus-recent-v1` | recent sample files list (browser) |
| `thunderplus-projpath-v1` | desktop: path of the open project file |
| `thunderplus-dirty-v1` (and `thunderplus-dirty-v1-smp`) | unsaved-changes flags |
| `thunderplus-resume-v1`, `thunderplus-chains-v1`, `thunderplus-arrange-v1` | UI modes (resume point on, chains screen setting, arrange mode): not checked in detail |

Settings objects (JSON): `thunderplus-audio-v1` (`aset`: `sink, lat, rate, vol, psLive, pdc, core, link, linkSync, inEngine, midiEngine`, plus more: not all read), `thunderplus-midi-v1` (`notes, cc, vel, want, outIds, clk, ...`),
`thunderplus-rec-v1` (**see the collision below**), `thunderplus-input-v1`, `thunderplus-export-v1`, `thunderplus-view-v1`, `thunderplus-wakelock-v1`, `thunderplus-bgplay-v1`, `thunderplus-vtrk-v1`
(read inside the core as `localStorage` but stubbed: `const localStorage = {getItem(){return null}}` in `seq-core.js` line 6; the page passes `vtrk` via `vars`).
Other `thunderplus-*` keys are UI preferences (skin, text size, layout, tips seen, ...): about 45 more, **names listed by grep, meaning not checked**.

IndexedDB (database name -> store -> content):

| database | store | key -> value |
|---|---|---|
| `thunderplus-samples` (also reads `thunder-samples`) | `smp` | sample id -> `{sr, name, pcm:ArrayBuffer (Int16), pcm2}` : **every recorded or loaded sample's audio** (16-bit PCM, converted from float) |
| `thunderplus-files` | `kv` | `'cur'` -> the open file's `FileSystemFileHandle` (browser); `'recent'` -> recent files list (with handles); `'bkIndex'` -> `[{k, at, name, kb}]` and `'bk<Date.now()>'` -> full project text: **automatic backups**, 10 kept, one every 3 min while not playing or rendering |
| `thunderplus-fonts` | `fonts` (keyPath `id`) | SoundFont `{id, name, bytes}` |
| `thunderplus-browser` | `smp` | key `'folder'` -> the sample folder's directory handle |

Cache Storage: `thunderplus-v548` (app files), `thunder-share` (a project shared into the PWA, waiting to be opened).
**Project file** (`.thunder`): `projText()` (22708) = `JSON.stringify({app:'thunder-project', v:1, build:BUILD, song:{...songOut(S), presets:fullPresets(), irs:irsFull()}, kit:kitToJSON(false)})`: samples inline, so a `.thunder` file is self-contained; the stored kit / song in `localStorage` are not.

**Finding (needs the owner's eyes):** `RCD_K = 'thunderplus-rec-v1'` (index.html 14709, the recorder's settings `rcdSet`: `input, ch, mon, mode, pats, lat, fit, inCh, nchs`) and `RECK = 'thunderplus-rec-v1'` (22202, the REC / metronome settings `recS`: `met, lvl, cnt, q, vel, offs`) are **the same key**. Each save (`rcdSave`, `recSave`) writes only its own object, so the later write drops the other's fields; each reader only picks its own names, so the other's settings read back as defaults after a save by the other. Not tested, found by reading.

## 7. Timing data: every place a clock crosses a boundary

| clock pair | where | how it is measured / used |
|---|---|---|
| audio time (`ctx.currentTime`, seconds) <-> `performance.now()` (ms) | `amap.off`, `mapUpd()` (15944-15958), `a2p(t)` = `t*1000 + amap.off`, `p2a(p)` | from `ctx.getOutputTimestamp()` `{contextTime, performanceTime}`; falls back to `performance.now()` and `currentTime - heardLat()` when `performanceTime` is 0; never lets the mapped moment be earlier than `baseLatency + outputLatency`; low-pass 0.02 per call, a jump over 50 ms is taken at once. Called from `moClock`, `msend`, the 500 ms input tick, Link |
| audio time -> heard time | `heardLat()` (14004) | `baseLatency + outputLatency`, replaced by `currentTime - (ts.contextTime + (now - ts.performanceTime)/1000)` when that is larger and under 1 s; smoothed 0.85 / 0.15; cached 100 ms. Read by playhead, pad blinks (`ms = e.t - currentTime + heardLat() + moFxLat()`), meters, scope, note preview at 23566. The recorder does **not** use it (calibrated on the old figure) |
| audio time -> MIDI | `msend(bytes, t, early, p)` (15964) | `at = a2p(t + moFxLat()) - (early ? gearDelay(p) : 0)`; sent as `output.send(bytes, max(now, at))`; `moFxLat()` = master chain latency + pump chain latency + safety limiter + `pdcPlan().T`, cached 250 ms. `MOB_AHEAD = 60 ms` outbox |
| page time -> helper OS clock | `IOH.clk` via `iohClock()` (16757): 8 `{op:'now'}` round trips, keeps the one with least RTT, offset `(t0+t1)/2 - ns/1e6`; refreshed about every 10-15 s (and every 20th 500 ms input tick) | `nmNs(ms) = max(0, (ms - IOH.clk) * 1e6)`: MIDI `at` for `midiSend`; incoming `ns` -> `timeStamp = ns/1e6 + IOH.clk` |
| helper input frames -> audio time | `io-in` message `{f0, t, ...}` with `t = p2a(hostNs/1e6 + IOH.clk)`; worklet `A` = anchor, re-anchors if error exceeds 1 ms (20 ms while recording); delay `D = min(0.5, base + output + 0.12)` | recorder alignment; `jumps`, `under` reported back |
| MIDI clock in | `clkIn(st, ts)` (16375): `ts` = `MIDIMessageEvent.timeStamp` (or helper ns converted); `ta = p2a(ts + clkLat())` | phase lock to the tempo and beat |
| MIDI clock out | `moClock(s)` (16239): ticks booked at most about 80 ms ahead, `msendClk([0xF8], at)` | 24 ppqn; ticks after STOP avoided by not booking far ahead |
| Link clock <-> audio | `lk.off` (page: performance ms minus Link µs / 1000, from `lkPing`), `lk.am` / `lkAm()` (performance ms minus audio ms), `lkA(us)`, `lkUs(t)`, `lkBeatAt`, `lkUsAt` | main process sends `at` (Link's own µs clock) and `hostNow` (ms epoch, "for checking the IPC delay") every 20 ms; the page says "the smallest offset of the last second = least IPC delay" |
| audio thread -> page | `rows[].t` (audio seconds, same clock as `ctx.currentTime`); `started.late` = `at0 - d.t0` (how late PLAY reached the thread); watchdog compares `currentTime` against `Date.now()` | no mapping needed: both are `currentTime` |
| output device | `ctx.sampleRate` vs `aset.rate`; `getOutputTimestamp`; `renderCapacity` events | |

PLAYBACK REPORT (`PB`, `pbLog`, `pbCopy` 22066): a text log (max 400 lines) the owner pastes into chat. Header: `build, userAgent, latency "<base> + <output> ms · heard <ms> ms", worklets used`. `pbState()` per line:
`ctx <state> t=<currentTime> on= core= seq=<aset.core> hidden= wall=<Date.now()/1000 % 100000>`. `pbSum()` every 2 s while rows arrive: `rows <n> · latest note <ms> ms late · dropped <n> · ahead <ms> ms · core skips <n> · voices <n>`. Other lines: `core PLAY: reached the audio thread <ms> ms before its start, took <ms> ms`,
`STOP`, `1 s / 3 s after STOP: level ... voices ... stray rows ...`, page-sequencer tick gaps (`pbPg`: ticks, longest gap, least lead, moved-on count, held count), visibility changes, engine state changes. The clock check `pbClock` (every 2 s while playing, phones only) compares audio time to wall time and is the "1.00 s in 1.00 s" figure the strategy refers to.

## Shapes that repeat

1. **Whole-song snapshot + diff.** `load {S, vars}` then `patch {ops}` (key / index / len / del), plus `chains` `patch` (whole patch doc) and `wlive` `{doc}` (whole waves patch). One JSON tree is mirrored on the other side; the page keeps a JSON-string mirror (`core.mir`) to compute the diff.
2. **Small command.** `bpm {v}`, `loop {l}`, `stop {at}`, `lead {s}`, `lxp`, `perf`, `hold`, `ack`; chains `param` / `transport`; tfx `{pset}`; master `{set}`; Link `setTempo`. Mostly carry an audio time or a single number.
3. **Event stream up with future timestamps.** Core `rows` (booked 0.25-1.2 s ahead, each event carries its audio time `t`), helper `midiIn` (OS ns), Link `link-time` (µs), rec `pk`, tfx `lvl` / `dyn`, chains `meter`, master `meter`. The consumer maps the timestamp to heard time itself.
4. **Bulk audio buffer, transferred.** Worker `done` (Float32Array), page -> core `buf` / `vbuf` / `sf` (a copy made first, then transferred), rec chunks, io-in chunks, chains `resample`. **Not transferred:** helper -> page `in` chunks (structured-clone copy, Electron `MessagePortMain` cannot transfer ArrayBuffers), `outWrite` (whole WAV as one IPC clone), wasm bytes as `processorOptions` / `pumpBytes` (copied), compiled `WebAssembly.Module`s (cloned).
5. **Request / reply with ids.** Only the helper port (`iohAsk`, 5 s / 30 s timeouts) and Electron `invoke` handlers. The render Worker uses ids (`id` in `render` / `done` / `fail`) but no timeouts and no cancel.
6. **Timed command inside the audio scope.** `tcAt` / `__q`, `__ov` AudioParam overrides, `__tcNodes`: the core drives other processors by direct object access. This is the shape a native engine replaces with plain function calls; it has no wire format to port.
7. **Direct JS objects (no wire).** Editors <-> page (`thunderWaves`, `thunderChains`, `wavesEmbed`, `chainsEmbed`), mixer (`rt.pl.*`), AnalyserNode reads, page voices.
8. **Page-side hosting of what the core cannot do.** Anything marked `core:true` was played in the core; anything else in `rows[].evs` is played by the page's player in the same pass. The bridge is therefore a split of one event stream, not a single owner.

Counts of distinct messages per boundary: core in 23 (17 used in production), core out 10 (7 in production); tfx in 10 shapes (+ the chain array / null) and out 8; master in 3, out 1; chains in 15 ops, out 6;
live engines in 4, out 1; rec in 2, out 2; io-in in 6, out 1; cpu out 1; render Worker in 3, out 3; pump-timer Worker in 1, out 1; Electron: 31 channels (19 `handle`, 6 `on` / `send`, 1 `sendSync`, 6 main -> page sends:
`close-ask, open-path, link-state, link-time, upd, io-port`; the `io-port` carries a MessagePort), helper port 12 in / 12 out (one input op, `midiSendMany`, has no page sender); editors 8 + 16 host methods and about 12 + 25 embed methods.

## Volume: the busiest messages while playing (*derived* from code unless marked)

| message | rate | size |
|---|---|---|
| `thunderDesktop.link.onTime` (`link-time`) | 50/s, only with Link on | tiny |
| tfx `lvl`, `dyn`, `pump` | ~31/s per tfx node, only while the FX bar / FX screen shows | `lvl` 8 numbers; `dyn` small arrays |
| chains `meter` | ~31/s **per chains node** while the chains screen shows, ~4/s otherwise | `mods`, `shows`, `jacks`, `byp`: size not measured |
| rec `pk` | ~62/s while a recorder is open | 2 numbers |
| pump-timer Worker tick | 40/s while the Page sequencer runs | none |
| master `meter` | ~20/s | ~15 numbers + `sup` 48 numbers |
| helper `in` (native input open) | about 200/s if the helper's 5 ms poll always finds data (derived from POLL_MS = 5); bytes ~5 MB/s for 28 channels at 48 kHz (code comment) | copied, not transferred |
| helper `midiIn` | up to 500/s polls, only sends when data | small |
| helper `midiSend` | one per MIDI message (clock out = 24 per beat, ~48/s at 120 BPM per port, plus notes) | tiny |
| Link `now()` ping | 4/s | tiny |
| core `rows` | one per row (8/s at 120 BPM LPB 4, batched up to 3 steps a block), each with the events of that row (events can have 40 fields) | not measured; whole rows resent every step |
| core `ack` | same as `rows` | none |
| core `patch` | while playing and editing, up to 10/s (100 ms debounce) | key / index diffs: sized by the edited items; `songOut` JSON of a whole song is "hundreds of KB" per a code comment (the SONG SYNC comment above `coreDiff`, near 21915) |
| live engines `{load}` | ~2/s per node | 1 number |
| io-in stats, cpu `load` | 3/s, 2/s | tiny |
| `kit` | at most 5/s check, only sent when changed | array of per-pad small objects |

Largest one-off crossings: `load` (whole song), `buf` per pad (4 bytes x frames), `sf` (decoded SoundFont samples), `pumpBytes` (1.6 MB), wasm / compiled modules at node creation, `outWrite` (a full WAV), recorder chunks (192 KB per channel per second).

## Not checked / uncertain

- Whether the 12 `createAnalyser` sites (one pair per track inside a loop at 20415) really amount to the strategy's "72 analysers"; per-track count at runtime not measured.
- Real message rates and sizes: every rate above is derived from loop counters and constants, none measured. Row-message and `patch` sizes not measured. Pad render sizes not measured.
- `PROTOCOL` constant of the chains / waves worklets and `chains-worklet.js` `telemetry()` shape: not read beyond the keys named above.
- The editors' own Web MIDI use and their own `AudioContext` graphs; the rest of `wavesEmbed` after line 3000 of `waves/index.html`.
- The ENGINE block's other functions the page calls on rendered voices (`renderVoice`, `ensure*`) are in-process, not a boundary today; not catalogued.
- `tfx` effect-slot object shape (`p`, `mp`, `pv`, `pump` fields) beyond what `fxpMsg` / `fxvMsg` produce; the chain array's exact slot fields.
- `XPROC('acid', ...)` / `'machine'` / other kinds: the exact AudioParam list per kind beyond `rate gate trig leg acc` (read 21186-21291 only partly).
- `thunderplus-*` localStorage keys other than the ones described (about 45 UI-preference keys), and what `thunderplus-chains-v1` / `-arrange-v1` / `-resume-v1` hold beyond the reads shown.
- Link addon (`link/`, `thunder_link.node`) native API, and `io/src` C++ (`thunder_io.node`) beyond `io/README.md`.
- `beta/` and `stable/` directories at the repo root contain their own copies of `index.html`, `master-worklet.js`, `sw.js` and subfolders; not compared with the root `index.html` (this inventory is of the root copy; whether `beta` or the root is the working copy per `CLAUDE.md` was not checked).
- `tests/` (including `tests/core.test.js`, `tests/io.test.js`) and `tools/seq-golden.mjs` were not read; only referenced from comments.
- Platform behaviour of the updater (Windows / Linux auto-update path through electron-updater) beyond the code comments.
- `docs/NATIVE-IO-HELPER-PLAN.md` was not read (the code and `io/README.md` were used instead).
