# The core bridge, version 1 (draft for N0 to prove)

Written 10 Nov 2026 by the lead. The contract between the interface (`index.html` and the two editors, in a web view) and
the C++ core. It replaces every boundary listed in [`BRIDGE-TODAY.md`](BRIDGE-TODAY.md) sections 1–3 and the audio / MIDI
parts of 4 and 6. Design rules are in `../NATIVE-DESKTOP-PLAN.md` sections 4.1 and 5; this file is the message list.
**`BRIDGE_V = 1`, frozen 15 Nov 2026 (tasks B1 and B2, the lead) for N1 steps 3 to 10.** One point stays open and blocks nothing (section 6): the Android bulk path's timings. A change to anything else now needs a new `BRIDGE_V`.

## 1. What is different from today (from the inventory)

- **The mixer becomes messages.** Today mute, fader, pan, sends and TRIM are direct Web Audio parameter writes
  (`rt.pl.*`); the core has no wire for them. New: `mix.set`.
- **One owner of the event stream.** Today the core plays what it can (`core:true`) and the page plays the rest in the
  same pass. In a native shell there is no Web Audio at all: everything the core cannot play yet is silent or uses its
  rendered buffer. The split survives only in the web copy during the port.
- **No reaching into other processors.** Today's core drives tfx / chains / master by object access inside the audio
  scope (`__tcNodes`, `__ov`, `__q`). In C++ these are function calls inside the core: no messages.
- **Renders go through the core.** Song renders run on the main thread in an `OfflineAudioContext` today; the render
  Worker has no cancel. New: `render.*` with progress and cancel.
- **Meters are one batched message**, not one stream per node (tfx `lvl`, chains `meter`, master `meter`, analysers).
- **Editors' own engines go.** …chains and …waves stop owning an AudioContext; auditioning in the editors plays through
  the core (`note.on` with a target).

## 2. Transport (the page sees one object on every platform)

```js
window.thunderCore = {
  v: 1,                                   // BRIDGE_V
  send(msg),                              // small JSON-able command, no reply
  ask(msg) -> Promise,                    // request / reply (id and timeout added by the bridge: 5 s, 30 s for device opens)
  put(id, {sr, ch, frames}, Float32Array[]) -> Promise,   // bulk audio in; resolves when the core holds it
  take(id) -> Promise<{sr, ch, frames, data: Float32Array[]}>,   // bulk audio out (renders, recordings)
  on(type, fn)                            // events up
};
```

| Host | `send` / `on` | `put` / `take` |
|---|---|---|
| Web copy | the `thunder-core` worklet's `port` (core.wasm) | transferables, as today |
| Android | a `WebMessagePort` pair to the Kotlin shell, JNI to the core | N0 decides: WebMessage `ArrayBuffer`, else a file in the app's cache the core maps |
| Windows / Mac | the web view's message channel (WebView2 `postMessage` / WKScriptMessageHandler) | shared buffer object (WebView2) / file fallback |

Rules: messages are plain objects `{t: 'type', ...}`; times are **frames on the core's sample timeline** (`f`), never
seconds or `performance.now()`; unknown types are ignored and logged once; every `on` event carries `f` when it refers
to a moment. The core never blocks on the page. `put` chunks are at most 1 MB.

## 3. Commands down

| `t` | Fields | Replaces today | Notes |
|---|---|---|---|
| `hello` (ask) | `{v, app}` → `{v, core, caps:{nativeAudio, midi, link, plugins, inputs, render, engines:[…], fx:[…]}}` | `CORE_REV` check | `caps.engines` / `caps.fx` list what this core build can play: the page shows "rendered" or "bypassed" for the rest |
| `song.load` | `{S, vars, rev}` | core `load` | Whole song snapshot; the core builds a new immutable copy off the audio thread and swaps |
| `song.patch` | `{ops, rev}` | core `patch` (key / index / len / del diff) | Same diff format as today: keep `core.mir` on the page |
| `kit.load` / `kit.patch` | `{kit, rev}` / `{ops, rev}` | `load` (kit part), page-side kit | Voice parameters; sample data travels by `put` |
| `buf.use` | `{id, pad, layer, kind:'pad'|'var'|'sample'|'ir'|'wt'|'sf'}` | core `buf` / `vbuf` / `sf` | Binds a buffer already sent with `put`; `buf.free {id}` releases it |
| `play` | `{mode, row, at?}` | `play` | `at` in frames for synced starts (Link, clock) |
| `stop` | `{at?}` | `stop {at}` | Clears booked MIDI and tails per the STOP rules |
| `loop`, `bpm`, `lead`, `hold`, `perf`, `lxp` | as today | same names | Small live commands |
| `mix.set` | `{tr, k:'vol'|'trim'|'pan'|'mute'|'solo'|'send'|'out', i?, v, ramp?}` | direct `rt.pl.*` writes | **New.** `ramp` in frames (the mute crossfade) |
| `fx.set` | `{tr, slot, p, v}` / `{tr, slot, on}` | tfx `{pset}`, master `{set}`, chains `param` | One message for track, bus, master, pump and chains parameters; target `{tr:'m'}` is the master |
| `note.on` / `note.off` | `{pad|tr, note, vel, target?:'waves'|'chains'}` | page voices, editors' own engines | Live playing (keys, pads, MIDI thru) |
| `meters.want` | `{rate, fx:[…], dyn:[…], chains:[…], master:bool}` (as built: section 7.2 item 2; the draft's `tracks` and `spec` do not exist) | per-node `lvl` / `dyn` / `meter` subscriptions | The page asks only for what the showing screen draws |
| `midi.cfg` | `{outs:[…], clockOut, clockIn, gearDelay}` | Web MIDI + helper config | Read as arrays of four (`outs:[id,id,id,id]`, `clockOut:[bool x4]`, `gearDelay:[ms x4]`; lead's ruling 5.5 of `U36-SPEC.md`, 20 Nov 2026): the page's four letters and its Gear delay per port. `BRIDGE_V` stays 1. The shell feeds `thunder_midiout_*` (`midiport.h`); the new upward event `midi.ports` is additive (U36 PR B / the shells) |
| `dev.list` (ask) / `dev.open` (ask) | `{}` → `[{id,name,ins,outs,rates}]` / `{out, in?, sr, buf}` → `{sr, buf, latIn, latOut}` | helper `devices` / `open`, `setSinkId` | Native hosts only (`caps.nativeAudio`) |
| `rec.arm` / `rec.start` / `rec.stop` | `{ins:[…], sync:'free'|'pattern'}` | recorder worklet, helper input ring | Result fetched with `take` |
| `render.start` (ask) / `render.cancel` | `{what:'pad'|'song'|'stems'|'phrase', …, id}` → `{id}` | render Worker, `OfflineAudioContext` | Result by `take(id)`; pad renders by the page's Worker remain allowed until N3 |
| `link.set` | `{on, tempo?, quantum?}` | Electron Link channels | `caps.link` |

## 4. Events up

| `t` | Fields | Rate | Replaces |
|---|---|---|---|
| `clock` | `{f, wallNs, sr, outLat, under}` | 10 /s | `a2p` / `amap`, `outputLatency`, playback report clock lines |
| `pos` | `{f, row, pat, step}` | 30 /s while playing | playhead from core `rows` |
| `rows` | `{f, evs:[…]}` | per tracker row | Kept for the web copy while the page still plays part of the stream; native shells need it only for UI flashes |
| `meters` | `{f, tr:Float32Array, m:[l,r,pk], fx:{…}, spec?}` | as asked, at most 30 /s | all meter streams, analysers |
| `fx.lvl` | `{seq, f, tr, a:[8]}` | every 12th FxHost block: 31.25 /s at 48 kHz (28.7 /s at 44.1 kHz), only for a watched track | the effects worklet's `{lvl}` (FX bar slot meters); section 7.2 |
| `fx.dyn` | `{seq, f, tr, d:[[slot, type, red, in, out], …]}` | as `fx.lvl`, only when the watched track has a dynamics slot | the worklet's `{dyn}` (FX dynamics, stage bars); section 7.2 |
| `ch.lvl` | `{seq, f, tr, in:[l,r], out:[l,r]}` | 16 /s by default (15.6 /s measured in whole quanta at 48 kHz), one event per watched track that has a chains node | page analysers on the chains node (`peaks` / `peaksIn`); section 7.2 |
| `master.lvl` | `{seq, f, inPk, outPk, tp, inLu, outLu, comp, compIn, lim, load, sup}` | the mastering processor's own cadence: every 19 quanta, 19.7 /s at 48 kHz | the master worklet's `{meter}` (`mstMeter`); section 7.2 |
| `midi.in` | `{f, wallNs, port, bytes}` | per event | helper `midiIn`, Web MIDI |
| `dev` | `{kind:'changed'|'lost'|'reopened', …}` | rare | `devicechange` |
| `render` | `{id, done, of}` / `{id, ok}` / `{id, err}` | 4 /s | Worker `done` / `fail` |
| `rec` | `{pk, frames}` | 15 /s | recorder `pk` |
| `link` | `{peers, tempo, beat, f}` | 10 /s | `link-time` (50 /s today) |
| `err` | `{where, msg, fatal}` | rare | watchdog, failed effects (`failed` → bypass) |

## 5. Not on the bridge

Files, dialogs, the updater, window chrome, file associations, wake lock, notifications: these stay on
`window.thunderDesktop.*` with today's shape (BRIDGE-TODAY section 4), implemented per shell. Storage (localStorage
and IndexedDB keys in BRIDGE-TODAY section 6) stays in the web view; native shells add the songs folder through
`thunderDesktop`.

## 6. B1: the draft reviewed against N0's results (15 Nov 2026), and what B2 froze

N0 passed on the owner's Fold (`native/tests/RESULTS-N0.md`): the clock held in all three runs with no underrun, the page
loaded in the Android web view, and commands and events crossed through the core's two rings. What that settles:

1. **Time is frames, and a command can name its frame.** Any command in section 3 may carry `at` (a frame on the core's
   timeline, as `clock` reports it). It takes effect on exactly that sample, wherever in a block it falls; without `at`, on
   the first sample of the next block; an `at` already past is applied at once. Commands for the same frame apply in the
   order they were sent. Built and tested in U05 (`native/tests/test_runtime.cpp`). The page converts a wall-clock moment
   to a frame from the last `clock` event (`f`, `wallNs`, `sr`); N0 measured that pair steady to 1 ppm.
2. **Latency classes.** N0 measured callbacks arriving in bunches (longest gap 33 to 41 ms) and 26 ms of output latency
   with the screen on. So: `note.on` / `note.off` and `mix.set` never wait for a snapshot; they are small commands and
   land on the next block (open point 4 of the draft: closed, no separate class is needed). `song.load`, `kit.load` and
   `buf.use` are snapshots: built off the audio thread, swapped at a block's start, and each is acknowledged.
3. **Acknowledgements (new in v1).** Event `song {rev, f}`: the audio thread reads snapshot `rev` from frame `f`. The page
   must not assume a `song.load` / `song.patch` is audible before its `song` event; `kit` and `buf` follow the same
   pattern when U06 / U08 land (`kit {rev, f}`, `buf {id, f}`). When several snapshots arrive between two blocks only the
   newest is acknowledged.
4. **`song.patch` keeps today's diff format** (key / index / len / del, `core.mir` on the page): it exists and is tested
   (draft point 2: closed).
5. **Block size.** The host may ask for any number of frames; the core works in pieces of at most `max_block`. The host
   sets flush-to-zero or not as it likes: the core sets it for the length of its callback and restores the host's mode.
6. **Overload is counted, never blocking.** A full command ring makes `send` fail on the control side (the shell retries
   on its next tick and reports `err {where:'cmd', fatal:false}` after one second). A full event ring drops the event;
   `clock` carries on, so the page's clock mapping survives. More than 64 timed commands waiting: the extra one is applied
   at once and counted.
7. **Bulk path on Android (draft point 1): chosen, timings still to come.** `WEB_MESSAGE_ARRAY_BUFFER` is supported on the
   owner's phone (Android 17, WebView 157), so `put` / `take` use ArrayBuffer messages in 1 MB chunks (path a2); the
   base64-to-file path stays as the fallback for a web view without that feature. **Open:** the A4 timings on the phone
   (not run in N0). They decide only whether a 5-minute sample is sent in one go or behind a progress bar, not the API.
8. **`meters` layout (draft point 3): fixed by U13, 15 Nov.** `{t:"meters", seq, at, a:[70 floats]}` at 30 a second.
   `a[2s]` is the peak and `a[2s+1]` the RMS of slot `s`: slots 0 to 31 the tracks (a 512-sample window of the mono mix
   (L+R)/2, as the page's analysers), 32 the preview bus, 33 master left, 34 master right (1024 samples per channel).
   Decay, holds and floors stay with the page. The FX bar's per-slot peaks come with U14; the master processor's own
   reduction meters are a separate small message.

Exactness (decided in U05, recorded here because every later unit depends on it): the core is built without fused
multiply-add and uses its own `exp` (fdlibm's, as V8), so values the sequencer and the automation compute are the same
bits as the web copy's on every platform. `pow` and `sin` join `js_exp` in the first unit whose golden needs them exact.


## 7. BRIDGE_V 1 additions for N2 (24 Nov 2026, the lead on Opus; FROZEN 25 Nov 2026 after the lead's top-model read)

All ADDITIVE (unknown types are ignored and logged once; reply fields are only added), so BRIDGE_V stays 1. Source: `docs/port/N2-PAGE-CLIENT.md` section 3 (gaps G1..G14), which has the page side and file:line for each.
Adopted as proposed there:
- **G1** `hello` reply adds `sr`, `outLat` (frames), `host` (`'android'`, `'web'`, `'android-n0'`), `maxPadBytes`, `maxSongBytes`.
- **G2** `put` is the contract's form only (`put(id, {sr, ch, frames}, Float32Array[])`); the client carries a small adapter for the N0 shim (`host === 'android-n0'`).
- **G3** new `pads.commit {n}` (shell: `thunder_live_pads_publish`); the existing `pads` event says the batch is audible.
- **G4** `buf.use` with `kind:'var'` adds `ii`, `vk`; new `pad.chop {pad, loopSecs, slices:[frames...]}` (shell: `var_set`, `pad_chop`, `pad_hits`).
- **G5a** new `fx.plan {tr, slots, bpm}` (shell: `fx_plan` then `fx_commit` at NOW); `fx.set {tr, slot, p:[12]}` as section 3. **G5b** new `master.config {mst, safety}`. **G5c** new `chain.edit {ops}` (shell: `thunder_live_chain_edit`; all or nothing; an `err` event on refusal).
- **G8** new `pdc.set {mode: 0|1|2}`. **G9** `clock` adds `load` (callback time over period, measured by the shell). **G13** new `engine.reset {}` (shell: stop, panic, destroy, create; the page re-sends song, kit and pads on `{t:'engine', up:true}`).
- **G14** `song.load` may be sent with `ask`; reply `{ok, rev, reasons:[...], warn:[...]}`; `ok:false` is the fallback verdict (the shell's `SongFit`). No reply within 5 s: treated as NOT ok (reason `no-verdict`): the old engine plays the song (the lead's ruling, 25 Nov: a missing verdict must never put a song the core may not fit on the new engine).
Deferred (not in N2; the page shows nothing or falls back): **G6** FX / chains / master / TRIM meters, **G7** spectrum, **G10** `note.on` / `note.off` (hand-played sound stays on the page's own AudioContext in N2), **G11** `lxp` (a song using it falls back), **G12** `play` from a step (RESUME restarts the row).
- **Added from H1c (#315), same status:** a shell event `{t:'device', reason:'noisy'|'focus'|'stream', stopped:true, sr?, resume?}` (headphones pulled, audio focus lost, output stream rebuilt: the page shows its transport stopped); `song.load` may carry `title` (the notification's song name; else the song's name unless it is "Song N", else the file name). The page must not set `navigator.mediaSession` while the native engine plays (one media notification, the shell's).
- **Added from H2 (#316), same status:** `midi.ports` (shell -> page, after hello, on every hot-plug and every 3 s): `{devs, ins:[{id, name, port}], ports:[four, with name], late, sent}` (`ins` and `ports[].name` extend the U36-SPEC sketch); `midi.in` (shell -> page, channel messages, Clock / Start / Continue / Stop, SPP; Active Sensing, Reset, SysEx, MTC dropped); `midi.cfg` (page -> shell: the four port letters, the clock boxes, Gear delay clamped to -50..200 ms; `clockIn` has no effect natively: MIDI-clock follow stays a fallback condition); `hello` reply `caps.midi`.
- **Added from H3 (#317), same status:** files (the page keeps `uri` where the desktop keeps `projPath`; the shell moves bytes and never parses a song): requests `file.folders`, `file.pick`, `file.forget`, `file.list {kind, dir?}` (songs newest first; samples folders first, then audio by name), `file.open {uri | dialog}`, `file.read` (samples as base64, cap 16 MiB; songs cap 16 MiB), `file.save {uri, text, force?}` (a file changed underneath answers `changed:true` and writes nothing; `readOnly` -> `file.saveAs`; the last 3 versions kept privately), `file.saveAs {name, text, dialog?}`, `file.ready`; event `file.opened` (held until `file.ready`); `hello` caps `files:true`. Full table: `docs/port/N2-NOTES.md` section H3.
- **Added from the verify-n2 fixes (#313), same status:** the `song.load` reply may be `{ok:false, pending:true, reasons:['no-kit']}` when no kit has been loaded yet: no verdict; the shell sends a `fit` event with the real verdict after `kit.load`, and the page waits for it (or asks again). SongFit reasons now include `resample`, `pump-in-chain`, `module`, `lxp`, `clock-follow`. (N2-PAGE-CLIENT section 4.6 must handle `pending`.)


### 7.1 Additions and clarifications from PC1..PC3 (25 Nov 2026)

Section 7 above stays FROZEN as written. This sub-section is additive only: BRIDGE_V stays 1, nothing above is changed or removed, and where a line here is more exact than section 7 it adds to it. Each item was checked against the page client on `beta` (`thunder-core-client.js`, `docs/plans/PC1-NOTES.md`, `PC2-NOTES.md`, `PC3-NOTES.md`, `PC23-NOTES.md`) and the shell on `n1/n2-anr` (`LiveBridge.kt`, `BridgeTranslator.kt`, `live_jni.cpp`).

1. **`hello` carries `live: true`** (page -> shell: `{t:'hello', v, live:true, app}`). The shell starts the live engine, and the page switches to it, only with this field; a `hello` without it is the N0 test core (`LiveBridge.hello`, `BridgeTranslator` `"hello"`). The client turns on only when the reply has `v === 1`, `caps.nativeAudio` and a usable `sr`. The reply also carries `live: true`, `chains`, `sf`, `engines` and `caps` (`midi`, `files`).
2. **`kit.load` is `{kit, rev}`.** `kit` is the page's kit JSON (`kitToJSON(true)`: voices, held, slots, waves; no sample data) and `rev` a counter the client bumps; it is sent only when the voice list changed (and after an engine rebuild). The shell renders the pads it can from it. It answers with a `{t:'kit', ok, rc, err, pads, skipped, dry, renderMs, empty:[slots]}` event (`empty` = the slots the page must `put`) and then a `fit` event (item 9). The client no longer acts on `empty`: after a real `kit.load` it sends every pad again (PC23 F3).
3. **`buf.use {kind:'pad', frames:0}` removes the pad.** No `put` is needed (`{t:'buf.use', id:-1, pad, layer:0, kind:'pad', frames:0}`; `BufUse.frames` defaults to -1 = a normal take). Shell commit c872f73 (`nativePadRemove`; live.h `pad_set` with `frames == 0`). Like a take, the removal is audible at `pads.commit`.
4. **`buf.free {id}` after every `buf.use`.** `PutStore.take` does not release the buffer, so without the free the 256 MB store fills. `put` ids are small integers (a counter), not strings. `pads.commit` force-sweeps unfinished puts, so the client commits only with an idle queue, one commit 40 ms after the last `buf.use`.
5. **`pad.chop` `slices` are FRAMES at the engine rate** (`NC.sr`, from `hello` / `engine up`; the shell divides by its rate), not seconds; `loopSecs` stays seconds. `n` (slice count) is sent, clamped 2..64 by the shell; `loopSecs: 0` with `slices: []` says the pad no longer chops. A negative or non-numeric point is refused.
6. **`song.load` is an `ask`** `{t:'song.load', title, S, vars, rev}` plus the ask's `id`. **Key order matters:** the shell reads `t` and `title` from the head of the text and `rev` and `id` from the tail (`idOf`, `revOf`; it never parses the song), so a client must keep `S` and `vars` in the middle. The whole song goes each time (never `song.patch` from this client), 250 ms after the last edit.
7. **The reply is `{ok, pending, rev, reasons, warn}`.** `ok:false` with `pending:false` is the verdict "fall back" (reasons are readable texts); `warn` is non-fatal facts. The shell's `fit` event carries the same verdict (`{t:'fit', rev, ok, pending, reasonTexts, warn}`).
8. **`pending` means no kit has reached the shell yet. It is not a verdict.** The reply is `{ok:false, pending:true, reasons:['no-kit']}`; the shell sends a `fit` event with the real verdict after `kit.load`. The client waits for that event for 5 s.
9. **No verdict is NOT ok.** A reply that does not come within 5 s, or a `pending` that no `fit` settles in 5 s, gives the verdict `{ok:false, reasons:['no-verdict']}` (this reverses the draft's "treated as ok" in G14; `docs/port/N2-PAGE-CLIENT.md` is corrected). A `no-verdict` is never "current": the client asks again (a new `song.load`) at PLAY, and a later `fit` for the same `rev` replaces it.
10. **Event `{t:'engine', up}`.** `up:false` while the shell rebuilds the engine (`engine.reset`, or a stream reopen that failed): a playing song has stopped, the page sends nothing until `up:true`. `up:true` carries `sr` (the new rate): the page sends the song, kit and pads again (`engine.reset`, G13).
11. **Event `{t:'device', reason, stopped, sr?}`.** `reason` is `'noisy'` (headphones pulled), `'focus'` (audio focus lost) or `'stream'` (output rebuilt); `stopped:true` = the shell has stopped playing, so the page shows its transport stopped and says why; `sr` is the new output rate when it changed (the page re-renders its pads at it). This is the H1c event of section 7 with its fields as built.


### 7.2 Effect, chains and master meters, `fx.values` and `recovery.put` (26 Nov 2026, from M1..M4 and PP, PC6, M5B, M5C as built)

Section 7 and 7.1 stay as written. This sub-section is additive only: BRIDGE_V stays 1 (METERS-DESIGN 5a ruling 8; every addition is a new message type, a new field or a new cap, and an old shell or page ignores what it does not know). It closes **G6** for the FX, chains and master meters (the TRIM light, the stereo focus pair, the spectrum and `master.resetPeak` are NOT here: see "Known gaps"). It was written from the code and the notes: core `live.h`, `live_host.cpp`, `live_api.cpp` (M1 #329 merged; M3 #334, M2 #336, M4 #332 not merged when this was written), shell `live_events.cpp`, `BridgeTranslator.kt`, `LiveBridge.kt`, `Recovery.kt`, and the page's `thunder-core-client.js` on `beta` (builds 680 to 683). Where the notes differ from the code, the code is followed here (list: `M6-NOTES.md`).

1. **`hello` reply caps.** Three new booleans in `caps`: `meters2` (the core has `thunder_live_meters_set` and the four records below; from the core's `THUNDER_LIVE_CAP_METERS2`, 0x400, set in every build), `fxValues` (the core has `thunder_live_fx_values`; from `THUNDER_LIVE_CAP_FXVALUES`, 0x800), `recoveryPut` (the shell stores `recovery.put`; always `true` from the live shell). The page asks for nothing from a group whose cap is missing: without `meters2` no `meters.want` is sent and the panels stay empty as before; without `fxValues` no `fx.values`; without `recoveryPut` no `recovery.put`. `meters2` covers all four meter events (no separate bit for chains or master).

2. **`meters.want {rate, fx, dyn, chains, master}`: the whole subscription, a REPLACEMENT state** (page -> shell, no reply). What is not named is switched off, so the page may re-send the full state at any time and it must after `engine {up:true}`. Exact rules (`BridgeTranslator`):
   - `rate` is required (a number; else the message is refused). It is clamped to 0..30 (below 0 is 0, above 30 is 30); 0 switches the 70-float `meters` event off. It drives only that event: the new events below are not paced by it.
   - **Rate-only form.** If none of the keys `fx`, `dyn`, `chains`, `master`, `trim` is present, the shell calls `thunder_live_meters_want(rate)` and nothing else changes (the old behaviour; the shell's own 10 Hz at `hello`). If any of the five is present the message is the whole state (`thunder_live_meters_set`): a list or `master` that is absent or `null` is OFF.
   - **`fx` and `dyn`: arrays of tracks.** Each entry is a number 0..31 (a track) or 32 or `"m"` (the master's effects); anything else refuses the message. `fx` non-empty switches `fx.lvl` on, `dyn` non-empty switches `fx.dyn` on. **The two lists are folded into ONE track mask** (their union), so `fx:[3], dyn:[5]` posts both events for both 3 and 5. The page always sends the same single track in both. A list that is not an array refuses the message. The number is read as an integer (3.7 is 3).
   - **`chains`: an array of tracks 0..31 with its OWN mask** (the chains screen's track need not be the FX bar's). 32 or `"m"` refuses the message (there is no chains node on the master). Non-empty switches `ch.lvl` on. A boolean (the old page's form) is accepted and ignored: it names no track. Any other type refuses the message.
   - **`master`: a boolean.** `true` switches `master.lvl` on; `false`, `null` or absent is off; any other type refuses the message. There is no track mask: the master is one.
   - **`trim`** is read as a key (it selects the whole-state form) and its value is ignored until the TRIM light exists.
   - There is no cap on the number of watched tracks in the core or the shell (ruling 3); the page client subscribes ONE track in `fx` / `dyn` at a time (a new one replaces the old) and the tracks that the chains patch's Track In / Chain Out modules name (usually 1 to 4) in `chains`.
   - **Refusals and errors.** A refused message changes nothing and answers with an `err` event `{where:'meters.want', msg}` (and `{ok:false, error}` as a reply if it carried an `id`). A core error (`E_ARG`: a bad rate or bit; `E_STATE`: no meters in this build; `E_BUSY`: the command ring is full) is an `err` event too; nothing is retried by the shell, the page re-sends its state on its next change.
   - **What the page sends (`thunder-core-client.js`).** One message with the whole state, 100 ms after the last change of anything it depends on (debounced), and again after `engine {up:true}`. The wish goes out only while a native PLAY runs, `meterHold()` (the page's METERS: OFF while playing) is false and `caps.meters2` is set; otherwise the message carries `fx:[]`, `dyn:[]`, `chains:[]`, `master:false`, so the core does no metering work. `rate` is 10 (the rate the shell asked for at `hello`). `chains` is asked at every look (the 100 ms timer and the 500 ms tick), so leaving the chains screen turns it off within about 0.6 s. Nothing is sent before the first wish unless forced (the shell's own `hello` state stands).

3. **Event `fx.lvl {seq, f, tr, a:[8]}`.** `tr` is 0..31 or 32 (the master's effects). `a[k]` is the **peak out of slot k, linear** (max of the absolute values of L and R) since the previous post; 0 for a slot that did not report, was off all the time, or does not exist (the core's `slots` bit mask is not forwarded). The page maps it to -54..0 dB itself. A value that is not finite, negative or above 1e30 is sent as 0. `f` is the engine frame at the end of the quantum that posted it; `seq` is the core record's own. Rate: the FxHost's own post, every 12th 128-frame block (1536 frames): 31.25 a second at 48 kHz, 28.7 at 44.1 kHz. **Age limit on the page: 400 ms** (a reading older than that draws as 0; the page stamps arrival time itself and ignores `f`). Only for a watched track; an unwatched track computes and posts nothing.

4. **Event `fx.dyn {seq, f, tr, d:[[slot, type, red, in, out], ...]}`.** The same cadence and watching as `fx.lvl`; posted only when the watched track has a dynamics slot (the level-only types 7, 26, 28 and the pump COMP, LIMIT and BAND slots), 1..8 entries. `slot` is 0..7; **`type` is the page's type string** (`"comp"`, `"pcomp"`, a MODFX key; from the core's `thunder_fx_type_name`, which turns the core's `int16` type id into it): the page drops an entry whose type is not the type of its own slot, which covers a plan swapped a moment ago. The three numbers are **dB, the worklet's own**: `red` is gain reduction (<= 0; 0 for the level-only types), `in` the level in, `out` the peak out; **-180 means silence**; a non-finite `red` is sent as 0 and a non-finite `in` / `out` as -180. Three decimals. An entry whose type has no name or whose slot is outside 0..7 is left out of the list. The page feeds the entries to its `fxDynIn` and keeps its own 80-point history; no age stamp applies to it.

5. **Event `ch.lvl {seq, f, tr, in:[l,r], out:[l,r]}`.** One event per watched track that has a chains node (a watched track without a node posts nothing). `tr` is 0..31. All four numbers are **linear peaks**: `in` is what the chains node received (after the mixer's TRIM), `out` what came out (before the track's effects, fader and pan); each is the running maximum since the previous `ch.lvl` of that track, so no peak is lost between posts; the first after watching starts covers one quantum. Rate: 16 a second by default (the page's 60 ms chains timer); the core's `chains_hz` (0 means 16, at most 30) is not on the bridge, so the rate is fixed. It is paced in whole 128-frame quanta, so at 48 kHz it comes every 24 quanta, 15.6 a second. The page draws one mono bar per track as `max(l, r)`. **Age limit on the page: 400 ms.** Only `peaks` and `peaksIn` reach the chains screen: `mods`, `shows`, `jacks`, `byp` and `load` stay dark in native mode (METERS-DESIGN 5a rulings 1 and 2, phase 2).

6. **Event `master.lvl {seq, f, inPk, outPk, tp, inLu, outLu, comp, compIn, lim, load, sup}`**, the field names of the master worklet's `e.data.meter` (the page's `mstMeter`). Units: `inPk`, `outPk`, `tp` **linear** (peaks since the last message; `tp` is the true-peak read-out, held since the metering was switched on, because nothing resets it); `inLu`, `outLu` short-term loudness in LUFS, **-180 = none yet or silence**; `comp` compressor gain reduction in dB (<= 0), `compIn` the compressor's detector level in dB, **-200 when the compressor is off** (a different number from -180 on purpose, as the worklet); `lim` limiter gain reduction in dB (<= 0, 0 when off); `load` the mastering processor's share of real time (0..1+, a half-second average, wall clock: not reproducible); `sup` the suppressor's 48 band reductions in dB (20 Hz to 20 kHz, log spaced) or `null` when the suppressor is off. A non-finite number is sent as 0 (loudness -180, `compIn` -200). Three decimals for dB. Rate: the processor's own, one message per `round(sr / 128 / 20)` quanta: every 19 quanta, 19.7 a second at 48 kHz; `rate` does not pace it. It comes from the MASTERING processor only (the song's `mst`), never the safety limiter (no `lim2`), and **nothing is posted while the mastering worklet is not in the path** (the song's `mst` off, or no stage on), as the page's own worklet does not exist then. The page ignores `inPk` / `outPk`, cleans every number, and **draws a record older than 600 ms as a silent one** (zeros and dashes, the limiter history keeps scrolling). Switching it on does not allocate or reset a filter on the audio thread: the meter state always exists and a flag turns the work on; the cost measured on the CI Linux machine is about +1.2 to +1.6 microseconds per 128-frame quantum (0.05 to 0.06 % of a core; `M2-NOTES.md`, not measured on the Fold).

7. **Core records (`live.h`, packed, little-endian, 8-byte aligned, a 16-byte head first; types 1..10 were taken), all on the big (non-critical) ring: a full ring drops the NEWEST and counts it (`evt_dropped`), as for `METERS`.** Posted in step 9 of the quantum in this order: `METERS`, FXLVL / FXDYN, CHLVL, MASTERM; `f` is the frame at the end of that quantum.

   | Type | Name | Bytes | Body after the head |
   |---|---|---|---|
   | 11 | `FXLVL` | 56 | `int32 track` (0..31, 32 the master's effects), `uint32 slots` (bit k: the chain has slot k), `float lvl[8]` |
   | 12 | `FXDYN` | 152 | `int16 track`, `int16 n` (1..8), `int32 reserved`, then 8 entries of 16 bytes `{int16 slot, int16 type, float red_db, in_db, out_db}`, the unused tail zero |
   | 13 | `CHLVL` | 40 | `int32 track` (0..31), `uint32 reserved`, `float in[2]`, `float out[2]` |
   | 14 | `MASTERM` | 248 | `float in_pk, out_pk, tp, in_lu, out_lu, comp, comp_in, lim, load`, `uint32 flags` (bit 0: `sup[]` present), `float sup[48]` |

   (**Not what METERS-DESIGN 4.1 sketched:** `CHLVL` is one record per track with L and R, 40 bytes, not a 280-byte record of all 32 tracks; `TRIMLVL` 15 and `FXPTEL` 16 do not exist, and the numbers 15 and 16 stay free.) `fx.lvl`, `fx.dyn`, `ch.lvl` and `master.lvl` are the shell's JSON for 11, 12, 13 and 14.

8. **`thunder_live_meters_set(L, const thunder_live_meters_spec*)`** (core API, CONTROL; the shell's `meters.want` maps to it). `thunder_live_meters_spec` is **40 bytes**: `uint32 size`, `uint32 kinds` (`THUNDER_LIVE_MK_FXLVL` 1, `FXDYN` 2, `CHLVL` 4, `MASTERM` 8), `double rate_hz`, `uint64 fx_tracks` (bit t 0..31 a track's effects, bit 32 the master's), `double chains_hz`, `uint64 chains_tracks` (bit t 0..31). **ABI size rule:** the caller sets `size` to the size of the struct it was built with and the core copies at most that many bytes and zero-fills the rest, so a caller built with the 32-byte struct (before `chains_tracks` was added) watches no chains track and one built with an older struct still works; a LARGER struct is accepted only if every byte beyond ours is zero. `E_ARG`: `NULL`, `size` below 4, a non-zero byte past our struct, unknown `kinds` bits, `fx_tracks` above bit 32, `chains_tracks` above bit 31, a `rate_hz` or `chains_hz` that is NaN, negative or above 30. `E_STATE` without `THUNDER_LIVE_F_METERS`; without `F_EFFECTS` the FX kinds post nothing, without `F_CHAINS` no CHLVL, without `F_MASTER` no MASTERM. `E_BUSY` when the command ring is full: nothing changed. It is a NEW command (`MetersSet`), applied at the top of the next quantum; `thunder_live_meters_want` still works alone, and `rate_hz` of the spec drives `METERS` exactly as it does (0 is off): of the two calls the later one wins for the `METERS` rate. A re-sent identical spec is a no-op (only the hosts whose watching changes are switched, so the 12-block window is not restarted). A host that becomes watched has an old unposted post dropped, so its first record is fresh. With nothing watched the cost is zero: no pops, no peaks.

9. **`fx.values {tr, slot, kind:'pump'|'mod', v:[numbers], at?}` (an ASK)**, page -> shell, with `hello.caps.fxValues`. The slot's WHOLE value list at once (`thunder_live_fx_values`, PP): for a pump slot the plugin's own parameter list in its own order (`pumpPv`: 32 values for WIDE, 277 for EQ), for a modular slot the module's controls (`TFX[type].map`, doubles, at most 16). `tr` is 0..31, 32 or `"m"`; `slot` 0..7; `at` as `fx.set` (a frame; absent is now). **The shell refuses before the core** a missing or bad `tr`, a `slot` outside 0..7, a `kind` other than `pump` or `mod`, `v` missing, empty or longer than **FXV_MAX = 512**, or a value that is not a finite number. **The core refuses** (`E_ARG`): a slot that is not a hosted pump / modular slot of that kind in the LAST PLANNED chain; a count that is not the planned one; a non-finite value (a pump value is cut to float32 first and must stay finite); and the values that ride in the song: the EQ's `phase` and `lin_quality` changed from the planned value, and **any** value for an EQ planned in linear phase (changing them allocates). `E_STATE` without `F_EFFECTS` (the shell words `E_UNSUPPORTED` too); `E_BUSY` when the core's ring of 16 is full. A refusal leaves the audio untouched. **Replies** (with the ask's `id`): `{ok:true}`; `{ok:false, busy:true, error:'busy'}` for `E_BUSY` (the page sends that slot's list again at its next tick); `{ok:false, error:'rc N (...)'}` for any other core error; `{ok:false, error}` for a message the shell refused and `{ok:false, error:'engine down'}` while the engine is down. Without an `id` a refusal or core error is an `err` event `{where:'fx.values', msg}` instead (the page always asks). The page sends only while a native PLAY runs, from its live diff (last value wins, one task per 16 ms); a slot that the core refused is marked and rides in the song until the next accepted song, a new `fx.plan` for that track, or an engine restart (no ask storm for a linear-phase EQ drag); a missing reply in 2 s counts as a refusal. Rides in the song, not here: a slot's on / off and type (`fx.plan`), COMP / VOX external sidechain, a song's own FXV lanes for the slot.

10. **`recovery.put {project}`** (page -> shell, `send`, NO reply), with `hello.caps.recoveryPut`. `project` is the page's own project file text (`{app:'thunder-project', v:1, build, song, kit}`, with kit sample audio). The page puts it about every 10 s while the song is unsaved and the text changed, never while a recovery copy awaits the person's answer, and never above **24,000,000 characters** (`RCV_MAX`); the shell applies the same cap (`PUT_MAX_CHARS`, 24,000,000 UTF-16 characters) and ignores a put that is not a string, is empty or is over it; the older copy then stays. It is written at once on the shell's recovery thread (never the main or audio thread), **atomically** (temp file then rename, the sidecar last) to `recovery.put.thunder` and `recovery.put.json` in the app's files folder, beside the `song.load` copy (`recovery.thunder`, which is still written only on swipe-away and is never overwritten by a put). **`recovery.get {id}`** (ask) returns the put text when a put exists, else the `song.load` copy: `{ok, title, time, file, source:'put'|'song.load', text}`; `source` is informational, the page decides by the text itself (`app:'thunder-project'` is a full restore; the `song.load` copy lacks the kit, presets and impulse responses and the page says so). The `recovery` event (after each `hello` reply, once, when a copy exists) uses the put's title / time / file when there is a put. **`recovery.drop {id}`** (ask) deletes both copies and answers `{ok:true}`. **The save-drops-put rule:** a successful `file.save` or `file.saveAs` also deletes the put (it is older than the save); the swipe-away writer is unchanged.

**Known gaps (all deliberate, none blocks the beta):**
- **No `master.resetPeak`.** Nothing calls the processor's reset live, so `tp` (the PEAK readout) only grows within one watching (it is zeroed each time `master:true` switches on); the page's click on the PEAK readout does nothing natively. A small follow-up command if the owner wants it.
- **LUFS after the master panel opens (decided at the lead's read of M2, 25 Nov).** Switching the metering on resets the two loudness windows (measurement only; the audio does not read them), so IN / OUT LUFS read none (`-180`) for the first 100 ms and then build over 3 s; nothing of an older part of the song is shown. `chains_hz` below 1 is taken as 1.
- **Undo-to-clean leaves the put.** If the person undoes back to a clean state without saving, an old put stays on disk and is offered at the next start (the page does not tell the shell; it only stops sending).
- **Union mask.** `fx` and `dyn` share one track mask (item 2): harmless for the page's one-track rule.
- **Not metered, by ruling:** the chains `mods`, `shows`, `jacks`, `byp` and `load` (phase 2); the pump METER / LIMIT loudness read-outs (`FXPTEL`, phase 2); the TRIM light and the stereo focus-track pair (M7, optional); the safety limiter's reduction; the preview bus's effects.

## 8. H7 additions (desktop audio and MIDI, additive)

`dev.list`, `dev.open`, `rec.arm`, `rec.start`, `rec.stop`, `take` (+ `take.chunk` events), `take.free`, `rt.reset`, `rt.snapshot`, and the `dev`, `rec`, `rec.done`, `midi.ports`, `midi.in` events are real in the desktop shell (they are no longer refused with H7). `hello.caps.midi`, `inputs`, `devices` and `rt` are truthful. Field-level detail: `H7-NOTES.md`.
