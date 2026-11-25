# N2: the Android app on the live engine. Inventory and plan

Written 24 Nov 2026 by the N2 builder (Sonnet), unattended overnight, for the lead's review. Branch `n1/n2-live-shell` on `native` 980a5e42 (C2a). Every claim has a file:line;
what is a guess says GUESS. `live.h` lines are those of `origin/n1/c2-live` (`6ff23811`, the chains host), because #311 had not landed when this was written; the
chains additions (F_CHAINS, CAP_CHAINS, `thunder_live_chain_edit`, `chains_bytes`) are guarded in my code by `#ifdef THUNDER_LIVE_F_CHAINS`, so it builds against both.
Progress and status of the build are in `docs/port/N2-NOTES.md`.

## 1. What the N0 / U36 test app does today

**One process-wide engine, the OLD small core.** `native/android/app/src/main/cpp/jni.cpp` (280 lines):
- One global `Engine g` (jni.cpp:28-51) holds a `thunder_core*` (the N0 test core of `core.h`: a command ring, a clock, a debug sequencer; NOT `thunder_live`), a miniaudio device (AAudio only: `CMakeLists.txt:16-19`; miniaudio, not Oboe), a 4096-frame scratch pair and a probe thread.
- `nativeStart` (jni.cpp:169-200): `ma_device_init` f32 stereo, `sampleRate = 0` (the device's rate), low-latency profile, usage media; then `thunder_create(sr, 4096)`; resolves two libaaudio counters with dlsym BEFORE the stream starts (jni.cpp:185-187); starts the MIDI out host (`tpa::host().engineStart`, jni.cpp:188); starts the probe thread; `ma_device_start`.
- THE AUDIO CALLBACK `dataCallback` (jni.cpp:69-89), reviewed by the lead on 15 Nov: flush-to-zero (jni.cpp:73, arm64 FPCR bit 24), the A5 logger begin / end (`rtlog`), `thunder_process` in pieces of at most 4096, interleave, then `midi->audioBlock` (the MIDI scheduler's block and the clock map). No allocation, no lock, no log, no JNI. The AAudio timestamp is read by this callback only, every 20 ms, into a seqlock (jni.cpp:98-109); the probe thread reads the snapshot (jni.cpp:132-139).
- `probeLoop` (jni.cpp:111-163): every 100 ms reads xrun count and output latency, calls `thunder_host_stats` (jni.cpp:153) and `setOutLatencyNs` for the MIDI clock map (jni.cpp:151).
- `nativeStop` (jni.cpp:202-216): joins the probe, `ma_device_uninit` (stops the callback), `engineStop`, then `thunder_destroy`. Order matters and is the model for the live stop.
- Commands in: `nativePostBytes` (jni.cpp:222-230) UTF-8 bytes, copied out of the JVM array, into `thunder_post` (one producer at a time); bulk audio: `nativePutBytes` (jni.cpp:232-244).

**Kotlin.** `NativeEngine.kt` (21 lines) is the JNI face. `ThunderHost.kt` (131) owns the engine, a poller thread (`pollLoop`, ThunderHost.kt:76-96: `nativePoll` in a loop, 5 ms sleep when idle, pushes the A5 snapshot every 250 ms) and `command(ctx, json)` (ThunderHost.kt:121-130: `open` / `close` / `play` / `stop` manage engine and service, anything else goes to `nativePost`). `shutdown()` (ThunderHost.kt:98-110) waits 2 s for the poller before destroying the core (N1 review A4). `ThunderService.kt` (49): a foreground service of type `mediaPlayback` (AndroidManifest.xml:30-33), one notification with ONE action, Stop (ThunderService.kt:24-31); `onDestroy` calls `ThunderHost.shutdown()`. `MainActivity.kt` (176): the real `index.html` in a WebView served by `WebViewAssetLoader` (MainActivity.kt:98-103), a native overlay with the three 5-minute clock runs (A6), `RESET`, `COPY RESULTS`, the MIDI out test (U36 T9).
**The bridge.** `Bridge.kt` (147) injects `res/raw/thunder_bridge.js` at document start, opens a `WebMessagePort` pair after each page load (Bridge.kt:55-75) and forwards: `hello` is answered with `core: "n0-test"` and `caps` all false but `nativeAudio` (Bridge.kt:86-89); `put.header` + ArrayBuffer chunks into `nativePutBytes` (Bridge.kt:93-103); everything else into `ThunderHost.command`. Core events come back through `ThunderHost.listeners` as JSON text.
**What it does NOT have (all by grep of `native/android`):** no audio focus (no `AudioManager` / `AudioFocusRequest` anywhere), no device-change or headphone handling (no `AudioDeviceCallback`, no `ACTION_AUDIO_BECOMING_NOISY`), no `MediaSession`, no Play action in the notification, no song or pad loading (the test core plays a debug pattern), no `thunder_live_*` call anywhere. The test app never touches `LiveHost`.
**Tests:** `MidiModelTest.kt` (118 lines, JUnit, `src/test`): pure-JVM tests of `MidiModel.kt`; CI runs `gradle ... :app:testDebugUnitTest :app:assembleDebug` (`.github/workflows/native.yml:231`), job "Android shell v0 (debug APK)" (native.yml:214).

**What the live host offers instead.** `thunder_live_*` (`live.h`): create (live.h:176), `process` on the audio thread only (live.h:189), `stats_get` (:260), `engine_offset` (:265), `song_publish` (:293), pads (:309-357), `collect` (:363), `play` (:398), `stop` .. `perf` (:403-422), `mix_set` (:442), `fx_*` (:458-467), `chain_edit` (:498), `midi` / `midi_drain` (:532), `poll` (:638), `meters_want` (:642), `host_stats` (:634). Thread model: one CONTROL thread at a time, one AUDIO thread (live.h:14-18).

## 2. How the page will drive `thunder_live_*`

The page-side client does not exist yet: `window.thunderCore` on the web copy is U37 P6..P10 (WORKPLAN row 11), and `index.html` is not touched by native work. The Android shell's job is to answer the CORE-BRIDGE messages (`CORE-BRIDGE.md` section 3, BRIDGE_V 1) with `thunder_live_*`. The map:

| CORE-BRIDGE message | live.h call | Gap |
|---|---|---|
| `hello` (ask) -> `{v, core, caps}` | `thunder_live_abi()`, `thunder_live_caps()` (live.h:166-167) | `caps.engines` / `caps.fx` lists are the shell's to compose from caps bits + the shell's own pad renderer; no single call |
| `song.load {S, vars, rev}` | `thunder_live_song_publish` (:293): whole text, `rev` echoed in the SONG record | none |
| `song.patch {ops}` | none: the host takes the whole text (ruling D3); the shell keeps `core.mir`-style patching OUT: the page sends the whole song, debounced | the page must send whole songs (a CORE-BRIDGE amendment: `song.patch` stays web-copy only) |
| `kit.load` / `kit.patch` | `pad_kit` (:318), `pad_route` (:322); but the kit's voices are rendered, see 3 | `kit.patch` for a voice change = re-render that pad + `pad_set` + `pads_publish` |
| `put` + `buf.use {id, pad, layer, kind}` | `pad_set` (:309) / `pad_take` (:315, block from `thunder_live_alloc`, saves a copy) for kind 'pad'; `var_set` (:328) for 'var' (MUT / GEN); `pad_chop` / `pad_hits` (:333-334); then ONE `pads_publish` (:357) | kinds 'sample' (a layer's sample), 'ir', 'wt', 'sf' have no call: samples / IRs / wavetables are inputs to the pad render, not to the host. `sf` is E_UNSUPPORTED (CAP_SF unset) |
| `buf.free` | none needed: `pad_set` of 0 frames removes; retired buffers are freed by `collect` | none |
| `play {mode,row,at}` | `thunder_live_play` (:398) (`t0` is a host frame; `at` -> `t0`) | `play` has no `at`, only `t0` |
| `stop {at}` | `thunder_live_stop` (:403); `hold` (:407) | none |
| `loop`, `bpm`, `lead`, `hold`, `perf` | `loop` (:410), `bpm` (:413), `lead` (:416), `hold`, `perf` (:422) | none |
| `lxp` | none (U04 drum module not wired: live.h:78 "NOT IN THIS API") | a song that uses it falls back |
| `mix.set {tr,k,v,ramp}` | `thunder_live_mix_set` (:442): keys VOL/TRIM/PAN/MUTE/SOLO/OUT; **`k:'send'` has none** (key 5 is E_ARG, live.h:440) | `send`: the page folds sends into the song text; confirm in H1b |
| `fx.set {tr,slot,p,v}` / `{on}` | `fx_plan` + `fx_commit` (:458-463) for structure, `fx_param` (:467, 12 values) for a knob; master: `master_config` (:473); chains params: `chain_edit` (:498, needs F_CHAINS) | pump slots have no FXV / `fx_param` (PICKUP 24 Nov 07:35); a `fx.set` on a pump param falls to a re-plan |
| `note.on` / `note.off` | **none** ("note.on / note.off from the host: D16", live.h:78) | keys, pad previews, MIDI thru stay with the page = the old engine (Web Audio in the WebView), or H1c adds a call (see 6) |
| `meters.want` | `thunder_live_meters_want` (:642); records METERS (type 4) | `fx` and `spec` lists are not in the record (304 bytes, live.h:590) |
| `midi.cfg`, `midi.in` | `thunder_live_midi` (:532) + `thunder_midiout_*` (U36, `midi_out.cpp` in the test app) | the test app's MIDI host owns its OWN `thunder_midi`; H2 re-points it at the live host's (U37-P5-NOTES "What the Android shell will call") |
| `dev.list` / `dev.open` | none in the core: the shell (AudioManager) | H1c |
| `rec.*`, `link.set` | none (not in the live API: live.h:78) | later stage (U36 Link / recording on the old test core) |
| `render.*` | `thunder_render_*` (render.h), a different handle | out of N2 (H1 does not render songs) |
| event `clock` | CLOCK record (type 1, live.h:564) | the shell fills `outLat` and `under` from AAudio (`host_stats`, live.h:634) |
| `pos`, `rows`, `meters`, `song {rev,f}`, `err` | POS 2, ROWS 3, METERS 4, SONG 7, ERROR 9 (via `poll`, live.h:638) | the shell turns the packed records into the bridge's JSON |
| `dev`, `render`, `rec`, `link`, `midi.ports` | shell-made | |

The poll record `PADS` (type 8, live.h:608) tells the shell a pads snapshot is audible.

## 3. How the pads get filled

The page renders every kit voice into a pad: `ensure(i)` (`index.html:8856-8862`) calls `renderVoice(v, padSR(), st, isSyn(i) ? v.len : 0)` (`index.html:4979`), where `padSR()` is the AudioContext rate (`index.html:8776`), `isSyn(i)` is `kit.held[i]` or `i >= NPAD` (NPAD = 8, `index.html:4919, 4924`). The core gets only `{choke, syn, rel, live, lk, sf}` per pad (`coreKitV`, `index.html:23537-23541`) and `vars` `{vtrk, NSLOT, TFX}` (`index.html:36497`).

**What the native core can render itself, as C++ (not in any C API).** The U22..U29 and U33 engines are in `native/core/src/engine/`:
- `thunder::eng_voice_from_json` (`engine_json.h:19-20`) + `eng_render_voice` (`engine.h:124`, the buffer size from `eng_render_bound` `engine.h:121`): the layer stack (sine .. grains .. Plaits / Elements; every layer type the JS has, U22-U25).
- `thunder::xeng_voice_from_json` (`xeng.h`, "Fills V from a song voice's JSON") + `xeng_render_voice` (`xeng.h:~136`): the whole-voice engines: tone, modal, swarm, drift, piano, ep, organ, breath, strings, fmdrum, wind, phase, partials, skin, chip, table, vector, scanned, resynth, bytebeat, acid, mach, blip, bass, vox, and the LXR voice kinds (`XengVoice::lxr`).
- The pad's own tail: gate / hold, `eng_env_lines_fit` (`engine.h:131`) for envelopes in lines, `eng_voice_grel_ms` (`engine.h:134`) for the release the kit entry carries.
- The tested ones are bit-checked against the JS in their units (`tests/test_engine*.cpp`, parity scripts `tools/parity-u22..u29.sh`).
- **Not ported:** the pad EFFECTS other than Degrade / Reson / PitchDelay / PitchReverb (`engine.h:12-13, 84`: `EngFx::Kind::Other` is a no-op, "the tfx wasm / macro wasm / MODFX pad effects"); the SoundFont engine (`sf`); `wavesl` and the wasm-backed engines (`xeng.h:8`); the pump synths' pad render is U33 (`pumpsynth.cpp`; to verify before H1b).
- No public C API renders a pad: `render.h` header says pad renders are "NOT IN THIS API" (`live.h:78`), `U34A-NOTES.md:85` ("the host renders the pads itself"), and WORKPLAN calls "pad renders in the core" an N3 remainder (WORKPLAN.md:179-183).

**ELECTRO, by measurement of its file (`sounds/ELECTRO.thunder`, `kit.voices`, 40 voices, `kit.slots` 24, no `kit.held`, so `isSyn(i) = i >= 8`):** 30 layer-stack voices (types sine, saw, white, pink, metal, square, tri, membrane, fm2, click, chord, bell, supersaw: all ported), 2 `lxsnare`, one each of tone, blip, skin, acid, mach, strings, bass, vox: ALL of them are engines the core has. Pad effects: 2 voices carry `spring` and `crush` (not ported: the core would play those two pads DRY). No sample layers, no SoundFont, no `wtData` voice beyond what census shows.
So **for ELECTRO the shell can fill every pad itself, with two pads dry**, calling the core's internal C++ from the shell's own native code (the test app already has `core/src` on its include path: `CMakeLists.txt:14`). This is what H1a does (a `PadFiller`, `native/android/app/src/main/cpp/pad_filler.cpp`): for slot i: parse the voice, `xeng_voice_from_json` else `eng_voice_from_json`, render at the stream rate with `hold = isSyn(i) ? len : 0` and `bpm` of the song, `thunder_live_pad_take`, kit entry `{choke, syn, rel}`, then one `pads_publish`. **GUESS:** the pad is rendered at the device's rate (the page's `padSR()` is also the context rate), mono; a stereo pad (`d.r`) only from a sample layer: not done in H1a.

**What MUST come from the page through `put` / `buf.use`:** (a) pads whose voice the core cannot render (SoundFont, wasm engines, a pad with an unported effect when the owner wants it exact), as float32 over the bridge (`put` 1 MB chunks, CORE-BRIDGE section 2; `pad_take` takes the finished block); (b) sample layers (the page decodes `sample.b64`; the C++ `layer_sample.cpp` takes the samples as input: **GUESS**, to check in H1b), IRs, wavetables (`wtData`); (c) MUT / GEN variation renders (`vbuf`, `var_set`): variations are made by the page's tracker commands at play time. A song with MUT / GEN cells has NO variations in H1a (counted as `notes_var_missing` in the stats: live.h `thunder_live_stats.notes_var_missing`).
**Chop data** (`pad_chop` / `pad_hits`): needs the pad's loop length and slice points, which the page computes from the rendered sample; H1a skips it (an instrument with `slc > 1` is silent-wrong); H1b computes loop_secs from the pad render.

## 4. The fallback rule

"A song the new engine cannot play goes to the old engine, with a short message" (PICKUP.md, 21 Nov 18:18 section, the owner's decision; the old engine is the page's own Web Audio inside the WebView).
**Where it is decided:** in the shell (Kotlin `SongFit`, with a C++ scan for speed on a large song), ONCE per song load, BEFORE the page's first `play`, from (1) `thunder_live_caps()` of this build and (2) a scan of the song. The page learns the verdict in the `hello` reply (`caps`) and in a `fit {ok, reasons[]}` event; when `ok` is false it keeps Web Audio and shows the message. The shell never plays half a song.
**Rules (each a reason string; GUESS where marked):**
1. `caps` lacks EFFECTS (build without the effects library): reason "no effects in this build" (live.h:117).
2. The song has chains (any `CHN` on with a chain patch) and the host lacks F_CHAINS / CAP_CHAINS (live.h c2-live:136): "chains need the new host" (until #311 lands; then never).
3. The chains patch uses a Sidechain In / Out module (`scin` / `scout`, silent in this version: live.h c2-live CHAINS paragraph); a song with a Resample take; the stretch (`sync` 2 / 3, `sx`: live.h:78); the U04 drum module command `lxp`; a pump module in a live chain with no spare (live.h:78): each a reason.
4. A voice whose engine is `sf` (CAP_SF unset: live.h:132-133), `wavesl` or another wasm-backed engine, or whose engine / layer type the core's pad renderer refuses (`kEngUnsupported`, `eng_render_voice` result): the pad can not be filled, and the page cannot be asked mid-load. Reason names the instrument. (Once the page client exists, H1b asks the page to `put` that pad instead of falling back: the fallback then applies only if the page cannot either.)
5. A pad effect the core does not host (`EngFx::Kind::Other`): **not a fallback in H1a** (the pad plays dry, with a one-line "N pads without their pad effects" note); the lead decides in review whether it must be a fallback (a dry pad is an audible difference). GUESS: owner prefers the note, because ELECTRO itself has two.
6. The song text over `opts.max_song_bytes` (default 16 MiB) or a pad set over `max_pad_bytes` (a phone passes 128 MiB, live.h:`DEFAULT_PAD_BYTES`): E_LIMIT -> fallback.
7. Any `thunder_live_song_publish` / `play` that returns E_SONG, E_LIMIT, E_MEMORY, E_UNSUPPORTED: fallback with the error text (`err` buffer of `song_publish`). E_BUSY is never a fallback: retry.
8. A dead handle (E_INTERNAL / E_STATE from any call, live.h:44-47): stop the stream, send all-notes-off, rebuild the engine once; a second death: fallback for the session.
9. MIDI in / Link / recording are not live-engine features at all (live.h:78): when the page asks for them the old engine keeps them (they live in the page); not a "song" fallback.
The detector is the census (`native/tools/census.mjs`, FXC list and engine list) re-done in C++ over the parsed song; it shares the list with the census test so it cannot drift (GUESS: a unit-test-sized task).

## 5. Audio focus, screen off, foreground service, device change (H1's list)

None of this exists today (section 1). Design (all Kotlin, control side; the audio thread never sees any of it):
- **Audio focus.** `AudioManager.requestAudioFocus(AudioFocusRequest.Builder(AUDIOFOCUS_GAIN).setAudioAttributes(USAGE_MEDIA / CONTENT_TYPE_MUSIC).setOnAudioFocusChangeListener(...))` on Play. LOSS (another app, a call): `thunder_live_stop` + stop the stream (a song is not resumable from the middle in a useful way: GUESS, the owner decides). LOSS_TRANSIENT (a call, a navigation voice): pause = `thunder_live_hold` is wrong (it holds at a pattern edge); use `stop` and remember the row, resume on GAIN with `play {row}`. LOSS_TRANSIENT_CAN_DUCK: ramp `mix_set(MASTER, VOL, ...)` down to about 30 % with `ramp` frames (live.h:442), back on GAIN. The decision "stop or duck" is one Kotlin function, pure, unit-testable on the JVM.
- **Headphones / device change.** `ACTION_AUDIO_BECOMING_NOISY` broadcast (headphones pulled): stop. `AudioManager.registerAudioDeviceCallback`: a USB / Bluetooth device added or removed. miniaudio's AAudio backend restarts a stream the system disconnected (check: `ma_device` notification callback; **UNVERIFIED**, GUESS); if the rate changes, the engine is rebuilt (create takes `sample_rate`, live.h:149): stop, `destroy`, `create`, re-publish the song and pads (a second pad render, 1 to 3 s on the Fold, GUESS), no `play` until the user presses it. A re-anchor from a restarted stream is NOT an error: `process` takes the new `frame0` and the engine keeps its gapless count (live.h TIME paragraph), so a stream that restarts at the same rate carries on without a seam.
- **Screen off, app in the background.** A foreground service of type `mediaPlayback` already holds the process (AndroidManifest.xml:30-33; no `WAKE_LOCK` needed, per the comment there and WORKPLAN A3). The WebView may stop its timers: the page's interface can pause, audio must not (H5). The engine's own threads (control, poller) are plain Java / C++ threads in the service's process. Doze: the same.
- **The notification.** Play and Stop actions (today only Stop: ThunderService.kt:24-31): `ACTION_PLAY` starts the loaded song (needs a "loaded song" in `ThunderHost`), `ACTION_STOP` stops. A `MediaSession` (media controls on the lock screen; Android 13+ shows the media notification style only with a session token): H1c, GUESS 3 h.
- **Thread plan.** AUDIO: the miniaudio callback (one thread, the system's). CONTROL: ONE Kotlin thread that makes every CONTROL call (song, pads, play / stop, `collect` every 5 ms, `E_BUSY` retry queue); the UI thread only enqueues. POLLER: one thread calling `poll` (ANY thread, one consumer: live.h:545 "ONE consumer thread") and `stats_get` / `host_stats` pushes. Destroy order (live.h:181, U37-P5-NOTES): stop the stream, `stop` + one more `process` is NOT possible after the stream is gone, so: `thunder_live_stop`, wait two callbacks, stop the stream, join, `destroy`.
- **Underruns / latency.** Callback budget: miniaudio's period (N0 measured 26 ms output latency, bunched callbacks up to 41 ms: RESULTS-N0.md); `max_block` = the burst size doubled, GUESS; the core renders 128-frame quanta from any call size (live.h THE QUANTUM). `thunder_live_host_stats(L, outLatencyFrames, xruns)` every 100 ms as the probe does today.

## 6. Work breakdown (PRs of one builder each)

| PR | Content | Needs the lead's read on the top model |
|---|---|---|
| **H1a** (this branch, started tonight) | JNI binding of `thunder_live_*`; `live_engine.{h,cpp}` (the audio callback body, a control thread with `collect()` and the E_BUSY retry queue, a poller thread); `LiveEngine.kt`; `PadFiller` (the core's own pad renderer for the bundled song); "Play bundled song natively" button with `sounds/ELECTRO.thunder`; the service's Play / Stop; N0 path kept; JVM test | **YES**: `live_engine.cpp` callback + threading (about 150 lines) and the destroy order |
| H1b | The CORE-BRIDGE messages on `live_*`: `hello` / caps, `song.load`, `kit.load`, `put` + `buf.use` -> `pad_take`, `play` / `stop` / `bpm` / `loop` / `mix.set` / `fx.set` / `meters.want`, the poll records -> bridge JSON (CLOCK, POS, METERS, STATE, SONG, ERROR), `SongFit` fallback + `fit` event | **YES** for the thread hand-off of events (a ring between the poller and the WebMessagePort); the rest no |
| H1c | Audio focus, noisy-broadcast, device callback and rebuild, notification Play / Stop, `MediaSession`, `host_stats` (xruns, latency), the A5 logger on the live callback (1.00 +- 0.1 % for 10 minutes) | **YES** for the rebuild-on-device-change sequence (destroy / create under a running stream) |
| H1d | Chains: `F_CHAINS` + `chains_bytes` for a phone, `chain_edit` for the page's `param` ops; after #311 merges | YES (it creates the chains memory budget on a phone; a number) |
| H2 | MIDI out on the live host: re-point `tpa::MidiHost` at `thunder_live_midi()`; `thunder_midiout_clock_map(engine frame = host frame - offset)` per callback (`thunder_live_engine_offset`); MIDI in (`midi.in`) through AMidi; loopback test | YES for the callback's clock-map call (audio thread) |
| H3 | Songs folder (SAF), `.thunder` open, newest-first list, changed-underneath warning | no |
| H4 | Signed release APK, in-app update check | no |
| H5 | Fold behaviour, back gesture, keyboard, screen-off WebView; `native/tests/ANDROID-CHECKLIST.md` | no |
| (page side) | `window.thunderCore` client in `index.html` for `put`, `song.load`, events: NOT in `native/`; it is U37 P6..P10 and the web chat's. Without it H1b can be driven by the test page `bridge-test.html` only | the lead decides who owns it |

## 7. Estimates (builder-hours: GUESSES from line counts and from how long N0 / U36 T9 took) and the owner's device tests

| PR | Builder-hours (GUESS) | Lead's review (GUESS) |
|---|---:|---:|
| H1a | 7 | 1.5 |
| H1b | 14 | 1.5 |
| H1c | 8 | 1.5 |
| H1d | 4 | 1 |
| H2 | 8 | 1 |
| H3 | 8 | 0.5 |
| H4 | 5 | 0.5 |
| H5 | 6 | 0.5 |
| page client (if one builder) | 16 | 1 |
| **Total** | **76** | **9** |
The CI cycle for an APK is long (Gradle + NDK, native.yml:214-244); budget two failed builds per PR (GUESS).

**The owner's device tests** (a test list for `HANDOVER.md` "Owner's punch list" once H1a is merged; nothing here is verified on a device):
1. H1a: install the APK from the PR's artifact; press "Play bundled song natively"; ELECTRO plays through the notification service; Stop in the notification stops it; screen off for 2 minutes keeps playing; the N0 three runs still PASS (the old path).
2. H1a: listen for the two dry pads (spring, crush) and compare to the web app; no clicks at start / stop.
3. H1b: the page's song plays natively; a fallback song shows the message and plays on Web Audio.
4. H1c: a call, a notification sound, headphones pulled, Bluetooth connect / disconnect, USB audio plug / unplug: each ends in a defined state (the table in section 5).
5. H2: the USB MIDI interface loopback (WORKPLAN H2 gate: owner's hardware).
6. N2 exit: ELECTRO and the owner's normal song with effects, 10 minutes screen off on the Fold: no gaps, 0 underruns; battery per hour against the web app (WORKPLAN.md:176-177).

## Open questions for the lead
1. Who writes the page-side client (`index.html`, `thunderCore` for Android)? Without it the page cannot send a song; the shell can still open a `.thunder` itself (H3) and fill the pads with the core's own renderer, which is enough for the owner's songs that use only ported engines.
2. A pad with an unported pad effect: dry plus a note (this plan) or fallback?
3. H1c: on LOSS of audio focus, stop (this plan) or pause at the row?
