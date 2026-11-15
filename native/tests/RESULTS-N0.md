# Stage N0 results

Written by the N0 build session, 14 Nov 2026. Sections: A8 and A9 (written checks, done), A3 / A4 (what CI and an
emulator can show; the timed numbers are "owner's device"), A6 (the owner's clock test: not mine, left empty).

## A8. libremidi's Android and iOS backends (libremidi 5.4.3, `io/vendor/libremidi`)

**Finding: both backends exist in 5.4.3. Android's is usable but thin and not shipped as a complete unit; iOS's rides on
the CoreMIDI backend. Recommendation: H2 uses AMidi through our own small JNI layer; keep libremidi for Mac and Windows.**

Android (`include/libremidi/backends/android/`, 718 lines, API `ANDROID_AMIDI`):
- Present and wired in: `backends.hpp:80-81` and `:142-144` (only when the build defines `LIBREMIDI_ANDROID`),
  `api.hpp:67`, `android.hpp:25` (`available()` is true under `__ANDROID__`). It reads with `AMidiOutputPort_receive`
  (`midi_in.hpp:128`) and sends with `AMidiInputPort_send` (`midi_out.hpp:104`); `helpers.hpp:4` includes
  `<amidi/AMidi.h>`, so minSdk 29 (the plan's) is the floor.
- Why not rely on it as is:
  1. It needs a Java class that is not in the vendored tree: `helpers.cpp:184` does `FindClass("dev/celtera/libremidi/MidiDeviceCallback")`
     and the JNI entry `Java_dev_celtera_libremidi_MidiDeviceCallback_onDeviceOpened` (`helpers.cpp:211`). We would add
     that Kotlin / Java class to our app by hand (upstream ships it separately). `find io/vendor/libremidi -name '*.java'` finds nothing.
  2. It gets the app context by reflection on the hidden `android.app.ActivityThread.currentActivityThread()`
     (`helpers.cpp:33-57`), a non-SDK interface that Android may restrict.
  3. Input is a polling thread, `sleep_for(1 ms)` (`midi_in.hpp:126`) then a non-blocking receive (`:128`), with
     `has_absolute_timestamps = false` (`:114`): the device's own AMidi timestamp is not used for timing. For MIDI-in
     timing in the core (frames on the sample timeline) we want the AMidi timestamp and a tighter loop.
  4. One client name per process (`midi_in.hpp:24-30`), and the port opens asynchronously: `open_port` returns success
     and sets `port_open = true` before the device is actually open (`:72`; the JNI callback opens the port later).
  5. libremidi needs C++20 and exceptions (`io/README.md:72`); fine in a host, not in `native/core/`. It has not been
     compiled for Android in this repo (`io/binding.gyp` has no Android target); that is untested here, not known broken.
- H2 plan: Kotlin `MidiManager` (device list, hot-plug, `openDevice`) plus about 150 lines of JNI over `AMidi_*`
  (`AMidiDevice_fromJava`, `AMidiOutputPort_open` / `receive`, `AMidiInputPort_open` / `send`), timestamps from AMidi
  converted to core frames in the host, one reader thread blocking on the port's file descriptor (`poll`) instead of
  sleeping.

iOS (CoreMIDI): `backends/coremidi/` is the Mac backend and carries iOS branches: `coremidi/helpers.hpp:15` and `:53`
(`#if TARGET_OS_IPHONE`: `mach_absolute_time` instead of `AudioGetCurrentHostTime`, its own `AudioConvertHostTimeToNanos`).
So iOS is plausible from the same code. Not built or run here: N6 should compile it in CI for an iOS target first.

## A9. Does a saved song always carry its samples? (headless test; app code read only)

**Finding: on `beta` today, yes for pad samples, song-preset samples and impulse responses, or the user is asked; the
plan's hole is already closed.** Nothing was edited. Code: `index.html` `smpToJSON` (`function smpToJSON`, writes only
`{sr, name, ref}` while `S.data` is null), `smpNoAudio()`, `smpSaveGate(capMs, quiet)`, called from `saveProj`
(`await presetSmpFill(); if (!await smpSaveGate()) return false;`), `shareProj` and `backupNow` (quiet mode). The gate
waits up to 10 s for the loads, then asks "Save anyway". Existing headless test: `tests/save-safety.test.js` (11 tests
including "a load that never finishes does not block a save beyond the cap" and "every file-writing path goes through
the gate"), run here: 11 pass, 0 fail. HANDOVER (7 Nov, "Save safety") says it also answers A9.

Remaining small gaps found (for the lead; no `HANDOVER.md` edit was made from `native`):
1. The "before Thunder import" backup (`index.html` near `projBlob()` in the import path) waits only while
   `smpResolving` for at most 100 x 100 ms, then writes without asking: a pad sample that is slow to load, a preset sample
   or an impulse response can still be missing from that one file. Low risk (one-off import safety copy).
2. A sample layer whose id is not in this browser's IndexedDB at all (a kit moved between browsers without its file)
   cannot be saved with audio; the user is asked (`Save anyway`), which is the correct behaviour.
3. SoundFonts are not in the file (by design, `index.html` comment "SoundFonts are deliberately not in the file"): the
   GM font is the app's 24 MB `sounds/FluidR3Mono_GM.sf3`, user `.sf2` / `.sf3` live in IndexedDB `thunderplus-fonts`.
   A song using a user font needs that font on the other device; the native app must say how it finds it (H3 / U32).
4. `ELECTRO.thunder` and the owner's `acid.thunder` carry no embedded samples (`CENSUS.md`: 0 samples), so neither
   exercises sample embedding on load.

## A3. Android shell v0: what is proven in CI, and what is not

Built in CI (run 43688661447, commit b092029; job "Android shell v0 (debug APK)" green). Debug APK
`thunder-plus-native-n0-android-5.apk`, 41.8 MB, sha256 `848a4cba5ee9b23a9b56673daeb7b3207bea5ac781b5827a4d24de1dd6151d35`,
attached to the GitHub PRE-release `native-n0-android-5`
(https://github.com/matthettich/thunder-plus/releases/tag/native-n0-android-5). From the job log (`aapt2 dump badging`
and `unzip -l`): package `app.thunderplus.nativetest`, minSdk 29, target 35, native code `arm64-v8a` only,
`lib/arm64-v8a/libthunder_android.so` (1.0 MB), the real `assets/index.html` (3.6 MB) with `chains/`, `waves/`, `pump/`,
`lxr/` wasm files beside it, permissions FOREGROUND_SERVICE, FOREGROUND_SERVICE_MEDIA_PLAYBACK, POST_NOTIFICATIONS
(no WAKE_LOCK, no INTERNET).

| Check | Where | Result |
|---|---|---|
| Test core: stopped = silence, hits land exactly on the core's own sample grid (24000 frames per quarter note at 120 bpm), clock events ten a second, echo, unknown commands refused, bulk put | CI (Linux, Windows, macOS: `ctest core_pattern`) | PASS |
| APK assembles with the NDK, miniaudio AAudio backend, the core linked in | CI | PASS |
| Installs on an Android 14+ device, the page loads, Play makes the click pattern audible (the plan's BrowserStack gate) | not run: no BrowserStack access in the build session | open |
| Clock 1.00 s in 1.00 s, underruns, output latency (A6) | owner's device (`OWNER-TEST-N0.md`) | **PASS, 15 Nov** (table under A5) |

Nothing was run on a device or emulator by the build session. The APK is untested at run time: expect the first install
to show small shell bugs.

What was built (all under `native/`): `core/` (the C API of WORKPLAN section 10, plus `thunder_put_info` and
`thunder_host_stats`, both additions for N0), `android/` (Kotlin: `MainActivity` with the overlay, `ThunderHost`,
`ThunderService` of type mediaPlayback, `Bridge`, `MimeAssets`; JNI + miniaudio in `app/src/main/cpp/`).
The audio callback (`dataCallback` in `jni.cpp`) and `thunder_process` were reviewed and accepted by the lead on 15 Nov.

## A4. Bridge transport proof: code is in, the timed numbers are the owner's device

Code: `native/android/app/src/main/java/app/thunderplus/nativetest/Bridge.kt` (WebMessagePort pair for `send` / `on` /
`ask`, ArrayBuffer path (a), base64-to-file path (b) through `addWebMessageListener`), the page shim
`app/src/main/res/raw/thunder_bridge.js` (`window.thunderCore`), the test page `app/src/debug/assets/bridge-test.html`
(overlay button "Bridge test"; debug builds only). Echoes go through the C++ core's command queue and event queue (the
audio thread answers), so the round trip includes the core.

| Measurement | Measurable where | Result |
|---|---|---|
| Echo, 60 messages a second for 5 minutes: sent / received / lost / duplicates | owner's device | open |
| Echo round trip: median, 99th percentile, worst (ms) | owner's device | open |
| 53 MB (13.25 M floats): (a1) one ArrayBuffer message, seconds | owner's device | open |
| 53 MB: (a2) ArrayBuffer in 1 MB chunks, seconds | owner's device | open |
| 53 MB: (b) base64 chunks to a file, then the core reads it: seconds, split into write and core read | owner's device | open |
| Does the core hold every float afterwards (count and exact sum) | owner's device (the page checks it) | open |
| Core side only: `thunder_put` of 53 MB in 1 MB chunks, no web view | CI Linux runner | 20 to 29 ms (floor; the bridge paths cost far more) |
| Device model, Android version, WebView version, `WEB_MESSAGE_ARRAY_BUFFER` supported | samsung SM-F956U1, Android 17 (API 37), WebView 157.0.8103.50, **supported** (owner, 15 Nov) | done |

Not done and why: path (b) as the plan words it, "a `WebViewAssetLoader` POST handler", is not possible: the loader's
`PathHandler.handle(path)` receives no request body, so POSTed bytes never reach it. The file path therefore uses
`addWebMessageListener` with base64 strings (about 33 % more bytes, plus encode / decode time), which the plan names as the
alternative. Fable picks the path and updates CORE-BRIDGE sections 2 and 6 once the numbers are in.

## A5. Real-time harness and clock logger (written by the lead)

Done: `native/core/include/thunder/rtlog.h` (the logger), `native/tests/rt_harness.cpp` (ctest `rt_core`,
`rt_guard_selftest`). CI: green on Linux, macOS and Windows (the self-test is skipped on Windows, which has no `fork`; the
wasm job builds no tests). Fixed on the way: clang elided the self-test's deliberate `new` / `delete`, so the self-test now
calls `operator new` / `operator delete` directly (the check is not weakened: the child must still die by SIGABRT).
The Android callback now runs the logger (`rtlog_begin` / `rtlog_end` around `thunder_process`, flush-to-zero set on
arm64 through FPCR.FZ at the top of every callback), the AAudio xrun count feeds `rtlog_host_underruns`, and the
debug overlay's readout and PASS / FAIL come from it. **Where the owner's numbers go (A6), one block per run, pasted from
COPY RESULTS:**

| Run (5 min) | ratio (s per s) | ppm | underruns | late | longest gap ms | longest callback ms | output latency ms | by ear | PASS |
|---|---|---|---|---|---|---|---|---|---|
| SCREEN ON | 0.9999 | −107 | 0 | 0 | 33.1 | 1.82 | 26.2 | "sounded great" | **PASS** |
| SCREEN OFF | 1.0000 | −1 | 0 | 0 | 41.4 | 2.51 | 54.0 | "sounded great" | **PASS** |
| BACKGROUND | 1.0000 | −2 | 0 | 0 | 37.3 | 1.64 | 54.8 | "sounded great" | **PASS** |

**A6 done: the owner's runs, 15 Nov 2026, 11:03 to 11:20** (pasted from COPY RESULTS, with three screenshots). Samsung SM-F956U1 (Galaxy Z Fold 6), Android 17 (API 37), WebView 157.0.8103.50; 48000 Hz, burst 192 frames, buffer 1536 frames, shared, low-latency performance mode; release `native-n0-android-8`. Cross-check from the core's clock events: 299 s (audio 299.008 s, wall 299.008 s), 300 s (300.032 / 300.033), 299 s (299.008 / 299.009), ratio 1.00000 each, 0 underruns.

**All three pass the gate (±1000 ppm, 0 underruns, 0 late).** Go or stop is the owner's decision. For N1, not failures:
- **Output latency doubles with the screen off or the app in the background** (26 ms → 54 ms): Android moves the stream off the fast path there. Fine for playback; playing pads live happens with the screen on.
- **Longest gap between callbacks is 33 to 41 ms** against a 4 ms burst and a 32 ms buffer, with no underrun counted: callbacks arrive in bunches. 26 ms with the screen on is not yet a low-latency figure for finger drumming; N1 should try a smaller buffer and exclusive mode and see whether the gaps then cost underruns.
- The -107 ppm with the screen on is the first-seconds settling of the timestamps (the cross-check reads 1.00000), well inside the gate.
- **By ear (owner, 11:24): "It sounded great."**
- **A4 bridge timings: not run** (optional; he pasted only the page's device block: `arrayBufferSupported: true`, same device and WebView). He asked whether they are needed and was told no: run them at the start of N1, when the numbers decide how samples cross the bridge.

## Stand-ins

- The core's own `clock` events stay as an independent cross-check (the overlay shows one line from them; they are not
  the readout). Their output latency is an estimate from AAudio's frame timestamp (`jni.cpp` `probeLoop`); the logger's
  underruns are AAudio's own `getXRunCount`, read through `dlsym` on the stream pointer inside miniaudio's device (a read
  of miniaudio's internal field `aaudio.pStreamPlayback`, fine while the vendored header is pinned at 0.11.26).
- (Removed: the old note that the overlay ratio was cumulative from the first second: the logger now owns that.)
