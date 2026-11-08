# io/: native audio and MIDI I/O for the desktop app (optional)

A native Node addon, `thunder_io.node`, that opens audio interfaces (and, later, MIDI ports) for the desktop app, loaded
by the main process when present. Plan and design: [docs/NATIVE-IO-HELPER-PLAN.md](../docs/NATIVE-IO-HELPER-PLAN.md).
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
io.version();                    // "thunder_io 1 / miniaudio 0.11.26"
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

The audio surface that follows (`openInput`, `read`, `closeInput`) comes in the next phases.
