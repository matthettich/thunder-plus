# Native desktop app + shared C++ core: a plan

Status: **plan only, nothing built yet** (written 2 Nov 2026). `HANDOVER.md` points here (open item 12 and the
"2 Nov" entry). Read this with [`ENGINE-PLUGIN-PLAN.md`](ENGINE-PLUGIN-PLAN.md): that plan ports the synth engine to
C++ for a VST and mobile apps; this one takes the same C++ core further, so the desktop app stops running inside
Electron's browser audio, while the web app keeps working on the phone.

Anyone picking this up: sections 1–3 are the why, 4–6 the design, 7 the phases (each one ships on its own), 8–11 the
rules, risks and open questions. File and line references are to commit `4ef0ea9` (build 355); line numbers drift, so
search for the names given.

---

## 1. What the owner wants (2 Nov 2026)

- **Thunder Plus must keep running on the Android phone** (Galaxy Z Fold 6), as today's web app / PWA.
- **The desktop app should be as functional and stable as it can be.** Most of the real work happens there (Windows
  PC first, MacBooks too).
- **More stability, but less maintenance on both sides.** Not two apps. The owner already tried keeping two codebases
  (…Thunder and …Thunder Plus) and merged them back into one because it was too much work. This plan must not
  bring that back.
- **MIDI and timing stability** as well as audio stability: tight notes, MIDI out, clock out to the MPC / Force, clock
  in, and later Ableton Link.
- Desktop extras wanted eventually: **VST hosting**, **Ableton Link**, a live performance mode, audio tracks.
- Licensing stays as it is: only permissive code in the app, anything with limits kept removable (HANDOVER's
  "Before a commercial release" checklist). The owner leans away from selling, but may change their mind.

## 2. The short answer

**Yes, this plan gets the MIDI and timing stability, fully on desktop and most of the way on the phone.** The reason
is one change underneath everything: the **sequencer moves onto the audio thread**. Today it runs on the page's main
thread, the same thread that draws the screen, so anything that stalls the screen can stall the song (see 3.2).

| | Today (Electron and web) | Web / Android after phases 1–3 | Native desktop (phase 4 on) |
|---|---|---|---|
| Note timing (audio) | booked 250 ms ahead from the UI thread; a long UI stall or a hidden window can make notes late | sample-accurate, inside the audio thread; UI stalls can't touch it | sample-accurate, inside the audio callback |
| Edits / mutes heard | up to ~250 ms later (1.2 s hidden) | next audio block (~3–10 ms) | next audio block (~1–5 ms) |
| Audio driver | browser output (WASAPI shared / CoreAudio via Chromium), no buffer size, no device choice beyond the output picker | same (a browser limit) | ASIO, WASAPI (shared/exclusive), CoreAudio; buffer size, sample rate, device, inputs |
| MIDI out timing | Web MIDI with timestamps; booked from the UI thread through a smoothed clock mapping | better (events come from the audio thread ahead of time) but still sent from the main thread: a very long UI stall can still delay MIDI | from the audio thread's own timeline, sent with OS timestamps; independent of the UI |
| MIDI clock out | booked ~80 ms ahead from the 25 ms pump | as MIDI out | sample-accurate, per-port latency compensation |
| MIDI clock in | averaged from browser timestamps, phase nudged every 16th | same timestamps, applied on the audio thread | OS timestamps, a proper phase-locked follower on the audio thread |
| Ableton Link | not possible in a browser | not possible | native, in-process |
| Plugins (VST3 / CLAP / AU) | no | no | yes (phase 5) |

The phone gets most of the gain without any native code: the sequencer-on-the-audio-thread change (phase 2) is done
**first in the web app**, so the Fold and the current Electron build both get it long before the native desktop app
exists.

## 3. How it works today (what this plan changes)

### 3.1 Pieces

- **`index.html`** (≈26,700 lines, no build step) is the whole app: UI, song model `S`, sequencer, audio graph, MIDI.
  `chains/index.html` and `waves/index.html` are the two modular editors, loaded in frames.
- **Engine block**: `/* ===ENGINE START=== */ … /* ===ENGINE END=== */` (≈ lines 2661–7124). Plain sample-by-sample
  JS (voices, layers, 44 oscillator types, 23 whole-voice engines, pad FX). Runs on the main thread, in the pad-render
  Worker and in `tools/drums/render.mjs`. Inventory in `ENGINE-PLUGIN-PLAN.md` → "What the engine is today".
- **Sequencer**: `makeSeq(S, mode, o)` (≈ lines 16361–16735). **DOM-free**: it reads `S` and a few globals (`NT`,
  `TFX`, `isBusT`…), and `api.step()` returns the next row's events. This is good news: it can move to another thread
  with modest work.
- **The pump**: `pumpTimer()` / `pump()` / `pumpRun()` (≈ line 17900 on). A Worker posts a tick every 25 ms (a Worker
  timer isn't throttled in the background the way a page timer is). Each tick books rows up to **250 ms ahead** of
  `ctx.currentTime` (**1.2 s** while `document.hidden`), and for each event calls `moEv(e)` (MIDI) or `rt.pl.play(e)`
  (sound), then `moClock`, `moLfo`, `moRamp`, `moEcho`, `metRun`.
- **Sound**: rendered pads play as `AudioBufferSourceNode`s started at a future `ctx.currentTime` (re-pitched with
  `playbackRate`, the Koala / SunVox one-shot model). Live engines are AudioWorklets: `thunder-bass`, `thunder-vox`,
  `thunder-${name}` (acid, mach…), `thunder-psyn` (the …pump synths, `pump/synth/`), plus `thunder-tfx` (track effects,
  `chains/dsp/tfx.wasm`), `thunder-chains` / `chains` (`chains/chains-worklet.js`, ≈5,100 lines), `waves`
  (`waves/waves-worklet.js`, ≈4,900 lines), `thunder-master` (`master-worklet.js`) and `thunder-cpu` (the CPU probe).
  The mixer, buses, sends and gain staging (`docs/GAIN-STAGING.md`) are Web Audio gain / splitter / analyser nodes
  around those.
- **C++ already in the app**: `dsp/` (Plaits, Rings, Clouds, Elements, Airwindows, DaisySP, the mods), `dsp/tfx/`,
  `pump/pump.wasm`, `pump/synth/pump-synth.wasm`, `lxr/lxr.wasm`. All built with wasi-sdk / clang to `.wasm`.
- **MIDI** (the `/* ---------- MIDI out and MIDI clock ---------- */` block, ≈ lines 13000–13700):
  - out: `msend(bytes, t, early, p)` converts audio time to `performance.now()` with `a2p(t)` and calls Web MIDI's
    `output.send(bytes, timestamp)`. The mapping (`amap` / `mapUpd()`) comes from `ctx.getOutputTimestamp()`, which
    jitters by a few ms per call, so it is smoothed (2 % per update; a jump > 50 ms is taken at once).
  - clock out: `moClock(s)` books 24 ppqn ticks only ~80 ms ahead (Web MIDI can't take back a booked message, so ticks
    booked further ahead would arrive after a Stop).
  - clock in: `onMidi` → `clkIn(st, ts)`: tempo from the last 25–49 tick timestamps, phase compared every 16th and
    nudged (±3 %, ±8 % right after Start) through `setTempoLive`. SPP both ways. `clkFrom()` locks to one source.
- **Electron shell** (`electron/`, Electron 44): `main.js` serves the app from `app://thunder/`, sets
  `disable-renderer-backgrounding` / `disable-background-timer-throttling` / `disable-backgrounding-occluded-windows`,
  single instance, atomic project saves, `.thunder` file association, "Save changes?" on close, RENDER TO save dialogs,
  a report-only CSP, the GitHub Releases updater (`updater.js`). `preload.js` exposes `window.thunderDesktop.app`,
  `.files`, `.updates` to the page. Built by `.github/workflows/desktop.yml`.

### 3.2 Why timing can slip today

Every note and every MIDI message passes through the UI thread:

1. The 25 ms tick has to land, and `pumpRun` has to get time on the main thread. A long task (a big `draw()`, a
   render landing, garbage collection, a system dialog, the window being hidden or covered) delays it.
2. Booking 250 ms ahead hides stalls shorter than that, at the price of edits and mutes taking up to 250 ms to be
   heard (HANDOVER "31 Oct (14)" raised it to 1.2 s when hidden for the same reason).
3. A stall longer than the look-ahead makes notes late; `pumpRun` then jumps the timeline forward
   (`s.time < ctx.currentTime - 0.5`) rather than playing a burst.
4. MIDI timestamps go through a second clock (`performance.now()`), mapped from the audio clock by a smoothed
   estimate. Good to a few ms, but not locked.
5. The browser picks the audio buffer size from a latency hint (Settings → Audio → Balanced / Safest). Underruns on
   the audio thread (too much DSP in the worklets) are a separate cause of clicks, and Chromium gives no ASIO and no
   real buffer control.

HANDOVER "13 Oct" already named the proper fix: *move the sequencer and note playback into an AudioWorklet, so
nothing on the UI thread can delay a note. That is also the shape a native (C++) engine needs.* This plan is that
fix, carried through to a native desktop app.

## 4. Target architecture

```
                 ┌──────────────────────────────────────────────────────────────┐
                 │  UI: index.html (+ chains/, waves/ editors), ONE copy        │
                 │  screens, editing, song model S, undo, files, skins          │
                 └───────────────┬──────────────────────────────────────────────┘
                                 │  Core bridge (section 5): commands down, events up
                 ┌───────────────┴───────────────┐
      web / Android host                         native desktop host (Windows, Mac, Linux)
      core-web.js                                native/ (C++ app)
      ├ AudioWorklet "thunder-core"              ├ webview showing the same index.html
      │   runs core.wasm                         ├ audio device I/O (ASIO, WASAPI, CoreAudio)
      ├ Worker(s): renders, same core.wasm       ├ MIDI I/O with OS timestamps
      ├ Web MIDI on the main thread              ├ render threads, file I/O, dialogs, updater
      └ files: as today                          ├ Ableton Link, plugin hosting (later)
                                                 └ links core as a static C++ library
                 └───────────────┬───────────────┘
                 ┌───────────────┴──────────────────────────────────────────────┐
                 │  core/ (C++, one copy, no framework types)                   │
                 │  sequencer · player/voices · engines · mixer, buses, sends   │
                 │  track + master FX (tfx, pump, chains, waves) · MIDI out /   │
                 │  clock scheduling · clock-in follower · offline render       │
                 └──────────────────────────────────────────────────────────────┘
                   also linked by: plugin/ (VST3 / AU / CLAP, ENGINE-PLUGIN-PLAN),
                   later android/ (Oboe) and iOS (AUv3)
```

What stays one copy: **the UI, the song file format, the core**. What exists twice: two **thin hosts** (the web one is
a few hundred lines of glue; the native one is an app shell). Features are added once, in the UI and/or the core, and
both hosts get them. The hosts only change when the platform changes.

### 4.1 Rules for `core/`

- Plain C++17/20, no framework types in its API (no JUCE / iPlug2 / Web types), so the same files build for wasm,
  native desktop, the plugin, Android and iOS.
- **Real-time safe on the audio thread**: no allocation, no locks, no file or network I/O, no logging that blocks.
  Fixed-size voice pools; preallocated buffers; lock-free single-producer / single-consumer queues for commands in and
  events out.
- **Song state swap**: the core never edits the song the audio thread is reading. A change builds a new immutable
  snapshot (or patches a copy) on a non-audio thread, then swaps an atomic pointer; the old one is freed later, off the
  audio thread. Small live changes (a mute, a fader, a tempo) go as commands through the queue instead.
- **Everything time-stamped in samples** on one timeline (the audio device's sample clock). Conversions to wall
  clock happen only at the edges (MIDI ports, Link, UI positions).
- Deterministic where it can be (seeded `rng()`, the shared `SIN` table), so parity tests work (see 8).
- Offline render is the same code as live playback, run faster than real time on a worker thread.

## 5. The core bridge (the contract between UI and core)

This is the piece that keeps maintenance down: the UI talks only to `core`, never to `AudioContext`, nodes, Web MIDI
or Electron. Both hosts implement the same messages. Version it (`BRIDGE_V`) and keep a table of it in this repo
(`docs/CORE-BRIDGE.md`, written in phase 1).

**Commands (UI → core)**, JSON unless noted:
- `song.load {song}` (the whole `.thunder` song, as `songOut` writes it); `song.patch {ops}` (small edits: a cell, a
  phrase, an instrument field; same paths as the undo system uses, so undo can drive it).
- `transport.play {mode, row, loop}` / `stop` / `pause` / `resume` / `locate {row}` / `setLoop`; `tempo {bpm}`.
- `live.note {pad|inst, note, vel, on}` (pads, keys, the MIDI keyboard), `live.param {target, value}` (faders, knobs,
  …chains controls, macros), `mute/solo`.
- `render.pad {pad}`, `render.song {range, stems, format}` (offline), `preview {...}`.
- `midi.config {ports, channels, clockOut, clockIn, gearDelay}`; `audio.config {device, rate, buffer}` (native only).
- `fx.*` / `chains.*` / `waves.*` patch messages (the editors keep their own message shapes; they go through the
  bridge instead of straight to their worklets).

**Events (core → UI)**:
- `pos {sampleTime, row, phrase, line, bpm}` at screen rate, with the time it will be **heard**, so the playhead and
  pad blinks line up (today `tp.q` and `padBlink` do this).
- `meters`, `scope`, `cpu`, `xrun` (underrun counts: new and useful on desktop).
- `render.done {id, ...}`, `render.progress`.
- `midi.in` (for MIDI learn and the input monitor), `clock.state {following, bpm, locked}`, `link.state`.
- `error {where, message}` (the core never throws into the void; the UI shows the toast as today).

**Transport of the messages**:
- Web: `AudioWorkletNode.port` (postMessage) for commands and events; renders through the Worker. Meters and scopes at
  30–60 Hz are small enough for postMessage. A `SharedArrayBuffer` ring buffer would be faster but needs
  cross-origin isolation (COOP/COEP headers), which **GitHub Pages can't set**; the existing `sw.js` could add them
  (the "coi-serviceworker" approach), but test that on the Fold before relying on it. Not needed to start.
- Native: the webview's JS ↔ C++ binding (one bound function for commands; the host posts events into the page).
  Large binary data (rendered waveforms, scope buffers, samples) is served from the app's own scheme
  (`app://thunder/core/buf/<id>`) rather than pushed as JSON.

**Capabilities**: `core.caps` tells the UI what this host can do (`nativeAudio`, `asio`, `link`, `pluginHost`,
`audioTracks`, `midiTimestamps`, `multiOut`…). The UI shows or hides features from `caps`, never by sniffing the
platform. That keeps one UI for both.

## 6. Timing design, in detail

### 6.1 Sequencer on the audio thread

- Each audio block (e.g. 128 samples on the web, 32–512 on desktop) the core asks the sequencer for every event whose
  time falls in the block, and voices start at the exact sample offset inside it. No look-ahead is needed for audio.
- Tempo, swing, groove (GRV), LPB, CHASE on mid-song starts, RESUME, loops, block mutes, parts (PRT), BUS FX ONLY, ARP
  and every tracker command move with `makeSeq`, unchanged in behaviour (the event-stream parity test in 8 enforces
  that).
- Edits arrive as `song.patch` and take effect from the next block. The 250 ms / 1.2 s look-ahead and its trade-offs
  disappear.
- The UI's playhead is driven by `pos` events; it no longer drives anything.

### 6.2 MIDI out and clock out

- The sequencer emits MIDI events on the same sample timeline as notes (`moEv`, `moClock`, `moLfo`, `moRamp`, `moEcho`
  logic moves into the core).
- **Native**: the host keeps a precise map from the device's sample clock to the OS host clock, taken in every audio
  callback (the callback's own timestamp plus the device's reported output latency), and smoothed by a delay-locked
  loop (DLL), not by occasional sampling. Each MIDI event is converted to host time, minus the per-port Gear delay,
  and sent:
  - **Mac (CoreMIDI)**: with a future `MIDITimeStamp`; CoreMIDI delivers it on time by itself.
  - **Windows**: through **Windows MIDI Services** where present (Windows 11, newer API with timestamped / scheduled
    messages; verify its scheduling support at build time), else from a dedicated high-priority MIDI thread that
    sends each message when its time comes (WinMM has no scheduling; expect ~1 ms jitter, still far better than
    today's path). A MIDI library that wraps both (e.g. **libremidi**, BSD) saves writing it.
  - Clock out is generated sample-accurately (24 ppqn), so ticks never arrive after a Stop and no 80 ms cap is needed.
- **Web**: Web MIDI isn't available inside an AudioWorklet or a Worker (browser limit). So the core sends MIDI events
  up to the main thread **ahead of time** (≈50–100 ms), each with its exact audio time, and the main thread calls
  `output.send(bytes, timestamp)` as today. The difference from today: the events are computed on the audio thread, so
  their times are exact, and only the hand-off can be delayed by a stall longer than the lead. Keep the existing
  `amap` mapping, fed from the core's own `pos` events.

### 6.3 MIDI clock in (following the MPC / Force)

- Clock bytes are timestamped where they arrive (OS timestamp natively; `MIDIMessageEvent.timeStamp` on the web) and
  passed to the core with that time. The follower runs in the core: a phase-locked loop on the incoming ticks (tempo
  from a filtered period, phase error corrected smoothly), replacing `clkIn`'s average-and-nudge with the same
  behaviour at the edges (Start / Continue / Stop / SPP, `clkFrom` source lock, the 2 s re-lock).
- On native the result is steadier because the timestamps are better (CoreMIDI host time; Windows MIDI Services
  timestamps) and nothing waits on the UI.

### 6.4 Ableton Link (desktop)

- HANDOVER "Plan: Ableton Link on the desktop app (1 Nov)" planned an N-API addon in Electron with ~20 ms timing
  messages to the page. In the native host Link lives **in-process with the core**: the audio callback asks Link for
  the beat at the block's host time (Link's `captureAudioSessionState` is made for exactly this), so phase lock is
  per-block, no bridge messages and no clock mapping. Start/stop sync and quantum work the same way. Much simpler and
  tighter than the Electron version; if Link is wanted **before** the native host exists, the Electron addon plan still
  stands, but it would be thrown away later.
- Licence: GPLv2+ or Ableton's proprietary licence (free, by application). Keep it an optional module (see 9).

## 7. Phases

Each phase ships on its own and leaves the app working on the phone and the desktop. Rough effort is for focused work
with Claude writing most of the code and the owner testing; it is a guess, not a promise, and the engine port (phase
3) is the big uncertain one.

### Phase 0: decisions and test harness (≈1 week)

- Owner decisions: section 11 (framework for the native host above all).
- **Event-stream golden tests**: run `makeSeq` over the factory and demo songs headless (as `tests/` already does with
  `helpers.js`), dump every event `{t, type, track, …}` to `tests/golden/seq-*.json`. Every later phase must produce the
  same stream. This is the safety net for moving the sequencer.
- **Audio golden renders**: extend `tools/drums/render.mjs` (already renders voices offline with the ENGINE block) to
  whole songs, and keep the compare step from `ENGINE-PLUGIN-PLAN.md` → "Making sure they sound the same".
- **Timing measurement rig** (repeatable, so every phase can be compared with today):
  - audio: a song of clicks rendered live vs offline; record the output (loopback) and measure onset error.
  - UI stall test: a debug switch that blocks the main thread for 50 / 200 / 500 ms every few seconds while playing.
  - MIDI out: loop a MIDI out port back to a MIDI in (loopMIDI on Windows, IAC on Mac) and log the arrival jitter of
    notes and clock; with real gear, record the MPC's audio against Thunder's.
  - Write the baseline numbers into this doc (section 12) before phase 1.

### Phase 1: the core boundary, still in JS (≈2–3 weeks)

Goal: the UI talks only to a `core` object; nothing else changes. Shippable, no audible change.

- New `core-web.js` (or a marked block in `index.html`, matching the no-build-step style) implementing the bridge in
  section 5 on top of today's code (`rt.pl`, the worklets, `msend`).
- Move every UI → audio call site behind it: `rt.pl.play` / `stopAll`, worklet `port.postMessage` calls, `ctx.*` uses
  outside the audio setup, `msend` from UI code, previews, renders, the editors' frames talking to their worklets.
  Find them: `grep -n -E "rt\.pl\.|\.port\.postMessage|ctx\.(create|currentTime)|msend\(" index.html`.
- Write `docs/CORE-BRIDGE.md` (the message table, with `BRIDGE_V = 1`).
- Exit: no UI code outside `core-web.js` touches `ctx`, nodes or Web MIDI; all tests pass; golden event streams equal.

### Phase 2: sequencer and playback onto the audio thread, on the web (≈3–5 weeks)

Goal: today's JS sequencer runs inside an AudioWorklet. The phone and the current Electron app both get the timing
fix. Still JS (no C++ yet), so it is quick to iterate.

- One worklet processor, `thunder-core`, holding: the song mirror (from `song.load` / `song.patch`), `makeSeq` and its
  helpers (moved as-is; it is DOM-free), and a **sampler** that plays the rendered pad buffers (today's
  `AudioBufferSourceNode`s can't be created from a worklet, so playback of rendered pads moves into the worklet: buffers
  are transferred to it after each render). Per-track outputs (an `AudioWorkletNode` can have several outputs) feed
  the existing mixer nodes, so the mixer, buses and effects don't move yet.
- Live engines (bass, vox, acid, mach, psyn, chains, waves) are other processors in **the same
  AudioWorkletGlobalScope** (one per AudioContext, shared by every processor in it). A small global event bus in that
  scope lets `thunder-core` hand them time-stamped events directly, with no messaging. Each engine already starts
  notes by time; check that each one accepts an event up to one block early and places it at the right sample.
  (Watch processing order: an event for the current block may land after that engine already ran; schedule ≥1 block
  ahead inside the worklet.)
- Pad renders stay in the Worker (`bgRender`), now sending finished buffers to the worklet.
- MIDI out: the worklet posts MIDI events ahead to the main thread (6.2 "Web"). Clock in: forwarded with timestamps.
- Remove the pump's look-ahead and the hidden-window workarounds once verified; keep the Worker tick only to forward
  MIDI and positions.
- Risks: the Fold's CPU (the worklet now does the sequencing too, but that is cheap next to the DSP); transferring big
  rendered buffers (use transferable `Float32Array`s, never copies on the audio thread).
- Exit: golden event streams equal; the stall test (200 ms blocks) plays notes on time; MIDI loopback jitter measured
  and better than the phase 0 baseline; heard on the PC and the Fold.

### Phase 3: the C++ core (≈2–4 months, the big one)

Goal: `core/` in C++ does everything the audio thread does; the web app runs it as `core.wasm` in the same
`thunder-core` worklet. This is where the plugin plan's port happens, so do it once for both.

Order (each step behind a switch, JS kept as the fallback until its parity test passes, as `ENGINE-PLUGIN-PLAN.md`
already proposes):

1. **Build pipeline**: `core/` + CMake for native, wasi-sdk / clang for wasm (as `dsp/build.sh` does today); a GitHub
   Action that rebuilds and commits `core.wasm` on push, so the app keeps "no build step" for everyone else.
2. **Sequencer** (`makeSeq` and the `mo*` MIDI functions). Easiest to verify: the event streams must be identical.
3. **Sampler / voice player** and the **mixer** (TRIM, …chains insert point, VOL, PAN, mute crossfade, sends PRE/POST,
   buses, master OUT; `docs/GAIN-STAGING.md` is the spec) and the **master chain** (`master-worklet.js`, 342 lines).
   From here the Web Audio mixer graph goes away; `thunder-core` outputs the stereo master (and, natively, more).
4. **Effects**: link the existing C++ (`dsp/tfx`, `pump`, the MI and Airwindows code) into the core directly instead
   of separate wasm modules; port the JS parts around them.
5. **Engines**: the ENGINE block, engine by engine (`ENGINE-PLUGIN-PLAN.md` milestones 1–3: layer types, XENG cores,
   bass, vox, acid, mach, blip, the pump synths). Port the ones the owner uses most first.
6. **…chains and …waves**: their engines are JS around the shared wasm (`chains/chains-worklet.js`,
   `waves/waves-worklet.js`, ≈10,000 lines together); port the patch graph runner to C++. The editors' UIs stay HTML.
   Note `waves/` also comes from the separate `matthettich/waves` repo (`tools/sync-shared.py`): decide whether the C++
   graph lives here and `waves` consumes it.
7. **Offline render** (song, stems, RENDER TO, freeze, resample) runs the same core on a Worker.

- Exit: the web app runs entirely on `core.wasm`; the JS engine and mixer are deleted; parity CI green; the Fold's CPU
  meter no worse than before (wasm should be lighter than the JS it replaces).

### Phase 4: the native desktop host (≈5–8 weeks)

Goal: a native Thunder Plus for Windows and Mac running the same `index.html` in a webview over the native core.
Ship it as a **beta beside the Electron app** until it does everything the Electron app does.

- `native/`: CMake project, linking `core/` as a static library. Stack: see 11.1.
- Everything `electron/main.js` does today, reimplemented (keep the page-side API `window.thunderDesktop.*` the same
  shape, so `index.html` needs no changes for it):
  - serve the app from `app://thunder/` (custom scheme in the webview);
  - single instance; `.thunder` file association, open from Explorer / Finder and on a second launch;
  - project read / atomic save (write `.saving`, rename), Save As, "Save changes?" on close with the 3 s fallback;
  - RENDER TO save dialogs (wav / zip / mid), "show in folder";
  - external links open in the browser; the window never navigates away; the CSP;
  - the updater from GitHub Releases (WinSparkle on Windows, Sparkle on Mac, both MIT, fed an appcast generated by the
    release workflow; or keep the current GitHub-API check in `updater.js` rewritten in C++);
  - the Mac menu (⌘C / ⌘V / ⌘Q), no menu on Windows.
- **New Settings → Audio (desktop)** when `caps.nativeAudio`: driver (ASIO / WASAPI shared / WASAPI exclusive /
  CoreAudio), device, inputs and outputs, sample rate, buffer size, an underrun counter. Settings → MIDI lists the OS
  MIDI ports through the bridge (not Web MIDI inside the webview).
- Webview engines: **WebView2** (Chromium) on Windows, so the UI renders as it does in Electron; **WKWebView** (Safari)
  on Mac: test every screen there, especially pointer / keyboard handling, `contenteditable`, fonts and canvas.
- CI: extend `.github/workflows/desktop.yml` with CMake jobs (Windows x64, Mac universal; Linux optional), artefacts
  on the same GitHub Release as Electron's during the beta. Signing: Mac notarisation needs the Apple Developer Program
  (already noted in `ENGINE-PLUGIN-PLAN.md`); Windows code signing optional (SmartScreen warning without it).
- Exit: a parity checklist (every Electron feature above, plus the HANDOVER test list) ticked on the PC and a Mac; the
  owner has made music on it for a while.

### Phase 5: desktop-only features (as wanted, each on its own)

- **Ableton Link** (6.4), in-process.
- **Plugin hosting**: VST3 (SDK MIT since 3.8), CLAP (MIT), AU on Mac. Start in-process; move plugins to a separate
  process later for crash isolation (a crashing plugin then can't take the song down). Plugin delay compensation in
  the mixer. Plugin state saved in the song (base64 + plugin id). **On the phone** a track with a plugin can't play it:
  the file keeps the data untouched, the track shows "desktop plugin", and the owner can freeze / render it to a slot
  on desktop so it plays everywhere (same idea as rendering LXR pads before removing `lxr/`).
- **Audio tracks** (HANDOVER "deliberately left for later … better with the native port"), audio input, multi-out.
- Live performance mode benefits from the tighter timing but doesn't depend on it.

### Phase 6: retire Electron

- When the native app has passed its checklist and the owner is happy: stop building Electron (`electron/` kept in
  history), point the Releases page and auto-update at the native builds. Installed Electron copies get one last
  update that tells them where the new app is.

### Later: native Android (optional)

- The web app on the Fold already gets phases 1–3. If it still isn't enough (latency, MIDI over USB), the same host
  pattern works on Android: a small Kotlin app with an Android WebView showing `index.html`, **Oboe** for low-latency
  audio, the **AMidi** NDK API for MIDI, `core/` built for arm64. It is the native desktop host's twin; the
  `ENGINE-PLUGIN-PLAN.md` Android app (the synth alone) could be the first use of it.

## 8. Keeping both sides the same (the maintenance rules)

- **One UI.** No desktop-only copies of screens. Desktop-only features are gated by `core.caps`, mobile layout by the
  existing `body.mob` / `applyPlatform()`.
- **One core.** No JS copy of anything once its C++ version passes parity; delete the JS.
- **One song format.** Same `.thunder` / JSON as today (`songOut` / `songX`). Data a host can't use (a plugin's state)
  is carried through untouched, never dropped.
- **Bridge versioned.** A change to a message bumps `BRIDGE_V` and updates `docs/CORE-BRIDGE.md`; both hosts move
  together (the web host is in the same repo, so this is one commit).
- **Parity in CI**: golden event streams (sequencer), audio renders (engines, effects, mixer) compared below
  audibility (the −80 dB idea in `ENGINE-PLUGIN-PLAN.md`), wasm vs native builds of the core compared too (…pump already
  does this in its own repo: `matthettich/pump` → `wasm/compare.cpp` + `compare.mjs`; HANDOVER "…pump" entries).
- **Release flow unchanged** for the owner: pushing to `main` updates the web app (and `core.wasm` via the Action);
  desktop builds when the owner decides (HANDOVER "Desktop releases").

## 9. Licensing (stays inside the existing checklist)

- Core and hosts: Thunder's own code plus permissive libraries only.
- Candidates and their licences (verify at the time):
  - Webview: **CHOC** (ISC, includes a webview wrapper and a JS binding) or **webview/webview** (MIT).
  - Audio I/O: **RtAudio** (MIT-style; WASAPI, CoreAudio, ALSA, ASIO with the ASIO SDK) or **miniaudio** (public
    domain / MIT-0; no ASIO).
  - MIDI: **libremidi** (BSD) or **RtMidi** (MIT-style).
  - Plugin hosting: **VST3 SDK** (MIT since 3.8; the VST name and logo have Steinberg's own terms), **CLAP** (MIT),
    Apple's AudioUnit API.
  - Updater: **WinSparkle** / **Sparkle** (MIT).
- **Limited licences, kept optional like `lxr/`** (own folder, build flag, every hook guarded, a line in HANDOVER's
  "Before a commercial release" item 1 / 6):
  - **ASIO SDK**: GPLv3 or Steinberg's proprietary licence. A closed build needs the proprietary agreement. Build flag
    `THUNDER_ASIO`; without it Windows uses WASAPI (exclusive mode gets close to ASIO latency on most interfaces).
  - **Ableton Link**: GPLv2+ or Ableton's proprietary licence (free, by application). Build flag `THUNDER_LINK`.
- **JUCE** (if chosen instead, see 11.1): AGPLv3 or a commercial JUCE licence (tiers by revenue; check the current
  EULA, a JUCE 9 EULA exists). A closed product needs the commercial one. The plugin plan already prefers iPlug2 for
  the same reason.

## 10. Risks and how to handle them

| Risk | Mitigation |
|---|---|
| The engine port (phase 3) is long and can stall | Port engine by engine behind switches; the app never depends on a half-ported engine; the order follows what the owner uses |
| Sound changes during the port | Parity CI; engines switch only when they pass; owner listens before each switch |
| WKWebView on Mac behaves differently from Chromium | Test every screen on Mac in phase 4; Windows (the main machine) gets WebView2, which is Chromium |
| Webview UI performance | The same as Electron (same HTML, same kind of engine); moving audio off the UI thread makes UI hitches harmless |
| Web MIDI stays main-thread on the phone | Lead time for MIDI events; native Android later if the owner needs more |
| `SharedArrayBuffer` blocked on GitHub Pages | Don't depend on it; postMessage is enough; the service-worker COOP/COEP trick is optional |
| Two release tracks during the beta | Native beta on the same Release; Electron retired as soon as the checklist passes |
| Windows MIDI jitter with old drivers | Windows MIDI Services where available; dedicated MIDI thread otherwise; measure with the loopback rig |
| Scope creep (plugins, audio tracks) delaying stability | Phase 5 only after phase 4's checklist; stability first, as the owner asked (Nov 2026: stability and usability next) |

## 11. Decisions for the owner

### 11.1 Native host framework (decide in phase 0)

- **A. Permissive libraries assembled** (CHOC or webview/webview + RtAudio + libremidi + VST3/CLAP SDKs + Sparkle):
  fits the licensing rule, no fees, small binary; more glue to write and test (device handling, plugin hosting quirks).
  **Suggested**, because it keeps the "nothing with limits in the app" rule.
- **B. JUCE 8/9**: the most battle-tested audio device, MIDI and plugin-hosting layer, with a web-view UI and JS
  bridge built in; less glue, fewer surprises with plugins. Costs: AGPL or a commercial licence for a closed product.
  Reasonable if the owner decides Thunder Plus will never be sold, or buys the licence.
- **C. Tauri (Rust)** with the C++ core over FFI: also reaches Android, but adds a third language. Not suggested.

### 11.2 Others

- Start phase 2 (sequencer on the audio thread) right away, before deciding anything else? Suggested: **yes**, it helps
  the phone and the current desktop app and is needed for every later step.
- Link before the native host (Electron addon, thrown away later) or wait for phase 5 native Link? Suggested: wait,
  unless it is needed for a gig or a record soon.
- Mac: buy the Apple Developer membership when the native beta starts (signing, notarisation, auto-update).
- Which engines matter most (port order in phase 3): same question as `ENGINE-PLUGIN-PLAN.md`.
- Linux native build: keep (cheap in CI) or drop?

## 12. Measurements (fill in as phases land)

| Measure | Baseline (build 355) | After phase 2 | After phase 3 | Native |
|---|---|---|---|---|
| Note onset error, idle | | | | |
| Note onset error, 200 ms UI stalls | | | | |
| Edit-to-heard delay | ~250 ms (1.2 s hidden) | | | |
| MIDI note jitter (loopback) | | | | |
| MIDI clock jitter (loopback) | | | | |
| Clock-in lock time / drift vs MPC | | | | |
| Fold CPU, demo song | | | | |
| Underruns per hour, demo song | | | | |

## 13. Where to start (next session)

1. Owner answers 11.1 (can wait until phase 4) and 11.2's first question.
2. Phase 0: the golden event-stream test and the timing rig; record the baseline in section 12.
3. Phase 1: `core-web.js` and `docs/CORE-BRIDGE.md`.
