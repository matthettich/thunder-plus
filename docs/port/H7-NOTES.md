# H7 (Mac first): desktop audio and MIDI in the native shell

Branch `n4/h7-mac-audio-midi`, PR into `native`. Windows (WASAPI, WinMM or WinRT MIDI) comes after the effects-library unit; its shell still compiles (`TPD_NO_MIDI`, the MIDI interfaces stay, miniaudio enumeration is neutral).

## Status

- Platform-neutral logic and the host tests: done, 12 desktop tests green on Linux (`ctest -R desktop_`).
- CoreAudio device code (`audio_miniaudio.cpp`, `#if __APPLE__` parts) and CoreMIDI (`midi_coremidi.cpp`): written, compiled only by the macOS CI job. NOT run on hardware.
- Page side: not done (no page client exists on `native`). The protocol below is what the page calls.
- Hardware gates still open (owner): A5 30 minutes at 128 and 256 frames, MIDI loopback jitter on CoreMIDI, the 28-input capture on the Cranborne 500R8, mic permission prompt, unplug tests.

## Design

One clock: `tpd::hostNowNs()` (`shared/host_clock.h`, `CLOCK_UPTIME_RAW` on Apple = mach absolute time without sleep, `steady_clock` elsewhere). Audio wall time, MIDI send times, the clock map and MIDI-in stamps are all on it.

Files (`native/desktop/shared/` unless noted):

| file | what |
|---|---|
| `audio_devices.{h,cpp}` | `AudioDevice` rows (`out:<hex>` / `in:<hex>` of the raw `ma_device_id`), merge of the in and out sides of one interface, `dev.list` JSON, `dev.open` validation, `AudioSettingsStore` (`audio.json`, atomic write) |
| `audio_miniaudio.cpp` | miniaudio on CoreAudio: enumerator, `MaOut` (device, rate, buffer, hardware latency from CoreAudio, stop/reroute notifications), `MaIn` (every channel of an input) |
| `input_ring.h`, `input_capture.{h,cpp}` | capture callback does a ring push only; a drain thread makes meters (about 15 Hz), appends armed channels to the take, fills gaps with silence, counts drops; 1 GiB cap per take |
| `midi_registry.{h,cpp}` | C++ port of `MidiModel.kt`: ids `n:<name>` (` #2` for twins, 95 bytes), tokens that are never reused, the `midi.ports` JSON |
| `midi_rx.h` | lock-free SPSC byte ring for MIDI in |
| `midi_backend.h`, `midi_coremidi.cpp` | the OS interface and the CoreMIDI implementation |
| `midi_manager.{h,cpp}` | `MidiManager`: wires `tpa::MidiHost` (the Android plain-C++ pieces, compiled in place, core untouched) to the backend, the live host and the bridge |
| `live_host.{h,cpp}` | `AudioConfig`, `MidiService` / `MidiControl` hooks, fall-back to the default output, A5 logger (`rtlog.h`) around the audio callback |
| `bridge_session.cpp`, `bridge_translator.cpp` | the new messages, the device watcher |
| `shell_core.cpp` | builds the settings store, capture, MIDI manager; the A5 log thread |

Order of an engine rebuild (page sees it in this order): `{t:'engine',up:false}`, then `{t:'engine',up:true,sr}`, then `{t:'dev',kind:'reopened'}`. A device that vanished: `dev lost`, `device stopped`, engine down, engine up, `dev reopened`.

Teardown order (`LiveHost::teardownLocked`): stop and transport(false) so the core queues its MIDI Stop, close the output (audio callback has ended), `engineStopping`, stop the host threads, free the output object, `engineDetached` (joins the MIDI sender, which flushes the Stop), destroy the engine.

## Protocol additions (CORE-BRIDGE v1, additive)

Down (all asks carry `id`):
- `dev.list` -> `{ok, devices:[{id,inId,outId,name,backend,inputs,outputs,ins,outs,rates,isDefaultIn,isDefaultOut}], out, in, audio:{open,name,id,sr,buf,latOut}, input:{...}}`
- `dev.open {out?, in?, sr?, buf?}` -> `{ok, rebuilt, fellBack, requested, audio, input, inError?}`. `sr` 0 or 8000..384000, `buf` 0 or 16..8192; 0 = the device's own. The same config again is a no-op (no rebuild). `buf` 0 never resizes the interface's buffer: the device's current CoreAudio size is used. The choice is saved to `audio.json`.
- `rec.arm {ins:[ch...]}`, `rec.start`, `rec.stop` -> `{take, frames, sr, nch}`; `take {take, ch?, off?, n?}` -> reply `{ok, sr, nch, frames, hostNs0, latencyFrames}` plus `take.chunk {rid, c, off, b}` events (float32 base64, 64 MiB per call); `take.free {take}`.
- `rt.reset`, `rt.snapshot` -> the A5 logger's fields (`sr`, `calls`, drift, worst callback, overruns).
- `midi.cfg` is applied by `MidiManager` (still refused in a shell with no MIDI).
Up:
- `hello.caps`: `midi`, `inputs`, `devices`, `rt` are true when the shell really has them; hello also carries the `audio` and `input` bodies.
- `{t:'dev',kind:'changed',devices:[...]}` (list changed), `kind:'lost'` (`which:'out'|'in'`, `id`, `fellBack?`), `kind:'reopened'`, `kind:'changed',wantedBack:'out'` (the wanted device is back; nothing is switched).
- `{t:'rec',pk,frames,rec,full,dropped}` about 15 Hz while armed; `{t:'rec.done',take,partial:true,why,...}` when the input vanishes mid-take.
- `{t:'midi.ports',devs,ins,ports,late,sent}` on change and every 3 s; `{t:'midi.in', ...}` (the Android shape: `f` host frame, `wall` ns, `port` token, message).

## Real-time code list (for the F-review)

Audio thread: `LiveHost::audioCb`, `MidiManager::audioBlock` (calls the existing `tpa::MidiHost::audioBlockLive`), `MaOut::data`, `MaIn::data`, `InputCapture::onCapture` (ring push only).
OS MIDI thread: `CoreMidi::read` (the CoreMIDI read proc) calls `MidiRxRing::push` only: no allocation, lock, log or system call.
miniaudio notification thread: `MaOut::notify`, `MaIn::notify` set atomics only.
No new entry was needed in the core.

## Tests here (Linux, no hardware)

`test_audio_devices`, `test_midi_registry` (incl. a 200000-packet two-thread ring), `test_input_capture` (28 channels, 2 s at real rate, gap and drop accounting), `test_audio_session` (dev.list/open, no-op, fall-back at hello, hot-plug order, input lost mid-take, takes, rt.snapshot), `test_midi_manager` (real live host paced in real time, a MIDI-instrument song through the core scheduler to a fake port: notes arrive 500.0 ms apart within 0.2 ms, 24 ppqn clock 20.833 ms mean with 0.06 to 0.10 ms sd; midi.in; hot-plug; teardown; restart).

## Decisions for the lead

1. One host clock, `CLOCK_UPTIME_RAW`.
2. MIDI in reaches the page as `midi.in` events via ring plus reader thread (the core has no MIDI input).
3. A requested buffer of 0 never resizes the interface (owner rule).
4. Device ids are the hex of the raw `ma_device_id`; the saved id survives reboots only if CoreAudio keeps the UID.
5. A missing device falls back to the default output; the requested id stays saved and is reported (`fellBack`, `requested`).
6. No automatic switch back when the wanted device returns; the page is told once (`wantedBack`).
7. The MIDI clock map uses the fallback (callback start plus the latency floor) because miniaudio exposes no output timestamp pair; expect a constant offset of up to one buffer. Measure on hardware.
8. CoreMIDI ports are declared timestamp-capable; no flush on CLEAR (events already handed to CoreMIDI play).
9. Takes cap at 1 GiB (about 11 minutes of 28 channels at 48 kHz); `take` is chunked at 64 MiB per call.
10. The input opens at the engine's rate on its own clock; `hostNs0` is capture start minus input latency, so a long take drifts against the output clock.
11. The A5 logger: `--a5-log <file>` makes the shell write `rt.snapshot` every 10 s.
12. MSVC builds without MIDI for now (`TPD_NO_MIDI`).
13. `NSMicrophoneUsageDescription` is added to Info.plist by a post-build step; the hardened-runtime audio-input entitlement belongs to H8.
14. Page-side client work is not in this PR.

## Resume

Build: `cmake -S native -B build -DTHUNDER_BUILD_TESTS=OFF -DTHUNDER_BUILD_DESKTOP_TESTS=ON -DTHUNDER_DESKTOP_SHELL=ON`, then `ctest -R desktop_`. On a Mac, run the app with `--a5-log /tmp/a5.txt` for the 30-minute gate. Known loose ends: `rtlog_reset` races with the audio thread (same accepted race as Android); `MidiManager::rescan` holds `mu_` across a blocking control call.
