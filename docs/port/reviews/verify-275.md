MERGE AFTER FIXES

# Verify PR #275 (U36 T9, Android MIDI OUT sink), head 44ea1c36

Independent read of the diff against `midiport.h`, `midi.h`, U36-SPEC 3.4 / 8 and U36-NOTES. Static review plus the CI log of the failing sanitizer job. My own local ASan/UBSan build of `android_midi_sink` was still compiling when this report was written (PARTIAL: finding F1's cause is a reasoned hypothesis, not yet reproduced). Line numbers: `midi_out.cpp` = the file's own numbering.

## Findings

### F1 (blocking, CI red) Sanitizer failure: instrumented std::shared_ptr internals vs a `-fno-rtti` core
Evidence: CI log, job "Core (Linux, sanitizers)": `shared_ptr_base.h:198:28 ... address 0x768ad7de0190 does not point to an object of type '_Sp_counted_base'`, dump `vptr=0x5a17152db910, use_count=0, weak_count=2, next word = a heap pointer`. A FakeIo block (make_shared in the test) would start with a pointer to a static vtable, not a heap pointer; so this is a control block created inside the core, almost certainly `Seq::ownSess_` (`shared_ptr<SeqSession>`, `seq.cpp:381`, `Seq::~Seq` at `seq.cpp:416`, released when `DebugSong` is deleted in `freeDebugLocked`, `midi_out.cpp` ~line 160) .
Cause (hypothesis): `thunder_core` is built `-fno-rtti -fno-exceptions` (`core/CMakeLists.txt:35`), so its vtables have no typeinfo. `test_android_midi_sink` and `midi_out.cpp` are built WITH RTTI (`tests/CMakeLists.txt` new block: only `-Wall -Wextra`), and they are the first RTTI translation units that instantiate `shared_ptr` machinery (`shared_ptr<PortIo>`, `make_shared<FakeIo>`). Under `-fsanitize=vptr` the out-of-line COMDAT copy of `_Sp_counted_base::_M_release` from the instrumented TU wins at link time, and the core's un-instrumented, typeinfo-less SeqSession block then fails the vptr check. Other tests never put shared_ptr into an RTTI TU linked with core, hence only this test fails. It is a false positive, not a memory bug.
Fix: add `-fno-sanitize=vptr` (when `THUNDER_SANITIZE`) or `-fno-rtti` to `test_android_midi_sink` (and to the `midi_out.cpp` compile of that target). Confirm by rebuilding the test with `-fno-sanitize=vptr`; if it still fails, the cause is real (then look at DebugSong deletion order: `Seq` holds references `S_`/`V_` to `song`/`vars`, destroyed after it, which is fine today).

### F2 (blocking for the owner test, should-fix at least) Stop/shutdown loses held messages on ports without timestamps
`engineStop` (lines ~166-192) waits until `st.sent + st.dropped_gone` is unchanged for 15 ms (loop at ~179-184), then sets `stop_` and joins; `senderMain` (321-345) exits without a final `flushDeferred`, and `engineStop` then resets `held_` (line ~191) without sending. A held message counts as `sent` as soon as `sendTo` returns 0 (line ~297), but is due up to 20 ms (plus output latency, 26 ms on the Fold: the Stop is stamped `now+10 ms+latency`) later. Sequence: song playing on a non-TS port (virtual/BLE), engine stops, pump hands Stop and note-offs to `sendTo` (held, due in ~36 ms), count goes stable, 15 ms later `stop_` is set, held entries are discarded: hung notes, no clock Stop. Not exercised: `testStopMidSong` uses `ts=true` only.
Fix: after the loop, and before `stop_`, wait until `nHeld_` (make it atomic) is 0 or call `flushDeferred(INT64_MAX)` on the sender thread when `stop_` is seen; add a non-TS stop-mid-song test asserting 0xFC and note-offs arrive.

### F3 (should-fix) Core CLEAR cannot recall held messages: possible stuck note after Stop on non-TS ports
`sink_.caps = CAP_TS` for the whole sink and `clear = nullptr` (line ~141). The core therefore hands a non-TS port every message up to 20 ms early (`midiport.cpp:346-351`), `sendTo` holds it (289-300) outside the core's reach, and a CLEAR (`midiport.cpp:169`) only drops the core queue. Sequence: note-on at T+15 ms already held; user Stop at T+2 ms emits note-off at T+2 ms (sent at once); the held note-on fires at T+15 ms after the note-off: stuck note. The PR comment calls this "at most 20 ms of notes", but it is a hang, not a trailing note.
Fix: implement `sink.clear` for the sender thread (it owns `held_`): drop the held entries for that device token, no OS flush needed. The core then re-sends what it must keep. (The core only calls `clear` for devices that hold something in its hand history, which is true here because messages are handed with CAP_TS.)

### F4 (should-fix) One refused write kills the cable until the next rescan, and Android rescans only on hot-plug
`AmidiPort::send` (`midi_jni.cpp:32-45`) returns -1 after 4 zero-byte writes (0.8 ms total with sleeps); the core marks the cable gone "until the next set_devices" (`midiport.h` DEVICE TOKEN; `midiport.cpp:362`). `MidiPorts.kt` calls `nativeSetDevices` only on scan/open/close (`rescan`, `onOpened`). A transient full FIFO (e.g. a burst of stamped messages) silences the port for the session, with `dropped_gone` growing. Also: a partial write (0 < n < len) is retried from byte 0 (line ~40-43), which would duplicate bytes if AMidi ever writes partially (the comment marks this UNVERIFIED).
Fix: treat "write refused" as lost-message (count it, return 0) and only return -1 for n < 0; or have Kotlin re-push the device list (cheap) when `send_errors` rises.

### F5 (should-fix) Fallback clock map has no output latency
`audioBlock` fallback (lines ~371-380): `frame0 = F`, `host0 = mono`, no latency. The latency floor `outLatNs_` is only set by `probeLoop` (`jni.cpp`), which needs the same AAudio timestamp that just failed (`have` false -> latFrames 0 -> `setOutLatencyNs` not called). So on the first callbacks after every engine start (getTimestamp fails "for a while") the map is early by the full output latency (26 ms measured), and the core's smoothing (2 %/25 ms, tau about 1.24 s; jump under 50 ms not taken at once) converges over several seconds. The owner's "tight or jittery" verdict on a freshly started test will include that error. Also `mono` is read AFTER the block was rendered (line 352), not at callback start as spec 3.4 says (error = render time, under 1 ms).
Fix: do not set `mapReady_` on a fallback pair until the first exact pair or about 500 ms; or add the last known `outLatNs_` (or AAudio buffer size) to `host0` in the fallback; read `mono` at the top of the callback (pass it from `dataCallback`).

### F6 (should-fix) `NewStringUTF` on UTF-8 that is not modified UTF-8
`nativeStatus` (`midi_jni.cpp:134`) returns `NewStringUTF(status())`, and `status()` prints device ids that contain raw UTF-8 from Kotlin. A device name with a 4-byte character (an emoji) is invalid modified UTF-8; the debug APK is debuggable so CheckJNI is on and aborts the app. Fix: return a `jbyteArray` and decode in Kotlin, or replace non-BMP / invalid bytes before `NewStringUTF`.

### F7 (should-fix, small) Order inversion between held and immediate messages
`senderMain` runs `pump` then `flushDeferred` (lines 332-334). If the sender is delayed longer than a held message's lead, the next `pump` sends a later message immediately (`at <= now+50 us`, line ~294) before the earlier held one is flushed. Fix: call `flushDeferred` before `pump` as well, or route every message for a non-TS port through the held list.

### F8 (note) `deferFull` and `len > 3` send EARLY
`sendTo` lines ~294-295: with 256 held, or for a message longer than 3 bytes (SysEx), a non-TS port gets the message up to 20 ms early with at=0. Probably acceptable, but untested and not counted in the core stats.

### F9 (note) engineStop contract is by convention only
`engineStop` does not and cannot know that the audio callback stopped. `nativeStop` and the start-failure path (`jni.cpp`) are correct (uninit first). But `~MidiHost` (static, `host()`) also calls `engineStop`: at process exit with a live stream it frees `o_/h_` under a running callback. Also `engineStop` holds `ctl_` for up to 200 ms, blocking `nativeSetDevices` from the main thread, and the stop-time Stop/notes-off exists only when `dbgOwned_` is set (debug path): the real host (U37) must send its own stop before `engineStop`.

### F10 (note) AAudioStream_getTimestamp in the callback
`tsRead` (`jni.cpp`) is bounded by the 20 ms rate limit but can take a mutex or a binder call depending on the Android version and MMAP/legacy path. UNVERIFIED on the Fold; suggest logging the max `tsRead` duration into `status()`. Also the seqlock in `tsRead`/`probeLoop` uses release stores without a fence between the odd store and the data stores; harmless on arm64 for a latency estimate, formally loose.

### F11 (note) PR touches the test list that `native` has since changed
`git diff origin/native HEAD` shows the `test_midi_glue` block removed from `native/tests/CMakeLists.txt`: this is because `native` moved after the PR's base (the PR file list says +9 -0). Expect a textual conflict at the same spot; rebase and keep both blocks.

## Tests (item 8)
Claims in the header comment versus what is asserted:
- "remove while a send holds a copy": `testTable` only checks `use_count` on one thread; no sender thread, no test that the destructor runs outside the lock.
- "hot-plug ... returns under a new token": only `io1->total() > 0 && io2->total() > 0`. No check that nothing reaches the old token after `detach`, that A is MISSING, or error counts.
- "clock map pair of spec 3.4": `exactTs` sets `pos == written == g_fed`, and `g_fed` equals `frames_` at each callback, so `off == 0` always. The sign of `off`, the `written - F` term, and the `ns` window are untested; `mapOdd` is never asserted; no timing of the sink output against expectation (`Rec.got` is recorded and never compared to anything).
- "shutdown order": single thread only.

Mutations of `midi_out.cpp` the Linux test would NOT catch:
1. `frame0 = p.pos - off` -> `p.pos + off` (or drop `off`): off is 0 in the test.
2. Widen / remove the `ns` sanity window (line ~375): `exactTs` is always in window.
3. `flushDeferred` sends immediately (early) instead of at `at`: only `deferred > 0` and `at == 0` are asserted, no `got - at` check.
4. Change `kMaxSleep`, `kEarly`, the `until - now > 20000` spin threshold, or turn the sleep into a busy loop: nothing measures lateness.
5. Remove the 20 ms map rate limit (`mono - lastMapNs_ >= 20000000`): no assertion on `mapExact + mapFallback` counts.
6. `sendTo`: ignore `r < 0` (always return 0): `FakeIo::failNext` and `closed` are never set, so the "device gone" path is dead in the test.
7. Break the `kHeld` cap or the `len > 3` branch: never reached.
8. `PortTable::put/remove` destroy the old port under the lock: FakeIo has a trivial destructor.
9. `copyClamped` UTF-8 boundary, `setOutLatencyNs` clamp, `applyDevicesLocked` E_BUSY retry: no test.
10. Drop the `engineStop` drain loop or shrink it: `testStopMidSong` may still pass by timing luck (ts port, 128-frame block spacing); non-TS stop is untested (F2).

Checks that can never fail:
- `test_android_midi_sink.cpp:93` `io2->count(0xFC) + io1->count(0xFC) >= 0, "no crash"` (sum of ints, always true).
- `test_android_midi_sink.cpp:50` `held == a && a.use_count() >= 2`: tautology (`held` is a copy of `a`), only fails if `find` returns a different pointer.
- Lines 102 and 103 (`!h.running()`, second `engineStop`) can only fail on a crash.
- The `if (ts) ... else ...` at 96-97 and `if (!ts && !hotplug)` at 98 are conditional, so the non-TS hot-plug case is unasserted.

CI: this test is not in the TSan job; the control/sender/audio interplay (table put/remove vs send, held list) has no race test.

## Checked and OK
1. Audio callback path (`audioBlock`): no lock, no allocation, no JNI, no I/O. `clock_map` is wait-free; `nowFn_` is `clock_gettime`; counters are relaxed atomics. `DebugSong` buffers are sized on the control thread (`evs.resize(256)` before publish); `SeqRunner::pump` is documented allocation-free. Not proven for `Seq::step` (it has `ids_.push_back` in `internStr`, `seq.cpp:233-236`, reachable only for new target ids; the debug song has none). Suggest running the debug song under the rt allocation guard.
2. Field ownership: h_, o_, sr_, tsFn_, tsUser_, sink_, nowFn_ are written in `engineStart` before `ma_device_start` (thread creation orders them) and cleared after `ma_device_uninit`; `g.midi` same. `dbg_` publish/clear/FINISHED protocol is correct (audio clears the pointer, then sets FINISHED as its last touch; control frees only on FINISHED or after the callback stopped). `held_`/`nHeld_` are sender-only while it runs; control touches them only before the thread starts and after `join`. `ctl_` serialises all control calls, so the core's single-producer rule holds (including `debugTest`'s raw/start/set_song).
3. Shutdown order in `nativeStop` / start-failure path: probe joined, `ma_device_uninit`, then `engineStop` (join sender, free `o_` then `h_`): obeys midiport.h LIFETIME. PortIo destructors run on the control thread (attached JVM thread), on the sender (attached via `senderEnter`/`senderLeave`, detached at exit), or from `nativeDetach` (JNI thread): all attached. `PortTable` never destroys under its mutex. `AmidiPort` closes the input port before releasing the device.
4. Clock-map math: `off = written - F`, `frame0 = pos - off`, `host0 = ns` matches spec 3.4 given `F` is the frame count before the block. A stale timestamp pair is still a consistent line (position-based), and a stream restart/reroute is absorbed because `off` is recomputed each call. `frames_` restarts at 0 only with a new engine (new core), consistent with the core's counter. The window `ns in (mono-2 s, mono+200 ms)` is reasonable. CLOCK_MONOTONIC matches AMidi's timebase.
5. Sleep computation: absolute `clock_nanosleep`, at most 2 ms (contract), `until` in the past falls to `sched_yield` for at most about 250 us per held message, no infinite spin found (the core's `next_ns` is in the future or `now` only inside the 0.5 ms spin window).
6. JNI: local refs deleted in the loop (`nativeSetDevices`), exceptions cleared in `utf8`, no global refs leaked, `AmidiDevice` released when the last port goes, tokens are a Kotlin counter (`TokenAllocator`) never reused, `JNI_OnLoad` is defined once.
7. Kotlin: ids follow the spec (`n:Name`, ` #2`, `n:Name / Port`, 95-byte clamp that never splits a character, uniqueness after clamping); all callbacks on the main handler so the registry needs no lock; list is pushed before ports are detached and closed; manifest `uses-feature android.software.midi required=false`, no permission needed for USB/virtual; `MidiPorts` uses `applicationContext` and is idempotent, so an activity destroyed while the engine runs (foreground service) leaves no leak.

## Could not verify without a device
- Whether `AAudioStream_getTimestamp` / `getFramesWritten` in the callback give `written - F == 0` (the owner test reads `offset ... frames`), how long `tsRead` takes, and whether the timestamp is available right after start.
- Whether AMidi honours timestamps on USB on the Fold, what BLE/virtual ports do, and whether a write is whole-or-nothing (F4).
- `AMidiDevice_release` threading (the sender attach is a defensive guess) and whether Java `MidiDevice.close()` while a held message still references the port is harmless.
- `setpriority(-19)` and `Process.setThreadPriority` effect for the sender.
- Whether the sanitizer false positive (F1) is what I think it is: not reproduced locally yet.

---

# Re-check of b7b464f

MERGE AFTER FIXES (one blocking item, R1: the PR does not compile for Android as pushed; the rest is small).

Method: read of the diff 44ea1c36..b7b464f for the PR's own files (midi_out.{h,cpp}, midi_jni.cpp, jni.cpp, MidiPorts.kt, the test and its CMake block), the core's `applyClear` (`midiport.cpp:164-190`) and the new U36-NOTES round 2 entries. I did NOT build or run the sanitizer test this time (the core alone takes more than 30 minutes at -j1 under ASan on this machine); no NDK is on this machine, so everything Android-only is unverified. CI on b7b464f: `test` (Linux ctest) was success when I looked; the Android jobs had not reported yet.

## Old findings

- F1 (CI sanitizer): FIXED as far as I can tell: `-fno-sanitize=vptr` on the test target, same as two other tests (CMake comment explains it). Not run by me.
- F2 (held messages lost at stop): FIXED for the debug path. `engineStop` now waits for quiet of `25 ms + latency` AND `heldCount_ == 0`, and `senderMain` pumps once more and `flushAllHeld()` after `stop_`. I could not break it: `heldCount_` is written only by the sender (release) and read by control (acquire); control resets it only after `join`; `pump` in the final step runs while `o_` is still valid (freed after join); the loop is bounded by `200 + need` iterations (need at most 325), so `ctl_` is held at most about 530 ms plus stat reads (UI jank on the main thread, not an ANR). Residual, unchanged from F9: the drain only runs when a debug song is playing; the real host (U37) must post its own Stop before `engineStop`. Note: `flushAllHeld` also sends held messages that are not yet due (up to 20 ms early); acceptable only because it runs after the drain.
- F3 (CLEAR vs held): FIXED, with the caveats under "clearDev" below.
- F4 (refused write kills the cable): PARTLY. The partial-write resend from byte n is correct (`AmidiPort::send`, progress does not count as a refusal). The main point is NOT fixed (builder says so): four zero-byte writes still return -1, the core marks the cable gone until the next `set_devices`, and Kotlin re-lists only on hot-plug. The partial-write change is in Android-only code (never compiled or run by the Linux test).
- F5 (fallback map): PARTLY. `mono` is now the callback start (`dataCallback` reads `cbStart` first, passes `t0`): fixed. The fallback adds `outLatNs_`, which is 0 in a fresh process (it is set only by the probe from a successful timestamp), so on the first engine start after install, which is the owner's first test, the fallback is still early by the output latency. `mapReady_` is still set on the first fallback pair. Suggest not publishing a fallback pair as "ready" for the debug test until about 500 ms or the first exact pair, or seeding `outLatNs_` from `AAudioStream_getBufferSize + framesPerBurst`.
- F6 (JNI UTF-8): the conversion is FIXED (see item 5 below), but the line that uses it does not compile (R1).
- F7 (held vs immediate order): FIXED: `sendTo` calls `flushDeferred(now)` before sending or holding for a port without timestamps; I found no recursion (`flushDeferred` calls `PortIo::send` directly, never `sendTo`/`clearDev`), no starvation (one pass, at most 256 entries per message) and no cross-device effect that matters (streams are per device; a due message of device Y is sent a little earlier than its own flush would, which is within the 50 us rule). A held message can still be overtaken only if the core's map moves a later message earlier than an already held one (jump of the smoothed map): note.
- F8 (note): left, now counted (`early>3B`). Fine.
- F9, F10: no code, as stated. F9 residual above.
- F11 (CMake conflict): FIXED: merge d69edb9 keeps both the `test_midi_glue` block and the test block.
- CodeRabbit "partial attach" (MidiPorts.kt): FIXED in logic. `missing` is filled in `onOpened` from `mine` minus `ok`, removed when the device leaves, and `retryMissing()` attaches only those tokens to the already open device; ports already attached are untouched; a second `nativeAttach` creates a second `AmidiDevice` handle for the same Java device, which is fine (each port keeps its own). It runs only on a debug-UI tap (`retryFailed()` from `cycleA`/`runTest`), never in a loop, as intended. Not compiled by anyone (Kotlin).

## New findings

### R1 (BLOCKING): `midi_jni.cpp:140` does not compile
```
JNIEXPORT jstring ... nativeStatus(JNIEnv* env, jobject) { return env->NewStringUTF(tpa::toJniUtf8(tpa::host().status()).c_str());   // NewStringUTF wants MODIFIED UTF-8: ... under CheckJNI }
```
The closing `}` of the function is inside the `//` comment. The next line `}  // extern "C"` then closes the function, and the `extern "C" {` block is never closed: `error: expected '}' at end of input` (I reproduced the pattern with g++ -fsyntax-only). This file is Android-only, so the Linux job cannot see it; it breaks "Core (Android arm64-v8a, NDK)" and "Android shell v0 (debug APK)". Fix: move the comment above the line or put the `}` before it. (Also a reminder that Android-only files need at least a syntax check on Linux with stub headers.)

### R2 (should-fix, device-only, note-level until measured): flush may drop a due-but-undelivered note-off, and flush is not private to this app
`clearDev` -> `PortIo::flush` -> `AMidiInputPort_sendFlush(port_)` for a stamped port. Core replay (`midiport.cpp:178-186`) re-sends only entries with `h.at > t` (t read in `applyClear`). A message with `at <= t` is assumed delivered. In Android the stamp is honoured by the MIDI service/driver scheduler on its own thread, so a message with `at` just before `t` can still be pending when the flush arrives (scheduler lag of about a millisecond or more): it is discarded and not replayed. Sequence: note-off stamped T+5.0 ms, Stop at T+5.4 ms, scheduler delivers at T+6.0 ms: flush at T+5.4 discards it, replay skips it (at <= t): stuck note. Also, the flush is delivered to the device's receiver for that port, which other clients of the same port share (my understanding of the Android MIDI service, unverified), so another app's scheduled data on that port can be discarded. Suggest: replay with a small margin is not available in the core, so either measure on the Fold with the owner test (a stuck note after Stop on a USB synth is the symptom) or do not flush and rely on the 20 ms rule for TS ports.
Signature check of `AMidiInputPort_sendFlush(const AMidiInputPort*)` -> `media_status_t`, API 29: I believe it is right from memory of amidi.h; no NDK here, UNVERIFIED (the Android CI job will compile it once R1 is fixed).

### R3 (note): `clearDev` duplicate window is up to 50 us, not 1 us
For a port without timestamps `sendTo` sends a message immediately when `at <= now + 50 us` (`midi_out.cpp` ~line 303). If a CLEAR is applied in the last 50 us before that message's `at` (core `t < at`), the message was already sent (not in `held_`, so `clearDev` cannot drop it) and the core replays it (`h.at > t`, frame before the marker): a duplicate. A duplicate note-on precedes the Stop's note-offs, so no stuck note; a duplicate clock tick or CC is harmless. Probability about 50 us per message at the moment of Stop. Also the replay uses `sendTo`, which holds it again: delivered once at its time otherwise, as the test checks.

### Order and loss analysis of `clearDev` (item 1), as I could reason it
- Thread: the core calls `sink.clear` and `sink.send` only from inside `thunder_midiout_pump` (`applyClear` at pump start, `midiport.cpp:320`), so both run on the sender thread; `held_`/`nHeld_` are sender-only: no race. `engineStop`'s final pump also runs on the sender.
- Core calls `clear(dev)` only when the history holds a message of that device with `frame > marker.frame` and `at > t`. All held messages come from such handed-over messages (they enter the history when `early && at > now`). A held message not in the history (its `at` was already past when the pump re-read the clock after a slow `flushDeferred`) is due almost at once; `clearDev` sends it if `h.at <= now`, so I found no loss path from that.
- `clearDev` drops held entries with `at > now` for the device: those with `frame > marker` are also dropped by the core (correct); those with `frame <= marker` are replayed by the core through `sendTo` and held again, in ring (time) order: delivered once. Due entries are sent now, and the core does not replay `at <= t` entries. The note-offs that follow the CLEAR are queued after the marker and never in the history at that moment, so the order rule of midiport.h holds (a CLEAR recalls what was queued before it, never the note-offs after).
- Duplicate note-on: only R3. Lost note-off: only through R2 (stamped ports).

### R4 (note): `outLatNs_` and `audioBlock(frames, t0)` (item 4)
Audio thread now reads `outLatNs_` (an `std::atomic<int64_t>`, relaxed, lock-free on arm64/x86-64) and `t0`: no lock, allocation, JNI or non-atomic read of a field written by another thread was added. `cbStart` is read before `setFlushToZero`: harmless. `heldCount_`/`earlyLong`/`heldCleared` are not touched by the audio thread.

### Item 5 `toJniUtf8`: OK
Bounds `i + len <= n` handle a truncated sequence at the end (becomes `?` per byte); lead bytes 0x80..0xC1 and 0xF5..0xFF give `len == 0` -> `?`; overlong 2/3/4-byte forms, values above 0x10FFFF and encoded surrogates (ED A0..BF) are rejected (each byte becomes `?`); 4-byte forms become two 3-byte surrogates; NUL becomes C0 80. No out-of-range read found. FIXED (modulo R1).

## Tests (item 7)
New tests and what they kill:
- `testMapPair`: `+off` instead of `-off`, dropping the `written - F` term, removing the 20 ms rate limit (old #1, #5).
- `testMapWindow`: removing or widening the `ns` window (old #2).
- `testHeldUntilDue`: held message sent early or late (`got` against its time) (old #3, partly #4: only coarse changes to `kEarly`/`kMaxSleep`).
- `testOrderHeldVsImmediate`: removing the `flushDeferred` call in `sendTo`.
- `testStopMidSongNoTs` (40 ms latency): the old 15 ms drain, no wait for `heldCount_` (old #10).
- `testClearDropsHeld`: no `clearDev`, `clearDev` not dropping post-marker held notes, replay delivered early or twice for a note-on before the marker; a stamped port is flushed once.
- `testDeviceGone`: ignoring a send result (old #6); `testJniUtf8`: the converter; `tableUsableFromDtor`: destroying a port under the table lock (old #8).
- `testHeldCap`: sends 356 messages and says the cap path is unreachable through the core, so it cannot kill a mutation of the `kHeld` branch (old #7 still uncaught).

Still uncaught (of my earlier ten): #7 (the `kHeld` and `len > 3` branches), #9 (`copyClamped` at 95 bytes, `setOutLatencyNs` clamp, the E_BUSY retry in `applyDevicesLocked`), and the sleep constants beyond a coarse change (#4). New uncaught: removing the final `flushAllHeld` in `senderMain` (the builder says so; the drain covers it); the `h.at <= now` send inside `clearDev` (single device and no duplicate check in the test); `clearDev` matching `h.dev == dev` (one device in the rig: dropping other devices' held notes would pass); the fallback `+ outLatNs_` (indistinguishable from the core's floor); `AmidiPort::send` partial-write and `flush` (Android-only); `retryMissing`; and a duplicate in the 50 us window (R3).

## Unverified without a device (new)
- Everything in midi_jni.cpp, jni.cpp and MidiPorts.kt: never compiled by the builder, and R1 shows they do not compile today.
- `AMidiInputPort_sendFlush`: existence and signature from memory; what the flush discards (R2), whether it affects other clients of the port, and its effect on USB note-offs already stamped.
- Whether partial writes happen at all; the cost of the extra `flushDeferred` per message on a real phone.
- The owner test should add: after Stop on a USB synth, no stuck note (R2); and the `held`/`heldCleared` counters on a Bluetooth or virtual port.
