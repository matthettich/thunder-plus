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
| `meters.want` | `{rate, tracks, fx:[…], spec:bool}` | per-node `lvl` / `dyn` / `meter` subscriptions | The page asks only for what the showing screen draws |
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


## 7. BRIDGE_V 1 additions for N2 (24 Nov 2026, the lead on Opus; DRAFT until the lead's top-model read, then frozen)

All ADDITIVE (unknown types are ignored and logged once; reply fields are only added), so BRIDGE_V stays 1. Source: `docs/port/N2-PAGE-CLIENT.md` section 3 (gaps G1..G14), which has the page side and file:line for each.
Adopted as proposed there:
- **G1** `hello` reply adds `sr`, `outLat` (frames), `host` (`'android'`, `'web'`, `'android-n0'`), `maxPadBytes`, `maxSongBytes`.
- **G2** `put` is the contract's form only (`put(id, {sr, ch, frames}, Float32Array[])`); the client carries a small adapter for the N0 shim (`host === 'android-n0'`).
- **G3** new `pads.commit {n}` (shell: `thunder_live_pads_publish`); the existing `pads` event says the batch is audible.
- **G4** `buf.use` with `kind:'var'` adds `ii`, `vk`; new `pad.chop {pad, loopSecs, slices:[frames...]}` (shell: `var_set`, `pad_chop`, `pad_hits`).
- **G5a** new `fx.plan {tr, slots, bpm}` (shell: `fx_plan` then `fx_commit` at NOW); `fx.set {tr, slot, p:[12]}` as section 3. **G5b** new `master.config {mst, safety}`. **G5c** new `chain.edit {ops}` (shell: `thunder_live_chain_edit`; all or nothing; an `err` event on refusal).
- **G8** new `pdc.set {mode: 0|1|2}`. **G9** `clock` adds `load` (callback time over period, measured by the shell). **G13** new `engine.reset {}` (shell: stop, panic, destroy, create; the page re-sends song, kit and pads on `{t:'engine', up:true}`).
- **G14** `song.load` may be sent with `ask`; reply `{ok, rev, reasons:[...], warn:[...]}`; `ok:false` is the fallback verdict (the shell's `SongFit`). No reply within 5 s: treated as ok.
Deferred (not in N2; the page shows nothing or falls back): **G6** FX / chains / master / TRIM meters, **G7** spectrum, **G10** `note.on` / `note.off` (hand-played sound stays on the page's own AudioContext in N2), **G11** `lxp` (a song using it falls back), **G12** `play` from a step (RESUME restarts the row).
- **Added from H1c (#315), same status:** a shell event `{t:'device', reason:'noisy'|'focus'|'stream', stopped:true, sr?, resume?}` (headphones pulled, audio focus lost, output stream rebuilt: the page shows its transport stopped); `song.load` may carry `title` (the notification's song name; else the song's name unless it is "Song N", else the file name). The page must not set `navigator.mediaSession` while the native engine plays (one media notification, the shell's).
- **Added from H2 (#316), same status:** `midi.ports` (shell -> page, after hello, on every hot-plug and every 3 s): `{devs, ins:[{id, name, port}], ports:[four, with name], late, sent}` (`ins` and `ports[].name` extend the U36-SPEC sketch); `midi.in` (shell -> page, channel messages, Clock / Start / Continue / Stop, SPP; Active Sensing, Reset, SysEx, MTC dropped); `midi.cfg` (page -> shell: the four port letters, the clock boxes, Gear delay clamped to -50..200 ms; `clockIn` has no effect natively: MIDI-clock follow stays a fallback condition); `hello` reply `caps.midi`.
- **Added from H3 (#317), same status:** files (the page keeps `uri` where the desktop keeps `projPath`; the shell moves bytes and never parses a song): requests `file.folders`, `file.pick`, `file.forget`, `file.list {kind, dir?}` (songs newest first; samples folders first, then audio by name), `file.open {uri | dialog}`, `file.read` (samples as base64, cap 64 MiB), `file.save {uri, text, force?}` (a file changed underneath answers `changed:true` and writes nothing; `readOnly` -> `file.saveAs`; the last 3 versions kept privately), `file.saveAs {name, text, dialog?}`, `file.ready`; event `file.opened` (held until `file.ready`); `hello` caps `files:true`. Full table: `docs/port/N2-NOTES.md` section H3.
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
