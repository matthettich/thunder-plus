# N2-PAGE-CLIENT: the page's client of `window.thunderCore` (design, nothing built)

Written 24 Nov 2026 overnight by a Sonnet design agent (owner asleep: every open choice is made and marked **CHOICE**; the owner's questions are in section 8).
READ-ONLY on the app: this file is the only thing added.

**What was read.**
- The web app on `beta` at **524948d8** (build 676, "Resize during playback", #309). Line numbers written `W:N` are `index.html` of that commit (39207 lines).
  `sw.js` and `tests/` are cited as `beta:sw.js`, `beta:tests/...`.
- `native` at **980a5e42** (C2a, #310): `docs/port/CORE-BRIDGE.md` (`CB:N`), `BRIDGE-TODAY.md` (`BT:N`), `WORKPLAN.md` (`WP:N`), `U37-DESIGN-DRAFT.md` (`U37:N`), `C2-DESIGN.md`,
  `native/core/include/thunder/live.h` (`live.h:N`), `native/android/**`.
- `docs/port/N2-PLAN.md` on `origin/n1/n2-live-shell` (`N2P:`; section numbers, no line numbers) and `live.h` of `origin/n1/c2-live` (6ff23811) for `thunder_live_chain_edit` / `CAP_CHAINS`,
  which are not on `native` yet.
- Not run: nothing here was executed; no device. Estimates are GUESSES and say so.

## 0. The answer in twelve lines

1. **The seam already exists and is small.** Every sound-making path of the page's *song* goes through ten or so functions that already begin `if (!rt) return;` (`rtFx` W:24822, `rtRoute` W:24805, `mstSync` W:24789,
   `mixChanged` W:24934, `chainsPush` W:24902, `chainsTempo` W:24929, `moApply` W:24820 ...) plus the six "core*" functions of today's JS core (`coreStart` W:25460, `coreBuf` W:25423, `coreKit` W:25386, `coreSong` W:25449,
   `coreSyncAll` W:25439, `coreMsg` W:25649). In native mode `rt` is `null` (no Web Audio graph exists), so every one of the first group is already a no-op: the client adds its branch above the guard.
2. **One new file, loaded only when wanted:** `thunder-core-client.js` at the repo root (so the Android build's `*.js` copy rule already ships it: `native/android/app/build.gradle.kts:49-50`).
   `index.html` gets a loader of about 12 lines and about 25 one-line branches (section 4.3). With no `window.thunderCore` and the switch off, the file is never fetched.
3. **Edits are sent two ways.** (a) *Small messages at once* for things the contract has a message for and the shell can apply live: `mix.set`, `bpm`, `loop`, `perf`, `fx` parameter sets, master config, chains parameters.
   (b) *The whole song, debounced 250 ms,* for everything else: `thunder_live_song_publish` takes only whole text (`live.h:262-273`, ruling D3), so **`song.patch` is never sent**. A state-diff in the client, driven from the one existing
   hook `saveS()` (W:22290), decides which small messages an edit needs.
4. **Pads:** `install()` (W:8922, the one place a pad render lands) already calls `coreBuf(i)`; the client takes over `coreBuf`, sends each pad with `put` + `buf.use`, and one `pads.commit`. The pad rate is the engine's rate from `hello`, not an `AudioContext`.
5. **Fallback is decided before the first PLAY of a song** by the shell's verdict (reply to `song.load`) plus three page-only conditions; the page then runs today's code unchanged, with a toast. A failure *during* play stops and tells (U37 D14).
6. **The page's `AudioContext` is not created for a native PLAY.** It is still created, lazily, by hand-played sound (keys, pad taps, previews: `audio()` has 28 call sites) because the core has no `note.on` (live.h:78, D16).
   That is a hole in the plan, listed as gap G10, not hidden.
7. **Eleven gaps in the contract (section 3), all additive: BRIDGE_V stays 1.** None needs a new version.
8. **Meters, playhead, CPU read the core's events** through a small "view" object that replaces the Web Audio analysers; what the core cannot report (per-slot FX meters, chains telemetry, master reduction meters, CPU load) shows as empty, listed as gaps G6 / G7.
9. **Service worker:** one file added to `FILES` of `beta:sw.js:4` only when the client lands on `beta`; on `native` only the Android asset copy matters (section 4.9).
10. **Inert with the switch off** is proven by a new test file, `tests/native-client-inert.test.js`, that loads `index.html` into the existing `vm` harness with no `thunderCore`, checks every branch predicate is `false`, no new global side effect runs at load, and a source guard counts the branches (section 5).
11. **Seven PRs, about 55 builder-hours (guess)**, plus the shell-side work the gaps need (H1b, listed, not counted).
12. **Five questions for the owner**, plain words, section 8.

## 1. Where the client lives, how it is loaded, when it is on

### 1.1 The file

`thunder-core-client.js`, **repo root**, one classic script (an IIFE that defines `window.__thunderClient`, no `import`).
**CHOICE: root, not `core/` or a new folder.** Reasons: `core/seq-core.js` is generated from `index.html` by `tools/make-core.mjs` and `beta:tests/core.test.js:10-12` fails if it is stale (a hand-written
file in `core/` invites confusion); the Android asset copy takes `index.html`, `*.js` at the root and a fixed folder list (`native/android/app/build.gradle.kts:49-50`), so a file at the root ships with no gradle change, a new folder would not.
It must NOT be listed under `native/` (the beta publish script copies `beta`'s tree only: `U37:12`, finding 5).
Size target: under 900 lines. It reads the page only through a small object the page hands it (section 1.3), never by `eval` and never by touching `S` except to read it.

### 1.2 When it is fetched

```
at the end of index.html's start-up (after `aset`, `S`, `kit` exist):
  if (window.thunderCore || aset.core === 'native') loadClient();        // else: nothing, no fetch, no global
loadClient(): <script src="thunder-core-client.js?v=BUILD-REV"> appended; onload -> window.__thunderClient.attach(HOST_API)
```
- The Android shell injects `window.thunderCore` at document start (`native/android/app/src/main/res/raw/thunder_bridge.js:3,71`; `Bridge.kt` per N2P section 1), so on Android the client always loads.
- The web copy has no `window.thunderCore` until core.wasm exists (WP:160, B3 / U37 P6..P10). There the client loads only when `aset.core === 'native'` (the owner's switch, default off) and then *creates* `window.thunderCore` from the worklet (U37 section 2.3, P8). That part is outside this document.
- A load error (404, offline cache miss) = the client does not exist = `NC.on` stays `false` = the page behaves as today. One `tpLog` line.

### 1.3 The switch (Settings > Audio)

Today: the segment `segCore` has two values, `core` ("Audio thread") and `page` ("Page (old)"), saved in `aset.core` (W:15961; `coreWanted` W:25338; default `'core'`, `aset` defaults W:8717).
**CHOICE (U37 D4, section 2.4): a third value `'native'`, label "Native core (test)"**, never a default on the web.
- On the web copy: default stays `'core'`; `'native'` only if the owner picks it. A saved `'native'` with no `WebAssembly` / `AudioWorklet`, or after a load failure, is treated as off with a one-line reason under the segment.
- On Android: the shell's presence is the choice. **CHOICE:** when `thunderCore.caps.nativeAudio` is true (reply of `hello`, CB:50) the segment is shown with `'native'` selected and a fourth value, "Web audio (old)", lets the owner force the old engine for a bad song. This is `aset.core = 'page'` (the existing value; the client treats `'page'` and `'core'` the same: not native).
- Takes effect from the next PLAY, as `updCore` already says (W:15961-15962). No mid-song hand-over between engines (U37 section 2.4; the page's own `seqHandover` W:25574 shows the cost).

### 1.4 What the page hands the client (`HOST_API`)

A plain object so the client never reaches into page internals:
`{S: () => S, kit: () => kit, nslots, songVer: () => songVer, cache: (i) => cache[i] /* {data, data2, sr} */, toast, toastErr, tpLog, pbLog, setPlaying(on, mode, firstT0), pushPlayhead(entry), setMeters(view), setCpu(v), aset, now: performance.now}`.
It is the only coupling; the web-copy client and the Android client are the same file.

## 2. Inventory: every place the page makes sound or talks to an audio engine (W: = `beta` 524948d8)

Legend. **msg** = the CORE-BRIDGE message that replaces it (CB:N); **GAP Gn** = no message yet (section 3); **stays** = not an engine concern (kept as is, or out of N2).
"Seam" = the page function that gets a branch (section 4.3).

### 2.1 Song and sequencer

| Today | W: | Replaced by |
|---|---|---|
| PLAY: `tStart` builds the page's own sequencer (`makeSeq`), books notes with `ctx.currentTime`, starts the pump timer | W:24946-25060 | `song.load` (if dirty) then `play {mode,row,loop,only,track,phrase,sect,t0}` (CB:53; N2P section 2: `thunder_live_play`, `t0` is a host frame). Seam: top of `tStart`. |
| the page sequencer's tick `pump` / `pumpRun`, the 25 ms Worker timer | W:25196, W:25284, W:25065-25072 | not started in native mode (the core sequences) |
| JS audio-thread core: `coreLoad`, `coreStart`, `coreSong`, `coreSyncAll`, ops `load patch vars kit buf vbuf sf play hold stop loop bpm perf lxp lead ack` | W:25343-25480 (BT:49-73) | `song.load`, `kit.load`, `put`+`buf.use`, `play`, `hold`, `stop`, `loop`, `bpm`, `perf`, `lead`; `lxp` and `ack` see below. `song.patch` unused (whole text only). |
| STOP: `tStop` (resume point, stop cursor, voices, MIDI stop) | W:25725-25760 | `stop {at?}`; the page-side bookkeeping stays (resume point, cursor), the sound part is the message. HOLD (exact stop at the pattern edge) = `hold`. |
| tempo while playing: `tp.seq.bpm = v` (5 sites, Link and MIDI clock too) | W:18384, 18430, 18480, 18502, 28469, 29904 | `bpm {v}`; the client installs the same `tp.seq` stand-in object `coreStart` makes (W:25469-25474), so these sites do not change |
| loop change while playing: `tp.seq.setLoop` | W:25472 | `loop` |
| perform pane (crossfade, macros, fill) | W:22627 `perfPush`, W:32961 `perfFill`, W:25462 | `perf` |
| LXR live transpose `lxp` | W:25439 (`coreSyncAll`), W:15773 | **GAP G11** (no call: `live.h:78`): a song that uses `lxp` falls back |
| look-ahead `lead` (screen off grows to 1.2 s) | W:25441-25447 | `lead`. The shell may ignore it (the host has its own); client sends it at PLAY and on `visibilitychange` as today |
| watchdog heartbeat `ack` (phones) | W:25666 | not needed: no page-to-core round trip is on the audio path; the shell's own `err {fatal}` replaces `self` (W:25646) |
| RESUME (carry on from the step it stopped on), HAND-OVER between sequencers | W:25000-25010, W:25574 | **GAP G12** (`thunder_live_play_opts` has `row`, no step: `live.h:352-368`). **CHOICE:** native PLAY restarts the row (step 0); the toast "Resume plays from the start of the row" once per session. Hand-over does not exist in native mode. |
| REC count-in and pattern takes, Ableton Link start, MIDI-clock follow | W:25011-25020 (`ci`, `lkp`), W:18346 (`clkIn`) | not live-engine features (`live.h:78`): **fallback conditions** (section 4.6) |
| CHASE (effect commands before the start row) | W:25057 | inside the core's `play` (`row` below the top: `live.h:371` "CHASE") |
| Random seed for chance commands | `Math.random` in `makeSeq` | `play` carries `random_seed` (`live.h:359`): the client sends a `Date.now()`-derived value (U37 D17) |

### 2.2 Pads and voice renders

| Today | W: | Replaced by |
|---|---|---|
| pad render in the Worker (`renderAsync` -> `PW.w`), main-thread fallback (`ensure`), `install(i, c)` | W:8893, W:8923 (`bgRender`), W:8987 (`prerender`), W:8922 `install`, W:8836-8843 | stays on the page (the core cannot render instruments the page can: N2P section 3). The result goes to the core: `put` + `buf.use {kind:'pad'}` |
| a render handed to the JS core: `coreBuf(i)` (copy + transfer, `buf`) | W:25423; called from `install` (W:8922) | `put(id, {sr,ch,frames}, [L,R])` then `buf.use`; **GAP G2** (the N0 shell's `put` takes one `Float32Array`: `thunder_bridge.js:36`; the contract's signature is CB:31) and **GAP G3** (one `pads.commit`) |
| kit voices list `{choke,syn,rel,live,lk,sf}` (`coreKitV`, `coreKit`, re-sent when its JSON changes, 200 ms) | W:25386-25399 | `kit.load {kv, rev}` (CB:53); shell maps to `thunder_live_pad_kit` / `pad_route` |
| MUT / GEN variations: `varPrefetch`, `varWant`, `coreVar` (`vbuf`) | W:11949-11975, W:25419 | `put` + `buf.use {kind:'var'}` with `ii` and `vk`: **GAP G4** (CB:54 has `pad, layer, kind` only; `thunder_live_var_set` needs `ii, kind, num, pad`: `live.h:308`). Variations a song names only through RND / RNL are not prefetched: counted by the core (`notes_var_missing`, N2P section 3) |
| chop data (`loopSecs`, `slicePoints`, `findHits`, `applyChop`) | W:24071, 24090, 24079, 24136 | **GAP G4** `pad.chop {pad, loopSecs, slices:[...]}` -> `thunder_live_pad_chop` / `pad_hits` (`live.h:313-314`) |
| SoundFont pads (`coreSf`) | W:25406 | `buf.use kind:'sf'` exists in CB:54 but `live.h` has `CAP_SF` unset (`live.h:137`): **fallback reason** until built |
| rendered twin of live engines (bass, vox, acid, mach, pump synths, waves) | `isLivePad` W:24751; `kit` `lk` W:25392 | the page renders them as pads like any other (`ensureLive` W:9006); with `CAP_ENGINES` unset the host plays the rendered twin on the sampler route (`live.h:76`, D8). One toast per PLAY naming the pads (U37 D8) |
| tempo-locked pad renders (`lnTempo`, `padBpm`) | W:8903-8921 | page side, unchanged; a new render is a new `put` |
| SampleRate used for renders: `padSR()` = `ctx.sampleRate`, else `srGuess` (a throw-away `AudioContext` made at load) | W:8901, W:8758-8765 | **GAP G1** (`hello` reply has no sample rate): `padSR()` returns the engine's rate in native mode; the `srGuess` block is skipped |

### 2.3 Mixer

| Today | W: | Replaced by |
|---|---|---|
| `rt.pl.applyMix` writes GainNode / StereoPanner for vol, pan, trim, mute, solo, PRE send (`setTargetAtTime` 12 ms) | W:23866-23880; called from W:24934 `mixChanged` and 7 direct sites | `mix.set {tr, k:'vol'|'trim'|'pan'|'mute'|'solo', v, ramp}` (CB:58; `live.h:408-423`, values 0..255, pan 128 = centre, mute / solo 0 or 1; `ramp < 0` = the page's 12 ms law). Seam: client's state-diff on `saveS` (so the 8 call sites do not change) |
| mixer SEND (`k:'send'`) | in the page's chains patch (Track In bus send) | none in `live.h:416` (key 5 is E_ARG): the client never sends it; a send change is a song publish |
| mute crossfade, TVL / TVA / TPN pattern commands (`coreMixT`) | W:25367 | inside the core (the Seq reads `S.mix`, `live.h:420`) |
| master volume `S.master`, OUT fader `S.mout` (`moApply`) | W:24820, `rtRoute` W:24809 | `mix.set {tr:32, k:'vol'|'out'}` (`live.h:409-414`) |
| latency compensation mode (`S.pdc`, `pdcSync`) | W:5 (`rt.pl.pdcSync` 5 sites), `pdcPlan` W:21628 | `pdc.set {mode}`: **GAP G8** (`thunder_live_pdc_set`, `live.h:429`; no bridge message) |

### 2.4 Track effects, master, chains

| Today | W: | Replaced by |
|---|---|---|
| a track's effect list: `rtFx(t)` -> `tfxChain(S.tfx[t])` -> `setFx` / `tfxNode` port | W:24822, W:20771, W:23882, W:21644 | structural change: **GAP G5a** `fx.plan {tr, slots, bpm}` -> `thunder_live_fx_plan` + `fx_commit` (`live.h:438-443`); a knob: `fx.set {tr, slot, p:[12]}` (CB:59; `thunder_live_fx_param`, `live.h:447`); the client computes `p` with the same `tfxChain` mapping the page uses (page-side function, passed in `HOST_API`) |
| master effects `S.mfx` (`rt.mfx`) | W:24826-24830 | same, `tr: 32` |
| pattern effect commands FXV (`fxpTo`, `tcAt`) | BT:146 | inside the core |
| mastering chain `S.mst` + safety limiter `S.limiter`: `mstSync`, `mstChain`, `rtRoute` | W:24789-24812 | **GAP G5b** `master.config {mst, safety}` -> `thunder_live_master_config` (`live.h:453`) |
| chains: node per track, `chainsPush`, `chainsReload`, `chainsTempo`, `chnDiff` (W:21399) -> `b.chn._send` (W:23936) | W:24902, 24928, 24929 | the whole `chainPatch` rides in the song text; the host classifies the change itself (C2a `chains_diff`, C2-DESIGN R3). So **`chnDiff` is not used by the client** |
| chains slider: `thunderChains.param` | W:18989-19015 | **GAP G5c** `chain.edit {ops:[{op:'param',id,name,value}]}` -> `thunder_live_chain_edit` (c2-live `live.h:133,141`; C2-DESIGN R8). Not `fx.set`: `fx.set` has no module id (CB:59) |
| chains structure edit: `thunderChains.mod`, `.reload` | W:19020-19045 (`mod`), `reload` just below | whole-song publish (the host takes a structural change as a plan swap: C2-DESIGN R3(e)) |
| chains tempo / transport to the nodes | W:24929-24933 | inside the host (C2-DESIGN R10); client does nothing |
| `…waves` live pad: `wlDoc`, `wavesChanged` | W:23444, W:18925 | none: waves is not a live engine in `live.h` (D8): rendered twin; the song's `kit.waves` rides in the kit. A waves edit re-renders the pad (existing path) = a new `put` |
| FX bar / pump telemetry `lvl`, `dyn`, `ptel` | BT:150-155, W:35859 `fxLoads` | **GAP G6** |

### 2.5 Meters, playhead, CPU

| Today | W: | Replaced by |
|---|---|---|
| per-track analysers `b.an` (512 samples) read 30 / s in `draw()` | W:36111-36126 | event `meters` (70 floats: peak and RMS of slots 0..34, `live.h:545-549`, CB:120): the client fills a view object (4.7) |
| master analysers `rt.man`, peak hold, CLIP | W:36127-36134 | `meters` slots 33 / 34 |
| TRIM light (`b.ai`, input analyser) | W:36123-36125, W:31422 | **GAP G6** (the record has post-fader slots only): light stays dark in native mode |
| spectrum, scope, header meter, drum-kit meters: `dkAn`, `HM.an`, `sndAn`, `dkSt`, `padBus.an` | W:32365, 32703, 33166, 33199, 33294, 33730, 14939, 9036 | **GAP G7** (`meters.want {spec}` exists in CB:61 but the record has no spectrum: `live.h:545`); hand-played-sound meters (`padBus`) read the page's own context and keep working |
| master reduction meters (`mstMeter`: comp, lim, sup bands) | W:24788, W:24800 | **GAP G6** (CB:124: "a separate small message", not built) |
| playhead: `tp.q` queue of rows with audio times, shown when `ctx.currentTime + 0.01 - heardLat() - moFxLat() - phOff` passes them | W:36096-36107, `heardLat` W:15867 | event `rows {f, valid, ent[]}` (`live.h:538-544`) + `pos`; times are frames, not seconds. Section 4.7 |
| CPU meter: `renderCapacity` (`cpu.cap`) or the cpu probe worklet | W:8766, W:8805-8806, W:35915-35935 | **GAP G9** (no load figure in `clock`: `live.h:519-528`) |
| pad blink / dogs / stage: `padBlink`, `sndKeep`, `stageTick` | W:25682, W:32303, W:32145 | follow `rows` (positions); the per-note flashes come from the page's own note data: **not available in native mode** (ROWS has positions, no notes: `live.h:538`); flashes are dropped in native mode (no message planned: owner question 4) |

### 2.6 MIDI

| Today | W: | Replaced by |
|---|---|---|
| MIDI out: `msend`, `moStart`, `moStop`, `moClock`, outbox, gear delay | W:17935, W:18219, W:18339, W:18210 | `midi.cfg {outs:[x4], clockOut:[x4], gearDelay:[x4]}` (CB:62); the host's `MidiSched` plays the song's MIDI instruments (`live.h:455-470`); shell sinks (H2) |
| MIDI in: `onMidi`, `kbOn`, clock-follow `clkIn` | W:17676, W:17763, W:18346 | event `midi.in` (CB:76; H2) for notes **plus** `note.on` (**GAP G10**); clock-follow is a fallback condition |
| Link | W:18487 `lkTempoOut`, BT section 4.3 | `link.set` (CB:66): not in `live.h:78` = fallback condition |

### 2.7 Recording, audition, preview, metronome

| Today | W: | Replaced by |
|---|---|---|
| recorder (`RCD`: worklet, input, helper) | W:16677 `rcdOpen`, W:16862 `rcdStop`, W:18797 `rcdGo` | `rec.*` (CB:64) is not in `live.h:78`: recording uses the page's own context (it records the *input*, not the output) and keeps working; **a take that must hear the song (pattern-sync take) is a fallback condition** |
| pad tap, keyboard (Z..M, Q..P), the synth page's play button: `play`, `playNote`, `holdNote` | W:9120, W:9080, W:9096 | **GAP G10** `note.on` / `note.off` (CB:60; `live.h:78` "D16"). Until built these use the page's own context (created on first use) |
| audition after an edit (`audition`, `playFresh`) | W:8966 `audition` | same as the line above |
| metronome click, count-in | W:25902, W:25910, W:25921 | page's context (`click`): not played in native mode; **CHOICE:** the metronome is silent while the core plays and the toast says so once (owner question 4). Count-in = fallback condition |
| editors' own AudioContexts (`chains/index.html:1939`, `waves/index.html:1856`) | BT:198 | they keep them for auditioning inside the editor (the old engine); the client calls the embeds' `pause()` (BT 5.2) when the core starts so two engines do not sound |

### 2.8 Renders to file

| Today | W: | Replaced by |
|---|---|---|
| `renderOut` (mix / stems), `pass` in an `OfflineAudioContext` on the main thread | W:26265, W:26198 | out of N2: `render.*` (CB:65, `thunder_render_*`, U34) is a later PR. Native mode keeps the page's offline path (it needs no live engine; it builds its own graph in its own `OfflineAudioContext`) |
| `wavN`, `outWrite` | W:26247 | stays |

### 2.9 Page-level audio plumbing that must not run in native mode

`audio()` (W:8804-8827, makes `ctx`, `master`, the safety-limiter worklet, the CPU probe), `rtInit` (W:24399), `resetAudio` (W:24263), `bgIdle` and the idle sleeps (W:25113-25140), `audioInterrupted`
(W:25088), `devRecover` (W:15876-ish block, W:25150), `wakeLock` (W:26040), `bgPlay` (W:26010, a hidden `<audio>` loop + `mediaSession`), the watchdog.
- `bgPlay` / `wakeLock` are for the page-owned sound; in the Android shell the foreground service holds the process (N2P section 5). **CHOICE:** native PLAY does not call `bgPlay(true)` (it would start the hidden `<audio>` loop for nothing) and calls `wakeLock(true)` only if the setting is on; the shell may ignore the web call.
- `resetAudio` in native mode: **GAP G13** (`engine.reset`).
- `audioInterrupted` / `devRecover` read `ctx`; with no `ctx` both return at their first line (`if (!ctx ...)`, W:25089 / W:15880-ish): already inert. Device change in native mode is the shell's (`dev` event, CB:77).

## 3. Gaps in the contract (each additive, so BRIDGE_V stays 1)

Rule for compatibility (CB:42-44): unknown message types are ignored and logged once, so a new page can send these to an old shell and an old page never sends them. Reply fields are only added.
"Owner of the change": the lead edits `CORE-BRIDGE.md` section 3 / 4; the shell side is H1b / H1c (N2P section 6).

| # | Gap | Proposed (all fields additive) | Shell maps to |
|---|---|---|---|
| **G1** | `hello` reply has no sample rate, no block size, no output latency (CB:50). The page needs the rate before the first pad render (`padSR`). | reply gets `sr`, `outLat` (frames), `host:'android'|'web'|...`, `maxPadBytes`, `maxSongBytes` | `thunder_live_create` options (`live.h:149-160`) |
| **G2** | The N0 shim's `put(id, f32, chunkBytes)` (`thunder_bridge.js:36`) is not the contract's `put(id, {sr,ch,frames}, Float32Array[])` (CB:31): one mono array, no rate. | H1b implements the contract's form (header carries `sr`, `ch`, `frames`; arrays are channels). **The client is written to the contract**; a 20-line adapter in the client accepts the N0 form (`put(id, f32)`) when `thunderCore.host === 'android-n0'` | `pad_take` (`live.h:295`) |
| **G3** | No message makes the host publish a batch of pads: pad calls only stage, `thunder_live_pads_publish` makes them audible (`live.h:337`); `buf.use` is per buffer (CB:54). | `pads.commit {n}`; reply is the existing event `pads` (the PADS record, `live.h:563`) so the page knows the batch is audible. The client sends it 40 ms after the last `buf.use` | `pads_publish` |
| **G4** | `buf.use` has `pad, layer, kind` only (CB:54); variations need `ii` and `vk` (`thunder_live_var_set(ii, kind, num, pad, ...)`, `live.h:308`); chop data has no message at all (`live.h:313-314`). | `buf.use` for `kind:'var'` also carries `ii`, `vk` (`'m3'` / `'g1'`: the page's key W:11959); new `pad.chop {pad, loopSecs, slices:[frames...]}` | `var_set`, `pad_chop`, `pad_hits` |
| **G5a** | `fx.set {tr,slot,on}` cannot carry a slot list; the host plans from a slot list (`live.h:438`). | `fx.plan {tr, slots, bpm}` (slots = `S.tfx[t]` or `S.mfx`, the JSON `FxHost::planFromSong` reads) then the shell commits (`at: NOW`); `fx.set {tr, slot, p:[12]}` as CB:59 | `fx_plan`+`fx_commit`, `fx_param` |
| **G5b** | no master-chain message although `thunder_live_master_config` exists (`live.h:453`). | `master.config {mst, safety}` | `master_config` |
| **G5c** | chains parameter has no message: `fx.set` has no module id; `thunder_live_chain_edit` takes the page's op array (C2-DESIGN R8). | `chain.edit {ops}` (an array of `{op:'param', id, name, value}`; all or nothing; reply `err` event on E_ARG / E_STATE) | `chain_edit` (needs `F_CHAINS`, H1d) |
| **G6** | `meters` has no FX slot meters, no chains telemetry (mods, shows, jacks, bypass, load), no master reduction meters, no input (TRIM) peaks (`live.h:545`; CB:124-125 "come with U14"). | later events `fx.meters {tr, slots:[..]}`, `chains.telemetry {tr, ...}`, `master.meters {inPk, outPk, comp, lim, sup}` (separate small messages, as CB:124 says). **N2 does without them**: the panels that read them show nothing in native mode (section 4.7) | U14 / U31 follow-ups |
| **G7** | spectrum / scope: `meters.want {spec}` (CB:61) has no record. | later: `spec` event (1024 floats, master only). N2 leaves the spectrum panel blank in native mode | later |
| **G8** | `pdc.set {mode}` missing. | `pdc.set {mode:0|1|2}` | `thunder_live_pdc_set` (`live.h:429`) |
| **G9** | no CPU figure: `clock` has `under` and `out_lat`, no load (`live.h:519-528`). | `clock` gets `load` (0..1+, the shell's callback time over the period, measured in the shell's callback wrapper, no core change) | the shell |
| **G10** | `note.on` / `note.off` are in CB:60 but not in `live.h:78` (D16): keys, pad taps, previews, MIDI thru cannot sound through the core. | **N2: none** (hand-played sound uses the page's lazily made `AudioContext`). Later `note.on {pad|tr, note, vel}`; the client already routes all hand-played sound through one function `playHand(...)`-sized seam (section 4.3 row 21) so the swap is one line | H1c / N3 |
| **G11** | `lxp` has no call (`live.h:78`). | none; fallback reason `"live transpose (LXP)"` | |
| **G12** | `play` has no step: RESUME cannot start mid-row (`live.h:352-368`). | later `play.step`; N2 restarts the row (choice in 2.1) | later |
| **G13** | `resetAudio` has no message. | `engine.reset {}`: shell does `stop`, panic, destroy, create, and re-sends nothing; the client re-sends song + kit + pads on the `engine` event `{t:'engine', up:true}` | shell rebuild (N2P section 5, the device-change path) |
| **G14** | `song.load` has no reply (CB:52: command; the `song {rev,f}` event only says the audio thread reads it, CB:104). The fallback verdict (N2P section 4: shell decides, "fit") has no field. | `song.load` is sent with `ask`; reply `{ok, rev, reasons:[...], warn:[...]}`; `ok:false` is the fallback verdict; `warn` lists non-fatal facts ("2 pads without their pad effects", "3 pads play their rendered twin"). A shell that does not reply: the `ask` times out after 5 s, the client treats it as `ok` and waits for the `song` event | `song_publish` error text + shell's `SongFit` |

## 4. The design of the client

### 4.1 Module shape

```
window.__thunderClient = { attach(hostApi), on: false, can(), start(mode,o,t0), stop(), edit(), ... }   // 'NC' in index.html: const NC = () => window.__thunderClient
state: tc (window.thunderCore), hello caps, sr, rev (song revision counter), sent signatures, pad table, playing flag, last clock pair, view {...}
```
All timers are owned by the client and torn down by `NC.detach()`.

### 4.2 Start-up handshake

1. `loadClient()` (1.2) -> `attach(hostApi)`.
2. `tc.ask({t:'hello', v:1, app:BUILD})` (CB:50, 5 s). Reply must have `v === 1` and `caps.nativeAudio` (or the web copy's equivalent). Else `NC.on = false`, one log line, **nothing else changes**.
3. Store `sr` (G1), `caps` (`effects`, `engines`, `fx`, `sf`, `chains`: `live.h:134-139`, c2-live `:141`).
4. `padSR()` now returns `sr`; the page's `prerender()` is called once (W:8987): pads render at the engine's rate, **before** any `AudioContext` exists (this replaces the `srGuess` throw-away context, W:8758-8765, skipped when `NC` will be on: the test is `window.thunderCore || aset.core === 'native'`, evaluated before that block: **an `index.html` edit of 1 line**).
5. `tc.send({t:'meters.want', rate: 30, tracks: 32})` only while a screen that draws meters shows (4.7); `tc.send({t:'midi.cfg', ...})` from `midi` settings (W:17626) when they change.

### 4.3 The seam in `index.html`: every function that gets a branch

Shape rule: **one line at the top of the function, `if (NCON()) return NC().xxx(...)`**, where `NCON = () => !!(window.__thunderClient && __thunderClient.on)` is defined once, early. With `NC.on` false every line is skipped by a single property read. Where the function must keep running (it has other jobs), the branch wraps only the audio part.

| # | Function (W:) | Branch shape |
|---|---|---|
| 1 | new `loadClient()` + `NCON` near `coreWanted` (W:25338) | the 12-line loader, section 1.2 |
| 2 | `srGuess` block (W:8758-8765) | `if (!(window.thunderCore \|\| aset.core === 'native')){ ...as today... }` |
| 3 | `padSR` (W:8901) | `() => NCON() ? NC().sr : ctx ? ctx.sampleRate : srGuess` |
| 4 | `tStart` (W:24946) | after the first guard lines (save, prerender): `if (NCON() && NC().can(mode, o)) return NC().start(mode, o);` (everything the page-sequencer path does between W:24960 and W:25058 is skipped); `NC().start` returns `false` to fall through |
| 5 | `tStop` (W:25725) | inside the `if (tp.core)` block region: `if (tp.nc){ tp.nc = false; NC().stop(); }`; the rest of `tStop` (resume point, cursor, `moStop`, rec end) runs as today. `rt` is null so `rt.pl.stopAll`, `pdcSync` are already guarded (W:25756-25757) |
| 6 | `saveS` (W:22290) | beside `coreSong()`: `try { if (NCON()) NC().edit(); } catch(e){}` |
| 7 | `install` (W:8922) | already calls `coreBuf(i)`; **`coreBuf` gets one line**: `if (NCON()) return NC().pad(i);` |
| 8 | `coreKit` / `coreKitCheck` (W:25386, 25393) | `if (NCON()) return NC().kit();` |
| 9 | `varWant` / `coreVar` (W:11959, 25419) | `coreVar`: `if (NCON()) return NC().variation(ev, rec);` |
| 10 | `setSong` (W:27177), `setKit` (W:15242) | `NC().newSong()` / `NC().newKit()` (forces a full publish and a pad re-send; `tStop(true)` has already run) |
| 11 | `draw()` meters + playhead (W:36096-36135) | one `else if (NCON()){ NC().frame(hearTime) }` after the `if (rt && ...)` blocks: it drains the playhead queue the client keeps (4.7) and writes the meter view |
| 12 | `cpuMeter` (W:35915-35925) | `const v = NCON() ? NC().load() : ...` |
| 13 | `thunderChains.param` (W:19000), `.mod` (W:19020) | `.param`: after the patch update, before `if (!rt \|\| !rt.pl) return;`: `if (NCON()) return NC().chainParam(id, name, value);` `.mod`: the existing `saveS()` already schedules the publish; nothing else |
| 14 | `mixChanged` (W:24934), `rtFx` (W:24822), `mstSync` (W:24789), `rtRoute` (W:24805), `moApply` (W:24820), `chainsPush` (W:24902), `chainsTempo` (W:24929) | **no edit**: `rt` is null, each returns at its first line, and `saveS` already told the client |
| 15 | `perfPush` (W:22627), `perfFill` (W:32961) | `if (tp.core)` -> `if (tp.core \|\| tp.nc)` and the `corePost` they call is `corePost` of 16 |
| 16 | `corePost` (W:25382) | `const corePost = (m, tr) => { if (NCON()) return NC().legacy(m); ... }`: the client maps the JS core's `loop`, `bpm`, `perf`, `lead`, `lxp` ops to bridge messages, so the stand-in `tp.seq` (W:25469-25474) works unchanged (U37 D4 (a)) |
| 17 | `resetAudio` (W:24263) | `if (NCON()){ NC().reset(); }` at the top (then it continues for the page context, if any) |
| 18 | Settings > Audio: `updCore` segment (W:15961) | third value + the reason line (1.3) |
| 19 | `pbState` / playback report (W:25485 region) | one extra line: engine, `sr`, `under`, `late`, `evt_dropped` from the last `clock` / `late` events |
| 20 | `bgPlay(true)` / `wakeLock(true)` calls in `tStart` | inside branch 4: not reached |
| 21 | hand-played sound: `play`, `playNote`, `holdNote` (W:9120, 9080, 9096) | **no branch in N2** (G10); one comment marks them. When `note.on` lands: a line each |
| 22 | `rtInit` call at start-up (W:24409 `if (coreWanted()) coreLoad()` inside `rtInit`) | no edit: `rtInit` only runs from paths that already needed `audio()` |

About 22 sites, 15 of them one line. Nothing inside `ENGINE START .. END` (the Worker's code), nothing `makeSeq` reads (the `core/seq-core.js` freshness test, `beta:tests/core.test.js:10`, stays green), and none of `songStore`, `songOut`, `projText`.

### 4.4 When and how edits are sent

**The client's `edit()` is called by `saveS()` (branch 6) and does two things, both coalesced to one task per 16 ms:**

1. **Small messages, now.** The client keeps `sent = {mix:[32 x JSON], master, mout, bpm, tfx:[33 x JSON], mst, limiter, chainOn, ...}`. For each small thing that differs from `sent` it sends one message and updates `sent`:
   - `S.mix[t].vol|trim|pan|mute|solo` -> `mix.set` (values as the page stores them, 0..255; `ramp: -1` for the page's law; mute: `ramp` of 12 ms in frames).
   - `S.master` / `S.mout` -> `mix.set {tr:32}`.
   - `S.tfx[t]` / `S.mfx`: same slot structure (same keys, `type`, `on`) and only parameters moved -> one `fx.set` per changed slot with `p` from `tfxChain` (`HOST_API.tfxP`); anything structural -> `fx.plan` (G5a).
   - `S.mst`, `S.limiter` -> `master.config` (G5b). `S.pdc` -> `pdc.set` (G8).
   - tempo while playing is not here (it goes through the `tp.seq.bpm` setter, section 2.1).
   Each signature is a `JSON.stringify` of a small object (the mixer: 32 objects; one effect list at a time). The big ones (`S.chainPatch`, `S.phrases`, `S.song`, `S.insts`) are **not** compared here: they only trigger step 2 by `songVer` (the counter `saveS` already bumps: W:22291).
2. **The whole song, 250 ms after the last edit** (U37 ruling D3(a); one timer, `publishT`). While **stopped** the publish is also sent (cheap on the shell: IDLE publish only keeps the Song, `live.h:262-266`) but **not before 250 ms of quiet and not while a pad render is in flight for the same edit**. Text = `JSON.stringify(S)` (`songOut(S)` is the identity because `NT = 32`: W:22254-22255, NT at W:19118); **memoised by `songVer`**, so a `saveS` that changed nothing sends nothing. `vars = {vtrk, NSLOT, TFX: coreTfx()}` (W:39058-39060).
   `song.load {S: text, vars, rev}` through `ask` (G14): `rev` = the client's counter; the verdict and the `song` event (CB:104) are tracked per `rev`.
   Why not diffs: `song.patch` has no host entry (the host takes whole text, `live.h:262-273`; N2P section 2 row `song.patch`), and `coreDiff` costs a `JSON.stringify` of every list item on every save (W:25429-25437).
   Cost watched in the first PR's gate: a stringify of a normal song is a main-thread task; the budget is **2 ms median / 8 ms worst on the Fold** for ELECTRO and one census song (measure, GUESS that it passes: the page already stringifies `S` for autosave every 600 ms, W:22103). If it fails: raise the debounce for songs over a size, or stream with a Worker.
3. **While playing**, a publish is a *successor snapshot* the audio side adopts at the next quantum with `Seq::takeOver`; a note already booked is not re-timed (`live.h:268-272`). Up to 16 can wait; the 17th is `E_BUSY` and the shell retries (CB:112); the client does not retry, it just sends the newest after the next `song` event.
4. **Send nothing mid-gesture:** a slider drag produces many `saveS`; step 1 coalesces to one message per 16 ms per parameter (last value wins); step 2's debounce restarts on each.

Order guarantee: messages go over one ordered port, so "mix.set then song.load" arrive in that order; the host's rule "the same change must also be in the next song text" (`live.h:420`) is met because step 2 follows every step 1.

### 4.5 Pads: rendered by the page, sent with `put`

- **Render:** unchanged (Worker, `renderAsync` W:8893; `install` W:8922). Only `padSR()` changes (branch 3), so pads are rendered at the engine's rate (a mismatch would also work: `pad_set` takes a rate, `live.h:289`, but a rate equal to the host's saves a resample).
- **Send:** `NC().pad(i)` (from `coreBuf`): if `cache[i].data` is the same array as last sent (`sent.get(i) === c.data`, as W:25427) do nothing. Else `tc.put('pad:'+i+':'+rev, {sr, ch: 1|2, frames}, [data, data2?])` then `tc.send({t:'buf.use', id, pad:i, kind:'pad'})`. The page keeps its own copy (it needs it for the wave display and previews), so `put` **copies** (the shell chunks 1 MB: CB:44; the N0 shim copies each slice: `thunder_bridge.js:43`). A removed pad: `buf.use {pad:i, kind:'pad', frames:0}` (a zero-frame pad removes it: `live.h:291`).
- **Batch:** every `buf.use` resets a 40 ms timer; when it fires: `pads.commit` (G3). The reply event `pads` tells the client the batch is audible. PLAY does not wait for pads still rendering (as today: "a pad with no render at all is silent until its render lands", W:24985-24988); it waits at most 1.5 s for the **first** commit when no pad has been sent yet (the first PLAY of a session), then plays.
- **Budget:** `maxPadBytes` from `hello` (G1; a phone 128 MiB, `live.h:154`). The client sums what it sent (4 bytes x frames x ch); over 90 % it stops sending new pads, toasts once ("Pads are too big for the native engine: playing with the old engine") and the song falls back at the next PLAY (reason `pad memory`). E_LIMIT from the shell does the same.
- **Order:** `kit.load` first (the host needs `choke / syn / rel` per pad), pads, commit, then `play`. On a kit change while playing the client re-sends `kit` (200 ms check, W:25393) exactly as the JS core does today.
- **What is never sent:** pads of an empty slot; SoundFont pads while `CAP_SF` is unset (fallback reason).

### 4.6 The fallback: "the old engine plays what the new one cannot, with a short message"

The decision is made **once per PLAY**, before anything sounds, in `NC().can(mode, o)`:

| Check | From | If it fails |
|---|---|---|
| client `on` and `hello` ok and a heartbeat (`clock` event) within 2 s | 4.2 | fallback, reason "the native engine is not running" |
| last `song.load` verdict `ok` for the current `rev` (G14) | the shell's `SongFit` (N2P section 4) | fallback, message = the first reason |
| a pad needed by the song could not be sent (E_LIMIT / budget / `CAP_SF` unset for a SoundFont pad) | 4.5 | fallback, reason names the pad |
| page-only conditions: recording armed or a pattern-sync take (`RCD.sync`, `rec.on`: W:16838, W:25011), count-in, Link on (`lk`), MIDI clock follow (`midi.clkIn`), `lxp` used, `S.nmono / hmono` > the host's tracks (TRACKS = 32: `live.h:62`), more than 32 tracks (never: `NT = 32`) | W:18346, W:25011-25020 | fallback, reason names it |

On fallback: `toast('Playing with the old engine: ' + reason, 4000)` (existing `toast`, W:9378), `NC().start` returns `false`, `tStart` continues down the page-sequencer path, **which calls `rtInit()` -> `audio()` and creates the `AudioContext` then** (as today, nothing else to write). The page's pad table is already filled (it renders at the engine rate; the old path re-renders pads whose rate differs from the new context: `stale` W:8918 already handles it: `c.sr !== padSR()`, and `padSR()` returns `ctx.sampleRate` once `ctx` exists). The verdict is remembered per `songVer` so a second PLAY of the same song does not re-ask and does not re-toast within the session.
- While the old engine plays, `NC` stays attached but silent: `saveS` still publishes (so a later edit can make the song fit, e.g. the owner removes the stretch) and `NC().can` is evaluated again at the next PLAY.
- **During play** (`err {fatal:true}`, a missing `clock` for 1 s: the heartbeat, `err` event CB:81): STOP (existing `tStop`), toast "The native engine stopped: <where>. Press PLAY to try again", and the **next** PLAY of that song uses the old engine (U37 D14 (a) "stop and tell", ruling 21 Nov). The client never restarts a song on the other engine by itself.
- A **non-fatal** `err` (`REANCHOR`, `VOICES`, `SESSION_FULL`, `live.h:565-577`) is written to the playback report only.

### 4.7 Playhead, meters, CPU, clock

**Clock.** Event `clock {f, wallNs, sr, outLat, under}` at 10 / s (CB:70). The client keeps the last pair. A frame to page time: `perfMs = lastWallNs/1e6 + (f - lastF) / sr * 1000` mapped through `performance.timeOrigin` **only if** `wallNs` is on the page's `performance.now()` clock; if not (the shell's `wallNs` is the OS monotonic clock), the client records `(f, performance.now())` at the moment the event is *handled* and uses it with an error bar of the message latency: **CHOICE:** use the handling moment; N0 measured the Android port latency small (a few ms) next to the 26 ms output latency (CB:99, `RESULTS-N0`); the playhead offset setting `phOff` (W:36102) exists for the rest.

**Playhead.** Event `rows {f, valid, ent[row,cr,step,phrase]}` per tracker row (`live.h:538-544`). The client turns each into the entry the page's queue has, `{t, pos[NT], bpm, tks, line}` (W:25677, W:23214): `t = f -> seconds on the page's time base`, `pos[i] = {row, step, ...}` from `ent`, `bpm` from the latest `pos` event (`bpm10 / 10`, `live.h:530-533`), `tks = S.ticks || TICKS`, `line = 60 / bpm / (S.lpb || 4)` (the same formula as W:23214). It pushes to `tp.q` and the existing code in `draw()` (W:36096-36107) shifts them off when `hear = now - heardLat() - moFxLat() - phOff` passes them. **For native mode `hear` is computed by `NC().frame()`** (the output latency is `outLat` from `clock`, not `heardLat()` which reads the page's context: W:15867). `tp.q` stays the single queue every other feature reads (`rcdStep`, `sndKeep`, follow-play), so none of them changes.
`end` of the song: the `state` event `ENDED` (`live.h:554-558`, `3`) sets `tp.seq._alive = false`; `STOPPED arg 1` ends the page's transport (calls `tStop(true)`); `HELD` (CB: exact stop) calls `rcdHeldAt` as W:25659 does.
Not available: per-note events (pad flashes, dogs, stage): see 2.5.

**Meters.** `meters.want` is sent only while a screen needs it (tracker / mixer / master bar / chains screen): a `NC().want(on)` called from the view switch (`view` changes; the existing `meterHold()`, `mtrWake` W:25749 show where the page already decides this). The client keeps `view = {lv: Float32Array(33), lvA, mpk: Float32Array(2), mlv, mlA, mpt}`, which `NC().frame()` updates from the last `meters` record **with the page's own decay law** (`dk = 0.86^(dt*0.12)` as W:36115; the contract says "decay, holds and floors stay with the page", CB:123), and writes into the same fields the paint code reads. Because those fields live on `rt.pl.buses[t]` and `rt.man` (W:32021 `paintMeters`, 35 `.lv` uses), **CHOICE: the client creates a stub `rt`-shaped object only for painting:** `NC().meterRt = {pl:{buses:[{lv, lvA, ipk, ipt, iseen, an:null, ai:null, fx:null, chn:null} x 33]}, man:null, mlv, mlA, mpk, mpt}` and the paint functions take `rt` from `meterRt()`: **two branches** (`paintMeters` W:32021 and `draw()` W:36111) `const R = NCON() ? NC().meterRt : rt`. `rt` itself stays `null`, so every graph function remains inert. (Alternative rejected: setting `rt` to a stub would make `if (rt)` guards run graph code against a fake: 100+ sites.)
FX load numbers (`fxLoads` W:35859, `rt.pl.buses[t].fx.load`) read `b.fx` which is `null` in the stub: they report 0, the CPU panel shows "effects: no figure" (G6).

**CPU meter.** `NC().load()` = the latest `clock.load` (G9), else `null` -> the existing code shows `0%`. `under` (underruns) turns the meter red as `glitch.t` does today (W:35925-35927).

**Playback report** (`pbLog`, W:25485): the client adds `late`, `evt_dropped`, `reanchors`, `fx_remakes` from the `late` event (`live.h:579-586`) and `clock`.

### 4.8 What happens to the page's own `AudioContext`

- A native PLAY **never calls `audio()` or `rtInit()`** (branch 4 returns before them: they are at W:24957 / W:25001). With nothing else making sound, `ctx` stays `null`: no `AudioContext`, no safety-limiter worklet, no CPU probe, no throw-away `srGuess` context (branch 2). This is the point of the Android port (the web view's clock ran ahead once any worklet loaded: `WP:13-15`).
- Hand-played sound (G10) calls `audio()` as today and creates `ctx` on first use, with `wkLazy()` true on a phone so **no worklet is loaded** (W:8736, W:8806-8819): a plain `AudioContext` with buffer sources. It never connects to the core and never runs while the page is not playing something by hand. Its idle sleep (`bgIdle`, W:25113) suspends it after 20 s hidden / 90 s shown (W:25102).
- **On fallback** the page proceeds exactly as with no client: `rtInit()` makes or resumes `ctx` (`audio()` already does `ctx.resume()` when not running: W:8826).
- If the `AudioContext` exists (hand-played sound) when a native PLAY starts, `NC().start` calls `ctx.suspend()` is **not** called (a suspended context is `bgSus`, which the page treats as deliberate: W:25113); instead the page's hand-played voices are left to end by themselves: they are short (previews).

### 4.9 Service worker and precache

- `beta:sw.js:4` `FILES` is the precache list (cache `thunderplus-v676`, line 3). The client file is **added to `FILES` only in the PR that lands the client on `beta`**, with its `?v=` form like the others (`'./thunder-core-client.js?v=1'`); a missing file would make `cache.addAll` fail and **the whole install fail** (`beta:sw.js:11-12`: `addAll` of every entry), so the PR also bumps `CACHE` and the existing app-logic test "service-worker file list" (`beta:tests/README.md`, `app-logic.test.js`) is the guard that the file exists.
  **CHOICE: list it in `OPTIONAL` (`beta:sw.js:8`), not `FILES`,** for the first release: optional files are cached "when they are there and a missing folder never stops the rest from installing" (`beta:sw.js:6-8`). The owner's daily web app then cannot lose offline start-up to a missing file, and the web switch (off by default) still gets it. Move to `FILES` when the switch leaves "test".
- `core.wasm` and the worklet (P6..P8, `U37:166`-ish) add their own entries later; not part of this document.
- Android: no service worker. `WebViewAssetLoader` serves `index.html` and `*.js` from the APK (`MainActivity.kt:98-103` per N2P section 1); `thunder-core-client.js` is picked up by the existing copy rule (`build.gradle.kts:49`).
- **Cache-busting:** the loader URL carries `BUILD-REV` the way `CORE_URL()` does (W:25339).

## 5. Risks for the owner's daily web app, and how `npm test` proves inertness

**What must hold with the switch off (and no `window.thunderCore`):**
1. No new network request: `loadClient()` is not called (`if (window.thunderCore || aset.core === 'native')`).
2. No new global except `NCON`, `NC`, `loadClient` (three small functions; no state).
3. Every new branch evaluates `NCON()` -> `false` and falls to the old line. `corePost` keeps its JS-core body. `padSR` keeps `ctx ? ... : srGuess`.
4. `srGuess` block unchanged (its condition is `false` on the web with the switch off).
5. Nothing `makeSeq` reads is touched, so `core/seq-core.js` stays byte-identical: `beta:tests/core.test.js:10-12` (`make-core.mjs --check`) proves it.
6. `sw.js` change is one `OPTIONAL` entry + a version bump; the "service-worker file list" test checks the file exists.

**Tests (all run in `npm test`, `node --test tests/*.test.js`: `beta:package.json`):**
- New `tests/native-client-inert.test.js` (about 150 lines, same `vm` method the other tests use: `beta:tests/README.md` header "loads the app's own files into a Node `vm`"):
  (a) load `index.html` with no `thunderCore`: `NCON()` is `false`; `padSR()` equals what it was; `tStart` / `saveS` run a stubbed song with `__thunderClient` undefined and produce the same `corePost` calls as before (a recorded list in `tests/golden/native-client-off.json`);
  (b) **source guards**: the number of `NCON()` occurrences equals the list in section 4.3 (22 minus the no-edit rows: the test holds the exact count and fails when a branch is added without a row here); no line outside those contains `__thunderClient`; `thunder-core-client.js` is not referenced by `index.html` except in `loadClient`;
  (c) `loadClient` is never called with the switch off (a spy on `document.createElement('script')`).
- New `tests/native-client.test.js`: the client against a **fake `thunderCore`** (an in-process object: `send` records, `ask` answers `hello` / `song.load`, `put` records buffers, `on` lets the test emit `clock`, `rows`, `meters`, `state`, `err`): handshake, pads (`put` then `buf.use` then one `pads.commit`), the state-diff (one `mix.set` per changed fader, no `song.load` before 250 ms, memoised stringify), PLAY order (`kit.load` -> pads -> `song.load` ack -> `play`), fallback reasons, fatal `err` -> stop, `rows` -> `tp.q` entries equal to the formula in 4.7, meters decay. No browser needed.
- Existing tests that read `index.html` source (`beta:tests/*` count: 216 files) keep passing because the new lines are additions inside functions; **PR 1 runs the whole suite and lists any test whose source guard counts a pattern the branches touch** (e.g. tests that count `corePost(` calls).
- The headless sweeps (`beta:tests/sweep/`, not in `npm test`) get one new run, `first-play.mjs` with the switch off, as a before / after for the first PLAY of a session.

**Other risks.**
- *Main-thread stringify cost of `S`* (4.4): measured in PR 3's gate; the fallback is a longer debounce.
- *The page's `AudioContext` appears later than today* in native mode: every code that assumes `ctx` exists after PLAY (the playback report, `pbState`, `audioWake`) must tolerate `null`; this is checked by running the sweep `first-play` with a fake client that turns native mode on (PR 5).
- *Two copies of the truth for pads*: the page keeps `cache[i]` and the core holds its own copy (memory x2 on the phone). It is the cost of the page's wave display; the budget in 4.5 counts only the core's copy.
- *A song that "fits" by the shell but sounds different* (two pads dry, rendered twin of live engines): the warnings in G14 go to the toast and the report; not silent.
- *`song.load` of a 16 MiB song over the port*: the shell takes `max_song_bytes` from `hello`; over it the client falls back before sending.
- *Order of events across the web view's two message paths* (WebMessagePort, ArrayBuffer for `put`): `put` completion (`resolves when the core holds it`, CB:31) is awaited before `buf.use`, so no ordering assumption is made.
- *Seam drift*: `beta` moves fast (build 676 now); the line numbers in this file will drift. The test in (b) counts branches, so a rebase that loses one fails loudly.

## 6. Work breakdown (PRs, files, gates, who, hours)

"Sonnet builder" does all; **TOP** = the lead reads every line on the top model before merge. Hours are builder GUESSES from line counts and the sizes N2P used (it estimated the page client at 16 h as one PR: this plan splits it and finds more, because of the inert tests and the meters stub).

| PR | Content | Files | Gate | Size | Builder h (guess) | Lead review |
|---|---|---|---|---|---|---|
| **PC0** (docs, lead) | Amend `CORE-BRIDGE.md` section 3 / 4 for G1..G14 (additive, v1), mark `song.patch` "web copy core.wasm only if built" | `docs/port/CORE-BRIDGE.md` | the shell builder (H1b) can implement from it | 60 lines | 1 (lead) | **TOP** |
| **PC1** loader, switch, inert tests, `padSR`, `srGuess` | `NCON`, `loadClient`, branches 1, 2, 3, 18; empty `thunder-core-client.js` with `attach` and the `hello` handshake; `tests/native-client-inert.test.js` (a, b, c); `sw.js` `OPTIONAL` entry | `index.html` (about 25 lines), `thunder-core-client.js` (150), `sw.js`, 1 test (150) | `npm test` green on `beta` unchanged with the switch off (the proof); counts match 4.3 | S | 6 | **TOP** (it is the only PR that touches the owner's daily file before the rest; every line read) |
| **PC2** pads | branches 7, 8, 9, 10 (`coreBuf`, `coreKit`, `coreVar`, `setSong`, `setKit`); `put` / `buf.use` / `pads.commit` / `pad.chop`; budget | `index.html` (10 lines), client (+250), fake-core test (+200) | fake-core test: order, copy, budget, removal; a real run on the Android emulator with H1b when it exists | M | 9 | **TOP for `put` and the budget code** (memory limits on a phone, ownership of buffers); the rest no |
| **PC3** song, transport, fallback | `edit()` publish side, branches 4, 5, 6, 15, 16, 17; `song.load` ask + verdict, `play` / `stop` / `hold` / `bpm` / `loop` / `perf`, `can()`, the fallback toast, fatal `err` | `index.html` (12 lines), client (+350), tests (+250) | fake-core test of PLAY order and fallback; **stringify budget** measured on ELECTRO + one census song in headless Chrome (2 ms / 8 ms) | M | 12 | **TOP** (PLAY / STOP / fallback logic is where a bad line silences the owner's app) |
| **PC4** live edits | the small-message state-diff (mixer, tfx plan / param, master, pdc, chains param); branch 13 | `index.html` (4 lines), client (+250), tests (+200) | fake-core test: one message per changed thing, coalescing, no `song.load` storm | M | 9 | partial: **TOP** for the diff signature code (stale `sent` = a silent fader); the rest by diff review |
| **PC5** events: clock, playhead, meters, CPU | branches 11, 12, 19; `clock` map, `rows` -> `tp.q`, `meters` view + `meterRt` stub (two paint branches), `state` events, `load` | `index.html` (10 lines), client (+300), tests (+250) | fake-core test: `rows` entries equal the formula; decay; **the sweep `first-play` with a fake native mode shows no throw with `ctx === null`** | M | 10 | **TOP** for the `meterRt` stub and the `ctx === null` audit (a throw in `draw()` is a dead screen) |
| **PC6** web copy wiring (later; WP row 11 / P7 / P8) | `aset.core === 'native'` creates `window.thunderCore` from the core.wasm worklet; segment label; web fallbacks | outside this document (U37 P6..P10) | `seq-golden --core` equal; `npm test` unchanged with the switch off | | not counted | |
| **PC7** Android E2E | with H1b: run the real page against the real shell on BrowserStack / the Fold: ELECTRO plays natively, a fallback song shows the toast; update `native/tests/ANDROID-CHECKLIST.md` | tests only | the owner's list (N2 exit, N2P section 7 item 3) | S | 5 | no |

**Order:** PC0 (lead) -> PC1 -> {PC2, PC3 can start in parallel on the fake core; PC3 needs PC1} -> PC4 -> PC5 -> PC7. PC1 first because every later PR depends on the proven-inert seam. PC2 and PC3 do not conflict (different functions); PC5 touches `draw()` which the owner changes often: it should rebase on `beta` the day it merges.
**Totals (guess):** builder 51 h + PC7 5 h = **about 56 h**; lead's line-by-line **about 5 h** (PC1 1, PC2 1, PC3 1.5, PC4 0.5, PC5 1). N2P counted 17 h for the "page client" including review: this plan is larger because it adds the inert-proof tests (about 8 h), the state-diff (9 h) and the event view (10 h), none of which N2P had.
**Shell side, not counted here (H1b of N2P):** answer G1..G14, implement the contract's `put`, the `song.load` verdict, `pads.commit`, `pad.chop`, `fx.plan`, `master.config`, `chain.edit` (with H1d), `engine.reset`, `clock.load`; about 6 h more than N2P's 14 for H1b (GUESS).

## 7. What stays outside the client (so nobody expects it)

Song renders to file (`render.*`, U34), recording through the core (`rec.*`), Link, MIDI-clock follow, `lxp`, SoundFont pads, hand-played notes through the core (`note.on`), spectrum and FX / chains telemetry (G6, G7, G10). Each has a defined behaviour in native mode (a fallback reason, or a panel that shows nothing), listed above; none breaks the song.

## 8. Questions for the owner (plain words, five)

1. **A pad you tap, a key you press, a preview on the Synth page: for now they sound through the old engine** (the phone's web audio is started the first time you touch one, then sleeps). The song itself plays on the new engine. Is that fine for the first Android beta, or should keys and pads wait until the new engine can play them too?
2. **When a song can't play on the new engine, the old one plays it and a short message says why.** Do you want that message to appear every time you press PLAY, or only the first time for that song?
3. **Resume (carry on from where you stopped) restarts the current row** on the new engine at first, not the exact step. Okay?
4. **Three small things are silent or empty on the new engine at first:** the metronome click, the pad flashes / dancing dogs while the song plays, the effect and chains level meters. Which of these matters most to you, so it comes first?
5. **The Settings switch on the web copy:** on the phone the new engine is simply on, with a way back to the old one for a bad song. On the computer web app it stays off unless you switch it on. Do you want a visible "Native core (test)" label in the Settings, or hidden until you ask?

## 9. Notes for the lead (not for the owner)

- The one thing I would check first before building: that `saveS()` is the only way a song change reaches the audio today. I read the mixer, effects, master and chains edit paths (W:24934, 24832, 18958, 19000) and they all call `saveS` (directly or through `mixChanged`, `fxChanged`, `chainsChanged`), but `tests` that bypass it, `rt.pl.applyMix` direct callers (W:22235, 27642, 32972, 32988) set up *after* a load or part change, which end in `setSong` / `saveS`. I did not trace all 8 `applyMix` sites to their save; PC4's first task is to list them.
- Not checked: whether Chrome's WebView on the Fold keeps `performance.now()` and the shell's `wallNs` on the same clock (4.7 assumes not).
- The lines `W:8922`, `W:9006`, `W:25xxx` in section 2 and 4.3 mark places I located by reading the neighbours and did not number exactly; PC1 re-checks every cited line against the then-current `beta` before it edits (the branch-count test makes a wrong one fail).
