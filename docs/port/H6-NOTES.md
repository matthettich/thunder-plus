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
- page -> shell: `thunderCore` JSON text; `B:<base64>` = the bytes of the `put` chunk whose `put.header` came just before; `D:{ch,id,a}` = a thunderDesktop call.
- shell -> page: `window.__thunderRecv(text)`; text is JSON objects joined by `\n`. `{t:'dreply',id,v|err}` and `{t:'dpush',ch,v}` are thunderDesktop; everything else is a thunderCore reply / event, exactly as the Android shell sends them.
