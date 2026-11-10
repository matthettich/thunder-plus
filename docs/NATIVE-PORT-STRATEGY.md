# Native Thunder Plus on Mac, Windows and Android (iOS as a stretch): strategy

Written 10 Nov 2026 by the lead session, at the owner's request: "write up a strategy to port thunder plus to a native
Mac, windows, and Android app (with iOS as a stretch goal, unless its easy) and spell out the benefits … I am debating
whether maintaining multiple apps for parity is worth managing. I still want to be able to open songs from desktop on
mobile". **Plan only: nothing here is built.** It builds on, and does not replace,
[`NATIVE-DESKTOP-PLAN.md`](NATIVE-DESKTOP-PLAN.md) (architecture, core rules, bridge, timing design, desktop host) and
[`ENGINE-PLUGIN-PLAN.md`](ENGINE-PLUGIN-PLAN.md) (engine port, plugin, stores). What is new here: Android becomes a
first-class native target (it was "later, optional"), the order changes so the phone is cured early, iOS is costed,
and the parity question is answered.

## 1. Why now: what changed on 10 Nov

The Worklets test on the Fold (HANDOVER "10 Nov (night): worklets confirmed") showed that with **any** AudioWorklet
module loaded, Chrome on the phone renders more audio than the device takes and drops the surplus: 208 s rendered while
83 s played, 137 underruns. With worklets off the clock held exactly. Every effect, live engine and the audio-thread
sequencer is a worklet. Builds 545–546 work around it (load no worklet until a song needs one; hold the song when the
clock leaps), but a song with effects still loads worklets, so on the phone it can still drop audio when hidden.

That changes one assumption in `NATIVE-DESKTOP-PLAN.md`: it expected the phone to get "most of the gain" from running
the C++ core as wasm inside a worklet. On this phone a worklet is the problem, so **the web path cannot cure Android.
Only an app whose audio runs outside the browser can.** The desktop case for native is unchanged (drivers, MIDI timing,
plugins).

## 2. The answer to "is parity across several apps worth managing?"

Not if it means several apps. The owner already tried two codebases (…Thunder and …Thunder Plus) and merged them.
This plan keeps **one of everything that holds features**:

| Piece | Copies | What it is |
|---|---|---|
| The interface (`index.html`, …chains and …waves editors) | **1** | The same HTML shown in each platform's built-in web view. Screens, editing, skins, keys, the Mobile setup: unchanged |
| The engine (`core/`, C++) | **1** | Sequencer, sample and voice player, mixer, effects, live engines, MIDI scheduling, offline render. Compiled for Windows, Mac, Android, iOS, and to wasm for the web copy |
| The song format (`.thunder`) | **1** | Same file everywhere; a host carries through data it can't use |
| Hosts (app shells) | 1 per platform | A thin program that opens the window, the sound card, MIDI ports and files, and passes messages between the interface and the engine. No features live here |

A new feature or fix is written once (interface and / or engine) and every platform gets it at its next build. What
does cost per platform, permanently: packaging and signing, platform chores (permissions, file pickers, OS updates),
and **testing a release on each device**. That is the real, ongoing price; CI builds, the parity tests and BrowserStack
(already connected) carry most of it.

The parity burden the owner fears is real for **one period only**: during the engine port the old JS engine and the new
C++ one both exist. The rule from `NATIVE-DESKTOP-PLAN.md` section 8 handles it: each piece switches only when its
parity test passes, and its JS is deleted then. Nothing is kept in two languages after the port.

**Recommendation: yes, worth it, in this one-interface / one-engine shape, and only after the spike in N0 proves the
Android shell cures the clock on the Fold.** If the spike fails, stop: the cost is a week or two.

## 3. Benefits

**Android (the phone)**
- The clock problem goes away at the root: the engine is driven by the sound card's own callback, not by Chrome. No
  racing, no dropped stretches, with effects, screen off or app in the background. The resync, the lazy worklet
  loading, the HYBRID sequencer and the Page sequencer fallback all become unnecessary there.
- Lower and steadier latency (Chrome on the Fold measured 43 ms growing to about 190 ms; native Android output is
  typically 10–20 ms on a recent flagship: to be measured in the spike).
- Proper background play (a foreground service with a notification), instead of the silent-audio-element workaround.
- Better battery: no browser audio graph (72 analysers pulled whether or not they are read), the engine sleeps when
  silent, and the interface can stop completely while the song plays on.
- USB audio interfaces and USB / Bluetooth MIDI through Android's own APIs, with OS timestamps.
- A real folder for songs and samples (see section 6).

**Mac and Windows**
- Driver choice, buffer size, sample rate and every input and output of the interface (the I/O helper gives inputs
  today; this makes the whole path native). Sample-accurate notes, MIDI out and clock from the audio thread, independent
  of the screen.
- No half-second first-PLAY stall, no App Nap or hidden-window throttling, no second audio engine: those are browser
  artefacts.
- VST3 / CLAP / AU hosting, audio tracks and in-process Ableton Link become possible (they are not in a browser).
- Electron retired: a much smaller download, one fewer updater and helper process to maintain.

**All platforms**
- The same engine is the synth plugin of `ENGINE-PLUGIN-PLAN.md`: the port is done once for both.
- Renders match live playback (same code, run faster than real time), and pad renders get faster (C++ instead of JS).
- Store distribution becomes possible if the owner ever sells it.

**What it costs**
- The engine port is months of work (section 5), mostly delegated but with every audio line under lead review.
- Risk of sound changes: handled by parity tests and the owner listening before each switch.
- Mac and iOS use Safari's web view (WKWebView), not Chromium: every screen needs checking there once.
- Fees: Apple Developer Program, $99 a year (needed for iOS at all, and for a signed, self-updating Mac app). Google
  Play, $25 once, only if it goes in the store; an APK from GitHub Releases needs no account.
- Until the port ends, new engine features cost double (JS and C++). The feature freeze already in force makes this
  cheap: keep it for engine features, with MOD the known exception (section 8).

## 4. Architecture (the delta from NATIVE-DESKTOP-PLAN section 4)

```
   Interface: index.html, chains/, waves/ (one copy), in the platform's web view
        │  core bridge (commands down, events up; NATIVE-DESKTOP-PLAN section 5)
   ┌────┴──────────┬───────────────────┬────────────────────┬──────────────────┐
   Windows host    Mac host            Android host         iOS host (stretch) Web host
   WebView2        WKWebView           Android WebView      WKWebView          browser tab / PWA
   miniaudio       miniaudio           Oboe or miniaudio    shares the Mac     core.wasm in a worklet
   (WASAPI; ASIO   (CoreAudio)         (AAudio)             host's audio and   (dev, demo, tests; on the
   only if free)   libremidi           AMidi                MIDI code          phone it keeps today's
   libremidi       (CoreMIDI)          foreground service   AUv3 later         clock limit)
   └────┬──────────┴───────────────────┴────────────────────┴──────────────────┘
   core/ (C++17, one copy): sequencer · sample and voice player · mixer, buses, sends · master ·
   track FX (tfx, …pump) · live engines · …chains / …waves graph runner · MIDI scheduling · render
```

- **Already C++ today** (built to wasm from `dsp/`, `pump/`, `lxr/`): Plaits, Rings, Clouds, Elements, the Airwindows
  and DaisySP effects, tfx, …pump effects and synths, LXR. These link into the native core as they are.
- **JavaScript to port:** the sequencer (`core/seq-core.js`, already isolated, with golden event streams), the
  mixer (a Web Audio graph today; spec in `GAIN-STAGING.md`), `master-worklet.js`, the sampler, the bass and vox
  cores, the ENGINE block's own layer types, and the …chains / …waves graph runners (about 10,000 lines).
- **Native libraries already vendored in `io/`**: miniaudio 0.11.26 and libremidi 5.4.3, both permissive, both used by
  the desktop I/O helper since v1.3.0. The hosts reuse them, and the helper's device, clock and MIDI code moves into the
  desktop host largely as written. This is framework option A of `NATIVE-DESKTOP-PLAN.md` 11.1 (permissive parts,
  nothing with limits), now with a head start. JUCE (option B) would also reach Android and iOS but brings a licence
  with limits; not suggested unless option A's glue proves costly in the spike.
- **The shortcut that brings the phone cure forward: rendered pads stay in the web view at first.** Most sounds are
  rendered to a buffer by the render Worker and then played like samples. Rendering is not real-time, so it has no
  clock problem. In the first native builds the interface keeps rendering pads exactly as today and hands the finished
  buffers to the native core, which only has to sequence, play, mix and run effects. So the first native app needs the
  sequencer, player, mixer, master and the effects (already C++), **not** the whole engine port. Live engines follow;
  until one is ported, a pad using it plays its rendered version (the owner's existing "safety net" idea for devices
  that can't keep up).
- **Hosts stay thin.** The page-side API keeps the shape of today's `window.thunderDesktop.*`; features are switched by
  `core.caps` (for example `nativeAudio`, `plugins`, `link`), never by copies of screens.

## 5. Stages

Sizes are rough, in weeks of agent-led work with lead review at the pace of the last fortnight; they are estimates, not
commitments. Stages N1–N3 are the critical path to the phone cure.

**N0. Spike and decisions (1–2 weeks). The go / no-go gate.**
- Android proof on the Fold: a bare app (Kotlin shell, Android WebView showing the real `index.html`, a small C++
  library opened through miniaudio or Oboe) that plays a sequenced sample pattern from native code while the interface
  runs. Measure: clock (the playback report's "1.00 s in 1.00 s") for 5 minutes screen-on, screen-off and in the
  background; round-trip latency; that the foreground service keeps it alive.
- Prove the two bridge paths: small messages (commands, meters, playhead at 30–60 a second) and a fast path for big
  sample buffers (WebMessage ArrayBuffer, or a file the core reads).
- Build pipeline in CI: CMake for `core/`; targets wasm (as `dsp/build.sh`), Windows, Mac universal, Android arm64
  (NDK); a debug APK attached to a GitHub pre-release so the owner installs it like a desktop beta.
- Confirm libremidi's Android and iOS backends or fall back to AMidi / CoreMIDI directly.
- **Exit:** the clock holds on the Fold with the screen off, and the owner says go. If not: stop here.

**N1. The playback core in C++ (6–10 weeks). The big one.** (NATIVE-DESKTOP-PLAN phase 3, steps 1–4, reordered.)
1. Sequencer and MIDI scheduling: port `core/seq-core.js`; the golden event streams must match exactly.
2. Sample / voice player, mixer (trim, …chains insert point, vol, pan, mute crossfade, sends pre / post, buses,
   master out) and the master chain. Parity: renders within −80 dB of today's.
3. Effects: link tfx and …pump directly instead of as separate wasm modules; port the JS around them.
4. Offline render through the same code.
- Runs on the web first as `core.wasm` behind a Settings switch (old path remains the default), so every step is
  testable headless and by the owner on the desktop before any native shell matters.
- **Exit:** ELECTRO and the owner's own songs play through `core.wasm` with parity CI green; pads still rendered by the
  page.

**N2. Android app, beta (2–3 weeks after N1).**
- Host: WebView + bridge, audio out, AMidi in / out, foreground service and notification, audio focus (calls, other
  apps), headphones unplugged, a songs / samples folder (section 6), the Fold's fold / unfold, back gesture.
- Distribution: a signed APK on GitHub Releases with an in-app "new build" check; Play Store only if wanted later (its
  closed-test rule: 12 testers for 14 days on a new personal account).
- **Exit:** the owner's normal song with effects plays for 10 minutes screen-off with no rush and no gaps; battery no
  worse than the web app; the web copy stays available as the fallback.

**N3. Live engines and the patch editors in C++ (4–8 weeks, can overlap N2).** (Phase 3, steps 5–6.)
- Order by use: …pump live synths (already C++), bass, vox, then the ENGINE block's layer types, then the …chains and
  …waves graph runners. Each behind its switch, rendered fallback until it passes.
- From here pad rendering also moves into the core (faster renders; the render Worker becomes a thin caller).
- **Exit:** no JS DSP left; the web copy runs entirely on `core.wasm`; the JS engine is deleted.

**N4. Windows and Mac hosts (4–6 weeks; can start once N1 is done).** (Phases 4 and 6.)
- Everything `electron/main.js`, `io-helper.js`, `link-session.js` and `updater.js` do, in the native shell; the
  parity checklist in NATIVE-DESKTOP-PLAN phase 4. New Settings › Audio (driver, device, buffer, rate, channels).
- Windows: WebView2, so the interface renders as in Electron. Mac: WKWebView, every screen checked.
- Beta beside Electron; Electron retired when the checklist passes and the owner has made music on it.

**N5. Desktop extras, each on its own:** plugin hosting (VST3 / CLAP / AU), audio tracks, the synth-engine plugin.

**N6. iOS (stretch; 3–5 weeks after the Mac host).** Not "easy", but most of it is shared: the Mac host's WKWebView,
CoreAudio and CoreMIDI code runs on iOS with small changes. New work: touch-only checks in Safari's engine, the
audio session (interruptions, background audio), the Files / iCloud Drive document picker, TestFlight, App Store
review rules, and the $99-a-year membership, which has no way round on iOS. The web app does not serve as an iOS
fallback (no Web MIDI in Safari). Worth doing only once the Android and Mac apps are settled; an AUv3 version can
follow from `ENGINE-PLUGIN-PLAN.md`.

**Suggested order: N0 → N1 → N2 (phone cured) → N4 (desktop) with N3 alongside → N5 → N6.** Android goes before the
desktop host because the phone is where the app is broken in a way no patch can fix, while the desktop has a working
Electron app plus the native helper. If the owner would rather have the desktop first (VST hosting, drivers), N4 and N2
swap with no rework: both wait on N1.

## 6. Opening songs across devices

- **One format, already true:** the same `.thunder` file opens on every host. The core reads it through the same
  code everywhere, so a song cannot sound different because of where it was opened.
- **Things one device can't do are carried, never dropped:** a desktop plugin on a track is kept untouched in the file;
  on the phone the track shows "desktop plugin" and plays its frozen / rendered audio if the owner froze it on the
  desktop. Multi-channel routing falls back to the main outputs. The same rule as today's Mobile setup hiding …chains.
- **Samples:** check in N0 that a song saved on one device carries or can find every sample it uses on another (embedded
  in the file, or a path relative to the chosen sample folder). If today's sample folder is an absolute path, add a
  "collect samples into the song" save option before promising cross-device songs.
- **Getting the file there, cheaply:** a native app can be given a real folder once (Android's folder permission, the
  Mac / Windows file system, iOS's Files). Point every device at one folder that a sync tool already mirrors (Google
  Drive for desktop, Syncthing, iCloud Drive), and "pick up the last save on Android, Mac and Windows" works with no
  Drive code in the app: the app shows that folder's songs newest first and warns if the file changed underneath an
  open song. This likely replaces the undecided Google Drive sync plan (HANDOVER "3 Nov (48)"); the web copy keeps
  today's open / save.
- **Version guard:** the file records the engine version that saved it; an older app opening a newer song says so
  instead of playing it wrongly.

## 7. Risks

| Risk | Handling |
|---|---|
| The Android shell does not cure the clock, or latency is poor on the Fold | N0 measures exactly this before anything else is spent |
| The engine port stalls or changes the sound | Piece by piece behind switches; parity CI; the owner listens before each switch; rendered-pad shortcut keeps the first milestone small |
| Two engines during the port | Feature freeze on engine features; each JS piece deleted when its C++ passes |
| WKWebView differences (Mac, iOS) | Windows and Android use Chromium; Mac checked screen by screen in N4; keep Electron until then |
| Big sample buffers across the web-view bridge are slow | Proven in N0; fallback is the core reading sample files itself |
| Android kills or pauses the app in the background | Foreground service of the media-playback kind; audio focus handled; tested in N0 |
| ASIO licence on Windows | Owner: without ASIO unless free; WASAPI exclusive first |
| More release work per platform | CI builds every target on each release tag; BrowserStack for device runs; one checklist per platform kept short |
| It pulls time from making music | The port is invisible until switched on; the owner tests only at stage exits |

## 8. Decisions for the owner

1. **Go ahead with the spike (N0)?** One to two weeks; it answers whether the rest is worth doing.
2. **Order after the engine: Android first (suggested) or desktop first?**
3. **Framework: permissive parts already in `io/` plus system web views (suggested), or JUCE?**
4. **Keep the web copy** as the free demo, the development copy and the fallback (suggested: yes; it costs almost
   nothing because the core also builds to wasm, but on the phone it keeps today's clock limit).
5. **Apple Developer membership** when the Mac host starts (signed, self-updating Mac app; required for iOS).
6. **iOS:** agree it waits until Android and Mac are settled.
7. **MOD and the port:** MOD's remaining steps (…pump targets, live-engine targets) touch the engine. Either finish them
   before N1 starts, or build them once in C++ during N3. Suggested: finish the current MOD hardening, then hold new
   engine features until N3.

## 9. Where a session starts

- "Start the native spike": do N0. Lead (Fable) writes the briefs and reviews; Sonnet agents build the Android shell,
  the CMake / CI targets and the measurements; nothing in `index.html` changes except a `caps` check.
- Read first: this file; `NATIVE-DESKTOP-PLAN.md` sections 4–6 and 8 (core rules, bridge, timing); `io/README.md`
  (the miniaudio / libremidi code to reuse); HANDOVER "10 Nov (night): worklets confirmed".
- Every stage ends with numbers in `NATIVE-DESKTOP-PLAN.md` section 12 and a line in the HANDOVER.
