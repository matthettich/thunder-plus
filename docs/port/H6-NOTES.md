# H6 notes: the desktop shell skeleton (N4; WebView2 on Windows, WKWebView on the Mac)

Builder: Sonnet worker, 25 Nov 2026, branch `n4/h6-desktop-shell` (worktree /home/claude/wt/h6). STATUS: in progress (see "Resume" at the bottom).

## What this is

One platform-neutral library, `thunder_desktop_shared` (native/desktop/shared), and two thin shells on top of it. The shells load the web app from `app://thunder/` (files copied from the repo root by a build step), inject
`window.thunderCore` (CORE-BRIDGE v1, same messages as the Android shell) and `window.thunderDesktop` (today's 31 Electron channels), and play audio through the core's live host (live.h) and miniaudio.

| Part | File(s) under native/desktop | Platform |
|---|---|---|
| app:// mapping (path rules, MIME, headers, report-only CSP) | shared/app_scheme.{h,cpp} | neutral, tested |
| thunderCore translator (port of the Android BridgeTranslator.kt) and put store | shared/bridge_translator.*, put_store.* | neutral, tested |
| the session: messages in, calls on the host, replies and events out (port of LiveBridge.kt) | shared/bridge_session.* | neutral, tested with the real engine |
| the live host: engine + output + song / kit / pad loads (port of the globals and JNI glue of live_jni.cpp) | shared/live_host.* (+ the Android shell's plain-C++ `live_engine`, `live_events`, `songfit`, `pad_filler`, compiled in place from native/android/app/src/main/cpp) | neutral, tested |
| thunderDesktop: the 31 channels, file rules, close and open-path hand-offs | shared/desktop_api.* | neutral, tested |
| audio out: miniaudio (WASAPI / Core Audio) and a real-time null device (CI, headless) | shared/audio_miniaudio.cpp, audio_null.cpp, miniaudio_impl.c | shells; null everywhere |
| page bootstrap (both objects, injected at document start; the `--selftest` gate) | js/desktop_bridge.js | neutral JS |
| Windows shell | win/main_win.cpp | WebView2 (NuGet, pinned in the workflow) |
| Mac shell | mac/main_mac.mm | WKWebView, Objective-C++, unsigned .app |

## Wire format (page <-> shell), one text channel each way
- page -> shell: `thunderCore` JSON text; `B:<base64>` = the bytes of the `put` chunk whose `put.header` came just before; `D:{ch,id,a}` = a thunderDesktop call; `S:<json>` = the selftest result.
- shell -> page: `window.__thunderRecv(text)`; text is JSON objects joined by `\n` (one script call per flush; events come from other threads and are batched on the UI thread). `{t:'dreply',id,v|err}` and
  `{t:'dpush',ch,v}` are thunderDesktop; everything else is a thunderCore reply / event, exactly as the Android shell sends them (`reply`, `clock` with `load`, `pos`, `meters`, `song`, `pads`, `kit`, `fit`, `err`, `engine`, `put.done`).
- WebView2: page -> shell `chrome.webview.postMessage(string)`, shell -> page `PostWebMessageAsString`. WKWebView: `webkit.messageHandlers.thunder.postMessage(string)`, shell -> page `evaluateJavaScript("window.__thunderRecv(<string>)")`.

## The 31 channels (BRIDGE-TODAY section 4; `kChannels` in shared/desktop_api.cpp is the table, test_desktop_api checks it: 19 handle, 5 send, 1 sendSync, 6 push)

| Channel | State in the shell |
|---|---|
| file-open, file-read, file-save, file-saveas, out-pick, out-write, out-show | **REAL**: system Open / Save dialogs (GetOpenFileName / GetSaveFileName; NSOpenPanel / NSSavePanel), the same file rules as electron/main.js (only .thunder / .json project files, only .wav / .zip / .mid renders, save writes `p.saving` then renames, folder memory for renders), Show in Explorer / Finder. `outWrite` takes the bytes as `{b64}` (the page's bootstrap converts the ArrayBuffer). |
| natural-scroll | **REAL** on the Mac (the global `com.apple.swipescrolldirection`), `false` on Windows (as Electron) |
| playing, reload | **REAL**: App Nap guard (`beginActivity`) / `SetThreadExecutionState`; reload of the web view |
| page-ready, close-ack, close-reply, close-ask (push), open-path (push) | **REAL**: the close hand-off (the page is asked first; a page that does not answer in 3 s does not stop the window closing; `close-reply false` keeps the window), the pending `.thunder` path held until the page is ready; Mac `application:openFile:` and a command-line `.thunder` argument feed it (Windows: command line only; double-click association and single instance are H8) |
| link-avail (sendSync) | constant in the bootstrap: `{available:false, why:'Ableton Link is not in the native desktop shell yet (H8)'}` |
| link-enable, link-sync, link-tempo, link-start, link-stop, link-now | **NOT YET**: answer `false` / `0` (Link off); link-state and link-time pushes are never sent. Link in the native shell is a later task (U36 / H8) |
| upd-state, upd-check, upd-act, upd (push) | **NOT YET (H8)**: `state()` answers `{version, avail:null, ready:false, notifyOnly:true, dl:null, file:null}` with the build's version; `check()` answers `false`; `act()` answers `null`; no push |
| io-start, io-mic, io-port (push) | **NOT YET (H7)**: `{ok:false, why:'...H7'}`; no port is ever pushed. `thunderDesktop.io.available` is `false` in the shell (the page's helper path is replaced by `thunderCore` for audio) |
| `thunderDesktop.power.on` (preload has it, not in the 31) | Windows `WM_POWERBROADCAST` resume and Mac `NSWorkspaceDidWakeNotification` push `thunder:power 'resume'` |
| `files.pathOf(file)` (runs in the page in Electron) | **NOT YET**: answers `''` (neither web view hands the page a dropped file's path); Save then asks where, as the website does |

## thunderCore (CORE-BRIDGE v1 plus section 7 / 7.1)
`shared/bridge_translator.cpp` is a message-for-message port of the Android `BridgeTranslator.kt` (its header comment is the shell's reading of the contract); `shared/bridge_session.cpp` is `LiveBridge.kt` (hello, song.load as an ask with the SongFit
reply, kit.load with the `kit` and `fit` events, put / buf.use / buf.free / pads.commit, play, stop, hold, loop, bpm, lead, perf, mix.set, fx.plan / fx.set / fx.commit, master.config, chain.edit, meters.want, pdc.set, pad.chop, engine.reset with
`engine up:false / up:true`, the 20 ms event pump with `load` added to `clock`). `shared/live_host.cpp` is the global state and JNI glue of `live_jni.cpp` as one class (start, teardown, song / kit / pad / var / chop loads, fit). The Android shell's own plain-C++
files `live_engine`, `live_events`, `songfit`, `pad_filler` are compiled in place (CMake path into native/android/app/src/main/cpp); they were written with no Android header on purpose. On MSVC a force-included `shared/posix_shim.h` gives them `clock_gettime`
and drops `__attribute__`; the Android files are not edited. **Duplicated logic (keep in step with the Kotlin):** the translator rules, PutStore, the LiveBridge execute switch, the song / kit / pad load bodies. If one side changes a rule, change the other and the two tests
(`LiveBridgeTest` and `test_translator.cpp`).
Refused with a clear err (and an error reply for an ask): `dev.list`, `dev.open` (H7), `note.on/off`, `rec.*`, `render.*`, `link.set`, `song.patch`, `kit.patch`, `mix.set` k `send`. `midi.cfg` is read, answers an `err` naming H7, and still tracks `clockIn` for the fit verdict.
`hello` caps: `nativeAudio:true`, `midi:false`, `files:false` (the page's file path is `thunderDesktop.files`), `link:false`, `inputs:false`, `render:false`; `host:'desktop'`. A `hello` without `live` is answered without starting anything (there is no N0 test core on the desktop).
`put` travels as base64 text (`B:`), 1 MB chunks (the contract's "file fallback" for a web view without a binary message channel; WebView2 SharedBuffer is a later optimisation). `take()` rejects (renders and recordings are H7 / N3).

## Audio
`LiveHost` opens one stereo f32 output at the device's own rate through miniaudio (WASAPI on Windows, Core Audio on the Mac: `shared/audio_miniaudio.cpp`, `io/vendor/miniaudio` compiled once in `miniaudio_impl.c`), makes the engine at that rate (F_EFFECTS | F_MASTER | F_METERS, plus F_CHAINS)
and runs `LiveEngine::render` in the callback (flush-to-zero, `thunder_live_process`; no allocation, no lock). **No audio-thread code in `native/core` changed.** The callback wrapper is new code that mirrors the Android one minus the A5 logger and the MIDI hook.
`--null-audio` (a real-time paced thread, no device) is what CI uses. Not here (H7): device list and choice, buffer size and rate, exclusive mode, a lost device (stream reopen), input, MIDI, the A5 clock-drift logger.

## The gate: "every channel answers; a menus-sweep equivalent passes in the shell"
- **Every channel answers: built and run in CI on both shells.** `--selftest` (both shells) loads the real `app://thunder/index.html`, waits 3 s, then runs `window.__thunderSelfTest()` from the bootstrap: awaits each of the 19 handle channels (5 s timeout each), sends the
  send channels, checks the sendSync constants and the push subscriptions exist, asks `hello {live:true}` (an answer is a `v:1` reply or an `err` reply; a missing answer fails) and `dev.list` (an error reply is the expected answer), and reports page errors. `judgeSelfTest` (C++, tested) requires all 31
  channel names plus `core.hello` and `core.dev.list` in the answered list. Exit code 0 / 1 / 2; `--report <file>` writes the text (a GUI exe has no console on a runner).
- **The menus sweep itself (`tests/sweep/menus.mjs`) is NOT run in the shell.** It needs a driver: Playwright over the Chrome DevTools Protocol. WebView2 offers one (`WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS=--remote-debugging-port=9222`, then `chromium.connectOverCDP`), but `ui-lib.mjs boot()` launches its own browser and
  serves the page over http (and `menus.mjs` needs `?seqtest=1`, which `app://thunder/index.html?seqtest=1` does reach), and `tests/sweep/` is outside the folders this task may edit. WKWebView has no CDP at all (Safari Web Inspector / WebDriver only): that is H9's problem
  (its gate names `layout.mjs` and `keys.mjs` equivalents in WKWebView). See "Decisions for the lead".

## What was checked where
- Linux (this box, -j1/-j2, GCC): the seven host tests `ctest -R desktop_` (app_scheme 24 checks, translator 47, put_store 26, desktop_api 114, live_host 17 (a real song and kit from sounds/ELECTRO.thunder play through the engine on a hand-driven device: finite, not silent, clock and state events), session 21 (the page client's whole sequence through the bridge with the real engine, a put of a pad, refusals, engine.reset), shell_core 11). The Linux jobs of the Native workflow run them under GCC, clang and ASan + UBSan.
- Windows and Mac runners (`.github/workflows/native-desktop.yml`): compile + link of the shell, the seven tests, the shell's `--selftest` with `--null-audio`, unsigned artifacts. See the PR for the run.
- NOT checked by anyone yet: sound out of a real device on either OS, the Save / Open dialogs by a human, drag and drop, the real keyboard, the owner's Mac. UNVERIFIED on hardware.

## Decisions for the lead (made by the builder; change any)
1. **Reuse, not move:** the Android shell's plain-C++ files are compiled in place from `native/android/app/src/main/cpp` (a path in CMake) instead of being copied or moved into a shared folder: moving them is a change to files outside this task. A later tidy-up can move `live_engine`, `live_events`, `songfit`, `pad_filler`, `pad_block.h` to `native/shell-common/`.
2. **Duplication accepted** for the Kotlin-only logic (translator, PutStore, LiveBridge, the load bodies): listed above; 400 lines of Kotlin versus a JNI-free C++ port that both shells could one day share (the Android shell could call the C++ translator through JNI and drop its Kotlin copy: a possible H-task, not done).
3. **WebView2 loader as a DLL** next to the exe (the static loader is /MT; the core is /MD). SDK pinned: Microsoft.Web.WebView2 1.0.2739.15 (the workflow's `WEBVIEW2_VERSION`). Licence text to be copied into THIRD_PARTY_NOTICES with the first shipped build (IF-SOLD row updated).
4. **Mac build is arm64 only** in CI (the runner builds the core in one architecture in about half the time; the owner's Macs named in memory are Apple Silicon). Universal can be a flag away (`CMAKE_OSX_ARCHITECTURES="arm64;x86_64"`). The CI step ad-hoc signs (`codesign -s -`) so the bundle runs; it is not a Developer ID signature: the first open from a download is right-click Open (or `xattr -dr com.apple.quarantine`).
5. **`io.available` false, `link.available` false, `caps.files` false, `caps.midi` false** in the shell, so the page does not start the Electron helper, Link, or the Android file messages. Flip them when H7 / H8 land.
6. **pad memory:** the host asks for a 512 MiB pad budget and 512 MiB chains budget (the phone uses 128 / 256). A guess; the lead decides.
7. **The menus-sweep gate** is met only in part. Update: a CDP driver now exists (`native/desktop/tests/sweep/`: a resolve hook swaps `ui-lib.mjs` for a shim that attaches to the shell with `--cdp-port`, no edit to `tests/sweep/`). On the Windows runner the sweep ran through the File menu (about 30 checks PASS/NOTE/SKIP, no FAIL) and then stalled until the step timeout, so the CI step is non-blocking (`continue-on-error`, 15 min) and its log is uploaded; the stall is not diagnosed (a likely cause is a native dialog or a `D:` call the page waits on in the next menu). The 31-channel selftest is the hard gate. (Original note:) Proposal: H9 or a small follow-up adds `native/desktop/tests/shell-sweep.mjs`: a CDP driver for WebView2 on the Windows runner that imports the sweep's parts by giving `boot()` an `attach` option (needs an edit to `tests/sweep/ui-lib.mjs`: the lead's call, outside this task's folders). On the Mac there is no CDP: H9's WKWebView gate needs another driver (an in-page runner using `?seqtest=1`, or Safari WebDriver on the runner).
8. **Selftest uses a null audio device** (a CI runner has no sound card); the miniaudio path is compiled and linked on both runners but its first real run is on the owner's machine.
9. **The `thunderDesktop` replies ride the same text channel as `thunderCore`** (`dreply` / `dpush`), not a second channel.
10. **`window.thunderCore.put` is base64 text** (see above); a binary path (WebView2 SharedBuffer, WKWebView `WKScriptMessage` has none) is an optimisation for after H7, only if the 1 MB chunk loop is too slow for a 5-minute sample (not measured).
11. **No single-instance lock, no file association, no updater, no Mac menu beyond Quit / Edit / Window:** H8.

## Verifier round (PR #339 verdict "merge after fixes", H6-VERIFY.md on `review/verify-h6`)
`native` (M4, 65e97ef) is merged into the branch (a merge, not a rebase).

**Fixed (all with host tests where the logic is platform-neutral; the shell edits are compiled by CI only):**
- **S-1** `file-read` / `file-save` / `out-write` touch only a path the person chose in a dialog (`file-open`, `file-saveas`, `out-pick`), a path handed over by `openPath` (a file opened from the desktop / second launch), a path kept from an earlier run (`<data>/allowed-paths.txt`, last 64, written only by the shell: so "Open recent" keeps working), or a file under `Config::allowDirs` (none today: the app has no songs folder of its own). Anything else is an `{error:'not allowed: ...'}` reply. Paths are compared normalised (weakly canonical, lower-cased on Windows). Decision for the lead: the persisted list is a step past "picked this session"; without it, recent projects from last week would not open. To drop it, leave `allowFile` empty in `ShellCore`.
- **S-2** WebView2: the message source must be `app://thunder`; custom-scheme allowed origins are `app://thunder` (was `*`); `Access-Control-Allow-Origin` is no longer sent; frames may load only the app or `about:`; the bootstrap script is wrapped to run only when `window.top === window` and the page is `app://thunder` (so a frame gets no bridge). WKWebView: the script-message handler checks `frameInfo.isMainFrame` and the security origin (`app` / `thunder`). Unverified on a real window: if the page stops loading after the allowed-origins change on Windows, the CI selftest says so.
- **S-3** the page's `S:` message is dropped unless `--selftest` (`ShellCore::Options::selfTest`).
- **S-4** only `app://thunder` (and `about:`) may be navigated to; http(s) opens in the browser; `data:` / `blob:` are refused (Windows `NavigationStarting`, Mac `decidePolicyForNavigationAction`).
- **R-1** caps (all refused with an err, and an error reply for an ask): `B:` chunk 24 M chars, `song.load` 16 MiB, any other JSON message 64 MiB, any page message 720 MiB, `file-read` / `file-save` 256 MiB, `out-write` 512 MiB decoded; the bridge queue holds at most 8192 messages / 256 MiB (a full queue refuses). My numbers: change in `bridge_session.h` / `desktop_api.h`. Not tested here: the queue-full path (it needs a blocked worker).
- **R-2** `resolveAppUrl` / `readWholeFile` / `DesktopApi::handle` / `ShellCore::onPageMessage` / the Windows callbacks catch everything; file helpers return `{error}`; Mac `S()` is nil-safe.
- **T-1** file reads / writes and base64 decoding run on a worker inside `DesktopApi` (`asyncIo`, one thread, order kept; the reply is emitted from it). NOT moved: the bundled-file read in `WebResourceRequested` / the scheme handler (small local files; a deferral that creates the response on the UI thread is the proper fix).
- **C-1 (now H6b, done here)** `fx.values` (an ask: `{ok}` / `{ok:false,busy}` / `{ok:false,error}`), `meters.want` with `fx` / `dyn` lists (`thunder_live_meters_set`), `recovery.put` / `get` / `drop` and the `recovery` event after `hello`, every translator refusal carries `ok:false, error`. `hello.caps`: `meters2` and `fxValues` from the core's bits, `recoveryPut` true. The recovery copy is the page's own put only (`<data>/recovery/recovery.put.thunder` + `.json`, atomic); there is no swipe-away song.load copy on a desktop. A successful `file-save` / `file-saveas` deletes the put. Data folder: Windows `%LOCALAPPDATA%\Thunder Plus Native\data`, Mac `~/Library/Application Support/Thunder Plus Native` (lead: its own folder, the Electron app uses `Thunder Plus`). The `fx.lvl` / `fx.dyn` events come from the in-place `live_events.cpp` (M4's).
- **C-2** a failed `engine.reset` restart sends `{t:'engine', up:false, error}` and a fatal `err` (`where:'engine.reset'`): the page's `died()` stops the transport, toasts, and the old engine plays; the next `hello` tries again.
- **L-1** `BridgeSession::shutdown` takes the pump thread under its mutex and no late `hello` can start one after it. **L-2** the window close waits at most 3 s for the bridge worker (`ShellCore::shutdown(3000)`), then ends the process without destroying what the worker uses; the WebView2 controller is `Close()`d and the COM objects released before `CoUninitialize`. **L-3** DevTools / Web Inspector only with `--devtools` (or `--cdp-port` on Windows; `--cdp-port` is a dev flag: any local web page can attach while it runs). **L-5** numbers are clamped before they become ints. **L-8** `audioName()` takes the lifecycle lock. **L-9** the Mac selftest leaves with `_exit`. **L-11** the workflow also watches `native/core/src/**` and `third_party`.

**Left (listed, not done):** L-4 the CSP is still report-only (the page needs `'unsafe-inline'`; S-1 no longer leans on it); L-6 base64 strictness; L-7 no Range support, 404 without CORS header; L-10 actions / runner images / the NuGet package are not pinned by hash; the bundled-file read stays on the UI thread; the queue-full path has no test.

## Resume (for another worker)
Everything above is on branch `n4/h6-desktop-shell`, PR #339. To continue: `git worktree add /home/claude/wt/h6 n4/h6-desktop-shell`; the host tests build on Linux with
`cmake -S native -B b -G Ninja -DTHUNDER_WITH_LXR=OFF -DTHUNDER_WITH_DSP=OFF -DTHUNDER_WITH_PUMP=OFF -DTHUNDER_BUILD_TESTS=OFF -DTHUNDER_BUILD_DESKTOP_TESTS=ON` then `cmake --build b --target test_desktop_session ...` and `ctest --test-dir b -R desktop_`. The shells can only be compiled by the CI runners.

## CI state before the verifier round (head e3c159b)
Native: wasm (fixed by skipping `desktop/` for non-desktop systems), Android, GCC, clang, MSVC, macOS jobs green when last read; the desktop host tests run in them. Native desktop: Windows WebView2 and macOS arm64 WKWebView build, 7/7 tests, selftest pass; unsigned artifacts uploaded.

## Lead's decisions (26 Nov, 00:40, Opus)
- Picked-paths list kept across runs (`allowed-paths.txt`, last 64): ACCEPTED (Open recent must work; the shell alone writes it).
- Caps as listed: ACCEPTED.
- Mac data folder renamed to `Thunder Plus Native` so it never shares the Electron app's `Thunder Plus` folder.
- Merge when CI is green; the menus sweep stays non-blocking until H9.
