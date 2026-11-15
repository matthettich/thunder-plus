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
