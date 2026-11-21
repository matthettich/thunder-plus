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
