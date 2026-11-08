# io/: native audio and MIDI I/O for the desktop app (optional)

A native Node addon, `thunder_io.node`, that opens audio interfaces (and, later, MIDI ports) for the desktop app. The main
process starts a utility process (`electron/io-helper.js`), and that process loads the addon when present. Plan and design: [docs/NATIVE-IO-HELPER-PLAN.md](../docs/NATIVE-IO-HELPER-PLAN.md).
Licences: [NOTICE.md](NOTICE.md). Vendored versions: [VERSIONS](VERSIONS).

```
node io/build.js          # → electron/io/thunder_io.node (+ licences, NOTICE.md); Mac: arm64 + x86_64 joined
```

Needs a C++17 compiler (Xcode / MSVC 2022 / GCC 10+) and node-gyp (npm's own copy is used). Plain N-API, so one build
loads in Electron whatever its version (no electron-rebuild). `.github/workflows/desktop.yml` runs this step before the
app build, with `continue-on-error`: a failure here never stops a release, the app then has no native audio helper.

## API (phase A0.2)

```js
const io = require('electron/io/thunder_io.node');
io.version();                    // "thunder_io 4 / miniaudio 0.11.26 / libremidi"
io.now();                        // Number: the OS clock in ns (macOS mach_absolute_time, Windows QueryPerformanceCounter,
                                 // else CLOCK_MONOTONIC): the clock the audio callback's timestamps will use
io.devices();                    // every input and output device, real backends (CoreAudio / WASAPI / ALSA / Pulse ...)
io.devices({ backend: 'null' }); // a throwaway null-backend context, for CI smoke tests (no hardware needed)
```

`devices()` returns `[{ id, inId, outId, name, backend, inputs, outputs, rates, isDefaultIn, isDefaultOut }]`:

- `inId` / `outId`: `"in:" + hex` / `"out:" + hex` of the raw `ma_device_id` bytes (trailing zero bytes dropped; pad with
  zeros to read back), `null` when the device has no such side. `id` is `inId`, or `outId` when there is no capture side.
- A capture and a playback entry are merged into one record only when both the name and the raw id bytes are equal; when
  unsure they stay separate records.
- `inputs` / `outputs`: the widest channel count in the device's native formats, `0` when there is no such side. miniaudio
  reports a channel count of 0 for "any" (and some backends cannot say): that is unknown and is reported as `2`.
- `rates`: sorted unique sample rates of the native formats (an "any rate" entry, 0, is skipped, so it can be empty).
- `backend`: miniaudio's name for the backend in use ("Core Audio", "WASAPI", "ALSA", "PulseAudio", "Null" ...).
- The real-backend context is created on the first call and kept; it is released by an environment cleanup hook.
- Errors throw a JS `Error` with miniaudio's result description (`ma_result_description`).

Test: `node io/test/devices.js` prints `devices()`, `devices({backend:'null'})` and two `now()` calls. `build.js`'s smoke
test checks the null backend returns an entry with numeric `inputs` / `outputs` and that `now()` increases.

The audio input (phase A1–A2):

```js
io.openInput(inId, {rate, block})  // → {ok, rate, channels, deviceRate, block, bufferFrames, latencyFrames, latencyMs, name} | {ok:false, why}
                                   //   every input channel of the device, float32 interleaved, at `rate` (converted if the
                                   //   device runs at another); latencyFrames / latencyMs: the interface's own delay (macOS)
io.read()                          // → null | {frame0, hostNs, channels, frames, rate, data: Float32Array, overruns}
                                   //   one contiguous run since the last read; hostNs: the OS time frame0 was captured
io.inputLost()                     // → true when the device stopped by itself (unplugged)
io.closeInput()                    // stops and frees it (safe when nothing is open)
```

MIDI (phase B, `src/midi.cc`, libremidi: CoreMIDI on macOS, WinMM on Windows, ALSA on Linux when its headers were there at
build time, else no ports):

```js
io.midiPorts()                   // → {outs:[{id, name}], ins:[{id, name}]}; id = the port's name (" #2"… when two share one)
io.midiOpenOut(id)               // → {ok, port} | {ok:false, why}
io.midiOpenIn(id)                // → {ok, port} | {ok:false, why}; its messages come back from midiRead()
io.midiSend(port, bytes, atNs)   // → true | false; bytes: Uint8Array or [numbers]; atNs on now()'s clock (0 or past: now)
io.midiClear(port)               // → how many queued messages were dropped (port 0: every port). STOP.
io.midiClose(port)               // an output (its queue dropped) or an input
io.midiRead()                    // → null | [{port, bytes, ns}]: what came in since the last call, ns = when it arrived
io.midiStats()                   // → {queued, late}: waiting now, sent more than 2 ms late since loading
```

One sender thread for every output, at the OS's highest normal priority (macOS QoS user-interactive; Windows time-critical
with a 1 ms timer): it sleeps until 0.5 ms before a message's time and spins the rest. Messages for the same time keep their
order. `test:loop` (listed when `THUNDER_IO_TEST` is set; always openable) is an output whose messages come back from
`midiRead()` as input `test:loop`, stamped with when they went out: `io/test/midi.js` checks the timing, order, clear and close
with no MIDI hardware. libremidi needs C++20 and exceptions (`binding.gyp`); every call into it is inside a try.

