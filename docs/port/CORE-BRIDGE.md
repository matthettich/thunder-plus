# The core bridge, version 1 (draft for N0 to prove)

Written 10 Nov 2026 by the lead. The contract between the interface (`index.html` and the two editors, in a web view) and
the C++ core. It replaces every boundary listed in [`BRIDGE-TODAY.md`](BRIDGE-TODAY.md) sections 1–3 and the audio / MIDI
parts of 4 and 6. Design rules are in `../NATIVE-DESKTOP-PLAN.md` sections 4.1 and 5; this file is the message list.
**Draft:** N0 proves the transport and the bulk path; a message may change until task B2 freezes `BRIDGE_V = 1`.

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
| `midi.cfg` | `{outs:[…], clockOut, clockIn, gearDelay}` | Web MIDI + helper config | |
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

## 6. Open points for N0 and B2

1. The bulk path on Android (N0 measures both options with a 5-minute stereo sample, 53 MB of float data).
2. Whether `song.patch` keeps today's diff or moves to the undo format NATIVE-DESKTOP-PLAN assumed (suggested: keep today's; it exists and is tested).
3. `meters` as one `Float32Array` layout: fix the layout in B2 so the UI indexes without parsing.
4. Whether `note.on` for the editors needs its own latency class (auditioning should not wait for a snapshot swap).
