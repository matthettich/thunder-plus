# Native I/O helper for the desktop app: a plan

Status: **plan only, nothing built yet** (written 7 Nov 2026, build 484). This is option 2 of
[`NATIVE-DESKTOP-PLAN.md`](NATIVE-DESKTOP-PLAN.md) section 14.2, made concrete. `HANDOVER.md` points here.

Why now (owner, 7 Nov): "we are going to need to write that helper sooner rather than later- my interface doesn't have an
internal mixer". The recorder only ever gets inputs 1 and 2 of the owner's interface, because Chromium (Electron 44)
hands `getUserMedia` two channels whatever the device has, and the interface has no mixer software to route other
inputs onto 1/2. There is no workaround inside the page.

Anyone picking this up: section 1 is the goal and the limits, 2 the shape, 3 the pieces, 4 clocks, 5 phases, 6 build
and licences, 7 tests, 8 risks, 9 decisions for the owner. File and function names are as of build 484; search for them.

---

## 1. Goal and limits

- **Phase A (needed now): every input channel of the interface in Record a sample.** Pick any input, or any pair, as
  the take's left / right, with the monitor, the meter, PATTERN sync and the editor all working as today.
- **Phase B (next): MIDI out (and in) through the helper**: messages that can be taken back on STOP (no OUTBOX
  workaround), sent from a high-priority thread, timestamps from the OS.
- **Phase C (only if needed): audio output through the helper.** The biggest timing win, but the hardest part; the full
  native host (`NATIVE-DESKTOP-PLAN.md` phase 4) may be the better place for it.
- **Unchanged:** the phone (the web app never sees any of this), the sequencer, the engines, the song format. A desktop
  build without the helper, or where it fails to load, works exactly as today (`getUserMedia` and Web MIDI).
- **Licences:** permissive only in the app, as everywhere (HANDOVER "Before a commercial release"). No JUCE.

## 2. Shape

```
 page (renderer, sandboxed)            main process                 utility process "thunder-io"
 ───────────────────────────           ────────────                 ───────────────────────────
 recorder / MIDI code                  starts the utility,          loads io/thunder_io.node
   │  window.thunderDesktop.io         hands both ends a            (miniaudio + libremidi)
   │  (preload.js)                     MessagePort, then            ├─ audio input thread (OS callback)
   ▼                                   stays out of the way         │    → interleaved Float32 chunks
 MessagePort  ◀──────────── audio chunks, device list, MIDI in ──── ├─ MIDI out thread (scheduled queue)
 MessagePort  ─────────────── open / close / MIDI out / clear ────▶ └─ MIDI in → timestamped messages
   │
   ▼
 AudioWorklet 'thunder-io-in' (a jitter ring) → today's recorder worklet / monitor / meter
```

- **Why a utility process, not the main process (as Link) or the page.** The page is sandboxed
  (`webPreferences.sandbox: true`, `electron/main.js`), so it can't load an addon, and that should stay. Audio through
  the main process would sit behind every IPC call and the menu, the updater and file saves. Electron's
  `utilityProcess.fork()` runs Node on its own, can load the addon, and talks to the page over a `MessagePort` that
  never passes through the main process once handed over: transferable `ArrayBuffer`s, no copies, no main-process hops.
- **How the port reaches the page.** Main: `const { port1, port2 } = new MessageChannelMain()`; `port1` to the utility
  (`child.postMessage({op:'port'}, [port1])`), `port2` to the window (`win.webContents.postMessage('thunder:io-port',
  null, [port2])`). Preload: `ipcRenderer.on('thunder:io-port', e => window.postMessage({thunderIo:true}, '*',
  e.ports))`. The page takes it from `message` with `e.ports[0]`. (The standard Electron pattern for a direct
  renderer ↔ utility channel with `contextIsolation` on.)
- **The addon has no JS callbacks on the audio thread.** The OS callback copies into a lock-free ring (single
  producer, single consumer); a Node-side poll (every 5 ms, `setInterval` in the utility process, which has no UI to
  stall it) drains the ring into chunks and posts them. Same "no threads into JS" rule as `link/`.

## 3. The pieces

### 3.1 `io/` (the addon, C++17, plain N-API like `link/`)

- `io/src/thunder_io.cc`: N-API surface, all calls cheap and non-blocking:
  - `devices()` → `[{id, name, inputs, outputs, rates:[…], default:{in, out}}]` (miniaudio's context enumerate; the
    full channel count from `ma_device_info` native data formats).
  - `openInput(id, {rate, block})` → `{ok, rate, channels, block, latencyFrames}`; opens **all** of the device's input
    channels (the page picks which to keep, so changing the pair never reopens the device or re-prompts anything).
  - `read()` → `null` or `{frame0, hostNs, channels, frames, data:Float32Array}` (everything in the ring since the last
    read, interleaved; `frame0` counts from open; `hostNs` the OS's time for that first frame, see 4).
  - `closeInput()`.
  - Phase B: `midiPorts()`, `midiOpenOut(id)`, `midiSend(port, bytes, atNs)`, `midiClear(port)`, `midiOpenIn(id)`,
    `midiRead()` → `[{port, bytes, hostNs}]`.
  - `now()` → the OS clock in ns (the same clock as `hostNs`), for mapping (4).
- `io/src/ring.h`: the SPSC float ring (power-of-two size, ~2 s of 32 channels at 96 kHz ≈ 24 MB is too much: size it
  to the opened channel count, 1 s, and report overruns as a counter).
- Libraries, vendored as single files, no system installs: **miniaudio** (audio devices: CoreAudio, WASAPI, ALSA;
  MIT-0 or public domain) and **libremidi** (MIDI: CoreMIDI, WinMM / WinUWP, ALSA; BSD-2-Clause). Both permissive.
- `io/build.js`: as `link/build.js` (node-gyp, Mac arm64 + x86_64 joined with `lipo`), output to
  `electron/io/thunder_io.node`, `asarUnpack` it as Link's.

### 3.2 `electron/io-helper.js` (the utility process) and `electron/main.js`

- The utility process: `require` the addon in a try (missing or failing → it reports `{available:false, why}` and
  exits; the page falls back to `getUserMedia`). Handles port messages: `devices`, `open`, `close`, later the MIDI ones.
  While an input is open: every 5 ms `read()` and post `{op:'in', frame0, hostNs, channels, frames, data}` with
  `data.buffer` transferred.
- Main: fork it lazily, the first time the page asks (`thunder:io-start`), so a session that never records never runs
  it. Restart once if it dies (`exit` event), then report "not available". Kill it with the window.
- macOS microphone permission: before the first `openInput`, main calls
  `systemPreferences.askForMediaAccess('microphone')` (the Info.plist already has `NSMicrophoneUsageDescription`). The
  repeat-prompt problem of unsigned builds remains until the app is signed (9).

### 3.3 The page

- `rcdOpen()` gets a second path, chosen when `window.thunderDesktop && thunderDesktop.io` is there and the helper
  answers: no `getUserMedia`, no `MediaStreamSource`. Instead an **`AudioWorkletNode('thunder-io-in')`** with
  `numberOfOutputs: 1, outputChannelCount: [2]` takes the port's chunks (forwarded from the page's port to the worklet's
  `port`, transferring the buffers again), keeps a jitter ring of ~30 ms, and outputs the two picked channels. It
  connects where `RCD.src` / the merger connect today (`rcdRoute`), so the recorder worklet, monitor, meter and take
  code are untouched. `RCD.nch` comes from the device's real channel count, so INPUTS (`rcdInOpts`) lists every pair
  and channel.
- The INPUT list (`rcdInputs`) shows the helper's devices (names and channel counts) instead of `enumerateDevices`.
- **Latency** for PATTERN sync (`rcdLat`): the helper path knows its parts exactly (the device's input latency from
  `latencyFrames`, the jitter ring's fill), so `RCD.inLat` is set from those instead of `getSettings().latency`.
  Measure… (`midiLatRun`) keeps working unchanged: it records through `rcdOpen` either way.
- INPUT ENGINE: AUTO (helper when available) / BROWSER (always `getUserMedia`), so the old path is one click away if the
  helper misbehaves. **In Record a sample, beside Input / Inputs** (owner, 7 Nov: engine and inputs apart was confusing);
  A0 put it in Settings › Audio, A1.4 moves it.

## 4. Clocks

Three clocks meet: the interface's sample clock (input), the output device's sample clock (Web Audio's
`currentTime`), and the OS clock (`hostNs`; `performance.timeOrigin + performance.now()` on the page).

- **Placing input frames on the audio clock.** Each chunk carries `hostNs` for its first frame. The page already maps
  the OS clock to the audio clock (`amap` / `mapUpd`, from `getOutputTimestamp`). Frame `f` of the input arrived at
  `hostNs(frame0) + (f − frame0) / rate` → audio time through `p2a`. The worklet writes captured frames into the
  recorder at that audio time instead of "now", so a take lines up to the pattern without guessing the input latency.
- **Drift.** The interface and the output run on separate crystals (tens of ppm: a few ms over 5 minutes). For takes:
  cut by timestamps (above), no resampling needed (a 5-minute take ends a few ms long or short). For monitoring: the
  jitter ring drops or repeats a frame when it drifts past ±10 ms (inaudible on a monitor; never on the take).
- **Same device in and out** (one interface): still two clocks as far as the browser knows, so the same code; drift is
  then near zero.
- **Phase B (MIDI out):** the page converts audio time to OS time with `a2p` plus `moFxLat()` and the Gear delay as
  today (`msend`), and hands `atNs` to the helper. The helper's thread sends at that OS time (CoreMIDI and WinMM take
  timestamps; libremidi exposes them) and `midiClear` drops everything queued on STOP: OUTBOX can go on the helper path.

## 5. Phases (each ships on its own)

1. **A0, scaffolding (≈2–3 days).** `io/` with miniaudio and the device list only; `io/build.js`; the utility
   process; the port into the page; `desktop.yml` builds it (continue-on-error, like Link); Settings › Audio shows
   "Input engine: native (Name, 18 inputs)" or why not. Tests: a fake addon (`tests/io.test.js`, as `link.test.js`).
2. **A1, input (≈1–1.5 weeks).** `openInput` / `read` / `closeInput`, the ring, the `thunder-io-in` worklet, the
   `rcdOpen` path, INPUTS with every channel, timestamps into the recorder, INPUT ENGINE in Settings. Owner tests on
   the interface: every input, a PATTERN take against the click (Measure… through the helper), a 5-minute take.
3. **A2, polish (≈2–3 days).** Device unplugged mid-take (the take so far is kept, a toast), sample-rate mismatch
   between the interface and the engine (resample in the worklet, or ask to match), Windows (WASAPI) and Linux check.
4. **B, MIDI (≈1 week). Built 7 Nov (build 498, HANDOVER item 5); waiting for the owner's test.** libremidi out and in, `msend` and the clock-in path on the helper when present, STOP clears,
   Settings › MIDI lists the helper's ports (port letters unchanged in songs).
5. **C, output (decide later).** Only if MIDI vs. audio timing is still not right after B: the master's audio to the
   helper means a page → utility audio stream (≈20–40 ms extra latency through ports), which is likely worse than the
   problem; this is where `NATIVE-DESKTOP-PLAN.md` phase 4 (a native host) takes over instead.

Rough total for what's needed now (A0–A2): **2–3 weeks** of sessions; B another week.

## 6. Build, packaging, licences

- `io/` beside `link/`; `node io/build.js` in `desktop.yml` before the app build, `continue-on-error: true`; the
  addon in `electron/io/`, `asarUnpack`ed.
- miniaudio (MIT-0 / Unlicense) and libremidi (BSD-2-Clause): add both to `THIRD_PARTY_NOTICES.md` and the
  "Before a commercial release" checklist (permissive, keep the notices). **No ASIO** in phase A: Windows uses WASAPI
  (ASIO's SDK licence is the open question in `NATIVE-DESKTOP-PLAN.md` section 9, item 7). Most interfaces work with
  WASAPI; add ASIO only if the owner's Windows setup needs it.
- macOS: the utility process inherits the app's Info.plist and entitlements. Hardened runtime and notarisation need the
  Apple Developer membership (9).

## 7. Tests

- `tests/io.test.js`: the utility process against a fake addon (scripted chunks with timestamps): chunks reach the page
  port in order, gaps and overruns are reported, close stops the poll.
- `tools/io-check.mjs` (headless Chrome): the `thunder-io-in` worklet fed fake chunks with known timestamps: a PATTERN
  take starts on the frame the timestamps say (to the frame), pair / single-channel picks, drift handling in the
  monitor ring (±10 ms) without touching the take.
- miniaudio's null backend in CI for an end-to-end smoke test of the real addon (open, read silence, close).
- On the owner's interface (no CI can): every input, a take against Measure's click, a long take, unplug mid-take.

## 8. Risks

- **macOS permission prompts** keep coming back on an unsigned build (the 7 Nov symptom with named outputs). The
  helper asks once per launch at worst; signing fixes it for good (9).
- **Utility process audio scheduling.** Chunk posting is not real-time; the page-side jitter ring (30 ms) absorbs it.
  If the owner's machine shows gaps, raise the ring or move the poll to a dedicated thread in the addon that posts via
  `napi_threadsafe_function` (allowed in the utility process; still no callbacks on the audio thread itself).
- **Two input paths to keep working.** Keep the BROWSER engine as the fallback and the phone's path; the recorder code
  above the source node stays shared.
- **Electron upgrades.** Plain N-API (no electron-rebuild), as Link: an Electron update doesn't break the addon.

## 9. Decisions for the owner

**Answers (7 Nov, 17:51):** 1. go ahead with phase A now, MIDI after. 2. Apple Developer membership: considering, waiting
for now (the repeat microphone prompts stay until then). 3. Windows: WASAPI only, unless ASIO can be used without paying.
4. The interface: **Cranborne Audio 500R8**, USB, presents **28 inputs** (8 from its 500-series slots, 2 S/PDIF, 16 ADAT,
2 from its analogue summing mixer's output; Sound On Sound's review), with **two 8-channel lightpipe preamps** on the ADAT
ins. No mixer software (its mixing is the hardware summing mixer), so inputs can't be routed onto 1/2: phase A's reason.
ADAT gives 16 channels only at 44.1 / 48 kHz (8 with S/MUX at 88.2 / 96), so A1 is checked at 48 kHz with all 28. The owner
also asked whether the build could be delegated to cheaper models with this session verifying (see HANDOVER 7 Nov (15)).
Also from the owner: the right MIDI Gear delay was "a combination of both latency numbers" (Measure's result and Settings ›
Audio's heard ms): MIDI may still be missing the output latency on the Mac; to check with numbers before changing `msend`.


1. **Go ahead with phase A (inputs) now**, B (MIDI) after? Suggested: yes, A first; B once A is stable.
2. **Apple Developer membership** (US$99 / year) to sign and notarise the Mac build: stops the repeated microphone
   prompts and Gatekeeper warnings, and is needed for the native host later anyway. Suggested: yes, before A1 ships.
3. **Windows: WASAPI only for now**, ASIO later if your Windows interface needs it? Suggested: yes.
4. **The interface's details** (make / model, how many inputs, the sample rate you run it at), so A1 can be checked
   against it.
