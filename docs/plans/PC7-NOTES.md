# PC7: the native desktop shell's audio, input and MIDI on the page (build 685, branch `pc7/desktop-audio-midi`, PR into `beta`)

Protocol: `docs/port/H7-NOTES.md` (incl. "The F-review") and `docs/port/CORE-BRIDGE.md` section 8, both on `origin/native`; code wins (the shell's code is `native/desktop/shared/bridge_session.cpp`, `bridge_translator.cpp`, `desktop_bridge.js`).
Builds on PC1..PC6 and M5 (build 684). Files: `thunder-core-client.js` (the PC7 block at the end), `index.html` (Settings > Audio block, the Record window's native input, native MIDI access), `tests/native-client-pc7.test.js` (17 tests), the inert test's three counts, `sw.js`, HANDOVER. No `native/` change, no worklet-core change (`tools/make-fx-cores.mjs` not needed).

## Status
Written and unit-tested against a fake `thunderCore`; the page-side pieces that need a Mac (device list, a real take, CoreMIDI) are NOT run: the owner's punch list ("Mac, native desktop build (build 685)") is the check. `npm test`: 2034 tests, 2004 pass, 0 fail, 30 skipped.

## Gating (all of it)
`natDev()` / `natIn()` / `natMidi()` in index.html (built on one `natC()` = `NCON() ? NC() : null`) are true only when the client's hello succeeded AND `hello.caps.devices` / `devices && inputs` / `midi` is `true` (the shell says them truthfully). With the switch off, on Electron, on the website and on Android (no such caps) none of the new code runs; the Settings rows stay as they were.
**One change to an existing line:** `IOAPI` (the Electron helper) is now `null` when `window.thunderCore` exists. The desktop shell's bridge also defines `window.thunderDesktop.io` (its `start` answers `{ok:false}`), so without this the Electron helper path would have run beside the new one and said "unavailable". Electron has no `window.thunderCore`: unchanged there. (Inert test counts: `NCON()` 20, `NC()` 19, `window.thunderCore` 7.)

## Design

### Client (`thunder-core-client.js`, "PC7" block)
- Hello: keeps `audio` / `input` bodies and `outLat`; after subscribing asks `dev.list`. `host.h7Ready()` tells the page once (Settings / MIDI may now show the shell).
- `NC.devOn() / inOn() / midiOn() / rtOn()`, `NC.devState()` (`devices, wantOut, wantIn, audio, input`), `NC.devList()`, `NC.devOpen({out, in, sr, buf})`, `NC.devRates(outId)`, `NC.recArm / recStart / recStop / recTake / recFree`, `NC.rtSnapshot / rtReset`, `NC.midiState()`, `NC.midiCfg(c)`, `NC.listen(type, fn)` for `dev`, `rec`, `rec.done`, `midi.ports`, `midi.in` (returns the off function).
- `devOpen` enforces the review's rule itself: `sr` is sent as given only if it is in the OUTPUT device's own `rates` (`''` = the default output's), else 0; `buf` 0 or 16..8192; `in` is sent only if named; `out` defaults to what is wanted now; 30 s timeout (the first input open can sit on the macOS microphone prompt). Never rejects: `{ok:false, error}`.
- `recTake` fetches channel by channel in pieces of at most 8M frames (32 MiB; the shell refuses more than 64 MiB per call) with the bridge's own `tc.take(id, {ch, off, n})`; frees nothing by itself.
- `midi.in`: the shell's `wallNs` (host clock) is turned into the page's clock as stamp + lowest arrival delay seen (lower envelope, let up 100 ppm): spacing is the shell's, the page's own wake jitter is not added. (H7-NOTES calls the field `wall`; the code sends `wallNs`; both are read.)
- `midi.cfg {outs[4], clockOut[4], gearDelay[4] (clamped -50..200), clockIn}`: sent only when it differs from what the shell holds; sent again on `engine up` (the shell rebuilds its MIDI manager with the engine; `host.midiCfg()` gives the current four).

### Settings > Audio ("Desktop audio device", shown only on the shell)
Output / Input / Rate / Buffer selects and one status line (name, rate, buffer, output latency in ms and frames from `audio.latOut`, input channels and `latIn`), and "Copy audio stats" (`rt.snapshot` to the clipboard). The page's own Output / Latency / Rate rows (they configure the browser's context, which a native PLAY does not use, and Rate offers 44.1 / 48 k) are hidden there.
- **Rate = Device (0) + the output device's own `rates`, nothing else** (source-guarded in the test).
- Buffer: Device (0), 64, 128, 256, 512, 1024.
- A row change sends the full `dev.open {out, sr, buf}` (plus `in` when an input is open, so the shell reopens it at the new rate / buffer). Nothing is sent at start-up: the shell opened its saved choice (`audio.json`) at hello and is the truth.
- Events: `dev changed` refreshes the lists; `lost` toasts (output: "unplugged: playing through the system default"); `reopened` toasts; `changed.wantedBack` toasts "X is back. Pick it in Settings" and switches nothing (the shell's rule 6). The existing PC3 handlers already stop the transport on `engine up:false` / `device stopped`.

### Recording / takes (the Record a sample window)
The shell sends no live audio, only a take after Stop; the Electron path streams chunks into a worklet on the page's audio clock. So the native input is a stand-in with the same shape: `RCD.io` (fields the recorder reads: `dev`, `opened`, `D`, `node.port` for the pick) and `RCD.node` (the recorder worklet's `'go'` / `'stop'`). `go` = `rec.start`; `stop` = `rec.stop`, `take` for the armed channels, `take.free`, then the take is handed to the recorder exactly as the worklet's last message (`rcdMsg({f0:0, ch:[L,R], done:true})`), so rcdCollect / rcdTake / the editor are untouched. Meters: the shell's `rec` events (peaks of the armed channels) go to `rcdMsg({pk})`. Only the picked pair (or single channel) is armed (`rec.arm` again when the pick changes while not recording): the 1 GiB cap then means ~1.5 h of a stereo pair, and every one of the 28 inputs is selectable (the Inputs list uses the device's channel count). The take's rate is the engine's (`RCD.natSr`; `rcdTake` uses it instead of the page context's). The input window's device list is the shell's (same `inId / name / inputs` fields as the helper's); picking one is a `dev.open {in}`, so Settings > Audio's Input follows. Closing the Record window leaves the shell's input open (it is the Settings choice).
An input unplugged mid-take: the shell ends the take itself (`rec.done {partial}`); the page fetches and keeps it ("Audio input lost (why): the take so far is kept"), as the Electron path does.

### MIDI
`midi.access` = `{native:true, ncore:true, outputs, inputs, onstatechange}` built from `midi.ports` (`devs` = outputs, `ins` = inputs with a port token); ids are the shell's `n:<name>` (` #2` twins), so the existing `midiRemap` (saved web ids mapped by name) works unchanged. `midi.in` -> the input's `onmidimessage({data, timeStamp})`: the page's existing `onMidi` path (learn, notes, CC, clock-follow input) is untouched. The four letters, clock boxes, Gear delay and clock in go as `midi.cfg` from `saveMidi()` (every Settings > MIDI change already calls it). The status line shows " · native" and " · N late" (from `midi.ports.late`). The start-up auto-connect runs before the shell answers hello, so `natAudInit` connects again then.

## Decisions (with reasons)
1. **Shell is the truth for the device; the page sends nothing at start.** The shell loads `audio.json` at hello; a page push would rebuild the engine at every start. The page keeps `aset.natSr` / `natBuf` only to show its two rows and to fill `dev.open` (see Q1).
2. **IOAPI is null on the shell** (above): otherwise the dead Electron helper path would run.
3. **Rates from the OUTPUT device's list only, enforced in the client AND the UI.** An input on another interface at a rate it lacks would be converted by the shell; we do not offer an intersection (Q3).
4. **Takes are free-length only.** No native PLAY position reaches the page yet (PC5, the playhead), so PATTERN mode cannot cut at edges; it records like FREE with a one-time toast. The take's metadata (`hostNs0`, `latencyFrames`) is fetched but not used yet. "Measure gear delay" is refused on the native input for the same reason (a take has no timestamps against the song).
5. **No input monitor** (the shell has none); the Monitor switch does nothing there.
6. **MIDI outputs are cables for `midi.cfg`, not senders.** A SONG's MIDI is played by the core on a native PLAY; but the page's own hand-played MIDI out (keyboard thru, previews) and MIDI of a song that falls back to the page's engine have no message to ride on, so `send()` drops (logged once).
7. **midi.in timestamps: lower envelope** (not the arrival time) so batching of events on the page's thread does not move them.
8. **Page does not auto-switch back** to a returned device (the shell's rule), it says so.

## Open questions for the lead
- **Q1:** `dev.list` returns what is wanted (`out`, `in`) and what is open, but not the REQUESTED `sr` / `buf` (0 = device's own vs the actual number). The page's Rate / Buffer rows therefore rely on localStorage (`aset.natSr` / `natBuf`): clear storage and they read "Device" while the shell may run another. Suggest `want:{out,in,sr,buf}` in the dev.list reply.
- **Q2:** The unplug fall-back rebuilds with "the old rate asked again (converted if the new device runs at another)": that is the resampling case the review forbids. Should `devFallback` use rate 0 (the new device's own)?
- **Q3:** Input rate: the input opens at the ENGINE's rate; on a different interface from the output that rate may not be one of its own. Do we want an intersection rule, or an input rate of its own in `dev.open`?
- **Q4:** A raw MIDI-out message (`midi.send {port, bytes, at}` + clear) would give hand-played MIDI out and a page-engine fallback their MIDI; and a live audio tap (or `rec` chunks) would give a monitor. Wanted?
- **Q5:** Pattern-synced takes need the playhead (PC5) and a `rec.start` tied to a song frame (`at`), or the `hostNs0` + `clock` `wallNs` mapping; which?
- **Q6:** `take` returns float32 base64 in 256k-frame events; 28-channel takes are not needed here (only the picked pair is armed), but a long mono-stem workflow would want a file path instead.

## Lead's decisions (applied)
- **Q2:** the page re-asks a rate only if it is in the (new) output device's `rates`, else 0: `devOpen` already did this against the wanted device; after a `reopened` with `fellBack` the saved `aset.natSr` is also reset to 0 if the new device lacks it. The shell's own fall-back (`devFallback`, which asks the old rate again) is native-side: see below.
- **Q3:** `dev.open` carries one `sr` (the engine's); the input is opened at that rate by the shell. The protocol cannot say "input rate only if in the input device's rates", and no message was invented: nothing on the page side.
- **Q5:** accepted: free-length takes until PC5. `IOAPI = null` under `window.thunderCore` and the test count changes: accepted.

## For native (later)
- Q1: `dev.list` should return the requested `sr` / `buf` (e.g. `want:{out,in,sr,buf}`).
- Q2: `devFallback` should ask the old rate only if the new device's `rates` has it, else 0.
- Q3: the input's rate rule (in the input device's own `rates`, else 0) needs the shell to apply it when opening the input.
- Q4: a raw MIDI-out message and an input monitor.

## Resume
- Tests: `node --test tests/native-client-pc7.test.js tests/native-client-inert.test.js`, then `npm test`.
- Lines to review: `thunder-core-client.js` PC7 block (`h7*`, `devOpen`, `recTake`, `onMidiIn`, `NC.midiCfg`); `index.html`: `natC / natDev / natIn / natMidi` (after `padSR`), `IOAPI`, `natAudSync / natAudApply / natAudEvent / natAudInit` (before `updLat`), `ncMidiConnect / midiCfgPush` (after `nmClose`), `rcdOpenNat / natArm / natFinish / natInDev` (before `rcdCloseIo`), the one-line branches in `rcdOpen1`, `rcdStart`, `rcdTake`, `rcdRateMsg`, `rcdCloseIo`, `midiLatRun`, the Record window's `fillIn`, `connectMidi1`, `midiAccessDrop`, `saveMidi`, `audStatus`, `audRefresh`.
- Next: owner's Mac run of the punch list; then Q1..Q5 with the shell side; then PC5-based pattern takes.
