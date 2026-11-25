# Independent verification: #316 `n1/n2-h2` and #322 `n1/n2-anr` (25 Nov 2026)

Reviewer did not write the code. Read-only review of the diffs (`n1/n2-h2` vs `n1/n2-h1c`, `n1/n2-anr` vs `n1/n2-h5`) plus the surrounding files. Kotlin was read, not compiled (no SDK).
Host tests built from the `n1/n2-anr` tree (it contains H2) and run: `android_a5_xrun`, `android_midi_in_fuzz`, `android_midi_sink`, `android_midi_live`, `android_live_engine` all PASS (incl. the new "destroy during an E_BUSY retry loop: call back -1000 after 1 ms").
Line numbers are from the branch named in each finding; "(anr tree)" = `n1/n2-anr`, which also holds all H2 code.

What I checked and found sound (no finding): the audio callback (`dataCallback`, `audioBlockLive`, `publishMap`, `liveTsRead`) takes no lock, allocates nothing, logs nothing, makes no JNI call; `liveTsRead` correctly has no `rerouteLock` (miniaudio's reroute job holds it across `ma_close_streams__aaudio`, which joins the callback). N0 `tsRead` try-locks, never waits. Lock order is consistent everywhere: `g_life` -> `g_dev` -> `rerouteLock`; `g_loadM` -> `g_life`; `ctl_` / `runMu_` / `t.mu` / `g_inMu` never nest the other way. `onDeviceNotification` (runs inside the reroute job with `rerouteLock` held) only does atomics. The load write-back guard `g.eng.get() == eng` has no ABA problem (the load holds a `shared_ptr`, so the address cannot be reused). `-1001` is never retried in Kotlin (no handling at all), and the MIDI runner returns it as an error, not BUSY. MIDI-in parser: fuzz is clean, running status / realtime-in-message / SysEx skip read correctly. MIDI-in port table: ports are closed under `t.mu`, so the reader never uses a closed port.

---

## #322 `n1/n2-anr`

### A1 (MED) LiveEngine.kt:46-58: stale `gen` wedges the engine: `up == true`, `active == false`, no shutdown queued
`start()` reads `gen = shutdowns.get()` and only afterwards enqueues the task on the worker. If another thread's `shutdown()` runs between those two statements (hello on the bridge thread vs `ThunderService.onDestroy` / `ThunderHost.ensureRunningLocked` on another thread), the order on the worker is: shutdown task (finds `up == false`, does nothing), then the start task. The start sees `shutdowns.get() != gen`, returns false after `nativeStart` has already opened the stream, MIDI host and service, and no shutdown task is queued behind it (the comment on line 57 says one is). `up` stays true, `active` false. Every later `start()` returns `if (up) return active` = false, so hello fails with "live engine did not start" until something else calls `shutdown()`. Window is tiny, outcome is a stuck engine with the stream running.
Fix: on the stale-gen path do the teardown inline: `up = false; LiveNative.nativeShutdown(); return false`. (Or read `gen` inside the same lock/step that enqueues.)

### A2 (MED) LiveEngine.kt:57-58: check-then-set race on `active`
`if (shutdowns.get() != gen) return false` then `active = true` are not atomic with `shutdown()`'s `incrementAndGet(); active = false`. Interleaving: worker passes the check, main runs `shutdown()` (active = false, shutdown task posted), worker sets `active = true`. Then the queued shutdown task runs `nativeShutdown` and `active` stays true with no engine: `status()`, `play()`, the pump and the notification treat the engine as up; `LiveMidi.engineUp()` and `startForegroundService` also ran for a dead engine. The generation counter was meant to prevent exactly this and does not close the window.
Fix: one lock (or a single `AtomicInteger` state) around "check gen + set active" and "increment gen + clear active".

### A3 (MED) live_jni.cpp:505 `nativePadTake` (and :488 `nativeFit`) still hold `g_life`, `nativePadTake` across `eng->call(..., 10000)`
The PR moves the three loads off `g_life`, but `nativePadTake` (page `put` + `buf.use`, the normal path for pads the core cannot render) takes `g_life` before `thunder_live_alloc`, the copy and the control call (up to 10 s, and its E_BUSY loop cannot be interrupted because shutdown needs `g_life` before it can reach `stopThreads`). So `nativeShutdown`, `nativeStart` and `nativeReopenStream` (all on the worker or the session thread) still wait up to 10 s behind one pad take, and `ThunderHost.ensureRunning` / `shutdownAndWait` (N0 "5 min" buttons, MIDI out test, page `open`) wait with them. Main is safe now, but the stated goal ("a shutdown must not wait behind a load") is not met for this path, and a stuck 10 s wait delays "Audio did not start" feedback and the stream-watchdog rebuild.
Fix: same pattern as the loads: copy `eng` and the kit entry under `g_life`, release, then alloc / copy / call.

### A4 (LOW) ThunderHost.kt:71 `ensureRunningLocked`: "a live start slipped in" case does not wait
After `shutdownAndWait()` (unlocked) another thread can complete a live start (hello). `ensureRunningLocked` then calls `LiveEngine.shutdown()` which only posts, and immediately `NativeEngine.nativeStart()`. The live engine's `nativeShutdown` has not run, so both engines open the output at once (the thing H1a forbids). The comment calls it a no-op; it is the one case where it is not.
Fix: in that branch release the lock and loop (`shutdownAndWait()` again), or have `LiveEngine.start`'s worker take ThunderHost's "N0 starting" flag.

### A5 (LOW) live_engine.cpp:102-106 `LiveEngine::call` can enqueue after `stopThreads` drained `q_`
`call()` checks `run_` and then pushes under `qm_` without re-checking `run_`. If `stopThreads()` ran between them (it clears `q_` after the joins), the task is never run or completed: the caller waits the full `timeoutMs + 500` (20.5 s for a load, 2 s for the MIDI runner, 10.5 s for pad take) before getting -1000. With the ANR change a load racing a shutdown is now routine, so this window is real, but only that caller's thread is stuck.
Fix: re-check `run_` under `qm_` before `push_back` (return -1000), and in `stopThreads` drain `q_` also after setting `run_ = false` but before the joins (see B4).

### A6 (LOW) live_engine.cpp:133 -1000 after a half-applied multi-step task
The comment says E_BUSY "changes nothing". True for one core call, not for the load lambdas (song publish and some `pad_take`s are already in when a later call returns E_BUSY). Harmless at shutdown (the engine is destroyed, the write-back is dropped), but `-1000` is documented as "dropped, nothing changed" for callers, and the MIDI runner maps `-1000` with `running() == false` to "run fn on the caller's thread". Only the engine's own teardown reaches it today; note it in live.h's -1000 contract so nobody retries on it elsewhere.

### A7 (LOW) LiveEngine.kt:107 (loadAsset) writes `loaded` / `projectName` after a concurrent `shutdown()`
`shutdown()` clears `loaded`; `loadAsset` (async thread) stores `loaded = r` after `nativeLoadProject` returns, even if the shutdown ran in between (the native side now drops the results and returns -1000/-1001, but a load that finished just before the shutdown returns ok). The stale `loaded` lives until the next start's `loadAsset`; `statusText` and the notification button state read it. Check `active` (and the generation) before publishing `loaded`.

### A8 (LOW) ThunderHost.command: `json.contains("\"open\"") || json.contains("\"play\"")`
A substring test on the raw JSON, not the parsed `t`. Any N0 message whose payload merely contains the word "play" or "open" (a name, a title) while N0 is not running shuts the live engine down. Parse `t` first (the locked half already does).

### A9 (LOW) xrun carry: xruns between the last status poll and the reroute are lost
`XrunGate.newStream()` carries `last_ - base_`, where `last_` is the value of the last poll (up to 500 ms old). Xruns the old stream counted after that poll are dropped. Documented as "too low for one round"; it is more than one round (permanent loss of those xruns for the A5 gate). Acceptable for a gate that wants "0 underruns", but a reroute that follows a burst can hide it. Could read `aaXrun(stream)` under `rerouteLock` in `onDeviceNotification`'s caller, but that is not available there (the stream is already closed), so say so in the notes.

### Verdict #322: MERGE AFTER FIXES
Fix A1 and A2 (a start that loses the race with a shutdown must leave nothing on), A3 (or reword the claim), and A4. A5-A9 can follow. The C++ lock changes (`g_loadM`, copy / write-back, N0 try-locks, `restart()` / `carried_`) are correct as written.

---

## #316 `n1/n2-h2`

### B1 (MED) LiveEngine/status path: `nativeStatus` calls `setOutLatencyNs` on the MAIN thread
`nativeStatus` (live_jni.cpp:375, anr tree; same code in h2) runs `tpa::host().setOutLatencyNs(newLatNs)` after releasing the locks. `LiveSession.tick` (main, every 500 ms), `onFocusChange` (main) and `ThunderService.postLive` call `LiveEngine.status()` on main. The first status after every engine start (and after every reroute: `lastLatNs = -1` reset) takes this path, and it goes through the control runner: `eng->call(..., 1500)`, up to 2 s on the main thread when the control thread is busy. That is exactly when the page loads its song and kit (control thread in a load lambda or its E_BUSY loop). Jank / near-ANR on start.
Fix: do not call it from the status path: have the poller thread (`LiveEngine::pollLoop`, which already forwards `outLat_`) or the control thread apply it, or make `setOutLatencyNs` store the atomic and let the audio-thread `publishMap` pick it up (it already reads `outLatNs_`; only `thunder_midiout_set_out_latency(o_, v)` needs the control thread).

### B2 (MED) MidiPorts.kt: single-thread invariant broken, plain HashSets / registry read and written from several threads
Class doc: "Everything here runs on the MAIN thread ... the registry needs no lock". H2 adds `publishPorts()` / `portsEvent()` which iterate `attached`, `attachedIn` and `registry.entries` and are called from the bridge worker (`LiveMidi.apply` after every `midi.cfg`, `LiveMidi.publish` from `hello`) while main mutates them in `rescan` / `onOpened` / `retryMissing` (hot-plug callbacks). `HashSet.contains` during a concurrent `add` / `remove` can miss or loop; the PR #322 `off{ runTest }` change then moves `runTest` -> `retryFailed()` -> `rescan()` and `cycleA()` onto a `thunder-overlay-work` thread, so the registry is now mutated from two threads. Debug button for the second, the real bridge path for the first.
Fix: make the three sets `ConcurrentHashMap.newKeySet()` (or guard with one lock) and keep `registry.entries` as the immutable snapshot it already is; run `runTest`'s rescan on main (post it) or lock.

### B3 (MED) midi_jni.cpp:`inLoop` reader never stops, polls at 1 kHz after the engine is gone
The reader thread is created on the first attach and lives for the process. After `nativeShutdown` the sink is null but the AMidi ports stay attached (MidiPorts keeps devices open across engine restarts), so the thread wakes every millisecond forever (`nanosleep` 1 ms) and parses and drops messages: a wakeup storm in the background (the foreground service has stopped), battery and thermal cost with a USB keyboard plugged in. Also, while no engine runs, MIDI-in messages are consumed and lost.
Fix: when `midiInSink() == nullptr` sleep 20-50 ms (still drain the port), or have `nativeShutdown` park the thread on the condition variable and `setMidiInSink` wake it.

### B4 (LOW) live_engine.cpp:53 `stopThreads`: queued tasks are only completed after the poller join
The poller's state hook (`setPlaying(false)` via the runner) can be blocked in `call()` on a task that the control thread never popped (it left its loop). `stopThreads` joins control, then joins the poller: the join waits for the hook's `call()` timeout (1.5 + 0.5 s), and only then drains `q_`. Shutdown stalls up to 2 s if a song ends at shutdown. Narrow, bounded. Fix: drain `q_` (complete with -1000) right after the control join and before the poller join.

### B5 (LOW) live_jni.cpp:369/375 `g.lastLatNs` is set before the call, result ignored
`setOutLatencyNs` returns void and its runner result (BUSY under a load, -1001) is dropped, but `lastLatNs` already says "applied". The MIDI clock map floor (`outLatNs_`) then stays at 0 until the buffer size changes or a reroute resets `lastLatNs`. Make `setOutLatencyNs` return the rc and set `lastLatNs` only on 0.

### B6 (LOW) midi_out.cpp:227 `viaControl`: `-1000` with the thread down runs `fn()` on the caller's thread while the control thread may still be finishing
`rc == -1000 && !eng->running()`: `run_` goes false at the start of `stopThreads`, before the control thread has exited its last task. A `setDevices` / `assign` / `setClock` from Kotlin in that window runs `fn` (thunder_midiout_* calls) on the caller's thread beside a control-thread `thunder_live_*` call: the F11 case the runner exists to prevent. The window is the join time of one task. Fix: make the runner distinguish "never started" (BUSY) from "no control thread" with a flag set after the join, not `run_`.

### B7 (LOW) MidiPorts.kt rescan / pushList on main: `nativeSetDevices` through the runner blocks main up to 2 s, and a BUSY result is never retried
`pushList()` runs on the main thread (hot-plug callback) and goes `viaControl` -> `eng->call(1500)`. Under a load that is up to 2 s on main; a returned `E_BUSY` (-3 / -8) only sets `note = "set_devices refused"`: the core keeps the old device list for good, so a plugged-in device never becomes selectable until the next scan. Re-push on BUSY from a worker, and do the unplug path (`pushList` then `nativeDetach`) off main.

### B8 (LOW) teardown's stop call can now time out behind a running load, so no MIDI Stop
`teardownLocked` does `eng->call(thunder_live_stop, 500)` first; before #322 a shutdown waited for the load (g_life), so the call always ran. Now a shutdown during a long load (control thread in an E_BUSY loop up to 20 s) lets that 500 ms call time out, the transport Stop is not posted, and `engineStop` (external mode) does not make its own Stop ("the live host made its own Stop when the shell stopped the transport"). A synth can keep a held note and a downstream clock keeps running. Rare (needs a shutdown during a load while playing). Fix: in external mode `engineStop` should also send all-notes-off / clock Stop through the sink when `thunder_live_stop` did not succeed.

### Verdict #316: MERGE AFTER FIXES
Fix B1 (main-thread block on every engine start) and B2 (racy shared sets on the bridge path), and B3 before the beta goes to a device with a USB keyboard. B4-B8 can follow. Parser, port table lifetime, sender thread, clock map, audio-thread path and external-mode create / free order are correct.

---

## Order of merge
#316 first (it is the base of #322 through h3-h5); fix A1/A2 and B1 in the same stacked branch before undrafting #322.
