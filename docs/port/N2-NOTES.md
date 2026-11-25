# N2 notes (builder status, newest first)

## STATUS (24 Nov): H1a BUILT on the host, waiting for CI on the APK
- DONE and run on this machine: `native/android/app/src/main/cpp/live_engine.{h,cpp}` (`LiveEngine::render` = the audio callback body, `controlLoop` = the ONE control thread with E_BUSY retry + `thunder_live_collect` every 5 ms, `pollLoop` = the poller + `thunder_live_host_stats`), `pad_filler.{h,cpp}` (`renderPads`, `extractMember`, `relOfVoice`), test `native/tests/test_android_live_engine.cpp` (ctest `android_live_engine`, Linux): ELECTRO publishes (9 publishes with no callback running: E_BUSY retried, `cmdBusy` 7), 38 of 40 pads render in 786 ms, plays 6 s, peak 0.89, no non-finite sample, `seqDropped` 0, poller sees PLAYING, destroy clean.
- WRITTEN, syntax-checked only (g++ with the JDK's jni.h and a stub android/log.h): `live_jni.cpp` (LiveNative JNI: start, shutdown, load project, play, stop, bpm, loop, mix_set, status, stats, rtlog). Kotlin written but NOT compiled here (no Kotlin / Android SDK): `LiveNative.kt`, `LiveModel.kt` (pure), `LiveEngine.kt`, edits in `ThunderService.kt` (Play + Stop actions, live notification), `ThunderHost.kt` (stops the live engine first), `MainActivity.kt` (three buttons + a status line), JVM test `LiveModelTest.kt`.
- `CMakeLists.txt` (android cpp) now builds `live_jni.cpp live_engine.cpp pad_filler.cpp`.
- NEXT: PR (draft), watch the job "Android shell v0 (debug APK)", fix Kotlin / NDK compile errors. Then merge origin/native when #311 lands (F_CHAINS is guarded by `#ifdef THUNDER_LIVE_F_CHAINS`, so nothing changes in the code but chains turn on).

## Decisions made without asking (contract-keeping)
- One miniaudio device per engine; the live engine has its own device and its own A5 logger (`nativeRtSnapshot` on `LiveNative`); the N0 core and the live host never run together (Kotlin stops one first).
- vars = NULL in `song_publish` (as `test_live_capi`): the page's TFX flags are not available without the page. The song plays with the core's built-in tables.
- The pads are rendered on the caller's thread (not control, not audio) and only the publish runs on the control thread.
- `F_MIDI` is on as asked but no ports are wired (H2): the MIDI outbox is never drained in H1a. If the ring fills, records are dropped and counted by the core (not verified with a long run: owner test item).
- The notification's Stop is the TRANSPORT stop (the service stays until "Quit native"); the N0 engine's Stop still ends the service.
- Host output latency given to `host_stats` is the AAudio buffer size (an upper bound, GUESS); H1c reads the timestamp.
- Not done: pad effects other than the four the core hosts (2 pads dry), the 2 LXR pads (module not linked), MUT / GEN variations, chop data, stereo pads, `kit.held` beyond the page's rule.
