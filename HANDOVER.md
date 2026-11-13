# Thunder Plus — handover notes

Pick-up notes for continuing this work in a later session. Read this together with
[`README.md`](README.md) (the app's own docs) and [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).
Keep this file up to date at the end of a work session.

## Where things stand (13 Oct 2026, morning; newest work is in the dated entries below)

- **One repo now:** `matthettich/thunder-plus` (this one), branch `main`, pushed, nothing uncommitted.
  `matthettich/Thunder-` is **retired** (13 Oct): its page forwards to this app and its service worker removes
  itself. Don't build or edit it; its old app code is in its history (last build 58a0a43).
- **Commits this session** (Plus, oldest first):
  - `2dcda77`: 14 new effects, 6 oscillators, tempo-synced modulation, …chains Steps/Clock/Repeat, and the
    song phrase-number fix.
  - `eebb19e`: 9 more effects (comb, transient, trance gate, multiband, frequency shifter, vinyl, dub, cab, tube).
  - The **break-chopping** commit on top (slices, sync, stretch), plus this file.
  - Thunder- has a matching rebuild after each of them.
  - 13 Oct: the **Beads crash fix** and effect crash guards (see below), with a Thunder- rebuild.
  - 13 Oct: **one app, two setups** (Settings → App → Mobile) and the first-PLAY render fix (see below);
    then the owner chose **one app, one repo**: Thunder- retired (forwarding page), `build-thunder.py` removed.
  - 13 Oct: **pad rendering moved to a Worker** (see below), and the offer to open ...Thunder's saved work.
- **App:** no build step. `index.html` is the whole tracker/synth app; `chains/index.html` and
  `waves/index.html` are the two modular patch editors loaded in frames.
- **Cache-busters now:** `chains-worklet.js?v=15`, `waves-worklet.js?v=11`, `master-worklet.js?v=2` (index.html, the editor
  pages, sw.js). Cache is `thunderplus-v79`.

## Build the native I/O helper — START HERE when the owner says "I want to build the helper"

Owner, 7 Nov: "make it so when I say 'I want to build the helper' the whole plan is clear to the next agent". This section
is the brief. The design is in [`docs/NATIVE-IO-HELPER-PLAN.md`](docs/NATIVE-IO-HELPER-PLAN.md) (read all of it first; its
section numbers are used below); the bigger picture is [`docs/NATIVE-DESKTOP-PLAN.md`](docs/NATIVE-DESKTOP-PLAN.md) §14.

**Why.** Record a sample only ever gets inputs 1 and 2: Chromium (Electron 44) gives `getUserMedia` two channels whatever
the device has. The owner's **Cranborne Audio 500R8** presents 28 inputs over USB at 44.1 / 48 kHz (8 from its 500 slots,
16 ADAT from two 8-channel lightpipe preamps, 2 S/PDIF, 2 from its summing mixer) and has **no mixer software**, so other
inputs can't be routed onto 1/2. Only a native helper fixes it.

**Owner's decisions (7 Nov, plan §9):** phase A (inputs) now, phase B (MIDI) after; Mac stays **unsigned** for now (no
Apple Developer membership yet: expect macOS to re-ask for the microphone); Windows **WASAPI only** (no ASIO unless it's
free to use); test at **48 kHz with all 28 inputs**; **delegate** the plumbing to cheaper models, this (lead) session
writes or line-reviews the real-time parts and approves everything; the owner's Mac + 500R8 is the final test.

**Rules (as for all Thunder Plus work):** permissive licences only (miniaudio MIT-0 / Unlicense, libremidi BSD-2-Clause;
add both to `THIRD_PARTY_NOTICES.md`); the phone and the web app must not change behaviour; a desktop build without the
helper, or where it fails, works exactly as today (`getUserMedia`); keep `npm test`, `tools/recorder-check.mjs` and
`tools/browser-check.mjs` passing; changelog line + `BUILD` + `sw.js` cache bump per page change, as usual.

**Branches and releases.** Work on a branch `io-helper` off `beta`; merge into `beta` (PR, CodeRabbit) when a phase passes.
Desktop test builds for the owner as **pre-releases** (Run workflow `desktop.yml` with `version` like `1.3.0-beta.1` and
`prerelease: true`: installed copies ignore it, the owner downloads it from the Releases page), so the normal update channel
never gets a half-built helper. Merge to `main` and a normal release only when the owner says A1 works on the 500R8.

**Tasks.** L = the lead session (Opus) does it; D = delegate (the Agent tool with `model: "sonnet"`; `"haiku"` only for the
mechanical ones marked D-h) with the card below as its prompt plus the plan file. The lead reviews every diff, runs the
tests itself (never trusts a subagent's "tests pass"), and sends it back with specific fixes until it's right.

| # | Task | Who | Plan § | Done when |
|---|---|---|---|---|
| A0.1 | `io/` skeleton: vendored `miniaudio.h` + libremidi (pinned versions, licences in `io/LICENSES/`), `binding.gyp`, `io/build.js` copied from `link/build.js` (Mac arm64 + x86_64 joined), `io/README.md`, `io/NOTICE.md` | D-h | 3.1, 6 | `node io/build.js` builds on Linux in this workspace; a stub `thunder_io.node` loads in Node |
| A0.2 | N-API `devices()` and `now()` in `io/src/thunder_io.cc` (no threads yet) | D | 3.1 | Node prints the device list (miniaudio null backend in CI: one fake device) |
| A0.3 | `desktop.yml`: build `io/` like Link (continue-on-error, `hashFiles('io/build.js')`), copy to `electron/io/`; `electron/package.json` `asarUnpack` adds `io/*.node`, `files` adds `io-helper.js` | D-h | 6 | a CI test build (no `version`) is green on all three OSes and the Mac app contains `io/thunder_io.node` |
| A0.4 | `electron/io-helper.js` (utilityProcess, loads the addon in a try, answers `devices`), `main.js` forks it lazily on `thunder:io-start`, MessageChannelMain port to the utility and to the window; `preload.js` forwards the port into the page (`window.postMessage({thunderIo:true}, '*', ports)`) and exposes `thunderDesktop.io.start()` / `.available` | D | 2, 3.2 | `tests/io.test.js` (fake addon, as `tests/link.test.js`): start → devices arrive on the page side; addon missing → `{available:false, why}` |
| A0.5 | Page: Settings › Audio row "Input engine: native (500R8, 28 inputs)" / "browser" + why; INPUT ENGINE AUTO / BROWSER setting | D | 3.3 | headless check with a fake `thunderDesktop.io`; the web app (no `thunderDesktop`) shows nothing new |
| A0.6 | Review A0, pre-release `1.3.0-beta.1`, owner confirms the 500R8 is listed with 28 inputs | L | — | owner's screenshot |
| A1.1 | The SPSC float ring (`io/src/ring.h`) and `openInput` / `read` / `closeInput` with the miniaudio capture callback (all channels, interleaved, `frame0`, `hostNs` per callback, overrun counter) | **L** | 3.1, 4 | a C++ unit test pushes from a thread and reads in order with no tearing; null-backend open / read / close in CI |
| A1.2 | `io-helper.js`: 5 ms poll of `read()`, post `{op:'in', frame0, hostNs, channels, frames, data}` transferring `data.buffer`; `open` / `close` messages; restart-once on crash | D | 2, 3.2 | `tests/io.test.js`: scripted chunks arrive in order, close stops the poll, a crash restarts once then reports |
| A1.3 | AudioWorklet `thunder-io-in`: chunks in through its port, a ~30 ms jitter ring, outputs the picked pair / single channel; each frame placed at its audio time from `hostNs` (via the page's `amap` / `p2a`, plan §4); drift handled by drop / repeat in the monitor path only | **L** | 3.3, 4 | `tools/io-check.mjs` (headless, fake chunks with known timestamps): a PATTERN take starts on the right frame (exact), pair / single picks, ±10 ms drift without touching the take |
| A1.4 | `rcdOpen()` helper path (no `getUserMedia`), `rcdInputs()` from the helper's devices, `RCD.nch` = real count so INPUTS lists all 28, `RCD.inLat` from the helper's latency, `rcdRoute` unchanged above the source | D, lead reviews line by line | 3.3 | `tools/recorder-check.mjs` passes on both paths (fake helper and browser) |
| A1.5 | Review, pre-release `1.3.0-beta.2`; owner records an ADAT input (e.g. 12) and a pair (e.g. 17+18) in PATTERN sync, runs Measure… through the helper, a 5-minute take | L + owner | 5 | owner says it works on the 500R8 |
| A2 | Unplug mid-take (keep the take, toast), sample-rate mismatch (resample in the worklet or ask), Windows WASAPI check, Linux build | D (+L review) | 5 | the cases in plan §7's last bullet |
| B | MIDI out / in through the helper (`midiSend(port, bytes, atNs)`, `midiClear` on STOP, `msend` and clock-in on the helper when present) | after A, same split: the scheduler thread is L | 4, 5 | plan §5 item 4 |

**Prompt for each D task** (the lead fills it in): the task row; "read `docs/NATIVE-IO-HELPER-PLAN.md` §…"; the exact files
it may touch (nothing else); the functions / messages / settings it must produce, with names; the test it must add and run;
"don't change `index.html` outside the named functions"; "report the diff and the test output". Keep cards small (one row
each); never hand a subagent the whole phase.

**A0 DONE (7 Nov 18:46, owner's screenshot from desktop 1.3.0-beta.1, build 486):** "Input engine: native (500R8, 28 inputs) ·
Core Audio · thunder_io 1 / miniaudio 0.11.26". PR #14 (io-helper → beta) open; CodeRabbit had to be asked (`@coderabbitai
review`: it skips PRs not aimed at main); its two findings fixed. **Owner feedback for A1.4:** "it's confusing that the engine
and inputs are separated": move INPUT ENGINE (Auto / Browser) into Record a sample beside Input / Inputs (the Input list
shows "500R8 (native, 28 inputs)", Inputs lists every channel and pair from it), and take the row out of Settings › Audio.

**OWNER'S BETA.2 TEST (7 Nov 19:50) and the fixes in 1.3.0-beta.3 (build 490):** (1) "the audio interface gets reset":
miniaudio set the device's buffer size (kAudioDevicePropertyBufferFrameSize, device-wide on CoreAudio); now it asks for the
size the device already has (`CurrentBufferFrames` in thunder_io.cc). (2) "enable the microphone 28 times": main asks macOS once
(`systemPreferences.askForMediaAccess`) before forking the helper. (3) "Discard and record doesn't do anything" / "close the
window to get the inputs again": a stalled native input (no chunk for 1 s) or one on a rebuilt audio engine is closed and
reopened by the next Record. (4) "meters don't work until Record": the native input opens with the window. (5) "kicking the
inputs back to 1&2": a failed native open fell back silently to the browser's input; now it says why and records nothing.
(6) "multiple layers on the same pad are the same sample": most likely the old take kept after a Record that failed to open
(rcdStart returns before clearing RCD.take): recheck on beta.3. ADAT inputs: not confirmed yet (the owner thinks these bugs
hid it): recheck on beta.3. tools/io-check.mjs covers 3–5 with the fake helper.

**ROOT CAUSE of the owner's beta.2 / beta.3 failures (CodeRabbit, PR #14, 7 Nov 21:05):** the helper posted chunks with
`[data.buffer]` as the transfer list, but Electron's MessagePortMain only transfers ports, so the first audio chunk threw and
killed the helper; after the second death the page fell back to the browser input (inputs 1–2). Fixed in build 492 / beta.4 (out 7 Nov 21:22, Mac addon confirmed built):
chunks are copied (structured clone); `tests/io.test.js` guards the wiring. Lesson: the fake ports in tests accept transfers the
real MessagePortMain doesn't; test the Electron wiring itself when possible. Also from that review: rcdOpenIo closes the device
in the helper if the worklet setup fails; io/README documents the input API. Skipped: its libremidi findings (vendored upstream
code, not compiled yet; look again when phase B compiles libremidi, and record any local patch in io/VERSIONS).

**A2 (part) built after beta.3 (build 491, not in a desktop pre-release yet):** (a) the interface's own delay: the addon reads
CoreAudio's input device latency + safety offset + first stream's latency (`InputLatencyFrames`) and moves every hostNs back by
it; `opened.latencyMs` shows in Record a sample's Extra latency line. (b) Unplugged mid-take: miniaudio's stopped notification
(not caused by us) sets `inputLost()`; the helper drains what arrived, then posts `closed` with why; the page ends the take where
it is and keeps it, then closes. (c) Rate mismatch: the window says when `opened.deviceRate` differs from the app's rate.
Tests: io-check (unplug, rate, latency line), tests/io.test.js (inputLost). Left for A2: Windows (the addon doesn't build on the windows-latest runner: node-gyp "Could not find any Visual Studio
installation" (the step is continue-on-error, so Windows builds ship without the helper; probably the same for Link: try
`npm config set msvs_version 2022` or setup-msbuild before it), then a WASAPI check; Linux builds fine.

**PICK UP HERE: EVERYTHING STILL NEEDED (updated 8 Nov 11:30).** Read this list first; the paragraphs below it are the detail.

**RELEASED (8 Nov, 11:52, owner: "Can we push a new main and desktop build"):** PR #14 merged into `beta`, `beta` merged into
`main` by PR, and desktop **v1.3.0** (normal release, auto-update) built from `main` (build 511 = beta.20 + two small Measure
fixes). Day-to-day work goes back to the `beta` branch; `io-helper` is history. The owner's tests below now run on v1.3.0.

**NEXT STEPS, in order:**
1. The owner tests **v1.3.0** (same as 1.3.0-beta.20 plus two small Measure fixes) with the checklist page https://claude.ai/artifact/6CgCLiVo8HEH3ywky5znWM ("Copy results"
   gives the report to paste). It lists: the beta.14 fixes to confirm (monitoring after reopening the window, Space and
   solo / mute keys, Apply (reload) and the rate message, Keep in this pad silent, no Cirklon "Sysex interrupt", Measure quiet,
   its new status line and a re-run by ear, long takes in RECENT), the beta.14 skips (unplug mid-take, Measure with Engine
   Browser, Playing in), clock in once more, and Windows. Everything else passed on beta.14 (item 0).
   Start a session by asking for the results; fix on `beta`, then a NEW desktop version (the next unused version, so no existing release is overwritten: `-beta.1` on it for a pre-release, plain for a
   release: never reuse one, the workflow would replace a release's files without moving its tag), and check the macOS and Windows job logs
   for "thunder_io.node built" (both), "adhoc-sign: signed" (macOS) and "thunder_link.node built" (both). Ask CodeRabbit with
   "@coderabbitai review" after each push (it skips PRs into `beta` otherwise) and fix its real findings.
2. Merged and released already (above). Next `beta` → `main` + desktop release only when the owner asks (beta → main by PR so
   CodeRabbit sees it; ask it with "@coderabbitai review").
3. Windows: the owner tests recording (WASAPI) and MIDI (WinMM) on the Windows PC (the build is fixed: windows-2022).
4. Later / low priority: Linux ALSA in CI (item 5, skipped on the owner's word); phase C (audio output through the helper)
   only if MIDI vs audio timing is still off after B, and then more likely the native desktop app (docs/NATIVE-DESKTOP-PLAN.md).

*State now:* branch `io-helper` (build 511: phase B MIDI built and reviewed, item 5; desktop pre-release **1.3.0-beta.20** (build 510; build 511 has two small Measure fixes, no beta yet). beta.19 = 509; beta.18 = 508; beta.17 = 507; beta.16 = 506; beta.15 = 505; beta.14 = 504; beta.13 = 503; beta.12 = 502; beta.11 = 501; beta.10 = 500; beta.9 = 499; beta.8 = 498 (first with MIDI, Windows with Link and the helper again); beta.7 = 496, ad-hoc signed; beta.4 = 492) = `beta` 485–488 + the helper (A0, A1, part of A2) + the beta.2/beta.3 test fixes + the
CodeRabbit fix that was the real cause of the beta.2/3 failures (the helper died on the first chunk: MessagePortMain can't
transfer an ArrayBuffer) + tonight's test fixes (493–496, item 1c). The owner tests beta.7 when convenient (item 1). PR #14
(`io-helper` → `beta`) is open. `beta` is at build 488 plus a CLAUDE.md line ("continue the helper"); `main` is at build 485
(desktop v1.2.101).

**0. OWNER'S TEST OF beta.14 (8 Nov, 10:13): RESULTS and what was done (build 505).**
- Passed: install; one microphone prompt; Record straight away; monitoring while the song plays; no "fell behind"; a new 5-minute
  take sounds good (one noisy bit, the owner thinks from a MIDI device); Measure picks the input and reads 5.5 ms; native MIDI on,
  setup carried over, timing (Browser "maybe a little tighter by ear"), Stop clean, clock out, plug / unplug, engine switching,
  "· 4 late". Clock in "seemed out of sync slightly". Windows: not tested yet. Unplug mid-take, Browser Measure: skipped.
- **Monitoring distorted and clicky after closing and reopening the window once; the song silent until Restart audio** (not
  reproduced by the owner after an app restart). Found: closing the window while the input is still opening (waiting on macOS /
  the helper) left a half-open input (the open carried on after rcdClose). Build 505: `RCD.gen` (rcdClose bumps it; rcdOpen1
  undoes what it opened and tells the helper to close when it changed; `rcdOpen` starts a fresh open after a stale in-flight one
  instead of handing back its "gave up"). Not proven to be the cause: if it happens again, ask
  for Settings › Audio › Copy report BEFORE restarting audio.
- **Cirklon "Sysex interrupt" the first time native MIDI reached it.** Web MIDI's send() throws on malformed messages and SysEx
  without permission, so the browser engine dropped them; native passed them on. Build 505: `nmValid` (whole messages only,
  data bytes < 0x80, lengths by status, no F0 / F7) in the native output's send, thrown as TypeError like Web MIDI.
- Keep in this pad played the whole pad once (playFresh): owner, 10:37: "Don't play anything" (build 506: removed).
- 11:00: "Measure button takes a couple of pushes" (asked: what the first push shows) and an "Audio engine reset" message after
  two runs: the reset after Measure is deliberate (7 Nov: opening the input moves macOS's output timing); build 508 does it
  quietly (`resetAudio(true)`), and resetAudio now reconnects the recorder's monitor to the new master (it stayed on the old one).
  Build 509 (CodeRabbit): a queued open whose window closed before it began never starts (io-check proves it never reaches
  the helper). CodeRabbit has reviewed every commit up to 4ab28ab (beta.19); its last pass had only a test nitpick, done.
- 11:26: Measure: "The window changes shape on first click, and it doesn't start measuring until later": the status replaced the long
  explanation (the window shrank under the pointer) and opening the input takes a few seconds. Build 510: a fixed-height status
  line below the fields; "Opening the input…", then "hit k of n", then "Working it out…". Build 511 (CodeRabbit): the line is a fixed
  height and scrolls; progress goes to the panel that started the run. CodeRabbit again raised same-name devices (ids reused
  after an unplug): deferred, it needs the stable device identity listed under item 5.
- 11:02: "If I monitor directly, it's spot on, but through [the app's] monitor MIDI sounds late": so the MIDI timing is right
  (Gear delay as measured); the app's own monitoring is late by its input path + 20 ms cushion + output latency (by design:
  use the interface's direct monitoring when tracking). A lower-latency monitor option could come later if wanted.
- CodeRabbit on build 505: the `RCD.gen` comment had swallowed rcdClose's disconnect line (monitor left on the master,
  beta.15 only). Fixed in 506, with an io-check that closing disconnects the monitor and node. Build 507 (CodeRabbit): the
  browser-input path checks `gone()` after getUserMedia and the worklet load too. Measure "closer but not perfect": the owner is checking again by ear; with only the gear on the input it trusts the
  output latency macOS reports (patching the click in too measures against what is heard).
- Delegated to Sonnet subagents (checked by the lead): Space always plays / stops even with a menu button focused, solo / mute
  keys on every screen; Apply (reload) did nothing in the desktop app; the rate warning only a status line (now a toast too);
  the 5-minute take missing from RECENT. Found: Apply (reload) was cancelled by main.js's will-navigate guard (now
  `thunderDesktop.app.reload()` → `thunder:reload` → sender.reload()); the rate note was cleared by fillIn on reopen (now
  `rcdRateMsg()`, a toast once per device + rate pair, and the status line); RECENT: a long take stores fine (16-bit PCM),
  the likely cause is localStorage full (the RECENT list write was swallowed): now retried smaller, kept in memory
  (`SMB_MEM`) and held (`smpHeld`). Not proven to be the owner's cause.

**1. OWNER'S TEST OF 1.3.0-beta.7 (build 496; waiting on the owner; the next session starts by asking for the results).**
https://github.com/matthettich/thunder-plus/releases/tag/v1.3.0-beta.7. Record a sample → Engine Native unless it says otherwise.
- [ ] **Microphone prompts.** Install beta.7, open Record a sample: one prompt at most (new ad-hoc signature), then none when the
      window is opened again or the app restarted. If it still prompts several times: what does the prompt name (Thunder Plus or
      a "Helper"), and is Thunder Plus listed in System Settings › Privacy & Security › Microphone?
- [ ] **Record right after opening the window** (before the meters settle): no "wouldn't open: no answer".
- [ ] **Monitoring after a PATTERN take.** Monitor ON, a PATTERN take, then a FREE take: the input is still heard after each,
      without switching Monitor OFF → ON. Also: any dropouts in monitoring while the song plays?
- [ ] **Measure…** (Settings › MIDI). First set the port's Gear delay back to its old value (or 0): beta.4 set it to ~200 ms,
      which was a bug. Pick the pair the gear is on in Measure's new **Inputs** list; with the native input only the gear needs to
      reach it. Send the whole result line (ms late / early, hits, spread, drift, "timed by the native input's timestamps" or
      not). If the click is patched in too, it is used instead: a second run with Engine Browser (gear + click on IN 1+2) would
      confirm both paths agree.
- [ ] **Listen to the 5-minute take** (beta.5; kept in the pad's layer 3, also in File › Samples… › RECENT): no clicks or gaps,
      and still on the grid at the end. Optionally 1–2 minutes through the 500R8 on an ADAT pair (load).
- [ ] When convenient: unplug the 500R8 mid-take (the take so far is kept); run the 500R8 at 44.1 kHz with the app at 48 (the
      window warns that the input is converted).
→ Problems: fix on `io-helper`, run the checks (below), pre-release the next unused number (`1.3.0-beta.21`…) (Run workflow `desktop.yml`, ref
  `io-helper`, `version`, `prerelease: true`); in the macOS job log confirm both "thunder_io.node built" and "adhoc-sign: signed"
  (both steps can fail without failing the build).

**1b. Already confirmed by the owner (beta.4 / beta.5, 7 Nov).** ADAT input (IN 12) and a pair (IN 17+18) in PATTERN, inputs stay
on the pick, takes on the beat; Discard and record; two layers of one pad differ; monitoring usable; a 5-minute take recorded
without errors (not yet listened to). Kept takes go into the pad's SELECTED layer (the owner's 5-minute take: layer 3) and RECENT:
not a bug.

**1c. What tonight's tests found and how each was fixed (builds 493–496).**
- 4–8, then 6 microphone prompts on opening: (a) the window's meter open (fillIn, not awaited) and Record's open ran at once,
  and the STALE rule (no audio for 1 s → reopen) reopened an input still waiting on macOS's prompt; fix (493): `rcdOpen` is
  single-flight (`rcdOpening`), STALE only after audio has come (`io.got`) and 2 s without it. (b) The Mac app was fully unsigned
  (CSC_IDENTITY_AUTO_DISCOVERY false): macOS can't keep a grant for it. `build.mac.identity: "-"` (495) was SKIPPED by
  electron-builder 26 ("no valid identity"), so beta.6 is unsigned too; 496: `electron/scripts/adhoc-sign.js` (afterPack:
  `codesign --force --deep --sign -` on the merged universal app only, then `--verify`), `hardenedRuntime: false` (hardened runtime
  would need JIT / library-validation entitlements for the .node addons). One prompt per update (each build's code hash differs)
  until a Developer ID. The updater downloads the .dmg itself (no Squirrel), so signing doesn't touch updates.
- "500R8 wouldn't open: no answer": the open gave up at 5 s while macOS's prompt was up; now waits 30 s (a toast after 2.5 s
  says to allow the microphone) and closes the device if it timed out (493).
- "The input fell behind for a moment" mid-take: D was output latency + 40 ms; now + 120 ms (cap 500 ms, +50 ms steps between
  takes). D costs the take nothing (rcdLat = D), only a little more wait after a PATTERN take ends (493).
- Monitoring silent after a PATTERN take (meters moving; Monitor OFF → ON fixed it): not reproduced with the fake; `rcdMonSet`
  sets the monitor level outright (cancelScheduledValues + setValueAtTime) and restarts the native monitor's read point at every
  take's start and end; monitor cushion 12 → 20 ms (496). If it still happens, the cause is elsewhere: look at what changes when
  the song stops after a PATTERN take.
- Measure… read ~200 ms (reproduced with the fake: 194–232 ms for a gear 20 ms late): `mltOnset` took any input above 0.01 (hum,
  DC) as the click, at the window's start (−120 ms), and the click skips the master's lookahead while MIDI waits for it
  (`moFxLat`, ~64 ms by default). Fix (496): the onset must clear 6× the window's 30th-percentile level; `moFxLat` is subtracted;
  with the native input a missing click falls back to the timestamps (`out.ts`). Measure has its own Inputs list (494: it only
  listened on Record a sample's pick). Build 497 (CodeRabbit): run from Settings (Record a sample closed) the input closed BEFORE
  the analysis, so `RCD.io` was null (no timestamp fallback) and `rcdLat()` gave the browser's latency: the click was looked for
  D (≥ 120 ms) off. Likely the third cause of beta.4's 200 ms. ts / lat / f0 are now read before the close. io-check's fake now has
  a device clock (frames at the true rate, stamped t0 + frame / rate): Measure reads 20.0 ms on every hit, open and from Settings.
- Desktop build: Windows now on `windows-2022` (windows-latest has Visual Studio 2026 / "version 18", which node-gyp 11 can't find:
  neither Link nor the helper addon built on Windows). Check on the next build: "thunder_io.node built" in the Windows job.
- `adhoc-sign.js` signs every finished Mac app (single-arch builds too), skipping only the universal build's -temp halves.

**2. CodeRabbit on the newest commits.** Its last review (02:54 UTC) covered A1 up to build 489; A2 (build 491) and the build-492
fix were pushed after it. Ask with a PR comment `@coderabbitai review` (REST: `gh api -X POST repos/matthettich/thunder-plus/
issues/14/comments -f body=...`), and only after the last push of a batch: a push right after the ask makes it skip ("Head
commit changed"). It skips PRs into `beta` unless asked. Read `pulls/14/comments` and `/reviews`; fix real findings.
Its libremidi findings (vendored, not compiled) wait for phase B.

**3. Merge, when the owner says beta.4 works.** PR #14 into `beta` (REST: `gh api -X PUT repos/matthettich/thunder-plus/pulls/14/merge`;
GraphQL is blocked from these sessions). Expect small conflicts: BUILD / CHANGES / sw.js CACHE (take io-helper's 492 and keep
every CHANGES line) and CLAUDE.md (keep both helper lines). Then `beta` → `main` and a normal desktop release (`1.3.0`) only when
the owner asks (beta → main by PR so CodeRabbit sees it). The punch-list fixes (FOLLOW PATTERN 487, wheel direction 488) are
on `beta` and in the helper betas but in no normal desktop release yet.

**4. Rest of A2.**
- [x] Windows build: fixed by building on `windows-2022` (windows-latest's Visual Studio 2026 isn't found by node-gyp 11); beta.8's
      log shows both thunder_link.node and thunder_io.node built. [ ] The owner's WASAPI test (WASAPI only, no ASIO).
- [ ] Linux: builds (no ALSA: no MIDI ports); untested on hardware (low priority, owner).

**5. Phase B: MIDI through the helper: BUILT (7 Nov night, build 498, on `io-helper`; owner go-ahead 22:44 "Do it!").**
Started before the owner's beta.7 test, by the owner's choice; the risk is only that a problem found in the shared plumbing
(helper process, port) may need the MIDI side adjusted.
- **B1 addon** (`io/src/midi.cc`, libremidi header-only; C++20 and exceptions on in `binding.gyp`, every libremidi call in a
  try): `midiPorts / midiOpenOut / midiOpenIn / midiSend(port, bytes, atNs) / midiClear(port|0) / midiClose / midiRead /
  midiStats`. One sender thread for every output (QoS user-interactive on macOS; TIME_CRITICAL + timeBeginPeriod(1) on
  Windows), a multimap by OS time (same-time order kept), sleeps to 0.5 ms before, spins the rest. Shared state is heap-
  allocated and never freed (a running thread at exit aborted Node). Past / zero times: now, in the order sent. MIDI in:
  libremidi callback stamps HostNowNs() and queues. Test port `test:loop` (listed with THUNDER_IO_TEST, always openable):
  `io/test/midi.js` (timing on this workspace: median 0.001 ms, worst ≤ 2 ms; order; clear; close; bad input), run by
  `tests/io-native.test.js` and smoke-checked in `io/build.js`. Linux builds ALSA only when its headers exist (this workspace
  has none: dummy backend, no ports); the workflow doesn't install libasound2-dev (Linux low priority).
- **B2 helper** (`electron/io-helper.js`): hello has `midi:true`; ops midiPorts / midiOpenOut / midiOpenIn (answered),
  midiSend / midiSendMany / midiClear (streams, no answer), midiClose; MIDI in polled every 2 ms and posted as one
  `{op:'midiIn', list}`; shutdown (page reload, exit) clears and closes every MIDI port. `tests/io.test.js` (4 tests).
- **B3 page**: `NM` / `nmConnect` / `nmScan` / `nmPort` / `nmClose` / `midiRemap` / `midiEngineSet` (next to the IOH code).
  midi.access is a stand-in for Web MIDI's (`native:true`; outputs `send(bytes, ts)` → midiSend at `(ts − IOH.clk)·1e6` ns,
  `clear()` → midiClear; inputs get `{data, timeStamp}` with the helper's arrival time), so msend, the clock out, clock in,
  Measure… and KEYS PLAY are unchanged; msend skips the OUTBOX for native outputs (moRelease already calls `clear()`, then
  the offs). Ports rescanned every 3 s (onstatechange = hookInputs); the clock offset re-measured every 15 s. Ids `n:` +
  name; saved Web MIDI ids mapped by name (`midi.names`, read from Web MIDI once when missing) and back on Engine Browser.
  Settings › MIDI › **Engine: Native / Browser** (desktop only; `aset.midiEngine`, default native). `connectMidi` is
  single-flight and first runs after the whole script (TDZ: it used IOAPI before its line).
- **Microphone**: no longer asked when the helper starts (MIDI alone would have prompted): `thunder:io-mic` (main.js,
  preload `io.mic()`), called by `rcdOpenIo` just before an input opens.
- io-check's fake helper speaks MIDI too: connect (no microphone), remap by name, a note booked 200 ms ahead reaches the
  helper at its OS time (0.0 ms off) with no OUTBOX, STOP = midiClear then the offs, clock in with timestamps, Engine
  Browser closes the ports.
- **Owner's test (after beta.7's):** Settings › MIDI shows Engine Native and "· native" in the status; the port letters still
  point at the same gear; notes on the gear in time with the song (compare with Engine Browser); STOP: nothing plays on after
  it; MIDI clock out to the MPC / Force steady (and clock in from it, if used); then Measure… again (Gear delay should come out
  small: the helper sends at the booked time; the owner once said the right delay was "a combination of both latency numbers":
  check whether that is still so). Windows: the same through WinMM.
- **Review round (build 499).** A Sonnet subagent reviewed B1–B3 read-only (findings checked by the lead before fixing) and
  CodeRabbit reviewed PR #14; both agreed on the main points. Fixed: `nmScan` single-flight (`NM.scanning`) with opens in
  flight remembered (`NM.opening`), a port that opens after a switch / reconnect is closed (not leaked), Engine switched while
  a connect is in flight (`midiAccessDrop`; connectMidi1 drops an access of the wrong engine), `midiRemap` again when ports
  change (a device plugged in later gets its letter; name substring match only for names ≥ 4 chars; the Web MIDI name lookup
  bounded to 3 s); addon: `MidiOpenIn` scans and opens without g_mx, `SendNow` sends without g_mx (device held by a
  shared_ptr copy), atNs clamped to an hour ahead, `timeEndPeriod` on Windows; helper: the MIDI-in poll stops with the last
  input; page: a DEAD HELPER (no answer to midiPorts twice) is noticed by `nmLost`, which drops the access locally, resets IOH
  and connects again (a fresh helper; main.js allows one restart; Web MIDI otherwise); `iohStart` adds its window listener
  once (a second one answered every waiting request with null). Delegated to a Sonnet subagent and checked by the lead:
  helper op `midiStats`, Settings › MIDI status "· N late" (`NM.late`, `nmLateShow`). Not changed (noted): the 15 s clock re-measure
  replaces the offset (±0.2 ms) rather than smoothing.
  CodeRabbit's second pass (build 500): `NM.scanning` holds the access being scanned (a leftover scan of an old access no longer
  holds up the new one), no midiRemap from a scan whose access has gone, and the helper's dead transfer-list argument removed.
  Third pass (build 501): opens in flight are kept per access (`acc.opening`), and a scan whose access has gone stops before
  opening inputs. Fourth pass (build 502): two same-name devices (ids "Name", "Name #2"): when one of the group goes, the
  whole group is closed and opened again by its new ids (the survivor kept the gone device's handle); io/build.js removes a
  staged addon on a missing prerequisite too. Fifth / sixth passes (builds 503–504): CodeRabbit first asked for saved choices to
  follow a renumbered group, then (rightly) that names can't tell which device left; build 504 keeps a gone choice as it is
  ("(not connected)", back when the device is); the group is still reopened so the device that's there works under its new
  id. io-check's fake gives a new handle per open, models the physical devices and refuses sends on closed handles.
  **Still to do (rare: two identical MIDI devices):** ids are names, so after the first of two "Gear"s is unplugged, a letter
  set to "Gear" plays the one still there. The real fix is a stable device identity from the addon (libremidi's
  port_information: `port` / `container` / `device_name` where the backend gives them), kept in midiPorts() and used by the
  page for its ids, with names only as the fallback. Skipped: its "docstring coverage" pre-merge check (this codebase comments
  inline). CodeRabbit has reviewed every commit up to 276c33c (build 504): its last pass had no new findings and marked
  the earlier ones addressed; only its docstring-coverage check remains, skipped on purpose.
- **Linux: skipped for now (owner, 7 Nov 23:09: "not as important"; still needs to happen).** The Linux addon builds without
  ALSA (no ALSA headers on ubuntu-latest), so Linux has no native MIDI ports (Web MIDI still works, Engine Browser). To do:
  `sudo apt-get install -y libasound2-dev` before "Native I/O helper addon (optional)" in desktop.yml, Linux runner only,
  continue-on-error. The step (written and then left out on the owner's word):
  `- name: ALSA headers for the helper's Linux MIDI (optional)` / `if: runner.os == 'Linux'` / `continue-on-error: true` /
  `run: sudo apt-get update && sudo apt-get install -y libasound2-dev`.
  Also untested on Linux hardware: the audio input.
- io/test/midi.js judges timing by percentile (median ≤ 0.5 ms, 95% ≤ 1 ms, none over 10 ms): on this 2-CPU workspace with
  Chrome running alongside, single messages came 2–6 ms late (the OS pausing the thread); idle, 95% ≤ 0.005 ms, worst ≤ 1 ms.

**6. Known limits to keep in mind.** Monitoring through the app is D + output latency late (D = base + output latency + 40 ms,
raised between takes if chunks arrive late). The interface at another rate than the app is converted (warned). The Mac build
is unsigned: macOS may still ask for the microphone per launch until the Apple Developer membership (owner: waiting).

**Checks to run before any push:** `npm test`, `node tools/io-check.mjs`, `node tools/recorder-check.mjs`,
`node tools/browser-check.mjs` (and `node tools/link-test.mjs` if Link code moved). The addon: `NODEDIR=/opt/node22 node
io/build.js`, `node io/test/input.js`. Lesson from beta.2/3: fakes accept things the real Electron ports don't; when the
helper's wiring changes, read Electron's API for it, not only the tests.
Working copies used: `/home/claude/thunder-plus` (io-helper) and a `beta` worktree at `/home/claude/tp-beta`.

**A1 BUILT, waiting for the owner's test (7 Nov, evening; branch `io-helper`, PR #14, desktop pre-release 1.3.0-beta.2, build 489).**
Tasks A1.1–A1.4 done; A1.5 (owner on the 500R8) is next. What's where:
- **A1.1 addon capture** (lead): `io/src/ring.h` (SPSC sample ring + block-header ring; a callback goes in whole or is dropped
  and counted, so a drop is an exact gap at its true device frame); `thunder_io.cc` `openInput(id, {rate, block, backend})`
  (every channel, float32, interleaved, miniaudio resamples to the page's rate and never changes the device's own rate),
  `read()` (one contiguous run per call), `closeInput()`. **Frame times:** the stream's frame-0 time on the OS clock is a lower
  envelope of callback time (`start = min(start + 100 ppm of elapsed, now - frames/rate)`): late callbacks can't push it, crystal
  drift can. `latencyFrames` is only what miniaudio knows (period x periods); driver / converter latency is left to Measure….
  Tests: `io/test/ring-test.cc` (real producer thread, TSan clean, 3M frames), `io/test/input.js` (null backend, real time),
  both run by `tests/io-native.test.js`; `io/build.js` smoke-opens the null input.
- **A1.2 helper** (delegated): `open` / `close` / `now` ops, 5 ms poll, up to 8 reads a tick, chunks transferred; `shutdown()` on
  a new port and on exit. Tests in `tests/io.test.js`.
- **A1.3 worklet** (lead): `IOIN_WORKLET` in index.html. The page's clock means "when heard" (`p2a`), so a frame that reached the
  computer at OS time P belongs to song time `p2a(P)`; the worklet plays it at `p2a(P) + D`, and on this path `rcdLat()` is `D`
  (+ Extra latency). `D` = base + output latency + 40 ms, raised 20 ms (max 250 ms) between takes when chunks arrive late.
  **Takes stay whole:** the anchor (audio frame - input frame) is re-set when the timestamps disagree by > 1 ms between takes,
  > 20 ms during one (`rcdGo` tells it). Monitor = the worklet's second output, ~12 ms cushion, nudged a frame for drift.
  Clock line-up: `iohClock()` (8 `now` pings, shortest round trip), re-done every 10 s between takes; `mapUpd()` every 500 ms.
  Tests: `tests/io-worklet.test.js` runs the real worklet source frame by frame (exact placement, drift held in a take, gaps,
  late count, monitor).
- **A1.4 Record a sample** (delegated, line-reviewed): Engine Native / Browser row (desktop only; owner: engine and inputs
  belonged together), Input lists the helper's devices ("500R8 (native, 28 inputs)", `rcdSet.ioDev`), Inputs every pair and
  channel; Settings › Audio's row removed. `tools/io-check.mjs` drives it with a fake helper streaming 28 channels and checks
  a FREE take on IN 17+18 comes back as channels 17 and 18.
- **Owner's test for A1.5:** an ADAT input (e.g. 12) and a pair (17+18) in PATTERN sync against the click; Measure… through the
  native input; a 5-minute take (no clicks, still on the grid at the end); monitoring latency acceptable? If PATTERN takes are
  consistently early or late by a few ms, that is the interface's converter latency: set it once as Extra latency (or we
  read it from CoreAudio's kAudioDevicePropertyLatency + safety offset in A2).
- **Known limits:** monitoring through the app is ~D + output latency late (use the interface's own monitoring when tracking);
  the device must run at the app's rate for zero resampling (miniaudio converts otherwise: A2 to warn); Windows / Linux untested.
- **Next:** A2 (unplug mid-take keeps the take; rate mismatch warning; Windows WASAPI; CoreAudio input latency), then B (MIDI).

**Settled 7 Nov (build 485):** MIDI was early by the reported base + output latency on the Mac; `mapUpd` now floors the
clock mapping at that latency (7 Nov (16)). Re-check MIDI timing with Measure… once the helper's MIDI (B) lands.

## Desktop releases

- **v1.3.4** (11 Nov 2026, a regular release; owner, 10 Nov 22:49: "do the queue and push a full desktop app after you do"; PR #92 beta → main at build 563, Run workflow through the API with version 1.3.4, run 42983473062, all three platforms built, 11 assets): everything since build 541 (MOD finished, the 10 Nov stability work, build 563). Installed copies update to it.
- **v1.3.4-beta.2** (9 Nov 2026, pre-release; owner asked for a beta desktop; Run workflow through the API from `beta` at build 541, run 42700107769): MOD screen, chains rail lines, acid Mutate. **v1.3.4-beta.1**: the same at build 540, without acid Mutate. Owner, 9 Nov: hold further desktop betas for now.
- **v1.3.3** (8 Nov 2026; owner: update the desktop app at a stopping point; Run workflow through the API with version 1.3.3, from `main` at build 523): builds 517–523 on top of v1.3.2 (`· beta` label, Mobile SHIFT+▲, pad MUTE / SOLO, the review fixes).
- **v1.3.2** (8 Nov 2026; owner: update the desktop app at a stopping point; Run workflow through the API with version 1.3.2, from `main` at build 516, run 42544191974, succeeded): builds 515–516 on top of v1.3.1 (touch sliders in dialogs, instrument big step, FLY label and same-row order, mobile stalls).
- **v1.3.1** (8 Nov 2026; owner: "Build the main and desktop app"; PR #18 beta → main at build 514, Run workflow through the API with version 1.3.1, run 42522985460): Live engines setting and lighter …waves live (513–514) on top of v1.3.0. Owner wants Thunder Plus changes to go through PRs (ask CodeRabbit with "@coderabbitai review" on PRs into beta).
- **v1.2.101** (7 Nov 2026; for the owner's MIDI testing; Run workflow through the API with version 1.2.101, from `main` at build
  485, run 42432167168): Measure's note (482), INPUTS wording (483), Measure resets the engine (484), MIDI mapping floored at
  the reported latency (485) on top of v1.2.100.
- **v1.2.100** (7 Nov 2026; owner: "push a desktop build so I can continue to test for bugs"; Run workflow through the API with
  version 1.2.100, from `main` at build 481, run 42429798901): Caps Lock on a Mac (480) and kit kinds to the core while playing
  (481) on top of v1.2.99.
- **v1.2.99** (7 Nov 2026; owner: "merge to main for me and make a desktop"; beta merged into `main` directly (gh had no token, so
  no PR / CodeRabbit pass) and released through Run workflow with version 1.2.99): builds 477–479 on top of v1.2.98 (INST layout by OUTPUT,
  recorder INPUTS, Ctrl/⌘+G generators, MIDI outbox, Measure gear delay, wider Gear delay boxes, MIDI after the output chain's latency).
- **v1.2.98** (7 Nov 2026; owner: "push it to main, build a desktop app"; Run workflow through the API with version 1.2.98,
  from `main` after PR #13 at build 476, run 42414595840): builds 468–476 on top of v1.2.97 (core watchdog, Pattern view arrows,
  Arrange new patterns, Chords on poly, playhead latency, EDIT pick-up / drop, PAD navigation, hold before drag, ?pbshow,
  no whole-sample work on the audio thread). Pump effect port: owner said hold off.
- **v1.2.97** (7 Nov 2026; owner: "once this is done, merge to main and make a new desktop build"; from `main` at build 467,
  run 42396748681): the recorder-cut fixes from CodeRabbit (PR #9, merged) on top of v1.2.96. From now on: beta → main by PR
  so CodeRabbit reviews it.
- **v1.2.96** (7 Nov 2026; owner: overnight stability pass, merge and a desktop build when done; from `main` at build 466,
  run 42352114563): the stability pass (465) and its review fixes (466) on top of v1.2.95.
- **v1.2.95** (6 Nov 2026; owner: merge and a desktop build when the punch list is done; from `main` at build 463, run
  42341637417): the six more generators (463) on top of v1.2.94.
- **v1.2.94** (6 Nov 2026; owner: "check-in, merge, and push a desktop build"; Run workflow through the API with version 1.2.94,
  from `main` at build 462, run 42341079747): unsaved-samples prompt, no FLX pop-up, phrase on the cursor, playhead on the sound,
  GEN key, screen-off ramp, playback report, keep screen on, clock watch, hybrid (phones only), Drum pattern and Mutate (450–462).
- **v1.2.93** (6 Nov 2026; owner: "can we push to main and make a new build?"; Run workflow through the API with version 1.2.93,
  from `main` after merging `beta` at build 449, run 42331534670): FOLLOW PATTERN in Pattern view (446); Record a sample…
  finish-the-pattern STOP, note keys + OCT in the editor, drop-down ▾ (447); the review fixes (448); ask first, exact STOP
  (core v7), F fits (449).
- **v1.2.92** (5 Nov 2026; owner: "let's push a desktop as well"; from `main` at build 445, run 42173205344): the Edit… button on sample
  layers, orange wave / blue selection, the FX-card waveform window (445).
- **v1.2.91** (5 Nov 2026; owner: "push these things to main and make a desktop build"; Run workflow through the API with version 1.2.91,
  from `main` at build 444, run 42170846534): FLX clock fix and FLY (438–439), the sampler (stereo, 5-minute samples, loops,
  Record a sample… with pattern sync, the sample browser, the sample editor: 440–444); the desktop app now allows audio input
  (audio only) and has the macOS microphone usage string.
- **v1.2.90** (5 Nov 2026; owner: "merge and build"; from `main` at build 437): BPM box in the page colour, a line before LEFT.
- **v1.2.89** (5 Nov 2026; owner: "merge it, please" / "and build desktop"; from `main` at build 436): bus outline
  see-through, bottom panel in the page colour with a black rule, tabs line up with the view buttons.
- **v1.2.88** (5 Nov 2026; owner: "Merge and build please"; Run workflow through the API with version 1.2.88,
  from `main` after merging `beta` at build 434): bus / send colours everywhere (433), keys play on INST / Mixer / FX, New MIDI instrument (434).
- **v1.2.87** (5 Nov 2026; owner: "merge it and build a new desktop"; Run workflow through the API with version 1.2.87,
  from `main` after merging `beta` at build 432): bus feeders in the Mixer, meter click → VOL, FX bar type in place, App Nap held off while playing.
- **v1.2.86** (5 Nov 2026; owner: "can we merge and build desktop?"; Run workflow through the API with version 1.2.86,
  from `main` after merging `beta` at build 430): the narrow-window deck fix and the Arrange paged-out look.
- **v1.2.85** (4 Nov 2026; owner: "Give me a desktop release please!"; Run workflow through the API with version 1.2.85,
  from `main` at cache v428): the transparent app icon (yellow kept on the Apple touch and maskable icons).
- **v1.2.84** (4 Nov 2026; owner: "push to the main and start a desktop build"; Run workflow through the API with version 1.2.84,
  from `main` after merging `beta` at build 427): the full test sweep's ten fixes (422–425), HOP docs (426) and the follow-up (427).
- **v1.2.83** (3 Nov 2026; owner: "push it and build a desktop app"; Run workflow through the API, from `main` at build 421; v1.2.82
  went out minutes before at build 420): the bus bars from the group bar, centred in the gaps, no white edge (419–421).
- **v1.2.81** (3 Nov 2026; owner: "merge build"; Run workflow through the API with version 1.2.81, from `main` after merging `beta`
  at build 418): the bolder bus frame in Arrange.
- **v1.2.80** (3 Nov 2026; owner: "merge and build!"; Run workflow through the API with version 1.2.80, from `main` after merging
  `beta` at build 417): END / JMP commands and the …pump CHAIN menu.
- **v1.2.79** (3 Nov 2026; owner: "Push all to main and build a desktop build"; Run workflow through the API with version 1.2.79,
  from `main` after merging `beta` at build 416): SWITCH TO …PUMP (409), CHN off when a track leaves …chains (410), ⌥▲▼ from
  the pattern on screen (411), pending FX commands kept + Enter twice on an effect value (412), FX bar in the skin's accent
  (413), the master column's own blocks + shared-phrase warnings (414–415), UNIQUE PER TRACK (416).
- **v1.2.78** (3 Nov 2026; owner: "push from beta to web and build a desktop"; Run workflow through the API with version 1.2.78,
  from `main` after merging `beta` at build 408): FX graph screens and animations (404–405), ⌥▲▼ between patterns, CURSOR ON
  STOP → PHRASE / ARRANGE ON STOP, REMOVE rows, row colour fixes and Pattern view row colour, sections beside Phrase,
  duplicate track from the block menu, master OFF blocks.
- **v1.2.77** (3 Nov 2026; owner: "let's push to main and build"; Run workflow through the API with version 1.2.77, from `main`
  after merging `beta` at build 403): the Table engine's Load wavetable… (402) and the 30 AKWF starter tables (403).
- **v1.2.76** (3 Nov 2026; owner: "merge and build desktop"; from `main` after merging `beta` at build 401): side panel
  instrument names in full, the Harmonizer's voices in the pitch animation.
- **v1.2.75** (3 Nov 2026; owner: "push a new merge and desktop build"; Run workflow through the API with version 1.2.75,
  from `main` after merging `beta` at build 400): the mouse audit follow-ups (builds 379–400): Arrange paging and bus frame,
  vintage samplers (VSAMP), FX bar animations for every effect, row colours, solo/mute rules, Acid generator from the song's
  key, side panel restyle, transpose lock, slow double-click fix, Enter picks up patterns, Alt+B / Alt+S, STOP CURSOR stays,
  FX bar keys, …chains folded I/O titles, and the …waves live engine.
- **v1.2.74** (3 Nov 2026; owner: "merge beta and the app and build a desktop version"; Run workflow through the API with
  version 1.2.74, from `main` after merging `beta` at build 378): everything in the 3 Nov entries (audio-thread FXV / BYP /
  MUT / GEN / SoundFonts, Ableton Link (its optional addon step: check it built on Mac / Windows), FX bar curves and Flex
  pictures, Audio & MIDI ports table, tracker keys, skins, STOP CURSOR, FLX fixes, GAT 00, edit logo).
- **v1.2.59** (2 Nov 2026, UTC; Run workflow through the API with version 1.2.59, from `fa3229d`) has build 355: the 1 Nov
  audit work (menus, latency comp, default song, favourites, Perform, sends, scale mode), the mobile top bar, render Cancel and
  the FX bar changes. All three platforms built.
- **v1.2.34** (31 Oct 2026, UTC; Run workflow through the API with version 1.2.34, from `3c992cb`) has build 294: the …pump
  creative effects, MASTER and the …chains pump modules ("30 Oct (26)" below). All three platforms built.
- **v1.2.26** (27 Oct 2026; Run workflow through the API with version 1.2.26) has the gain staging (TRIM, master OUT, PRE
  sends, the CHN crossfade) and its review fixes: "27 Oct (6)" below, build 265.
- **v1.2.22** (24 Oct 2026; Run workflow through the API with version 1.2.22) has everything in "24 Oct" below: the Windows
  Arrange-box and …pump-meter fixes, the reverb Mix curve, mouse on values, the mixer fader bars and the header meter.
- **v1.2.20** (24 Oct 2026, UTC; Run workflow through the API with version 1.2.20, from `0e73a86`) has everything up to
  that commit: the LXR-02 engines (`lxr/`), the …pump bus and master effects (`pump/`), the grouped FX column picker,
  ROWS → PATTERN LEN, LPB 8 for new songs and the phrase cursor wrap. All three platforms built; installed copies update
  themselves from it. Releasing from a session: `gh api -X POST repos/matthettich/thunder-plus/actions/workflows/desktop.yml/dispatches -f ref=main -f 'inputs[version]=X.Y.Z'`
  (sessions can push branches but not tags), next version after the newest on the Releases page.

## Before a commercial release (standing checklist: keep it current, read it before selling)

- **Vintage sampler model names** (3 Nov): the `vsamp` effect's MODEL list (`VSampCore.MODELS`: SP-1200, SP-12, MPC60, SP-202 grades,
  Emulator II) and its presets (`FX_FACTORY.vsamp`) name the machines. Rename to neutral names ("12-bit drum machine 26k"…) before
  selling; the DSP is original.
- **Skin and preset names that name products or people** (2 Nov): the Hardware skins (Vintage MPC, Nord red, 808, 909,
  Dr. Sample SP-202), Wes Anderson, TE, Braun, Koss, and the Hack / Owls preset groups. Fine for the owner's own use;
  rename to neutral names (e.g. "Grey sampler", "Red keys") before selling. The skins use colour palettes only, no logos.

Owner (23 Oct): leaning away from selling Thunder Plus, but may change their mind. Keep a commercial build a short,
mechanical job: everything it would need is listed here, and new work must not add to it without a line below. This
is housekeeping, not legal advice: have a lawyer look before selling.

**Today:** Thunder Plus itself has no licence (no LICENSE file; `electron/package.json` says `"license": "UNLICENSED"`),
so it is all rights reserved, though the repo is public. All third-party code is MIT / BSD / ISC / zlib / Apache, fonts
are OFL, the GM SoundFont is MIT, **except `lxr/` (GPL-3.0) and `link/` (Ableton Link, GPL-2.0-or-later, desktop only)**.

1. **GPL: `lxr/` only.** To ship closed: `git rm -r lxr` (optionally also the `<script src="lxr/lxr-engine.js…">` line in
   `index.html`; every other LXR hook is guarded by `typeof LxrCore`, `sw.js` caches it as OPTIONAL, and
   `electron/copy-app.js` copies it only when present). Pads on a Firmware engine then open as Layers, so render any that
   matter to samples first (Render a part to a slot, or Export this pad). Check: no "LXR-02 firmware" engine group, no
   File ▾ → Import LXR kit, no LXR errors besides the script's 404. The other way is to release under GPL-3.0 (selling
   is allowed; the source must be offered; buyers may share it). The plugin / mobile plan
   (`docs/ENGINE-PLUGIN-PLAN.md`) must leave `lxr/` out of any closed build.
2. **The rule that keeps this list short.** Only permissive code goes into the app itself. Anything with limits (GPL,
   LGPL, non-commercial, attribution-only-with-conditions…) goes in its own folder the way `lxr/` does: its own LICENSE
   and NOTICE, loaded optionally, every hook in the app guarded, OPTIONAL in `sw.js`, copied-if-present by
   `copy-app.js`, a section in `THIRD_PARTY_NOTICES.md`, and a line in item 1. Never non-commercial code at all (the
   original Sonic Potions LXR firmware is non-commercial; the LXR-02 code in `lxr/` is Erica's GPL release).
3. **Names in the UI** (trademarks; a "based on X" credit can stay):
   - layer / oscillator names `Plaits`, `Elements`, `FM Genesis`, `FM SoundBlaster` (the oscillator table near the top
     of the engine block); pad effects `Clouds`, `Rings`, `Beads`; the `Plaits` section title. Later additions already
     use generic names (Cross-mod, Space reverb, Lo-fi…) with credits in `FX_CREDIT`.
   - drum kits and presets: "CR-78- / 808- / 606- / 909- / 707- / 727- / 505-style" (and the one "Roland" in the kit
     notes), "(808-ish)", "(Nord-ish)", "House organ (M1 style)", and the "MPC swing" labels.
   - help and README comparisons (Renoise, M8, SunVox, Koala, Ableton) only describe; reword if needed.
   - the …waves and …chains editors (`waves/`, `chains/`, also from `matthettich/waves`) carry the same Mutable names.
   - LXR names go with `lxr/`; if it stays, keep them truthful ("from the LXR-02 firmware"), as its NOTICE requires.
   - Find them: `grep -n -E "Plaits|Elements|Clouds|Rings|Beads|Genesis|SoundBlaster|Roland|CR-78|808|909|606|707|727|505|Nord|M1 style|MPC" index.html chains/index.html waves/index.html README.md`
   - check that "Thunder Plus" / "...Thunder" is free to use as a product name where it would be sold.
4. **Notices that must ship** (in place; keep them current): `THIRD_PARTY_NOTICES.md` (the MIT texts and credits, and pffft's
   BSD-style notice for `pump/pump.wasm`, which is otherwise the owner's own …pump code: nothing to remove for a closed build; it is
   copied into the desktop app), `fonts/OFL-*.txt`, `sounds/FluidR3Mono_License.md`, and Electron's own licence files
   (electron-builder adds them). The credits shown in the app (`FX_CREDIT`, the effect help) stay.
5. **Thunder Plus's own terms:** pick a licence or EULA, set `"license"` in `electron/package.json`, and decide whether
   the repo stays public. Code signing (Windows, Mac notarisation) is separate from licensing.
6. **Plugins and mobile, if they happen** (`docs/ENGINE-PLUGIN-PLAN.md`): iPlug2 is permissive; JUCE needs a paid licence
   (or the AGPL); the VST3 SDK is MIT since 3.8, but the VST name and logo have Steinberg's own terms; check the app
   stores' terms.
7. **A native desktop app, if it happens** (`docs/NATIVE-DESKTOP-PLAN.md`, section 9): the **ASIO SDK** (GPLv3 or
   Steinberg's proprietary licence) and **Ableton Link** (GPLv2+ or Ableton's free proprietary licence) are kept as optional
   build flags (`THUNDER_ASIO`, `THUNDER_LINK`); a closed build needs both proprietary agreements or leaves them out. If
   JUCE is chosen for the host instead of permissive libraries, it needs a commercial JUCE licence.
8. **GPL: `link/` (Ableton Link in the Electron desktop app, 3 Nov).** The addon is built from the Link SDK (GPL-2.0-or-later,
   or Ableton's free proprietary licence by application: link-devs@ableton.com). To ship closed without that licence:
   `git rm -r link` (the workflow's "Ableton Link addon" step then does nothing; `electron/main.js` loads
   `electron/link/thunder_link.node` in a try, `package.json` packages `link/**` only if present, and every page hook is
   guarded by `LINKAPI` / `lk`). Delete a locally built `electron/link/` too. Check: Settings → Audio & MIDI → Ableton Link
   says "Not available in this build", and no `thunder_link.node` in the packaged app (`app.asar.unpacked/link/`). The code
   that stays (`electron/link-session.js`, the `index.html` block) is ours and has no Link code in it. With Ableton's licence,
   keep `link/` and replace its LICENSE / NOTICE with what that licence asks for. Either way, "Ableton Link" in the UI
   follows Ableton's guidelines (the name in full, Enabled / Disabled).

## To-do (owner's list: keep it current; newest dated entries below have the details)

### Where things stand (13 Nov, morning: READ THIS FIRST; it supersedes the 10 Nov block below where they disagree)

**Web beta is build 575.** Builds 569 to 575 (12 to 13 Nov, the dated entries below) came from the owner's phone testing: Arrange cursor and follow fixes, one song wrap for all tracks, effect state that persists, master block mute, loop play / LP.SEL / INS, the lost-notes fix, per-track playhead, renders with gaps, the core coming back after a clock hiccup, the Mobile header, the CPU panel, Lite reverb, 111 presets, and the draw pass (playhead redraws in place). **None of 569 to 575 has been verified on a real phone; nobody has listened to Lite reverb or the new presets.** The owner's test items are in the punch list.

**Next work, in order (the owner: stability, efficiency and battery first; "if you have to make a decision, weigh … a solution that would port to a native app easily"; best guess and tell him):**
1. **Meter rate as a setting** (owner, 13 Nov: "Can we make #1 a setting"): the remaining long tasks on Mixer and Pattern view while playing are the browser painting the 30 Hz meters and bands. In progress (build 576).
2. **First PLAY stall (audio review, lead):** at 4x the press handler runs about 446 ms (`rtInit` 256 ms) and a second `tStart` pass after the worklet hand-over holds the thread about 1.5 s; his phone showed a 130 ms long frame and a first note 53 ms late. The fix is in the audio start (warm the engine before the first PLAY), not the draw.
3. **Song / Arrange playing-row mark in place** (it still takes the full `draw()` per row; Sonnet, display only).
4. **Idle timers:** three 200 ms timers run while stopped (the frame loop, the header meter, a generic idle runner); let them sleep (small battery gain).
5. **CPU panel:** per-engine cost of drum and synth voices is still not measured, so "what is using the most" is incomplete there (needs audio-thread measuring: lead decides how).
6. **`tcChunks` without allocation** (described in `docs/audits/2026-11-13-draw-pass.md`; only matters if a browser ever delivers blocks over 128 frames: low priority).
7. Then the older list: "Feature priority" and "Longer-term priority" below (MOD expansion, automation lanes, …), still under the feature freeze except what the owner asks for.

**Waiting on the owner:** (a) the double-tap-PLAY-goes-to-the-top gesture: keep it limited as in build 573, or remove it; (b) LP.SEL on the pad ("I will think about pad placement"; a mock put it between LOOP and ALT); (c) **native apps: "we will decide if we push forward with native tomorrow" (said 12 Nov)**: nothing started, the plan is under "Native port" below; (d) two experiments that would explain the phone's clock hiccups: 2 minutes untouched vs tapping every 10 to 15 s, and plugged in vs on battery (the report records battery since 573); (e) on Safest sounding quieter: the app's output level is identical (−14.8 dB RMS both ways), so try the phone's Dolby Atmos / equaliser off.

### Where things stand (10 Nov, 15:10: READ THIS FIRST; it supersedes older lines below where they disagree)

**11 Nov update, builds 563–567 (567: the core's per-block garbage; the wide node left by the owner's decision; 564: title bar touch-ups; 565: Song grid centred with SECT; 566: the core sequencer profiled and its fast paths fixed, see the 11 Nov entries): read this before the 10 Nov lines below, which it supersedes where they disagree.** Owner (10 Nov, 22:49): "do the queue and push a full desktop app after"; had "a really stable seeming few minutes" on the phone after Background play went back on; interests while building: stability, usability, efficiency and battery on mobile, and a simple move between platforms (phone, Mac, Windows, maybe a tablet or Android hardware). "Fable can fix anything challenging for less capable agents without my input": the lead decides and reports. Queue status after build 563 (see the 11 Nov entry):
- DONE: 1a text churn (redraw paths now write text only when it changes); 3 per-block garbage (chains / waves hosts and modules, `rvMix`, `__tcOv`, pump synth); 4 silence shortcuts (master worklet; ThunderFx nodes clocked only by Flex / Step gate); 5 the −58 dB floor (NOT the master: the bass engine's own harmonics, intended; `docs/audits/2026-11-11-audio-questions.md`); 6 Vox REL (fixed, vox goldens regenerated) and pump synth repeatability (a test artefact: the sweep's first render was at `ENGINE_LEVEL` −6 dB; sweep fixed); 7 second half (the tools were already right: they use the app's own `coreLoad`); 8 usability U4 / U6 / U12 / U15 / U16 (U14 was already done); 9 as a safe reload offer (a true AudioContext rebuild was judged unprovable without hardware: about 300 holders of `ctx`).
- **The 20-minute page-vs-core A/B is DONE (11 Nov, build 563, headless Chrome, 2 CPUs, no real audio device; raw output not kept):** page sequencer: 259 dropouts (13 a minute), no late or dropped notes, 0 FAIL. Core (audio-thread) sequencer: 3,584 dropouts (179 a minute, 35.8 s of underrun), a note up to 459 ms late, up to 5 notes dropped, audio clock −15.3 ms / min against −7.2, and renderer memory growing 3.24 MB / min (bound 3; FAIL). The page run had MORE outside load than the core run (the lead's test runs overlapped it), so the gap is not machine noise: on a starved machine the core costs far more audio-thread time. Not shown: that it is worse on a real device (the owner likes its timing on desktop; the phone already uses the page sequencer). **For audio review (lead): profile the core's per-block cost and the memory growth with the core on (10 Nov soak saw the same direction, 24.6 vs 2.4 a minute); nothing changed in the defaults.**
- Review leftover (PR #91, CodeRabbit): `tests/sweep/redraw-churn.mjs` counts an A→B→A text write inside one observer batch as unchanged (a measuring tool only). The other three findings were fixed in the PR (reload rechecks recording / render and counts song IRs; a limiter switched on resets the zero run).
- STILL QUEUED (the core was profiled and fixed in builds 566–567; the 37-output node is left by the owner's decision): smoothing only if the owner hears steps; Link desktop builds in CI; chains host and core silence shortcuts (any chains module can be a source; the core's idle work is clock logic: lead only); `tcChunks` (blocks over 128) still allocates; ThunderFx host paths (`modRun`, `sub()`) and the bass / vox / mach processors not audited for garbage; about 40 same-value attribute writes per redraw (no allocation); screens never walked (Pattern view on touch, Synth page unfolded, pump windows); open question: the live bass node reads 25–40 dB above `BassCore` offline at cut 300 in the high band (audit file, unresolved).

**Web beta: build 562** (https://matthettich.github.io/thunder-plus/beta/). `main` and the desktop app are far behind (desktop v1.3.4-beta.2 = build 541): nothing since build 541 is on the desktop; a desktop beta from `beta` only when the owner asks. Every PR into `beta` now runs `npm test` and the sequencer goldens in GitHub Actions (`tests.yml`).

**Owner's direction (10 Nov afternoon):** keep working on AUDIO STABILITY of the web app; the native port is PARKED ("especially if we can lower the priority of a port"). He will test when he has time ("it's been hard for me to have the time to review"): do not block on him; queue tests in the punch list.

**Done on 10 Nov (builds 548–560):** the three audio-review fixes, usability / battery quick wins, save safety (a real data-loss bug), device recovery, the delay clock under holds, background-play restart, audio-thread stalls, idle sleep, audio-thread memory, engine goldens and CI, **MOD finished except smoothing** (LFOs always run; pump targets incl. bands and windows; live-engine targets and the tracker commands on live engines; master out; render parity), Reset audio settings, the runaway kick and its fix, the media-session experiment, the Background play guard, level over a play.

**Waiting on the owner (all in the punch list below; the first three matter most):**
1. **The long screen-off run with Background play ON** (3–5 minutes, a volume key press in the middle): decides whether the phone needs the native port at all, and whether the kick (`RUNKICK`) and the 40 Hz background clip (`MS_ACTIVE`) stay.
2. **MOD by ear:** steps at 1/16–1/32 on a filter cutoff and on a delay-time / pitch parameter of a built-in effect (decides smoothing: he said steps are fine except pitch and sometimes filters); live-synth CUT / LFO on a held note; old songs that used CUT / RES on a live synth (now the synth's own filter, not a low-pass after it).
3. **Desktop hardware:** unplug / replug the interface, default output change, sleep and wake, Reset audio engine; Windows recording and MIDI; the helper checklist; Link.
4. A playback report from the song that stutters and then stops under LOOP. Whether pads feel late after a runaway (output delay read about 390 ms twice).
5. Small choices: the "Help" header in File; closing the pump window after Add LFO…; scenes / macros on CUT for live synths (offset from the knob); whether the PROJECT screen's BACKGROUND PLAY row should go or ask first (a stray EDIT can flip it).

**Queued work that needs no owner input (do in this order; delegate to Sonnet, lead reviews audio):**
1. (`fix/worklet-robustness` is MERGED as build 561, see the 10 Nov entry; `test/soak` still to review. A new owner choice came out of it: chains / waves Flex pattern SCRATCH has depth 0.125 there and 0.105 in the track effect, and Step gate with Pattern CV at 0 plays the custom steps there; unifying the copies changes old chains patches, so it waits for his word.) Was: review and merge what was running at 15:10: `fix/worklet-robustness` (block sizes other than 128, pump-synth wasm guard, chains' Flex / Step gate copies) and `test/soak` (`tests/sweep/soak.mjs` + `docs/audits/2026-11-10-soak.md`); then fix what the soak finds (leaks, growth, stalls over 10 minutes).
1a. From the soak test: (`#tTitle`, `#tInfo`, `#tState` done in build 562; the `<output>` readouts and Mixer spans still churn.) Then a 20-minute page-vs-core sequencer A/B on a quiet machine (the core showed more dropouts; unproven).
2. Smoothing, ONLY if the owner hears steps: the effect host ramps a modulated value toward its next point per block, scoped to pitch / cutoff-type parameters (lead designs; about one session).
3. Left from the memory pass: per-block garbage in the chains / waves hosts and some modules (generated / shared text), `rvMix`, `__tcOv`'s per-block objects, pump-synth worklet's `at` closure.
4. Battery finding 3: `clocked` effect worklets and the master / chains / core with silence shortcuts while stopped (audio review).
5. The master chain's roughly −58 dB high-band floor on bass-heavy mixes (limiter / clipper harmonics; found by the engine-targets sweep): is it intended?
6. Vox REL has no audible tail in a render (older than build 555); pump live synths are not repeatable render to render.
7. Ableton Link desktop builds in CI; `tools/flex-check.mjs` and `tools/sampler-check.mjs` still build the core node with NT + 4 outputs (now NT + 5).
8. Usability leftovers: audit items U4 Close / Done, U6, U12, U14, U15, U16; screens never walked (Pattern view on touch, Synth page unfolded, pump windows); `~` mark inside the pump windows.
9. Reset audio engine cannot rebuild the AudioContext (a stuck stream or the 390 ms delay after a runaway needs a reload).

**Parked:** the native port (`docs/NATIVE-PORT-STRATEGY.md`, `docs/port/WORKPLAN.md`; its "only cure for the phone" premise is unproven, see the 10 Nov entry "with Background play ON"). Feature freeze otherwise holds: MOD was the exception and is done.

**The older sections below ("Stability plan", "Feature priority" items 1–2) describe the plan as of 9 Nov and are largely done; keep them for the decisions they record.**

### Stability plan (owner, 9 Nov night: "my priority is getting the app to a usable, efficient, and stable place for Android, Mac, and Windows")

The owner's goal is stability and usability first, with only the MOD page and its live-instrument targets as near-term features. How to work it: the lead plans and writes each brief, lower-cost agents the lead spawns do the work, the lead verifies every result (reads the diff, reruns the tests) before pushing. Every change goes through a PR into `beta` with a CodeRabbit review asked for (`@coderabbitai review`; it skips `beta` PRs otherwise). Audio, timing and both sequencers stay under the lead's line-by-line review.

0. **Land build 544 and unique-per-track** (10 Nov, see the dated entry below). Done when both PRs are reviewed and merged, and the owner has run the Worklets test on the Fold.
1. **Confirm the 9 Nov fixes.** Agents retest each item of "9 Nov: the owner's phone bug list" and builds 531–543 headless (Pixel 7 profile and desktop). Lead sorts each into fixed / not fixed / only testable on the phone. Done when every item has a status and the owner's punch list is current.
2. **Stability on Android.** Waits on the Worklets test. Clock holds with worklets off: build a phone-safe audio mode (which effects and limiter can run on native nodes; a context made without worklets when the song needs none). Clock still races: build the resync sketched in "8 Nov, end of session". Either way fix what the `slow …` and `long frame` report lines show for the stutter that stops under LOOP.
3. **Battery audit.** Agents measure main-thread and audio CPU idle, playing and hidden, per screen, and list every timer, redraw and node that runs when it needn't. Known candidates: sleep the engine sooner when hidden and stopped on phones (20 s today; held back until the Worklets test is done, since the test needs that window), …waves embedded starts its own AudioContext and worklet when its screen is opened (`waves/index.html` `startEngine`, no EMBED guard) and it stays alive, five to seven worklet modules loaded at first touch whether or not the song uses them. Done when before / after numbers show the saving and the golden and timing tests are unchanged.
4. **Usability audit.** Agents walk every screen folded, unfolded, Mac and Windows sizes for awkward flows, dead ends and keys or gestures that differ between screens (start from `docs/USABILITY-AUDIT.md` and the 3 Nov mouse audit so nothing decided is reopened). Lead ranks; only findings that change something the owner chose go to the owner. Small fixes ship in batches.
5. **Mac and Windows desktop.** Agents run the desktop checks (MIDI, Ableton Link, the I/O helper, updater) and reduce the hardware checklist to what only the owner can do. A desktop beta from `beta` once steps 1–4 are in.
6. **MOD page, then live instruments.** Harden MOD as built (the owner's by-ear and touch checks), then its targets on the live engines' own filter and envelope, one engine at a time (Feature priority items 1 and 2).

Steps 1, 3 and 4 can run side by side. Step 2 waits on the phone test. Step 6 is last.

### Which model for which work (owner, 9 Nov night: wants to spend usage carefully; the top model only for audio work and verification)

| Work | Model |
|---|---|
| Archiving old handover entries, build number / changelog / `sw.js` bumps, merging reviewed PRs, checking on CodeRabbit, starting desktop builds | Haiku 5.5 |
| Stability plan steps 1, 3 (measuring), 4 and 5: retesting fixes, battery measurements, the usability walk, wording and layout fixes, song-grid behaviour (like unique per track), MOD page interface work, desktop checks | Sonnet 5.5 |
| Planning or reviewing non-audio work | Opus 5.5 |
| Reading playback reports, the phone clock and worklet problem (plan step 2), anything in either sequencer, the audio thread, worklets, timing or latency, and the final check on any audio change | Fable 5.2 |

Rules for every session, whichever model:
- One job per chat, started fresh. Read this To-do section first, not the whole file.
- Every change goes through a PR into `beta`; ask for the review with a `@coderabbitai review` comment, fix real findings, merge only when it and `npm test` are clean.
- A cheaper model does not change sequencer, audio-thread, worklet, timing or latency code. If a fix needs it, write what is needed under "Bugs still open" marked **for audio review** and stop there.
- End by updating the punch list and adding a short dated entry.

Opening lines the owner can paste:
- Haiku: "Pull thunder-plus, read the To-do section of HANDOVER.md, and do this housekeeping job: …"
- Sonnet: "Pull thunder-plus, read the To-do section of HANDOVER.md, and do step N of the Stability plan. Follow the rules under 'Which model for which work'."
- Fable: "Pull thunder-plus. Here are my playback reports from the punch list: …" or "Review the items marked for audio review."

### Feature priority (owner, 9 Nov: build future features in this order)

The owner's top priority to add is the MOD screen. Work down the list; ask before reordering it. The owner will run the tests in "Owner's punch list" below.

1. **MOD screen: LFOs assignable to FX parameters** (owner, 8 Nov; first version built 9 Nov, build 539, see "9 Nov: MOD screen" below. Still to come, one step at a time: …pump effects as targets, live engines' own filter and envelope via item 2's per-engine map, renders matching live, smoothing where steps are audible). The original brief: A new screen named
  **MOD between FX and PROJECT**; PROJECT can become PROJ if the tabs need room (six already fill a folded phone: check at real size; hide the tab when
  folded, SHIFT+◀▶ still reaching it, only if it's still too tight). Suggested: one LFO per row (track, slot, parameter, shape, rate, depth), an "Add LFO…"
  on an FX parameter's value menu opening MOD with that target, a small mark on modulated parameters. Alternative was a pop-up by the parameter. Already
  built: Perform → LFOs (`S.perf.lfo[t]` {id, sh, rt, dp}, one per track, `fx:t:s:p` targets, not …pump; 4 points a row live, 2 in renders, so steppy at fast
  rates). Several per track means a save-format change, and audio-rate smoothing is per-effect work.
2. **Tracker commands that control the live instruments** (owner, 8 Nov: "I would love commands that can control the live instruments"). Today CUT / RES reach live engines only as a low-pass on their output (`liveCut`), and ATK / REL / DEC work on rendered sounds only. Wanted: commands driving each live engine's own filter and envelope (…pump live synths, …waves live, bass, vox); each engine's parameters differ, so it needs a per-engine map.
3. **Renoise-style automation lanes** (owner, 30 Oct: wanted). Plan in outline: "Possible future work: automation lanes, like Renoise" below. Shares its playback path with `FXV` / `fxp` events, so check what MOD builds first and reuse it.
4. **Search in the effect pickers** (owner, 30 Oct: nice to have). Survey and suggested order: "Possible future work: search and tidier groups in the sound and effect pickers" below; start with type-to-filter in the fxp pop-up. Line numbers there are from build 270.
5. **Track reordering** (moving a track next to its bus, as other DAWs do). Big job: every song row, mixer, send, bus
  group, block mute and MIDI mapping refers to tracks by index, so it all has to be renumbered together. Owner (2 Nov):
  later, not while stabilising. **More than 32 tracks** (owner, Oct: likely up to 128) goes with it: the GUI and the mono / poly split need working out first. Anything built above that stores a track index (MOD's `fx:t:s:p` targets, automation lanes) joins the renumbering.
6. **Song sync between devices through Google Drive** (planned, owner undecided; wants to pick up the last save on Android, Mac and Windows): "3 Nov (48): plan, not built" below. Confirm with the owner before starting.
7. **Live performance mode** (plan: "Plan: live performance mode" under 1 Nov (7–11)), starting with the queued pattern
  change it needs.
8. **Put off for now, in one bucket:** sampler key zones, velocity layers and round robin (5 Nov); pump AMP (on hold); porting pump TABLE (skipped).

### Owner's punch list (owner, 9 Nov: will test these later; keep it current)

Start a session by asking for results. Fix on `beta`. (10 Nov, 15:10: the web beta is build 560; the build numbers in the rest of this paragraph are older.) Web beta: https://matthettich.github.io/thunder-plus/beta/ (build 547 as of 10 Nov morning, 548 once its PR merges; the night of 9–10 Nov shipped 544–547, see the four "10 Nov" entries below. Decisions the lead made that night for the owner to confirm or reverse: on a phone the saved Sequencer default plays on the Page unless Audio thread is picked on that device; on a phone the safety limiter is its stand-in until another worklet loads; the resync only runs with a worklet loaded and its margins are untested on a device). Desktop: v1.3.4-beta.2 (build 541, no feed view or scopes); further desktop betas on hold until the owner asks.

**Build 575** (less screen work while playing; The screen in the CPU panel)
- [ ] Phone, a song playing, on the Mixer, Inst, FX, MOD and Project in turn: everything looks as before (meters move, values right), and nothing is stale after a tap or an edit. A value that a pattern command changes while playing (track volume / pan, an effect switch) may now show up to half a second late on those screens: say if that bothers you.
- [ ] Phone and computer, Phrase and Pattern view playing: the playing-row marks and the per-track bands move with the sound (FOLLOW off / PATTERN / ON, after a HOP, across a song wrap); typing, selecting and moving the cursor while it plays feel instant.
- [ ] Phone: does it run cooler / longer on battery, and does the CPU % read lower on the Mixer and Pattern view than on build 574? (In emulation the remaining stalls on those two screens are the browser painting the meters and bands.)
- [ ] Tap the CPU %: under the audio list a section "The screen" shows Screen drawing (a share of the page's time, draws a second) and Sequencer (page); it is not added to the audio percentage.
- [ ] Playback report after a STOP: "voices 0" (it used to keep 1 per live engine that had played).

**Build 574** (Lite reverb; more presets)
- [ ] Phone and computer, by ear (nobody has listened to it yet): add Lite reverb (FX, Reverbs group, after Plate) to a drum track and to an acid line; try its six presets and the extremes of Size, Decay, Tone, Pre-delay, Mix. Pass: a usable small room to long wash, no clicks when the knobs move, no ringing or metallic tone you dislike, silent when nothing plays. Then put it on four tracks at once on the phone and tap the CPU %: each should be a few percent at most.
- [ ] By ear: the 20 new Acid presets (Phuture 303 … Wow siren), 7 Bass, and the new ones on the other synth engines: say which to keep, cut or retune. Levels were matched by measurement only.

**Build 573** (loop play icon, LP.SEL, ALT navigation, Mobile header, CPU panel, the core comes back, media name, battery in the report)
- [ ] Phone unfolded and computer, each skin you use: the control bar's loop play button shows a play sign inside a circular loop (the word LOOP beside it on the thin bar); playing, the stop mark. After switching pad / keys mode it is still the icon.
- [ ] Phone unfolded: in Arrange select rows 4 to 7, press LP.SEL: those rows loop, the button is lit, pressed again it stops. Nothing selected: it loops the cursor's row and says to select rows. Folded: not on the bar; Edit menu and the Arrange right-click menu have it.
- [ ] Phone unfolded, song playing, Phrase with Arrange beside it: tap ALT then ▲ repeatedly across the playhead's row (FOLLOW off, PATTERN, ON), and hold it. Pass: the pattern you went to stays open and in view in Phrase and in Arrange; Arrange does not jump back until you have stopped for 4 s (a minute with FOLLOW off), then it moves once. Do it once with the Audio thread sequencer (emulation could not).
- [ ] Phone unfolded, Arrange holding the keys, cursor on a track around rows 8 to 12, song playing: tap PLAY twice quickly. Pass: the cursor stays where you left it. (Untouched cursor, Phrase holding the keys: a quick double tap still goes to the top.)
- [ ] Phone, pad mode: the tab row holds SONG … PROJECT (PROJ when tight) plus BPM, LPB, the dots, ROWS and REC; no TRANSP; in KEYS mode TRANSP is back. Say whether the row under the tabs is gone on YOUR phone unfolded (in emulation the tabs already fitted one row at 884 px; your screenshot showed two).
- [ ] Phone, song playing: tap the CPU %. Pass: a list of the heaviest parts first, a line saying whether it is keeping up, one "Try", and the two buttons work. Say what is still unclear. (Screen drawing and the sequencer are not in the list: the app does not measure their share yet.)
- [ ] Phone: open a song saved under a file name, PLAY: the notification shows that name (not "Song 1") and the app's icon (Chrome may still add its own small one); rename while playing: it follows.
- [ ] Phone, Audio thread sequencer: after a clock hiccup ("The audio clock went out of step… moved to the Page sequencer"), keep playing or stop; 10 to 20 s of steady clock later the report says "audio clock steady again"; the next PLAY is on the audio thread sequencer again (report: core=1).
- [ ] Copy a playback report: the settings line and each PLAY line show battery % and charging / on battery. Then one run plugged in and one on battery with the same song and buffer, to settle whether charging helps.
- [ ] Owner's experiment (asked 12 Nov): play 2 minutes without touching the screen, then 2 minutes tapping somewhere harmless every 10 to 15 s. Does the clock hiccup only come when untouched?

**Build 572** (top-bar Play plays the song)
- [ ] Any device: on Phrase, Mixer and FX, press the Play button at the top by the CPU meter. Pass: the whole song plays from the cursor's song row (not just the open phrase); pressed again it stops. The three plays are now: top bar = song, control bar = loop play, pad PLAY / Space = this screen's plain play.

**Build 571** (lost notes found and fixed; per-track playhead; renders with gaps; each track on its own row at a hand-over)
- [ ] Phone, Mobile setup: new song, SHIFT+▶ on the empty Arrange cell, type notes, PLAY with FOLLOW ON for more than 5 s. Pass: the notes stay on screen, are on song row 000 and play. (This was the lost-notes bug.)
- [ ] Phone or computer, Pattern view: PLAY with the cursor on one track while another track HOPs or plays a different pattern. Pass: that track's column shows its own moving band (FOLLOW off / PATTERN / ON); the bands clear on STOP. Say whether the band is easy enough to tell from the cursor row's tint.
- [ ] Computer: render a song where one track has a gap and another comes in on a later row. Pass: both are in the WAV in the right place.
- [ ] Phone, Audio thread sequencer, a song whose tracks have patterns of different lengths: turn the screen off and on mid-song (the song moves to the page sequencer and back at the next PLAY), and STOP / PLAY with RESUME. Pass: no track jumps back to an earlier row.
- [ ] Computer, PAD mode: Shift+▼ on the Song / Arrange opens the block's phrase (from Phrase it still goes to Mixer).

**Build 570** (owner's flight list of 12 Nov: loop play, song wrap, effect state, master block mute, INS, SHIFT+▼, AUTO MASTER, clone down)
- [ ] Phone unfolded or computer: the control bar's ▶↻ plays with LOOP lit; press LOOP mid-song: it carries on as normal play, no jump; ▶↻ again stops and LOOP is back as it was. The pad's PLAY and Space play plain. (Folded in PAD mode the bar's play button is hidden: LOOP then PLAY on the pad.)
- [ ] Phone, both sequencers, a song where one track's blocks start a few rows down and another has a gap: let it play past the end twice. Pass: every track comes round on its own rows each pass (no track playing its later rows against the start), the gap track comes back after its gap, nothing jumps to another section.
- [ ] Phone: a pattern with an effect-off command (FXO 10 switches slot 1 off, FXO 90 all slots; FXO 00 puts every slot back to its saved setting, so not that one), then patterns below without that command; also let the song wrap. Pass: the effect stays off until another command turns it on; STOP then PLAY starts clean. Old songs that relied on the wrap to switch an effect back on need a command on their first row.
- [ ] Arrange: M.PAT (or Alt-click / long press) on a master-column block strikes it through; while muted its commands do nothing (the master still sounds); unmute and they act again. It survives copy, paste, move, undo, save and load.
- [ ] Any device: INS on Phrase and on the Song adds an empty row below the cursor's row, the cursor stays, one Undo reverts it; Edit menu "Insert row below" (Ctrl/⌘+Alt+Enter). On the folded Song screen it is in the menus only.
- [ ] Phone, pad: SHIFT+▼ on Arrange opens the block's phrase on its track (Pattern view when that is on); on an empty cell a new phrase opens and nothing is put on the song until a note is entered.
- [ ] Phone folded: the Mixer's master row reads AUTO MASTER ON / OFF without clipping.
- [ ] With a bus that has an old note pattern on it: CLN↓ many times leaves no empty row. (If an empty row still appears with no bus involved, say which rows: a clone that lands on a block inserts a whole song row by design, 27 Oct / 2 Nov.)
- [ ] Phone: new song, type three notes into a new pattern, PLAY at once. Pass: the notes are still there, also after reloading the app. If it hangs again: did the app reload, or did the notes vanish while it stayed open?

**Build 569** (owner's phone list of 12 Nov: Arrange arrows, STOP cursor, GEN, transpose, clear solo / mute, bus notes, Low power selection)
- [ ] Phone, Audio thread sequencer, a long song playing: on the Song / Arrange hold ▲ then ▼. Pass: the cursor keeps up and the audio does not lag or glitch. (Measured in emulation only: long tasks fell from 15–41 to 1–11 per 4 s hold; ▼ still has one slow redraw when it runs past the song's end plus 32 rows.)
- [ ] Phone, FOLLOW ON, then FOLLOW PATTERN, playhead looping on the top rows: hold ▼ for more than 5 s. Pass: the cursor runs past the bottom of the screen and the view pages with it; nothing jumps back while the arrow is held.
- [ ] Phone, ON STOP at STAYS: PLAY from the pad, ▼ a few rows, STOP before the playhead reaches row 2. Pass: the cursor stays where you put it.
- [ ] Phone: on the Song / Arrange press GEN on a block: its phrase opens with the generator menu. On an empty cell: a new pattern is made, opened, and the menu shows. On a bus or the master column: only a message.
- [ ] Phone or computer: select several blocks in Arrange (one pattern twice), SEMI + / − or Alt+, / Alt+.: every pattern moves once, the message counts them and says how many are also used elsewhere; one Undo puts it all back. No selection: the block under the cursor.
- [ ] Phone UNFOLDED or computer (the pad's MUTE / SOLO keys are hidden under 600 px wide, so folded there is only the menu entry "Clear all solo and mute"): solo and mute a few tracks, tap SOLO (or MUTE) twice quickly. Pass: every solo and mute clears, per-block mutes in Arrange stay. A single tap still acts at once. Check the marks: soloed track numbers are solid blocks on the Song header and Mixer, the Phrase title says SOLO / MUTED, the pad's keys show a ring while any track is set.
- [ ] Phone or computer: paste notes into a bus track's phrase: nothing pasted (commands still paste) and a message says so. Paste, Alt-drag or Duplicate a pattern with notes onto a bus column in Arrange: refused with a message; a command-only pattern goes in.
- [ ] Phone, the three Low power skins, folded and unfolded: in Arrange the cursor (bright fill, white ring), selected blocks (dashed outline, white text), the playing block and muted blocks can each be told apart at a glance.

**Build 568** (Low power hot pink / mustard skins)
- [ ] Phone (Fold, OLED): Settings, pick Low power (hot pink), then Low power (mustard). In daylight check each is readable: the tracker (note / instrument / volume / FX columns each a different colour, cursor, playhead), Song / Arrange (blocks, bus headers), Mixer, the pads (MUTE / SOLO / LOOP / PLAY), and the Settings switches. Pass: no blue, cyan, purple or violet anywhere; background pure black. Say which colours are hard to read.
- [ ] Phone: on Low power (hot pink) or (mustard), turn Low power display ON: the skin stays. Turn it OFF: you are back on the skin you had before (not left on pink / mustard).

**Build 567** (audio-thread sequencer: less garbage)
- [ ] Same test as build 566 (computer, Audio thread sequencer, a busy song for 5 minutes): sounds exactly as before; beat-locked effects (Flex, Step gate, Retrigger) and fades / glides on live engines in time.

- [ ] Phone, Audio thread sequencer (app in view), only if you use it: a very slow song (rows more than 3 s apart, e.g. 20 BPM at 1 LPB, or a long PDL) keeps playing; before, it stopped itself after 3 s.

**Build 566** (audio-thread sequencer lighter)
- [ ] Computer (Mac and Windows), Audio thread sequencer: play a busy song 5 minutes. Pass: sounds exactly as before (levels, pans, fades, glides, Flex / Step gate timing), and the CPU box reads lower or the same. Say if either sequencer stutters on real hardware: the test machine cannot tell.
- [ ] Phone, only if you try the Audio thread sequencer (app in view): fewer or no crackles compared with before.

**Build 565** (Song grid centred with SECT on)
- [ ] Fold unfolded (and computer), Song screen with the SECT lane: the grid has about the same space on its right as the lane takes on its left; nothing cut off on the right (MST column whole).

**Build 564** (title bar touch-ups)
- [ ] Fold folded: the Tracker button's left edge lines up with SONG below it (both at the screen edge).
- [ ] Fold unfolded: the word "Thunder" is either whole or gone, never cut off; with it gone, Tracker starts at the left edge like SONG. Rotate / fold / unfold: it comes back when there is room.

**Build 563** (lighter on the phone; Vox release; reload offer; touch and contrast fixes)
- [ ] Phone, Background play on: play 2 minutes on Song, Phrase, Mixer and FX. Pass: every readout still updates live (BPM, rows, the deck's TRACK / INST / FX headings, VOL and PAN, slider values); nothing frozen or stale after changing track, instrument or tempo.
- [ ] Computer: double-click BPM or LPB in the transport and type a value. Pass: it takes the edit and shows the new number.
- [ ] Phone and computer, by ear: stop a song with the master limiter and compressor on, wait 5 s, PLAY. Pass: the first note comes in at normal level, no dip or pump; meters fall to silence when stopped.
- [ ] By ear: a track with Flex or Step gate, song stopped for over 14 s, then PLAY. Pass: the pattern starts clean, no stale audio.
- [ ] By ear: a Vox pad with REL at 1000 ms or more rings on after the note ends; old songs with Vox sound right (long-REL Vox notes now ring longer than before).
- [ ] Reset audio engine with a healthy engine: the "Audio engine reset" toast and no dialog. After a runaway on the Fold (output delay still about 390 ms): the "Reload the app?" dialog; Reload brings back the song, screen and kit with a toast. With a Bluetooth speaker the dialog may appear: "Not now" leaves everything as it was. With a recording running or a take not on a pad: it refuses or asks first.
- [ ] Computer: unplug and replug the interface, then Reset audio engine: if the engine will not run, the same dialog; say whether the desktop app's own reload fires.
- [ ] Fold folded: Synth page, pick an engine: the page scrolls so its editor is at the top. Sheets (Skin, Preset): Close / Done easy to hit.
- [ ] Phone: Settings → Screen: ROW COLOR IN PATTERN says "press and hold a row number"; in Pattern view press and hold a row number: the colour menu opens (NOT tested with a real finger).
- [ ] Windows: the Song screen's SELECTING hint says "Ctrl+C", not "Ctrl/⌘". Skins IDM, IDM pink paper, IDM pink & green, 909: BPM / LPB readable while playing; "Stop" dark on pink in pink paper; POLY 09–16 label readable.

**Build 562** (chains / waves effects unified with the track effects)
- [ ] Computer: open a chains patch and a waves patch that use Flex and Step gate (one with Step gate on its own custom steps, one with something patched into Pattern). Pass: they play; Flex SCRATCH may be a little shallower, anything else that sounds wrong is a bug.
- [ ] Phone: while a song plays, the tracker's title, info line and PLAYING / STOPPED state still update when you move or switch view.

**Build 561** (sturdier audio; nothing should sound different)
- [ ] Phone and computer: play two or three songs you know well (one with …pump synths, one with a chains or waves track). Pass: they sound as before; no "stopped working" toast.

**Build 560**
- [ ] **Background play guard (phone):** switch Background play off (Settings, or the PROJECT screen's row): a note says it is for testing only. PLAY, lock the screen a few seconds, come back: a message offers "Turn on"; tap it: it is on again and the report shows the session starting.

**The test that decides how urgent the native port is (phone, Background play ON: check the report's `settings:` line)**
- [ ] **Long screen-off run:** your normal song WITH effects, PLAY, screen off for 3–5 minutes, press a volume key once in the middle, screen on, STOP, Copy playback report. By ear: steady the whole time? Does the volume key change media volume now (no vibration)? The report: clock lines near 1.00, `held` / `kicked` counts, underruns.
- [ ] **The same with a heavier song** (more effects, live synths).

**Build 559 (phone: Background play was OFF)**
- [ ] **Turn Background play back on** (Settings › Audio › Reset audio settings does it, or the Background play switch), then repeat the screen-off test and the locked-screen volume key test, and Copy playback report: its third line now lists the settings, and `media session: …` lines should appear at PLAY. Everything measured on the phone with the screen off on 10 Nov (the runaway, the kick, the volume key) was with Background play OFF.

**Build 558 (phone: is the app a media player to Android?)**
- [ ] **Locked-screen volume key:** PLAY, lock the screen, press a volume key: is the slider that appears the MEDIA one (no vibration), does the sound carry on, and is there a media control for Thunder Plus on the lock screen? Then Copy playback report (it has `media session: …` lines; if the key still moved the ringer, the lines just before say whether the background element was paused or refused).
- [ ] **The 40 Hz tone:** on headphones or monitors from the phone, with a quiet song: can you hear or measure a constant 40 Hz at about −60 dB while playing (Android only; not in renders or recordings)? If it bothers you it can go lower, or off (`MS_ACTIVE`).

**Build 557 (phone: the kick, again)**
- [ ] **Runaway kick, retest:** the simple song with the delay, Page sequencer, PLAY, screen off, press a volume key (that sets the runaway off within a second or two), wait 15 s, screen on, STOP, Copy playback report. By ear: a short gap at the kick, then the song CARRIES ON (in build 555 / 556 it went silent until STOP). The report should show `kicked 1×` and `moved on 0×` for that stretch.
- [ ] **Volume keys with the screen off:** do they change the MEDIA volume or the ringer (the phone vibrated: ringer)? Does every press set off the dropout, or only the first?
- [ ] **After a runaway:** pads feel late (the report header read 389–391 ms of output delay twice after one, 43 before)? Does reloading fix it?

**Build 556**
- [ ] **MOD on a …pump EQ band:** select a band in a pump EQ, right-click its FREQ (or GAIN) › Add LFO…: that band sweeps, the others stay; a second LFO on another band works at the same time. The MOD screen lists only bands in use ("FX1 pEQ · B3 FREQ"). Same for BAND / WIDE / HEAT.
- [ ] **Add LFO… inside a …pump window:** right-click a knob in the window: the entry is there (not on switches / choices) and the window closes after adding: is closing right?
- [ ] **MOD on the master out:** on the master, the last target is VOL · master out: the whole mix swells and dips, the limiter is not pushed harder, and the OUT fader reads what you set after STOP.
- [ ] **Renders vs live:** a rendered song with fast LFOs sounds like it did live (renders used half the LFO resolution before).
- [ ] **Smoothing check (decides whether it gets built):** an LFO at 1/16 or 1/32 on a built-in effect's filter cutoff, and on a delay time or pitch parameter: acceptable, or a staircase?

**Build 555 (live synths: commands and MOD on a held note)**
- [ ] **CUT on a held live note:** a long bass (or acid / machine) note with `CUT 30` on its row and `CUT FF` a bar later: it goes dark and opens again without retriggering. `RES`, `ATK`, `DEC`, `REL` likewise (acid has a fixed attack; vox: CUT acts on brightness, only ATK / REL otherwise; …pump kits and fold are untouched).
- [ ] **MOD on a live synth:** on a track playing a live synth, the TARGET list has CUT, RES, DEC, ENV, ATK, SUS, REL. An LFO on CUT sweeps the held note up and down around the knob; try ENV and SUS. Steppy or clicky at fast rates? (Listen on the track, not for tiny movements under heavy master limiting.)
- [ ] **…pump live synths:** the same on mono / poly / orbit / gloss / wob (filter and envelope) and coil / tract / freeze (envelope only).
- [ ] **Your knobs are untouched:** after playing with commands and LFOs, the pad's own cutoff / envelope knobs read what you set.
- [ ] **Old songs:** a song that used CUT / RES on a live synth sounded through a low-pass AFTER the synth; now it is the synth's own filter: does any song of yours sound different in a way you do not want?
- [ ] **Scenes / macros on CUT** for a live synth: with no CUT command, the value is an offset from the knob (FF = the knob, lower closes): OK, or should it be absolute?
- [ ] **Starting mid-song:** earlier CUT / ATK / … commands are now in force for live synths when you start from a later row.

**Build 554**
- [ ] **MOD on a …pump effect:** right-click a …pump knob (for example COMP threshold on a track, or a creative effect's knob) › Add LFO…: it sweeps audibly while the song plays, returns to your own setting on STOP, and the knob shows `~`. Zipper noise at fast rates? (Per-band EQ rows are not targets yet; the …pump pop-up windows have no Add LFO… yet.)
- [ ] **Level comparison (Safest vs Balanced, or anything else):** play the same 15 s of a song on each setting, STOP, Copy playback report: compare the `level over this play: average … peak …` lines (a difference under about 1 dB is nothing).
- [ ] **Save with a missing impulse response or preset sample** (only if you ever see it): the question now names what is missing.

**Build 553**
- [ ] **MOD on the master and on a bus:** put an LFO on a master effect parameter (no pattern in the master column) and one on a bus effect: both move while the song plays, and the scope's dot keeps time. An LFO on a track that is silent for a few song rows carries on in phase when the track comes back.
- [ ] **MOD in Phrase / BUS PLAY:** playing one phrase, only that track's LFOs, its buses' and the master's move.

- [ ] **Idle sleep (build 553), phone:** leave the app showing and stopped for 2 minutes, then tap a pad and press PLAY: does the first pad sound at once, and does PLAY start cleanly? The CPU meter reads 0% while asleep; the report shows `engine asleep after N s idle`. A long reverb tail must not be cut off by the sleep.
- [ ] **Idle sleep, computer with the audio interface:** the same after 5 minutes idle (it does not sleep while a MIDI keyboard is connected, MIDI clock out is on, or the waves / chains screens are open): any click from the interface, or a late first note?
- [ ] **By ear after the memory work:** reversed pads and pads with CUT / RES / DEC, stereo pads with moving pan, bass, vox and pitch effects sound as before (tests say bit-identical).

**Build 552**
- [ ] **Computer, interface unplugged:** with your interface chosen as Output, unplug it while stopped: a message says it is playing on the system default; plug it back: "is back", and sound returns to it. Repeat while a song plays.
- [ ] **Computer, system default changed** (once with System default chosen in the app, once with a specific device): sound follows / stays as chosen, no reload needed.
- [ ] **Computer, sleep and wake** (Chrome, then the Mac and Windows apps once a desktop build has 552): after waking, PLAY makes sound without a reload.
- [ ] **Reset audio engine** with the interface chosen: sound comes back on the interface. (It cannot yet rebuild a truly stuck connection: say if a reload was still needed.)
- [ ] **…waves follows the output device:** play a key on the waves screen: it sounds on the chosen output.
- [ ] **Save with samples (any device):** reload a song with samples and Save to a file at once: it waits a moment or asks; reopen the file elsewhere (or after clearing nothing: just load it): every sample plays.
- [ ] **Phone, delay while held:** the simple song with the synced delay, screen off: do the repeats still bend in pitch while the song is being held? (A full runaway still sounds wrong: that is the kick test in build 551.)
- [ ] **Phone, background play:** PLAY, screen off, then let another app make sound (a notification or a video): does Thunder keep playing? The report shows `background play paused by the system` and `restart … ok / refused`.
- [ ] **Computer, Audio thread sequencer:** STOP late in a long song (row 60 or later) and RESUME: no freeze, and it continues exactly where it was.
- [ ] **First use of an effect in a session:** no click or stumble when the first pump or track effect is added while playing.

**Build 551**
- [ ] **Runaway kick (the important one), Fold:** the same simple song with the delay, Page sequencer, PLAY, screen off 45–60 s (the runaway began after about 30 s last time), back on, let it play 20 s, STOP, Copy playback report. By ear: is the bad stretch after unlocking shorter, with only brief gaps? The report's `kick result` lines say whether the kick worked.
- [ ] **Reset audio settings** (Settings › Audio): change a few audio settings, press it, confirm: they return to default; song, skin, output and MIDI setup untouched.
- [ ] **After waking from a locked screen:** do pads feel late until Reset audio engine? (the report once read 391 ms of output delay just after a wake)

**Build 549**
- [ ] **Record a sample settings stick:** pick an input and mode in Record a sample, change a REC / metronome setting, reload: the recorder's choices are still there.

**Build 548 (10 Nov morning; on the web beta)**
- [ ] **First PLAY of a session (computer):** reload, make PLAY your very first click or key: the song should start cleanly after a short pause (under a second) instead of stumbling in its first bars. Later PLAYs start at once.
- [ ] **MIDI after STOP while hidden (web, hardware synth):** PLAY a MIDI track, hide the window, STOP from a pad / controller or come back and STOP at once: no notes play on. Drum gear that ignores note-off can still sound the booked hits (up to about 1.2 s): say if that matters.
- [ ] **…waves inside Thunder Plus:** open the screen, play a key: sound starts on the first note (a short delay the first time is expected); held keys, MIDI notes and changing the voice count still work; leave and come back: still plays.
- [ ] **Idle look:** the CPU bar now steps instead of gliding; Mixer meters, faders and the stopped Mixer spectrum look as before (a custom colour change shows on the stopped spectrum only after the next redraw).
- [ ] **Phone, Arrange:** after a pad press it holds its page 4 s (FOLLOW on) or a minute (FOLLOW off).
- [ ] **Phone, pad keys:** a slow press never acts twice.
- [ ] **Folded:** Tracker | Synth shows both words; File rows are taller with no Ctrl shortcuts; "Help & keys" is the last File row (under a "Help" header: keep the header?); the top bar is about 6 px taller: OK?; FX opens on SLOT 1 the first time after a song loads, then wherever the cursor was left (TRACK included); Settings has a pinned Done; pop-up messages (toasts) appear just below the tab strip; Settings › Screen › POP-UP MESSAGES off hides them except the 41 marked as problems (couldn't save, storage full, render failed, audio interrupted…): any you miss, or any that still get in the way?; stars, Arrange zoom, PIN and Settings switches are easier to hit (not measured: say if any still miss).
- [ ] **Build 550, Settings › Screen › BIGGER CELLS ON TOUCH** (off by default): folded, Arrange cells go from about 15×16 to 28×26 px and the grid scrolls sideways (about 225 px for a full row of tracks); playhead, selection and bus bars line up; the zoom buttons work on top; Off brings your own zoom back. Unfolded the cells are already bigger, so nothing changes. Not the Mixer (MIXER SIZE in View) and not the classic text Song grid. Owner declined wider header Play / CPU.

**Build 546 (phone): the song is held when the clock leaps**
- [ ] **Your normal song with effects**, Page sequencer: PLAY, screen off 20–30 s, back, STOP, Copy playback report. By ear: does it keep its tempo now (gaps are expected, rushing is not)? The report's `page ticks …` lines end with `held N× (S s)`.
- [ ] **Same song, screen on for a minute:** any small pauses that were not there before? (`held` should stay 0 while the clock lines read 1.00 s in 1.00 s.)

**Build 545 (phone, after it reaches the web beta)**
- [ ] **A song with no effects** (pads and samples only), Page sequencer, Worklets On: Copy playback report should say `worklets none` on its second line. PLAY, screen off 20 s, back, STOP, Copy report: the clock lines should read about 1.00 s in 1.00 s and it should sound steady.
- [ ] **Your normal song with effects:** it plays with its effects as before; the report lists what loaded (for example `chains, cpu, master, tfx`). Expect this one can still rush when hidden until the resync lands.
- [ ] **Add the first effect to an effect-free song while it plays:** the effect is heard without a reload (a brief click at that moment would be worth reporting).
- [ ] **MUT on a pad with a sample:** the stutter that stopped under LOOP: better?
- [ ] By ear on any device: Bass engine and mastering sound unchanged.

**New on 10 Nov (build 544 and unique-per-track; on the web beta once their PRs merge)**
- [ ] **Worklets test (the important one), Fold, a simple song, Page sequencer:** (1) Settings › Audio › Worklets **On** (if it was Off: Apply (reload) first): PLAY, turn the screen off about 20 s, back on, STOP, Copy playback report. (2) Worklets **Off (test)**, Apply (reload), the same steps, Copy report. The report's second line says `worklets on` or `worklets OFF (test)`: check it matches before comparing. Effects are silent in Off; that is expected. Also by ear: does Off stutter or rush less?
- [ ] **Stopped and hidden:** in each mode (applied and reloaded as above), PLAY a few seconds so the engine is running, STOP, screen off about 15 s (under 20 s, before the engine sleeps), back on, Copy report. Same with Off.
- [ ] **Stutter under song play:** the song that stutters with Arrange showing and stops under LOOP: play it through once, STOP, Copy report (it now has `slow …` and `long frame` lines). Does that song use MUT, CUT, RES or DEC on sample pads?
- [ ] **Unique per track (build 547):** on an empty track a pattern brought from another track gets its own copy; after that, moving it up or down that track, repeating it there, and rearranging the song make no new patterns. Should Alt-drag copies onto another track also get their own copy (today they share)?

**New on 9 Nov (web beta, builds 536–543)**
- [ ] **MOD by ear:** do fast rates (1/16, 1/32) sound steppy? Which effects? That decides where smoothing goes.
- [ ] **MOD on the master:** an LFO on a master FX parameter actually moves during playback (never confirmed).
- [ ] **MOD on the Fold:** touch hold-and-drag on SHAPE / RATE / DEPTH; tabs fit folded.
- [ ] **MOD keys:** Alt+7 opens MOD; Ctrl/⌘+arrows step TARGET (▲▼ by eight), SHAPE, RATE, DEPTH; ⌘ on a real Mac.
- [ ] **Add LFO…** from an FX parameter's right-click (FX screen and FX bar); the `~` mark on modulated rows.
- [ ] **LFO scopes:** shapes look right; the dot keeps time with what you hear (measured only against the app's clocks, about 20 ms ahead of the playhead); SUM scope with two LFOs on one target; Low power display shows still shapes; no extra load on the phone.
- [ ] **Acid Mutate:** intensity low vs high stays related to the line; hand tweaks (volumes, instruments, FX, note-offs) survive; Original (O) restores; TIME off keeps the rhythm.
- [ ] **Chains rail lines:** straight accent lines from real cables look right on your patches; 909 skin contrast OK? (curves are a quick revert if wanted).
- [ ] **Chains feed view:** a track feeding a chain shows it, its path lit, the rest greyed; the output track's view unchanged.

**Carried over**
- [ ] **Desktop v1.3.0 helper checklist:** https://claude.ai/artifact/6CgCLiVo8HEH3ywky5znWM (see "PICK UP HERE" in the START HERE brief), including monitoring after reopening the window and clock in, which "seemed out of sync slightly".
- [ ] **Windows:** recording (WASAPI) and MIDI (WinMM).
- [ ] **Ableton Link:** CI build on Mac / Windows, then against Live or Koala.
- [ ] **Phone, builds 529–535 on a real Fold:** the list in "9 Nov, end of session: what to test on the phone", plus "9 Nov: builds 531–535" below.
- [ ] **Phone clock toggle test:** a simple song hidden about 20 s with Background play off, then with Keep screen on off, Copy report after each (see "8 Nov, end of session").
- [ ] **Hardware MIDI tests.**

### Bugs still open

- **Phone audio clock dips, then runs fast for about 5 s** (biggest one; cause unknown): the owner's Balanced report of 12 Nov (build 572; "12 Nov (night, 2)" entry) shows it twice, after a screen-on and 39 s into a play with no touch for 43 s; it recovers by itself. A starved audio thread catching up. Since 573 the core comes back afterwards. Wanted from the owner: untouched vs tapping, plugged in vs battery. Older notes: "8 Nov, end of session".
- **"Clock instability when the song looped round"** (owner, 12 Nov): not reproduced in emulation (`docs/audits/2026-11-12-wrap-clock.md`); probably the item above. The older "stutter that stops under LOOP" suspect (sounds prepared the first time a pattern plays) was measured: buffer builds cost 1.5 to 16.5 ms at 4x per pad on the page sequencer and recur into pass 2.
- **Quieter output on Safest:** NOT the app (levels identical in his two reports); the phone's output path is the suspect (unverified). "heard" latency on Safest is 546 ms against 75 ms, which is why it feels worse to play. After a screen-off and wake the output latency read 43 → 190 ms and stayed.
- **First PLAY stall** (see "Where things stand", item 2).
- **Notes after Stop:** no evidence either way; the report's "voices 1 / 2" was a counting artefact, fixed in 575.
- **Mixer / Pattern view long tasks while playing:** paint of the meters and bands (about 64 per 10 s at 4x after the draw pass); the meter-rate setting is the answer chosen.
- **Hand-over and RESUME with tracks of different lengths:** since 571 each track restarts on its own row (`o.at`); exact only when lines per beat match; not run on a device.
- **Mobile setting found off once** (8 Nov): cause unknown; look at storage if it recurs.
- Test-suite notes: `tests/waves.test.js` golden hashes depend on render order in the shared wasm instance; `effects.mjs`'s two "master: … default chain" checks fail on an untouched base; a heap-sampling test can fail once when the machine is busy (rerun it alone).

### Native port: Mac, Windows, Android, iOS as a stretch (owner, 10 Nov: asked for the strategy; **plan only, waiting on the owner's go**)

Owner: "write up a strategy to port thunder plus to a native Mac, windows, and Android app (with iOS as a stretch goal, unless its easy) … I am debating whether maintaining multiple apps for parity is worth managing. I still want to be able to open songs from desktop on mobile". Full text, benefits, costs and risks: [`docs/NATIVE-PORT-STRATEGY.md`](docs/NATIVE-PORT-STRATEGY.md). It builds on `docs/NATIVE-DESKTOP-PLAN.md` (core rules, bridge, timing, desktop host) and `docs/ENGINE-PLUGIN-PLAN.md`. **When the owner says "start the native spike", do stage N0.** Nothing else starts before N0's exit. **The plan a managing session works from (tasks, who does each, pass / fail gates, the brief template, the status table) is [`docs/port/WORKPLAN.md`](docs/port/WORKPLAN.md)**, with `docs/port/ENGINE-INVENTORY.md` (every piece to port, units U01–U37), `docs/port/BRIDGE-TODAY.md` (today's messages) and `docs/port/CORE-BRIDGE.md` (the new contract, draft). Owner (10 Nov): the web app must stay fully working until there is a native beta with few bugs, so the port lives on a `native` branch in new folders (`native/…`) and never edits the app; wants a plan a less capable managing model (Opus) can run, calling on Fable for the heavy work.

**Why:** the Worklets test (10 Nov night) showed any AudioWorklet makes Chrome on the Fold outrun the sound card, so the old plan's "core as wasm in a worklet" cannot cure the phone. Only audio running outside the browser can.

**Shape (the answer to the parity question): one interface, one engine, one song format, a thin shell per platform.** `index.html` (with …chains and …waves) is shown in each platform's web view; `core/` in C++17 holds the sequencer, player, mixer, effects, live engines, MIDI scheduling and render, built for Windows, Mac, Android, iOS and to wasm for the web copy; the shells only open the window, sound card, MIDI ports and files. No feature lives in a shell; features switch on `core.caps`. Two engines exist only during the port: each JS piece is deleted when its C++ passes parity (golden event streams; renders within −80 dB).

**Head start:** Plaits / Rings / Clouds / Elements, tfx, …pump (effects and synths) and LXR are already C++ built to wasm; `io/` already vendors miniaudio 0.11.26 and libremidi 5.4.3 (permissive; framework option A of NATIVE-DESKTOP-PLAN 11.1), and the helper's device, clock and MIDI code moves into the desktop shell. **Shortcut:** pads keep being rendered by the page's render Worker at first and are handed to the native core as buffers, so the first native app needs only the sequencer, player, mixer, master and effects; a pad on an unported live engine plays its rendered version.

| Stage | What | Size (rough) | Done when |
|---|---|---|---|
| **N0** spike, the go / no-go gate | Bare Android app on the Fold: Kotlin shell, Android WebView showing the real `index.html`, a small C++ library through miniaudio or Oboe playing a sequenced sample pattern. Bridge proven both ways (small messages 30–60 a second; a fast path for sample buffers). CMake + CI targets (wasm, Windows, Mac universal, Android arm64), debug APK on a GitHub pre-release. libremidi on Android / iOS confirmed or AMidi / CoreMIDI used directly. Check a song finds its samples on another device | 1–2 weeks | Clock reads 1.00 s in 1.00 s for 5 minutes screen-on, screen-off and in the background; latency measured; the owner says go. If it fails: stop |
| **N1** playback core in C++ (the inventory, 10 Nov: about 17,000 lines of JS DSP in all, and the 28 JS effects in the shared …chains / …waves block are needed here too, so re-estimate after the first five units) | Sequencer and MIDI scheduling (port `core/seq-core.js`), sample / voice player, mixer per `docs/GAIN-STAGING.md`, master chain, tfx and …pump linked directly, offline render. Runs on the web first as `core.wasm` behind a Settings switch | 6–10 weeks | ELECTRO and the owner's songs play through `core.wasm`, parity CI green, pads still rendered by the page |
| **N2** Android beta | Shell: WebView + bridge, audio out, AMidi, foreground service and notification, audio focus, songs / samples folder, fold / unfold. Signed APK on GitHub Releases with a new-build check | 2–3 weeks after N1 | The owner's song with effects plays 10 minutes screen-off, no rush, no gaps; battery no worse than the web app |
| **N3** live engines, …chains, …waves in C++ | …pump synths, bass, vox, ENGINE layer types, the two graph runners; then pad rendering moves into the core | 4–8 weeks, can overlap N2 | No JS DSP left; the web copy runs wholly on `core.wasm` |
| **N4** Windows and Mac shells | Everything `electron/` does (NATIVE-DESKTOP-PLAN phase 4 checklist), Settings › Audio (driver, device, buffer, rate, channels). WebView2 on Windows, WKWebView on Mac (check every screen). Beta beside Electron, then Electron retired | 4–6 weeks, any time after N1 | Parity checklist ticked on the PC and a Mac; the owner has made music on it |
| **N5** desktop extras | Plugin hosting (VST3 / CLAP / AU), audio tracks, the synth-engine plugin | each on its own | |
| **N6** iOS (stretch) | Reuses the Mac shell's WKWebView, CoreAudio and CoreMIDI code; new: audio session, Files picker, TestFlight, store review. Needs the Apple Developer Program ($99 a year) | 3–5 weeks after the Mac shell | |

Suggested order: N0 → N1 → N2 (phone cured) → N4 with N3 alongside → N5 → N6. N2 and N4 swap freely (both wait on N1).

**Songs across devices:** the same `.thunder` file everywhere; what a device can't do is carried untouched (a desktop plugin shows "desktop plugin" on the phone and plays its frozen audio); each native app is given one folder, and pointing every device at a folder a sync tool mirrors (Google Drive, Syncthing, iCloud Drive) gives "pick up the last save" with no Drive code (likely replaces Feature priority item 6); the file records the engine version that saved it.

**Who does what:** the lead (Fable) writes the briefs and reviews every audio line; Sonnet agents build the shells, CMake / CI and measurements; Haiku does release chores. Engine features stay frozen during N1–N3 (MOD's engine-side steps: finish before N1 or build once in C++ in N3).

**Owner's decisions still open:** (1) go ahead with N0? (2) Android first (suggested) or desktop first; (3) permissive parts + system web views (suggested) or JUCE; (4) keep the web copy as demo / dev copy / fallback (suggested yes; on the phone it keeps today's clock limit); (5) Apple Developer membership when the Mac shell starts; (6) iOS waits until Android and Mac are settled; (7) MOD's engine steps before N1 or in N3.

### Longer-term priority (owner, 9 Nov: the bigger items, in this order)

1. **Native apps (10 Nov: now Mac, Windows and Android, iOS as a stretch): see "Native port" just above and `docs/NATIVE-PORT-STRATEGY.md`.** Was: native desktop app, including its host framework decision and VST hosting. **A more stable desktop app, when the owner wants one** (7 Nov): `docs/NATIVE-DESKTOP-PLAN.md` section 14 lists which of
  7 Nov's bugs came from the web platform, and the options cheapest first: stay web; a native I/O helper inside Electron
  (MIDI out first, then inputs, maybe audio out), built like the `link/` addon; or the full native host (phases 3–6). Framework decision: plan section 11.
2. **Helper phase C, audio output, only if timing is still off.** **Native I/O helper** (7 Nov): phases A and B are released in desktop v1.3.0 (8 Nov). What remains is the owner's v1.3.0 and Windows tests, then phase C (audio output) only if MIDI vs audio timing is still off: "PICK UP HERE: EVERYTHING STILL NEEDED" in the START HERE brief, design in `docs/NATIVE-IO-HELPER-PLAN.md`.
3. **Skins cull** (which to keep).
4. **Synth engine as a VST, then Android and iOS apps** (plan only so far): `docs/ENGINE-PLUGIN-PLAN.md`. Its engine port is phase 3 of `docs/NATIVE-DESKTOP-PLAN.md`, so do it once, in `core/`.
5. **Before-selling checklist:** "Before a commercial release" above (renaming vintage sampler models, skins that name products, and the rest). Can wait; the owner has leaned away from selling.

### Done

- [x] **Recording counter in Record a sample** (owner, 7 Nov 21:18; built 8 Nov, build 512, by a Sonnet subagent, checked by the
  lead): `.rectime` beside Record (m:ss, h:mm:ss past an hour), on the audio clock from capture start (`RCD.t0w`; PATTERN from
  the edge `R.t0`, "waiting…" while armed), `rcdClk` / `rcdFmt` / `rcdFreeSecs`, a 250 ms timer only while recording; the
  final length stays until the next take. recorder-check covers FREE, PATTERN, Stop, next take and closing mid-take.


## 13 Nov (before dawn): the draw pass: playhead redraws in place; the wrap-clock measurement; voices after STOP (build 575)

Build 574 (PR #104) merged after two CodeRabbit fixes (Lite reverb: a Size / Pre changed while asleep applies at once on waking; its Size test compares both channels). The session's server restarted mid-pass and stopped the performance agent; it was resumed from its transcript. Sonnet agents measured and built; the lead read the display diff and did the voice count.
- **Wrap-clock measurement** (`docs/audits/2026-11-12-wrap-clock.md`, `tests/sweep/wrap-*.mjs`; build 572, 31 runs): **the owner's "clock instability when the song looped round" did NOT reproduce**; no consistent first-wrap slowdown (two exceptions, long-task size only: core Song at 4x 131 ms at wrap 1 against 86 / 52 / 88 ms later; core Pattern view 349 ms against 254 ms), the wrap step costs 0.1 to 0.8 ms, the core's counters stayed at zero. Its per-pass underrun table (272, 73, 46, 14) was computed before a fix to the report script and is a lower bound, unverified (the raw files are gone). Headless Chrome has a null audio sink and the throttle slows only the main thread, so this does not clear the phone: his Balanced report (12 Nov entry) is the evidence, and it shows a starved audio thread catching up, not the wrap. What it did find: **redraw saturation while playing** (Pattern view and Mixer: 13 to 15 long tasks per 2 s at 4x).
- **Draw pass** (`docs/audits/2026-11-13-draw-pass.md`, `tests/sweep/draw-pass*.mjs`, `idle-wake.mjs`, `first-play.mjs`, `tests/draw-pass.test.js`; display code only). `playTick()` replaces the full `draw()` per playing row: when what the screen shows has not changed since the last full redraw (`plStamp()`: cursors, phrase, tempo, layout flags), `playLite()` moves the playing-row marks in place (`litRows`, `pvPlayMark`, `drawArr` for the side panel); Mixer, Inst, FX, MOD, Project draw nothing for a row. A full `draw()` still runs when the stamp differs, at least every `PL_BEAT` = 500 ms, on Song / Arrange (its grid text carries the playing row), and whenever there is a selection, a drag, Arrange focus or paging; anything the user does draws at once as before. Touch screens: the playhead draws at most every `PL_MIN` = 50 ms (`plOwed`). `busPlan` is wrapped on the page to answer once per redraw (it re-hashed the patch per cell: 10 to 13 % of a Pattern view / Mixer redraw; the core's copy is the raw one). Inst's in-use scans memoised on `songVer` during a redraw; `pvRoom` reads a width cached on resize. **Draw ms per second at 4x, page sequencer, unfolded, before → after:** Song / Arrange 86 → 81; Phrase 126 → 28; Pattern view 384 → 55; Sect 96 → 19; Inst 272 → 12; Mixer 230 → 14; FX 87 → 10; MOD 70 → 7; Project 82 → 10. Pattern view at 6x: the page sequencer's least lead −144 ms with 6 late events → +185 ms, none. **Lead's note on what this trades:** a value that playback itself changes (TVL / TPN, effect switches by command) now reaches the Mixer / FX screens at the 500 ms heartbeat, not per row.
- **Left, in order of worth:** (1) Mixer and Pattern view still show about 64 long tasks per 10 s at 4x: the browser's Paint / Layerize after the 30 Hz meter and band writes (about 17 ms a frame at 4x); cutting it means a lower meter rate or fewer painted layers, which the owner would see (ask him). (2) Song / Arrange still takes the full draw (about 5 ms, 16 a second): an in-place playing-row mark there is the next step. (3) **First PLAY press (audio review, lead):** at 4x the press handler runs about 446 ms (`rtInit` 256 ms) and the second `tStart` pass after the worklet hand-over holds the thread 1.5 s; the fix is in the audio start, not the draw. (4) Idle and stopped: three 200 ms timers (the frame loop, the header meter, a generic idle runner) cost about 2 to 2.7 ms a second; they could sleep.
- **CPU panel:** a second section "The screen" (Screen drawing with draws a second, Sequencer (page), each a share of the page's time; a separate budget from the audio percentage); the tip suggests Low power when drawing alone takes 30 % or more.
- **The Safest setting (report):** the three latency settings only set the AudioContext's `latencyHint` (plus texts); they do not change the scheduler's lead, the pump or the render size, and the app never passes `renderSizeHint`, so **Safest cannot make blocks longer than 128 frames: the `tcChunks` allocation path is not what made Safest feel worse** (the lead's earlier lead was wrong). `tcChunks` allocates one object and closure per chunk, `.map` arrays and a subarray per channel per chunk; an allocation-free version is described in the write-up.
- **Voices after STOP (lead):** the report's "voices 1", then 2, were not notes: a live engine's note has no end of its own and stayed in the player's `alive` set until that engine's next note. `stopAll` now forgets a live voice once it has let it go. The levels after STOP in that report were tails. "Notes after Stop" (open bug) has no evidence either way from it.

## 13 Nov (small hours): Lite reverb and 111 presets (build 574)

Build 573 (PR #103) merged after three small CodeRabbit fixes (a STOP inside the first PLAY's warm-up restores LOOP / LOOP SEL; the CPU panel never says "plenty of headroom" when overloaded; two assertions had slid into a comment). Owner (going to bed): "can you make a very low CPU reverb for the fx section", "more presets for the synths, especially acid" (a Prophet-5-style poly: asked, then "scrap the polysynth for now"). A Sonnet agent on `feat/lite-reverb-presets`; **the lead read every DSP line of the reverb** (one change: the mix pair read by index).
- **Lite reverb** (`Lverb`, key `lverb`, short LITE; Reverbs group after Plate). The app's reverbs are shared modules in `chains/chains-worklet.js` (copied to `waves/` by `tools/sync-shared.py`, exposed through MODFX: `tools/modfx-spec.mjs` → `node tools/modfx.mjs`), not in `TFX_SRC`; it follows that route, so it is a track / bus / master / pad effect, a chains and waves module, a MOD target, and renders offline (bit-identical to live). Mono sum → pre-delay → 2 all-passes → 4-line feedback network (Householder mix, per-line gain and one-pole damping) → two orthogonal stereo taps. Integer delays, power-of-two rings, nothing transcendental per sample, no allocation after `create`; targets recomputed only when a knob or the sample rate changes; changed lengths crossfaded over a block; sleeps (buffers cleared once) when input is under −140 dB and the tail under −90 dB. Plain float math in one object: ports to C++ as it is (owner asked to weigh that). **Cost per 128-frame block at 48 kHz (offline harness, drums / pad): Lite 9.6 / 5.7 µs; Spring 40.6 / 37.5; Reverb 41.2 / 42.4; Room 45 / 41.8; Plate 59 / 56.1; Space 70.3 / 76.1; Convolution 150 / 141; Pitch reverb 415 / 398; asleep about 1.7 µs.** RT60 0.15 s / 1.4 s / 12.6 s at min / default / max Decay. Half-rate tank not built (little to gain at this cost). An older build loads a song that uses it with that slot empty (`normTfx` → null) and drops it on save. `tests/lverb.test.js`. Worklet versions bumped: chains `?v=48`, waves `?v=38`. **Nobody has listened to it.**
- **Presets (data only, 111):** Acid 20, Bass 7, and 4 to 6 each for Modal, Piano, Tine EP, Organ, Table, Strings, Wind, Phase, Partials, Chip, Scanned, Swarm, Drift, Machine, Vector, Bytebeat. RMS within each engine's existing range at C-2 / C-3 / C-4; `engines.mjs --only=` on the 18 engines: 451 PASS, 0 FAIL; `engines.json` gained 207 entries, no existing hash changed. Not done: drum engines, LXR-02, pump engines, SoundFont, Resynth, Layers. Agent's notes: Acid has no accent or drive reachable from a preset; its `wave` between saw and pulse drops about 12 dB (compensated per preset).
- **Found, not fixed:** `tests/waves.test.js` golden hashes depend on how many renders came before in the shared wasm instance (adding a module mid-list shifts about 150 hashes; the test skips `lverb`); `effects.mjs`'s two "master: … default chain" checks fail on the untouched base too; `tools/modfx.mjs` output differs from `index.html` in a comment and "Centre" / "Center" on one pan row (put back by hand).

## 12 Nov (night, 2): build 573: loop play icon, LP.SEL, ALT navigation, second PLAY, Mobile header, CPU panel, THE CORE COMES BACK; the owner's two playback reports

Owner (evening, then to bed: "If you notice anything else that will help with battery, efficiency, or stability, please fix it"; "Continue on the webapp and we will decide if we push forward with native tomorrow. If you have to make a decision, weigh the benefits and drawbacks of picking a solution that would port to a native app easily. If you have any questions, make your best guess and tell me"). Three Sonnet agents (`fix/phone-list-4`, `fix/phone-list-4b`) and the lead (`fix/core-watchdog`), merged on `build-573`. No `makeSeq` change in this build.
- **THE OWNER'S PLAYBACK REPORTS (build 572, Fold; read by the lead).** *Balanced* (18 min into a session, a screen-off in the middle): healthy stretches are clean (0 ms late, 0 dropped, level −14.7 dB RMS, peak −1.9). Twice the audio clock dipped and then ran fast for about 5 s: after the screen came back (up to 2.29×; `out t` up to 4.5 s old), and 39 s into a play with the app in view and no touch for 43 s (0.83, 1.34, 1.41 → "audio clock at 1.37×", the core's watchdog let go, `coreFail`, a 251 ms long frame in `MessagePort.onmessage`, underruns 17 → 25); both recovered by themselves. The pattern is a starved audio thread catching up in a burst; the rendered audio that never reached the output is dropped (the song lurches). Cause of the starving unknown; both came after a stretch without touch (asked the owner to compare touched / untouched, and plugged / on battery: he thinks plugged in is better). After each STOP the report counts 1, then 2, voices still alive ("Notes after Stop", open). *Safest* (fresh load, two plays of 104 s and 83 s): clean, no episode; **level identical (−14.8 / −1.9 dB): the app's output is NOT quieter on Safest**, so what he hears is after the app (lead's guess, unverified: Android's deep-buffer path applies the phone's sound effects, the low-latency path skips them: he was asked to try Dolby Atmos / equaliser off). "heard" latency 546 ms on Safest vs 75 ms on Balanced (why Safest feels worse to play). After a screen-off and wake the output latency read 43 → 190 ms and stayed, with about 40 underruns around the wake. First PLAY: a 130 ms long frame in `kPlay.onpointerdown`, first note 53 ms late.
- **The core comes back (lead, page-side only; the watchdog and its limits unchanged).** After `coreFail` the audio thread sequencer stayed off until a reload. Now `coreHeal()` (beside `pbClock` in the 2 s tick) compares the clocks while it is off; `CORE_OK_N` = 5 steady readings (within 3 %) allow it again from the NEXT PLAY (no second switch mid-song); the third failure in a session (`CORE_BAD_MAX`) keeps it off as before. Decided against making the watchdog more patient: during a burst the core races ahead and skips music while the page sequencer holds the song, so letting go quickly is right. `tests/core-heal.test.js`. Web-only workaround; nothing to port.
- **ALT navigation vs the playhead (agent):** `arrFollowPage` held paging off only by `arrHandT` / `arrTouchT` when Arrange has no keys, and a pad press sets neither; `phraseGrid` / `pvGo` never cleared the playhead's `pg`. New `navT` (any ALT+arrow) and `handsOn(ms, …timers)`; `pg` cleared; when Arrange pages to the playhead and the cursor fits on the same page, the page starts at the higher of the two rows. Left (owner's 3 Nov decision): with FOLLOW ON after the 4 s hold `cPhrase` becomes the playing phrase while `cs.song.y` stays, so the next ALT+▲ steps from the playing pattern. Core sequencer not testable headless ("audio thread fell behind").
- **Second PLAY jumped the cursor to the top (agent):** the 31 Oct "STOP tapped twice goes to the top" rule fired on a second tap within 450 ms whatever the user had been doing. Now only when Arrange does not have the keys and nothing was pressed since that play began (`playWallT`); else a plain PLAY. Owner asked whether to drop the gesture: no answer yet.
- **LP.SEL (agent):** control bar button beside loop play (unfolded, desktop; not folded; Edit and Arrange right-click menus). 2+ rows selected: LOOP SEL's own path (`toggleLoop` + `playCtx`); else `playPatLoop` on the cursor row with a toast. Owner: stays on the control bar; pad placement "I will think about" (a mock was shown: between LOOP and ALT).
- **Loop play icon:** `#ic-loopplay`, `cmdSongSet`; the agent fixed a stale label (`applyCmdOrder` wrote "▶↻" over it) and sized it for the thin bar.
- **Mobile header (agent; owner's idea):** Mobile PAD mode hides TRANSP − / + (the pad's SEMI does the same) and puts BPM, LPB, dots, ROWS, REC in the tab row; PROJ when tight; own line when they cannot fit (folded: as before, no row saved). In emulation 884 px was already one row, so the gain shows from about 740 px (Arrange 13 → 14 rows, phrase 23 → 25): the owner's phone renders text about 18 % larger, so he should see it unfolded. `tests/mobile-header.test.js` (its real-page check is opt-in: `TP_BROWSER_TESTS=1`).
- **CPU panel (agent):** the tap opens a panel: total, parts biggest first (track effects, chains, master effects, master dynamics, live bass; "Everything else" = total minus parts), a keep-up line (dropouts, late notes, screen stalls in the last minute), one "Try", then Copy playback report and Reset audio engine. **Not measured, so not ranked: screen drawing, the sequencer, per-engine cost of drum and synth voices** (wanted by the owner: "What's using the most"; needs measuring work: performance pass).
- **Media session:** title = the project file's name (else the song's), artist "Thunder Plus", artwork the 192 / 512 icons; set again on rename / save as / open / new. **Power line** in the playback report (battery %, charging, cores, memory, data saver, reduced motion) at each PLAY and on change.
- **Queued (owner):** the performance and battery pass (measure first: where CPU goes on the phone profile incl. screen drawing, the Safest > 128-frame path, the first-PLAY stall, then fix the safe top items); the wrap-clock measurement (`measure/wrap-clock`, running); one more reverb, very low CPU, for the FX section; more synth presets, especially acid (a Prophet-5-style poly was asked for and then scrapped "for now"). Native: decision tomorrow.

## 12 Nov (late): top-bar Play plays the song (build 572)

Build 571 (PR #101) merged after two CodeRabbit fixes in `o.at` (the recorded chain row `cr` is restored; tracks are lined up by time, not step counts, since phrases can differ in lines per beat). Owner: "The play button in the upper section, that is near the CPU meter should play the whole song, not just the track": `#tSong` now always does `tStart('song', {row:songRow()})` or stops (supersedes 9 Nov's "plays this screen", build 538); label and title updated; `tests/play-keys.test.js`. Lead, direct.

## 12 Nov (night): THE LOST NOTES found; per-track playhead; renders with gaps; each track on its own row (build 571)

Build 570 (PR #100) merged first; CodeRabbit found three real things in it, fixed before the merge: the song wrap reset each track with `fresh()` and so lost NTH counts, the instrument, TSP, ARC, DIR (it now resets only the track's place: row, cr, step, next, cnt, g, rel, r0); `wake()`'s `wr` guard kept a waiting track out of a LOOP set afterwards (the guard is `!loop` only); the punch list named FXO 00 as an off command. Owner's answers (flight): folded bar without loop play / INS is OK; SHIFT+▼ on desktop too; build the per-track playhead; the lost notes "vanished while it was open"; "please fix the two findings". A Sonnet agent (`fix/phone-list-3-ui`) and the lead (`fix/phone-list-3-seq`), merged on `fix/phone-list-3`.
- **LOST NOTES: cause found, reproduced, fixed (agent; editing code only).** On Arrange / Song, SHIFT+▶ on an EMPTY cell: `nav()` called `go('phrase')`, which already made the cell's block (`newBlock`), then made a SECOND fresh phrase and set `pendPlace` for the now-filled cell. At the first note `placePending` saw the cell filled and dropped the pending phrase silently: the notes sat in a phrase on no block. PLAY then played the block's own empty phrase and FOLLOW ON / PATTERN switched the screen to it ("the phrase was clear of the notes"). Fix: `nav()` sets `goNoNew` around `go('phrase')` for an empty cell; `placePending` never drops a phrase that has notes (its own cell, else the first free row of its track with a toast; an error toast if the track is full); `setSong` clears `pendPlace`. `tests/pending-phrase.test.js`; a pad fuzz at 6x CPU (about 60 episodes of 30 taps) found the bug on the old code and nothing after. The save-before-PLAY guard of build 570 stays (harmless). Cosmetic, left: a new song's first phrase is 001 (a stale pending phrase reserves 000).
- **Per-track playhead in Pattern view (agent):** `pvPlayMark` puts class `pvpl` on each track's own cells on the row it plays (`tp.pos[t]`, when the column shows the phrase the track plays); marked in place, no rebuild; a 24% mix of `--ts-ink`, no animation. Cost at 4x throttle: a playing redraw 6.7 → about 8.1 ms median (noisy machine). Cleared on STOP. The agent found the band hard to tell from the cursor row's tint in its screenshots: owner to judge.
- **Renders (lead):** `wakeOk` now includes `once`, and a `once` track that hits an empty cell sets `wr`: blocks after a gap, and tracks with no block on the first row, are rendered. Goldens unchanged (ELECTRO has neither).
- **Each track where it was (lead):** `makeSeq` takes `o.at` / `o.atRef` (the song's positions, `tp.pos`); a track that was on another song row than the reference starts on its own row, `d` lines in (or `-d` lines later), `d` being its step minus the reference's. Passed by RESUME (`tp.paused.pos`), `seqHandover` (`rz.pos`) and `coreFail`. Exact when the tracks share lines per beat, approximate otherwise; a track that was waiting (no position) starts as before. Not run on a device.
- **SHIFT+▼ on desktop:** the `isMobile()` gate is gone (PAD mode); KEYS mode's Alt+▼ from Sect opens the phrase too.
- The old-commit comparison tests (`eng-delivery`, `mod-run`) no longer compare whole-song plays (different by design since 570 / 571).
- **Still open:** the clock wobble at the first wrap is not measured (lead: first PLAY stalls the page 1.3 to 2.4 s in emulation; measure the wrap itself, Arrange's page turn and sound preparation).

## 12 Nov (evening): the owner's flight list: loop play, ONE SONG WRAP, effect state persists, master block mute, INS (build 570)

Owner tested build 568 on a flight and sent twelve items; build 569 (PR #99) merged meanwhile (CodeRabbit found two real bugs in it, fixed: a MOVE with a block the bus refuses now changes nothing; mix-paste on a bus redraws). Two Sonnet agents in worktrees (`fix/phone-list-ui`, `fix/phone-list-seq`), merged by the lead on `fix/phone-list-2`. **The sequencer changed (lead-reviewed, lead wrote the wrap):** `core/seq-core.js` regenerated; the four sequencer goldens are unchanged. Nothing verified on a phone.
- **ONE WRAP FOR THE WHOLE SONG (lead; owner: "Ok to make the track loop around change").** Cause of "looping around jumps to a random section" (both sequencers, the audio not the display): `settle()` sent a track that hit an empty cell back to the top of ITS OWN run of blocks, so tracks whose blocks start on different rows drifted apart from the second pass, and a track with a gap looped its first blocks for ever. Now it waits (`T.wr` = the empty row + 1); `wake()` brings it back at a later row where it has a block (not on a row it already played ahead of the lead); when every track waits, all restart at the last one's end time from the top of the run of song rows that hold a block on any track (an entirely empty row still divides sections). Renders (`once`), LOOP, JMP unchanged; END clears `wr`. **Polymetric songs change:** a track with shorter patterns now waits for the longest instead of phasing. `tests/song-wrap.test.js`. **Not changed, still open:** hand-over (`seqHandover`), `coreFail` and RESUME restart every track at the reference track's row, so tracks that had run ahead move; a render still stops a track at its first gap (`once`, no `wake`): later blocks of a track with a gap are missing from renders: **for audio review**.
- **Effect state persists (owner: "should persist as off"; "Changes to old songs are ok"):** the per-track `fxreset` at a wrap (31 Oct) is gone; only STOP resets. A TPO also survives a wrap (it always did).
- **Muted blocks:** a muted TRACK block is as before (notes and effect commands dropped; TPO / TIC / GRV / PDL / JMP / END / HOP still run, so a mute never moves the song; the agent's first version dropped those too and the lead put them back). **Master column:** `S.smuteM` (sorted song rows, absent when empty; older builds ignore it), `masterBlockMuted(S, y)`, `bmu(y, t)`, `setMasterBlockMute`; the sequencer's `r0` reads no row for a muted master block (`mutedAt`); make-core copies the helper; `coreDiff` sends and deletes the key. This is the likely answer to "I could still hear fx from a muted pattern" (the master had no mutes).
- **Save before PLAY (guard, not a proven fix):** the owner's lost notes (new song, notes into a new pattern, PLAY hung, notes gone) were NOT reproduced in 12 emulated tries. The autosave runs 600 ms after a change (`sPend`) and the first PLAY stalls the page 1.3 to 2.4 s in emulation; `tStart` now writes a pending save first. Other suspect: `placePending` silently drops a pending phrase when the cursor phrase changed or the cell got filled. Asked the owner whether the app reloaded.
- **Loop play (owner):** the control bar's `data-c=song` calls `playPatLoop()`: LOOP on (lit), plays from the cursor row, LOOP restored on any stop (`lpW`, `lpRestore`; supersedes the 30 Oct rule that a stop turned LOOP off). LOOP pressed while it plays → `setLoop(null)`, the song carries on. Pad PLAY / Space plain. The bar's play is hidden on a folded phone in PAD mode (9 Nov rule): owner's call whether to show it.
- **INS** (`insRowBelow`, Ctrl/⌘+Alt+Enter, Edit and right-click menus; hidden on the folded Song screen, where the bar is full). **SHIFT+▼** (`navDownToPhrase`, Mobile only; desktop still goes to Mixer: owner's call). **AUTO MASTER** (the switch's strings only; the window is still MASTERING). **Clone down:** the empty row came from build 569's bus refusal after `songRowsInsert`; refused cells are dropped first and inserted rows removed when nothing was cloned; not reproduced without a bus.
- **Not built:** HOP playhead in Pattern view (cause: `prLit` lights only tracks whose playing phrase is the open one; recommended: a per-track playhead from `tp.pos[t].step`, its own small build). **Not measured:** the clock wobble at the first wrap (the first PLAY stall is the known part; the Arrange page turn and sound preparation at a wrap still to measure: lead).
- `tests/eng-delivery.test.js` / `mod-run.test.js` compare with old commits' cores (only when history is present): their live case is now one pass, since a looping play differs by design.

## 12 Nov: the owner's phone list: Arrange arrows, STOP cursor, GEN, transpose, clear solo / mute, bus notes, Low power selection (build 569)

Owner (from the phone, before a flight; "scrolling" = moving the cursor with the pad's arrows). A Sonnet agent built all eight on `fix/arrange-scroll-play`, one commit each; the lead read the whole `index.html` diff, ran `npm test` (572 pass, 0 fail) and looked at the Low power screenshots. **No sequencer / audio-thread code changed.** Nothing verified on a real phone.
- **1 Arrange lagged a playing song while ▲ / ▼ was held (PARTLY):** every cursor move rebuilt and compared all of Arrange's HTML (about 100 KB) in `drawArr0`; ▲ at row 0 redrew with nothing moved; 1,500 `.tagrid b::before` hit areas (inert: clipped by the block's `overflow:hidden`, checked with `elementFromPoint`) doubled compositing. Now: `move()` draws nothing when nothing moved, one redraw a frame while playing (`arrSoon`), `drawArr0` keeps the built blocks on a cursor move (`arrFast` / `arrFastKey`), the `::before` rule removed. 4× CPU throttle, core sequencer, 4 s hold: long tasks ▲ 15–33 → 1–3, ▼ 32–41 → 5–11. **Left:** ▼ still does one full rebuild when the row count grows past the song's end + 32 (about 200 ms throttled). The owner's "▲ worse than ▼" was not reproduced (▼ was worse here). The core's own counters never moved in emulation (late 0, dropped 0), so what he heard as lag on the phone is not explained by them: **if the phone still lags, it is for audio review (lead)** with a playback report.
- **2 Cursor stuck at the bottom:** the pad's auto-repeat did not refresh `workT` / `arrActT`, so 4 s into a held ▼ the playhead paged the view back (FOLLOW ON) or FOLLOW PATTERN pulled the cursor to the playhead. Every repeat in `press()` now counts as hands-on.
- **3 STOP jumped the cursor to the top:** `tStop` restored `fpHome` (the cursor before FOLLOW moved it) even when the owner had moved since; it is now used only if at least as new as `workT` and `arrHandT`, and `followPlay` notes it again after an Arrange scroll. The 9 Nov STAYS behaviour for an untouched cursor is kept.
- **4 GEN on the Song / Arrange** (`genToPhrase`, also Alt+B): opens the block's phrase, then the generator menu two frames later (an Arrange scroll closes menus); an empty cell gets a new pattern first (`arrNewAt`; owner: "Make a new pattern"); a toast only on the master column, a bus, or an empty cell with sections on.
- **5 Low power skins:** in Arrange, selected blocks had no look at all (the skins' `!important` rules beat `.sl`): now a dark tint, dashed outline and white text; the cursor a fill with a black and a white ring. Per-skin variables `--lpsl`, `--lpslbg`, `--lpcur`, `--lppl`, `--lppbg`. Noted, not changed: the pink skin still shows blue bus frame lines in Arrange (`--abc`).
- **6 Transpose on the Song / Arrange** (feature, the owner's exception to the freeze): the existing Song transpose now takes the selection plus picked blocks (`sxPicks`), else the cursor block; each pattern once; the pattern itself is edited (lead's decision) and the toast says how many are also used elsewhere; the pad's SEMI keys work with no selection on the Song. One undo step.
- **7 Clear all solo and mute:** the pad's SOLO or MUTE tapped twice within 350 ms (`msTap`, `clearSoloMute`; lead's decision: either button clears both); menu entry "Clear all solo and mute (every track)". `S.smute` untouched. Two undo steps (the first tap, then the clear). Marks: solid block for a soloed track number on the Song header and Mixer, `· SOLO` / `· MUTED` after the Phrase title, a ring on the pad keys while any track is set. **Not done:** no solo mark on the Pattern view band; the pad's MUTE / SOLO keys are hidden under 600 px, so a folded phone has only the menu entry (owner's choice to make); Ctrl/⌘+Shift+/ still clears solos only.
- **8 No notes onto a bus:** `busNo` / `busNoteNo` in `setCell`, `newBlock`, the drags, paste and mix-paste, Duplicate; toasts batched by `busRefFlush`. Commands still paste; a refusal writes nothing. Old songs are not stripped. **Open question:** a code comment says notes already on a bus keep playing until PLAY is pressed again, the Phrase hint says they are silent; not checked which is true.
- Tests: `arrange-move`, `arrange-hold`, `gen-from-arrange`, `lowpower-arrange`, `transpose-song`, `solo-mute-clear`, `bus-no-notes` (`usability.test.js` regex updated). Screenshots were in `/tmp` (not kept).

## 11 Nov: Low power hot pink and mustard skins (build 568)

Owner: two colour variants of Low power (black) for OLED battery saving, with no plum / purple / violet / blue / cyan anywhere (a Sonnet agent, branch `feat/lowpower-pink`).
- `lowpowerpink` ("Low power (hot pink)") and `lowpowermustard` ("Low power (mustard)"): the whole LOW POWER (black) block copied per skin with its colours mapped (Low power's own rules untouched), `[data-skin]` of both added after `lowpower` in every dark-skin list (10 list lines each; `--mxbus` cyan in the matrix list is overridden by the new blocks, which come later), two SKINS entries. Pink leads in the first (text, cursor mustard), mustard leads in the second (cursor and solo / inst pink). Poly orange `#FF8C1A` (the spec's `#FF8C42` was too close to coral), master coral `#FF6A55`, bus / `.v` / selection rose `#FF6FA5`, red `#FF3B30`.
- Found and fixed: bus colours (`BUS_COLORS` blue, green, violet, set inline as `--bus1..8` and `--bgc`) leaked blue into the Song group headers and Phrase bus bands; both new skins override them (`!important`) with warm colours. Low power (black) still shows the bus colours, as before.
- Contrast on #000 (all text 4.5:1 or better): pink #FF2D78 5.90, mustard #D4A017 8.84, dim #917323 4.68 (the spec's #8A6D1F was 4.29, lightened), #A8862A 6.11, #C9A33A 8.78, orange #FF8C1A 9.02, coral #FF6A55 7.44, rose #FF6FA5 8.07, red #FF3B30 5.92, cursor #E0B020 10.40 (black text on it), white on playhead #4a3a08 11.05 / #4a0d22 15.31. Dark outline #3d2f0c is 1.61 (decoration, like the old #3a3a3a).
- `lpSkin()`: `LP_SKINS = ['lowpower','lowpowerpink','lowpowermustard']`; Low power display ON while on any of them keeps it, OFF restores the earlier skin when on any of them. Build 568 (`BUILD`, `sw.js` cache, `CHANGES`).

## 11 Nov: the core's per-block garbage (build 567); the wide node measured and LEFT (owner's decision)

Owner: "Can we fix the two leftover issues?" (the profile's fixes 2 and 3).
- **Fix 2, done (a Sonnet agent, lead read the diff; `core/core-processor.js`, regenerated):** `Auto.prune` folds in place (reuses a consumed `{k,t,v}` event for the head and a consumed `k:2` for the target, shifts the tail; the FLX CHECK 2 ramp case kept; consumed events are held by their own list only); `Auto.cancel` truncates in place; new `Auto.fill(C, t0, dt, N, i0)` does a block of `at()` in one call (a call per sample boxed the time and value as heap numbers) for the beat clock, `mixFill` and `__tcOv`'s per-sample path; `process1` makes `rows` only when a step is due; `stBal` read by index, not destructured. Offline, 8,000 blocks of the captured song: scavenges 143 → 73, sampled heap 140 → 70 MB (`prune` 34 → 0.65 MB), `process()` about 60 → 48 µs a block. Proof: lockstep old vs new (134 million output samples, 404 messages, 21,948 hook blocks: identical; `trig` and the `started` message's `ms` differ between two copies of the old core, so excluded); `tests/core-garbage.test.js` (6,000 random timelines against the build 566 `prune` / `cancel` kept verbatim; `fill` against `at` by `Object.is`; no `slice` / `concat` / `filter` / `map` / `splice` in 600 steady blocks). Left: `tcChunks` (lives in `index.html`, copied into four worklets, only runs for blocks over 128 frames); heap numbers from the voices' slow-path `at()`; per-step allocations in `api.step` / `engEmit`.
- **Watchdog false alarm fixed (review of PR #97, CodeRabbit; older than this build; phones only, where the core is watched):** "deaf" fired when 3 s of audio passed since the last acknowledgement, so a song whose rows are more than 3 s apart (very slow tempo, a long PDL) was dropped by the core though the page was fine. Now counted from the oldest batch of rows still waiting for its acknowledgement (`ackW`; −1 when none). `tests/core-watchdog.test.js`. Not tested on a phone.
- **Fix 3, NOT done, by decision.** The lead measured a do-nothing node in Chrome (traced; µs of worklet CPU per block): plain stereo 7; the core's layout (37 outputs, 131 channels) 56; without the two NT-channel mix outputs 36; 16 stereo + 3 mono 22; 12 stereo + 3 mono 18. Cost follows the channel count (about 0.35 µs a channel). The most to win is about 40 µs a block, 1.5 % of the budget, and outputs cannot change on a live node: it would mean rebuilding the core at PLAY when a song needs more (re-sending song, kit and every pad buffer: PLAY latency) and re-indexing every output write in the core. Lead: not worth the risk untested on devices. **Owner: "Ok, no need to make things more risky."** Revisit only if a real device still struggles on the Audio thread sequencer after builds 566–567, and then behind a switch.

## 11 Nov: the core sequencer profiled, and its fast paths made to work (build 566)

Owner: "Do I do the profiling or you?" → the lead; "Yes please". A Sonnet agent measured (no app changes): `docs/audits/2026-11-11-core-profile.md`, tools `tests/sweep/core-profile.mjs` (worklet CPU profile through a raw CDP client, `cdp-lib.mjs`), `cp-analyse.mjs`, `cp-trace-analyse.mjs`, `core-offline-bench.mjs`, `wide-outputs.*`.
- **Finding:** the core's extra audio-thread time (about 0.38 ms a block here; 68–73 % busy against 55–59 %) is NOT sequencer stepping (0.2 %). It is per-sample automation: `__tcPost` (cos / sin per sample at a constant pan), `__tcOv` (a per-sample array for every overridden parameter) and the sample voices' slow path. Root: `Auto.constFrom` says "constant" only when the timeline's LAST event is over, and the sequencer books events 0.25 s ahead, so the constant shortcuts almost never ran while a song played. Main-thread work is the same in both modes. No memory leak found (flat RSS and heaps over 7–10 minutes; the A/B's 3.24 MB/min is a straight-line fit over a sawtooth from the waves / chains frames at song reload). The "late / dropped notes" counters only exist in core mode, so page mode's zeros mean nothing. The 10–30x dropout ratio is this starved VM amplifying a real but modest cost.
- **Fix (lead, `core/core-processor.js`, regenerated):** `Auto.constBlk(t, t1)`: the value when nothing moves the timeline inside the block (only set / ramp-end points before t; a target or curve before t, an event inside the block, or a ramp running to a later point give null). Used by `__tcOv` (with `Math.fround` where the old test took the Float32 path, so the number is the same), `__tcPost` (constant gain and pan, stereo: cos / sin once), `mixFill`, the voices' fast path. `constFrom` stays for the "all over" decisions. A still parameter now reaches a processor as ONE number where it was 128 equal ones.
- **Proof:** `tests/core-constblk.test.js` (64,000 random blocks over sets, ramps, targets, curves, cancels, pruned as the core does: whenever `constBlk` answers, every sample's `at()` is that value by `Object.is`; never less than `constFrom`); the lockstep replay of a captured song, old core against new, 8,000 blocks: no parameter differs except `trig`, which differs identically between two copies of the OLD core; `tests/worklet-memory.test.js` `__tcOv` compares as 128 values. Agent's numbers with the same change as a scratch copy: core cost per block 180 → 83 µs offline, audio-thread CPU −4.5 points, dropouts about 5x fewer in Chrome. Lead's 2-minute core soak at build 566: 45 dropouts a minute (179 in the A/B), no page errors.
- Review (PR #96, CodeRabbit): nothing on the core change. On the measuring tools: the local file server now listens on 127.0.0.1 only and checks the folder strictly (fixed); left as they are: `cdp-lib.mjs` needs Node 22's built-in WebSocket (Node 20 needs `--experimental-websocket`), `core-profile.mjs` capture mode does not force the core sequencer, keeps its heap-snapshot listeners, and does not close the browser when a run throws.
- **Not done (ranked in the report):** per-block garbage in `Auto.prune` and the `rows` objects (tail latency); the 37-output node (about 50 µs a block before any work: medium to high risk). Real-device behaviour still unknown.

## 11 Nov (morning): Song grid centred with the SECT lane (build 565)

Owner (screenshot, Fold unfolded; build 564's title bar confirmed in it: name gone, Tracker at the edge): "with the sect added in song mode, the song layout looks a little skewed. Can we add some padding on the right side". `body.asong.sxsecs .tarr{padding-right:min(calc(var(--sxw) + 4px), 48px)}` beside the lane's own left padding: the lane's width again on the right, capped at 48 px so a long section name does not eat the grid (lead's choice). Song screen only, not Arrange beside Phrase. At 884 wide: 54 px either side of the cells (was 54 / 18); each cell about 2 px narrower.

## 11 Nov (morning): title bar touch-ups (build 564)

Owner (two screenshots, Fold folded and unfolded): "make the tracker button align with the song button on the left side in folded mode" and "make the word thunder disappear on screens that aren't big enough to support it, like my phone unfolded" (it showed "Thunde").
- `brandFit()` (after `alignTabs`): on the Mobile layout the name gets `.bhide` when `scrollWidth > clientWidth + 1`, measured with it showing; a ResizeObserver on `.top` and its children, `resize` and fonts-ready call it through `brandFitDue()`. Measured, not a width rule, because skins letter-space the name differently and the level meter / ? come and go.
- CSS (by `body.mob .top .brand`): `.top` has no left padding at ≤ 480 px on Mobile, and whenever the name is hidden by `brandFit` (lead's choice: the same alignment unfolded once the name is gone); the kit name keeps an 8 px margin at ≤ 480 px.
- The name is the edit on / off control in the tracker (`edoff`); it was already hidden folded, so nothing new is lost.
- Headless at 884 and 1020 wide the name fits (78 px), so the hiding was checked by squeezing the bar with a 260 px spacer: hidden, then back when released. The owner's phone has the meter and ? in the bar, which headless did not show.

## 11 Nov (night): the queue, build 563; desktop v1.3.4

Four Sonnet agents in separate worktrees (`fix/redraw-churn`, `fix/usability-leftovers`, `fix/audio-garbage-idle`, `fix/reset-rebuild`), merged by the lead on `lead/build-563`; the lead read the audio diffs line by line and made the Vox fix.
- **Redraw churn:** `setText(e, s)` (after `el()`) writes only when the node does not already hold exactly that text (compares the node, no cache). Used in `draw()`, `drawPat`, the deck, `drawArr`, `makeRow`'s `<output>` readouts. `tests/sweep/redraw-churn.mjs`: same-value text writes per 5 s of play went from 1,100–2,100 to 0 on six screens. `tests/redraw-churn.test.js`, `tests/core-outputs.test.js` (NT + 5 guard).
- **Audio-thread garbage:** chains and waves hosts and modules (views kept, `cpTo` loops, module-level helpers), `rvMix` scratch, `FlexCore` / `StepGateCore` / `PitchFxCore` (source in `index.html`, regenerated), `__tcOv` keeps its result object (drops keys the node no longer has), pump synth `psAt`. Bit-identical to build 562 (`tests/worklet-memory.test.js` second pass); no golden regenerated for it.
- **Silence shortcuts** (`tests/idle-shortcut.test.js`): master worklet skips its loops when the input has been exactly zero for the limiter lookahead + 8 samples, the suppressor is off, limiter gain > 1 − 1e-9, compressor `quiet()`, meter filters `quiet()`; wakes in the same block; states within 1e-9 of rest, not bit-for-bit. ThunderFx: a node clocked only by Flex / Step gate sleeps after 14 s of silence with the clock stopped (`chard` nodes: Retrigger, Grains, Scramble, Glitch, clocked modules never). Known edge (older than this): a limiter switched on during silence keeps whatever was in its delay line.
- **Vox REL:** `VoxCore.enter()` zeroed `t.av` for the `'end'` segment, so the release faded a silent source; now `t.av = this.k.av`. Vox goldens regenerated (33 vox presets + `electro|20|Synth 21`; at default REL the rms moves in the 4th digit). `tests/eng-targets.test.js` and `tests/worklet-memory.test.js` give the old build the same one line before comparing.
- **Reload offer:** `audStuckCheck()` after Reset audio engine; `audStuckVerdict` (not running; or reported ≥ 0.3 s on a non-`playback` hint; or measured ≥ 0.3 s and ≥ 0.2 s past reported); `audReloadSafe()` refuses in a recording / render, stores samples, writes kit and song and CHECKS the writes, then `applyUpdate()`. Kill switch `AUD_RELOAD`. `tests/audio-reload.test.js`.
- **Usability:** `touchUi()`, `MODK`, `engShowEditor()`, the 40 px sheet buttons, contrast block before the second `<style>`.
- Worklet URLs bumped: chains 47, waves 37, pump synth 12, master 8.
- **Lead's decisions for the owner:** "full desktop app" = a regular release v1.3.4 from `main` (installed copies update to it); Vox fix shipped although it changes how long-REL Vox notes end; the Bluetooth false positive on the reload offer accepted ("Not now"); a true AudioContext rebuild not attempted.

## 10 Nov (evening): chains / waves take Flex, Step gate, Retrigger from index.html (build 562)

Owner (15:40): "Unify, it's ok if the chains sound different … and waves too." Sonnet agent, lead checked. `tools/make-fx-cores.mjs` cuts `clkSpb`, `FlexCore`, `StepGateCore`, `RetrigCore` out of `index.html` into a BEGIN/END GENERATED region of `chains/chains-worklet.js` and `waves/waves-worklet.js` (`--check`; `tests/fx-cores.test.js`). **After changing one of those in index.html, run it** (like `make-core.mjs`). The old copies were an older 17-pattern Flex: the worklets now carry the full 256-pattern core, but the modules still reach patterns 0–16 only (exposing more = a module change). Step gate: the module sends `|| 16` so Pattern CV 0 still means the custom steps (the core reads 0 as "gate open"). Lead ran patterns 0–16 of both through the generated code: finite, each acts. Also in 562: `draw()` assigns `#tTitle` / `#tInfo` / `#tState` only when changed (soak finding 1).

## 10 Nov (late afternoon): soak test (test only; no leak found)

Sonnet agent. `node tests/sweep/soak.mjs --minutes=10 --seq=page|core [--phone]` (see `tests/sweep/README.md`); the write-up is `docs/audits/2026-11-10-soak.md`. Three runs (page 10 min, core 10 min, page with 4x CPU throttle 8 min): **0 FAIL**; heap after GC grew 0.03–0.07 MB / min; DOM, listeners, timers, voices, audio nodes and the app's queues all return to level. Shared machine, so timings are trends only. **Findings to act on (queued in "Where things stand"):** (1) `draw()` assigns `#tTitle` / `#tInfo` (and the `<output>` readouts, `#tState`, Mixer spans) every redraw even when unchanged: about 500 text nodes a second of GC pressure while playing, worse on the phone; fix = assign only when the string changes. (2) The core sequencer showed about 10x the page sequencer's dropouts (24.6 vs 2.4 a minute; one note 924 ms late; 4 notes dropped at a song reload): UNPROVEN (machine load); needs a 20-minute A/B on a quiet machine. (3) On a throttled CPU the page sequencer "moved on" 19 times a minute: its margin on a slow phone is thin. (4) A song reload costs 3–14 s of main-thread long tasks. Not measured: a phone's audio stack, the hidden page, real input, MIDI, Link, anything slower than 1 MB / min over two hours.

## 10 Nov (late afternoon): worklets take any block length; pump synth guard (build 561)

Sonnet agent, lead reviewed the diff. **Why:** Chrome is adding a render size hint, so a block need not be 128 frames; `ThunderFx` threw at 256, the engines dropped or overran frames, the core's SoundFont scratch gave NaN. **What:** `tcChunks` (same text in `index.html`'s `TFX_SRC`, `pump-synth-worklet.js`, `chains-worklet.js`, `waves-worklet.js`) runs a block over 128 in chunks of at most 128 on views; 128 or less takes the old path (bit-identical, tested); over 4096 → silence + `{failed:'block size N'}`. Core scratch / override buffers follow the block length; the master meter counts frames. Known limit: chunks after the first see the block's `currentTime` (per-block timing slightly coarser on long blocks). **Pump synth:** a throw in `process()` → silent, marked failed, one toast; a non-finite sample (found by the existing output loop) → block zeroed, engine freed and reopened, `{nan}` logged, the 8th reset gives up. Tests: `tests/block-size.test.js`, `tests/psyn-guard.test.js`. **Not done, owner's choice:** chains / waves keep their own Flex / Step gate / Retrig copies (hand-maintained, no tool): Flex SCRATCH depth 0.125 vs the track effect's 0.105, and Step gate at Pattern CV 0 = custom steps vs gate open; all else is bit-identical.

## 10 Nov (afternoon): Background play guard (build 560); the port is parked

Owner (15:01): "I am down to continue working towards audio stability, especially if we can lower the priority of a port." **The native port is parked** (N0 not started; the plan stays on file) until the long screen-off run with Background play on says whether the phone needs it. Build 560 (Sonnet agent, diff read by the lead): `setBgOn(on, byHand)`; on Android, hidden while playing with it off logs `hidden while playing with Background play OFF…` each time and, once a page load, a `toastAct` on return offers "Turn on" (`bgGuardHide` / `bgGuardShow`); switching it off by hand shows a "for testing only" toast; the Settings title and the PROJECT row hint state the consequence. Nothing in the code turns it off by itself: the only `setBgOn(false)` callers are the Settings checkbox and **the tracker PROJECT screen's BACKGROUND PLAY row** (reachable with the pad, so it can be flipped by a stray EDIT: a likely way it went off). `tests/bgplay-guard.test.js`. Running: worklet robustness (block sizes, pump-synth guard, chains' Flex copies) and a soak test (`tests/sweep/soak.mjs`).

## 10 Nov (afternoon): with Background play ON the phone held its clock with effects loaded (build 559 report). THE PORT'S PREMISE NEEDS RE-CHECKING

After Reset audio settings (report header: `background play on · latency balanced · runaway kick on`; worklets `chains, cpu, master, tfx` loaded; the phone plays on the Page sequencer): PLAY, 20 s with the screen off: `clock +0.99 s in 1.00 s` throughout, 1 underrun, `held 0×, kicked 0×`, the background element playing (`media session: element playing`, `bg on`). Owner: "Things were really stable". **This contradicts the conclusion of 10 Nov night ("with any worklet module loaded, Chrome on this phone renders more than the device takes") as a general statement: that Worklets test, and every screen-off report of 10 Nov, may have been made with Background play off** (it was off until this reset; when it went off is not known: the 8 Nov toggle test asked for it off). What is established: Background play off + worklets + hidden → underruns from the first second and a runaway within 15–30 s; Background play on + worklets + hidden → healthy for 20 s (one run). **Not yet known:** minutes-long hidden playback with Background play on, the locked-screen volume key with it on, the owner's bigger songs, and whether the 9–10 Nov on-screen burst ("it began in the first second on screen before anything played") also depended on it.
- **Consequence for `docs/NATIVE-PORT-STRATEGY.md` section 1 ("the web path cannot cure Android"): unproven until the owner repeats a long screen-off run with Background play on.** If that holds, the native port is no longer the only cure for the phone; its other benefits (latency, drivers, plugins, battery, one engine for the plugin) stand. The owner (10 Nov, 13:52, before this report): stop hacking at Chrome audio, finish MOD and stability, then put the energy into the port. Tell him this finding before N0 starts.
- The build 558 media-session lines work: `session set: playing`, `element play() ok`. The 40 Hz clip and the kick stay as they are (kill switches `MS_ACTIVE`, `RUNKICK`) until the long run says whether they are needed.

## 10 Nov (afternoon): the phone had Background play OFF all along today (build 559)

The owner's build 558 report had no `media session:` line at all. In `bgPlay`, `if (on && !bgOn) on = false` comes first, and with `on` false and no handlers yet nothing is logged, so the only way to get no line at PLAY is `bgOn` false: **Background play is switched off on the Fold** (most likely left off after the 8 Nov "phone clock toggle test", which asked for it off). So Android has had no media player to see: the page is an ordinary hidden tab (throttled, underruns climbing from the first second), and volume keys move the ringer. **Every screen-off measurement of 10 Nov (the runaway about 15–30 s after hiding, the kick, the volume-key dropout) was made in that state** and needs repeating with Background play on before any conclusion about them stands; the Worklets test of 9–10 Nov may have been too (not known). In that same report the build 557 fix held: `moved on 0×, held 167× (14.82 s), kicked 1×`, no leap at the suspend this time (34.09 vs 33.01), `kick result` +1.93 s in 2.00 s; a second runaway began 3 s later and `RK_GAP` (8 s) kept a second kick from firing before the owner stopped.
- Build 559: the report header gains a `settings:` line (Background play, Keep screen on, Low power, Latency, Runaway kick) with a note when Background play is off on Android. Lesson for sessions: a test that asks the owner to switch something off must ask for it back on, and reports must show the settings.

## 10 Nov (afternoon): media session experiment for the locked-screen volume keys (build 558)

Owner: "Look into it" (a volume key pressed with the screen locked moved the ringer and set off the dropout). A Sonnet agent read the code and the public documentation. **Code:** the Media Session was already complete (`MediaMetadata` with the song name, `playbackState` playing / paused, play / pause / stop handlers); the background element (`bgEl`, Android only) loops an 8 s generated WAV whose samples alternated 0 and 1 (about −96 dBFS); nothing routes Web Audio through an element. **Platform (sources old or silent on the key points):** Chrome shows a media notification only for media of 5 s or more and "no notification for audio from the Web Audio API unless it is played back via an audio element" (developer.chrome.com, 2015; web.dev 2024 repeats the 5 s rule); nothing found on whether near-silent media holds audio focus, nor any bug linking volume keys / ringer changes to AudioWorklet clock trouble. **So the likeliest gap:** Chrome or Android treats the −96 dBFS decoy as silence, and the page is then not a media player. **Built (`MS_ACTIVE`, `MS_LEVEL = 0.001`):** the clip is a 40 Hz sine at about −60 dBFS (whole cycles, loops cleanly); `media session: …` lines in the playback report for the session state and the element's playing / pause / ended / volumechange / error and `play()` ok / refused. `tests/media-session.test.js`. **Unverified on a device.** **Not built, the likely real answer if this fails:** pipe the master through a `MediaStreamAudioDestinationNode` into an `<audio>` element (`srcObject`) and mute the direct output, as a switchable experiment (`MS_PIPE`): the element then IS the audible media; risks: added latency, the context clock following that sink, glitches in both paths.

## 10 Nov (afternoon): the kick works; the silence after it was the lead's own bug (build 557)

Two reports from the owner's Fold (builds 555 and 556, the simple song with a delay, Page sequencer, screen off). **The kick breaks the runaway:** the clock reached ×4.5 about 14–17 s after hiding, the kick fired (`pausing the engine 150 ms`), the engine was running again 0.4 s later and `kick result` read +1.93 and +1.96 s in 2.00 s; one more one-second burst (+9.5 s) about 6 s later cleared without a kick. Without the kick (build 550) the runaway lasted about 14 s (167 s of surplus clock).
- **But the song then went fully silent until STOP** (owner: "audio dropped, phone vibrated … then no sound. Fully silent. It comes back after a stop and start"). Cause, from the reports' page lines (`moved on 1× (22.74 s), held 105× (37.26 s)`; `moved on 1× (38.01 s), held 89× (49.58 s)`): the clock leaps 22–38 s at the instant `ctx.suspend()` lands; while the kick was in flight `rsTick` returned early (the lead's `rkBusy` correction of build 551, which keeps the learned band), so the moved-on rule in `pumpRun` moved the song on by the leap, and at the resume `rsTick`, band intact, held it by the same leap again: the next row lay 22–38 s ahead. **Fix:** `pumpRun` returns right after `rkTick()` while `rkBusy()`: nothing is booked and the moved-on rule does not run during the pause; `rsTick` takes the leap once, with `rsHold`'s full bookkeeping, when the engine runs again. `tests/runkick.test.js` gains a leap-at-suspend case.
- **The owner found a trigger:** pressing a volume key with the screen locked sets the dropout off (reproduced by him); the phone vibrated, so the key moved the RINGER, not media volume: Android did not treat the page as playing media at that moment. Not investigated: whether a Media Session `playbackState = 'playing'` / metadata makes the keys act on media volume, and whether the vibration itself (the haptics path) disturbs the output stream.
- **After a runaway the output delay stays high:** the report header read `latency 21 + 389 ms` (and 391 the first time) against 43 before; the per-line `lat` values are cached at 0.043. Pads will feel late until a reload. Not fixed (a context rebuild needs a reload today).
- Idea not built: the kick could fire sooner (it waits three 0.5 s windows above ×3).

## 10 Nov (afternoon): MOD: pump bands, pump windows, master out, render parity (build 556)

- **Per-band pump targets.** The lead's format decision: a banded row's id carries the band as an optional fifth part, `fx:t:s:p:b` (0-based; four-part ids unchanged). Bands are fixed in number (EQ 24, BAND / WIDE / HEAT 4; none can be added or removed), stored as `f.pump['b3_freq']` (1-based in the symbol). Band rows carry `q.lfo` and `q.pb` (each band's parameter index); `tgtOk` / `tgtParse` (`b`) / `tgtFx` / `tgtList` (only bands in use: `tgtBandOn`) / `tgtLabel` / `tgtOwn` / `tgtEvs` (`b` on the event) / `fxpTo`; `pumpPv` overrides keyed by the band's symbol; the core's `fxp` branch takes `ev.b` (`coreTfx` sends `pb`). Add LFO… on a band row targets the selected band (`tgtRowId`).
- **Add LFO… in the pump windows:** one shared right-click menu in `pump/ui/kit.js` (`?v=10`), `PumpFX.lfoId` / `PumpFX.addLfo` in index.html; closes the window after adding; no `~` mark in the windows (would need every control's draw code).
- **Master out target `vol:MT`:** the OUT fader (`S.mout`, gain node `rt.mo`, after the limiter; `S.master` is the drive and is not touched). New `mout` event (the existing `mvol` moves the drive): page `setTargetAtTime(curve(v), t, 0.004)`; core `mixEv` / `mixOut` on a new output NT + 4 plugged into `rt.mo.gain` (`coreLoad` makes NT + 5 outputs; `tools/flex-check.mjs` and `tools/sampler-check.mjs` still build NT + 4: guarded, not run). `resetSeq` restores `curve(S.mout)`.
- **Render parity:** measured in headless Chrome (16 bars, four effect LFOs): 2 points a step 3.6 s / 511 suspends, 4 points 3.9 s / 1023 (+8 %), so renders now use 4 like live for every target kind (`LFO_PTS_LIVE`, `LFO_PTS_RENDER_FX`, `LFO_PTS_RENDER_PARAM`, `lfoPts`). A render with 1 tick a step gives 1 point (was 2).
- Two Sonnet agents; the lead merged the two (three conflicts in `tgtLabel` / `tgtList` / `CORE_REV`, core regenerated). **MOD still open:** smoothing (the owner: steps are fine except pitch and sometimes filters; he will listen: see the punch list; the lead's plan if needed: the effect host ramps a modulated value toward its next point per block, scoped to pitch / cutoff-type parameters).

## 10 Nov (afternoon): commands and MOD on the live engines (build 555)

The lead's contract (`docs/mod/ENGINE-TARGETS-DESIGN.md`), three Sonnet agents (engines JS; pump synths; delivery), every diff read by the lead, then a fourth agent's end-to-end sweep in headless Chrome (`tests/sweep/eng-targets.mjs`, not in `npm test`).

- **Engines** (`BASS_PROC`, `VOX_PROC`, `XPROC` for acid / mach; `pump/synth/pump-synth-worklet.js` v=9): twelve k-rate AudioParams (`cCut cRes cDec cAtk cRel` default −1; `oCut oRes oEnv oAtk oDec oSus oRel` default 0), read once a quantum. JS engines: `engOn(P)` swaps the effective values into `this.c.p` for the quantum and `engOff()` restores them in a `finally` (one reused Float64Array). Pump: effective values written into the staged array over `values()` of `this.p`, committed only when changed (`TMAP` by wasm type: mono, poly, orbit, gloss, wob filter + envelope; coil dec / rel; tract and freeze ADSR; kits and fold unmapped; commit about 0.5 µs). All defaults → today's code path: engine goldens in full and old-vs-new renders bit-identical.
- **Delivery:** `makeSeq` emits `{type:'eng', track, t, c:{…}, o:{…}}` (`engEmit`, from `lfoEvs`) for any track with a command in force (`T.cut`, `T.res` once `T.rsg`, `T.dec`, `T.cAtk`, `T.cRel`), an LFO or a scene value on the seven targets; LFOs 4 points a step live, 2 in renders; `o` = value − centre. The sequencer cannot know liveness, so the player drops events for tracks without live nodes. `PF.pno` unchanged (sampled pads identical: buffers equal to `beta`). Page: `engSt` / `engPut` / `engTo` / `engReset`; core: `engEv` / `engPut`, per-track node list `engL`, cleared in `stopAll`. `eng` is in `CHASE_T` (RESUME / a later start replays the state). `liveCut` is skipped for kinds in `ENG_CUT` whose node has `cCut`. Targets `env|atk|sus|rel:t` (`tgtOwn` 0), listed when `tgtLive(t)`. `CORE_REV` regenerated at merge.
- **End-to-end (headless):** offline: CUT 30 lowers a held bass note's high band 24×, CUT FF a bar later restores it; a cut LFO swings it 2.7× within the note; ATK / REL / DEC / res / env / sus all act; a command renders bit-identically to setting the pad's own knob; kit unchanged; acid, mach, pump mono / poly / tract pass. Live page sequencer: params follow, defaults after STOP. Live core: cut LFO swings the pre-master mix 146×, sus 288×; nothing leaks after STOP. **A false alarm on the way:** measured at the master output the LFO looked flat: the master chain puts a roughly constant floor (about −58 dB above 2 kHz) on a bass-heavy mix (limiter / clipper harmonics), which hid the filter; tapped at `rt.out` it is plain. Worth a look some day (is that floor wanted?).
- **Known:** vox REL has no audible tail in a render (so does the pad's own `rel` on `beta`: older); acid `rel` tops at 1 s; pump live synths are not repeatable render to render (on `beta` too: free-running state); tract lists CUT / RES / ENV but has no filter (CUT goes through `liveCut`); no SUS / ENV tracker commands; envelope times changed mid-stage act on the running stage; resolution is one quantum (about 2.7 ms), no extra smoothing.
- **MOD still open:** render parity for effect targets (2 points a row in renders vs 4 live), smoothing if the owner hears steps, per-band pump rows (needs a target id that carries the band), Add LFO… in the pump windows, master volume target.

## 10 Nov (afternoon): MOD pump targets; save gate widened; level over a play (build 554)

- **MOD pump targets** (Sonnet agent, diff read by the lead). A pump row is a target (`q.lfo`, `q.pi`) when it is a continuous, automatable knob and not a band row, selector, whole-number row, output / bypass or a Thunder-added row. Same id `fx:t:s:p`. `pumpPv(D, f, ov)` builds the plugin's whole value list from the song's `f.pump` plus the moved rows (`{pset:{slot, pv}}`); in the core it runs from the parameter table the page sends with `coreTfx` (`pd`), on the page through `fxpMsg` / `fxpTo`; renders use the page path. The song's own value is never written; STOP restores the slot. `CORE_REV qohjyu`. `tests/mod-pump.test.js`. **Open:** per-band rows (EQ bands, BAND / WIDE / HEAT per-band) need a target id that carries the band: a format decision; no Add LFO… in the pump pop-up windows.
- **Save gate** now also counts song-preset samples (`fullSmp` wrote `null`) and impulse responses (`irsFull` kept a bare `{ref}`; `irsResolve` only ran at start-up), waits for all three kinds and names them in the question. Wavetables, resynth data and frozen samples are always inline; SoundFonts stay out of the file on purpose.
- **Level over a play:** `pbLvReset` / `pbLvRead` / `pbLvStop`: mean square and peak of the master analysers, four readings a second from a timer that exists only while a song plays (lead's change from the agent's once a second), logged at STOP and in the report header. A quiet STOP before a restart gets no summary. Hidden: the timer slows to once a second; whether the analysers update while hidden was not checked.
- **Design for MOD and tracker commands on the live engines:** `docs/mod/ENGINE-TARGETS-DESIGN.md` (the contract: twelve k-rate AudioParams per live node, `c*` commanded bytes and `o*` signed offsets; per-engine map; delivery), on facts in `docs/mod/ENGINE-TARGETS-FACTS.md`. Commands keep their sampled-pad meaning (CUT on the 30 Hz – 18 kHz scale, FF = the pad's own knob); LFOs sweep both ways around what is in force; new targets `env`, `atk`, `sus`, `rel`; no new commands. Build next: engines (bass / vox / acid / mach; pump synths), then delivery.

## 10 Nov (afternoon): MOD step 1, LFOs always run (build 553)

Owner's choice: an LFO moves whenever the song plays. In `makeSeq` the LFO block became `lfoEvs(t, t0, dur)`, called in its old place for a track whose row ran (`lfoRan`), plus a pass after the track loop for idle tracks (not on, with LFOs, within `S.ntr` or the master) over the time to the next step, on the song's beat clock `api.bt` (phase never restarts; only PLAY resets it). `hearT`: in phrase / sect / BUS PLAY only the playing tracks, the buses they feed (through `busPlan().targets`) and the master. A block-muted track stays still; Mixer mute / solo are the player's, as before. `cut` / `res` / `dec` on an idle track refresh `PF.pno` each step (a woken track's first note sees the previous step's value, as today). `CORE_REV 44fr4c`. `tests/mod-run.test.js`: no-LFO songs give identical event streams old vs new (3 songs × 5 modes); master with no MST cell; idle rows in phase; block mute; bus; 2 / 4 points. Sonnet agent, diff read by the lead. Noted by the agent: in a live, non-loop song a track with a gap loops its own block, so "idle" there means no cell on the start row; a muted on-track's `pno` write is not rolled back (older behaviour). Next MOD steps: pump targets (wherever a pump effect sits), render parity, smoothing, then the lead's design for engine targets and tracker commands on the live engines (generic set).

Also in build 553:
- **Idle sleep** (`IDLE_SLEEP`, battery audit finding 2). `bgWait()` is 90 s on phones / tablets (`seqHandHere()`), 300 s on computers when the app is showing (hidden 20 s and Low power 30 s unchanged); `bgIdle` keeps its exclusions and adds `idleHold()` (recording / monitoring, helper starting, MIDI clock out, Link, `warmTok`, `rkBusy()`, `DR.busy`, the waves / chains screens, playheads) and `idleQuiet()` (two readings of the master analysers under −80 dB, 5 s apart). `bgSus` set as for the hidden sleep; `tStart` adds `ctx.resume()` to the warm-up race after a sleep; the CPU meter reads 0 % asleep; `smbAudition` now wakes the engine (it did not for a cached sample). `tests/idle-sleep.test.js` (30). Headless: suspended, pad woke it in 2 ms; PLAY after sleep 673 ms vs 576 ms awake first PLAY. First-note delay on the Fold and with the interface: unmeasured.
- **Audio-thread memory** (bit-identical; `tests/core-memory.test.js`, `tests/worklet-memory.test.js` render old vs new sample by sample; full engine goldens unchanged). Core: `stBal` fills one shared array (was an array per sample on stereo pads with moving pan), no per-block arrays / closures in the voice loop and `mixOut`; `Rd` rings (64 KB per shaped note) pooled (`RD_POOL`, 32, zero-filled on reuse, returned where an ended voice leaves `alive`); BassProc / VoxProc closures, `ThunderFx` `subarray` views, the PitchFx reverb's iterator. `VAR.bufs` (the shaped-buffer cache): byte cap 64 MB phones / 256 MB computers beside the 256 count, least recently used first (`tests/varbuf-cap.test.js`). Left for later (generated / shared text): `rvMix` array per block, `__tcOv`'s per-block objects, `subarray` views and closures in the chains / waves hosts and some modules, pump-synth worklet's `at` closure. The audit's "VoxCore and PitchFx reverb allocate per block" was mostly not so.
- Merged with MOD in one build; the core regenerated once (`CORE_REV aigv0e`).

## 10 Nov (afternoon): stability batch before the port (build 552)

Owner: "build as many as you can and delegate what you can" from the lead's list of stability items worth doing before porting. Five Sonnet agents, one worktree each; every diff read by the lead (audio ones line by line), three corrections, `npm test` on the merged tree.

- **Save safety (a real data-loss bug).** After a reload a sample layer is `{id, data:null}` until `resolveSamples()` fills it from IndexedDB; `smpToJSON` then writes only a `{ref}`. `saveProj` (File System Access, download, `DESK.save` / `saveAs`), `shareProj`, kit export and `backupNow` all wrote such a file. `smpNoAudio()` / `smpSaveGate(capMs, quiet)` wait up to 10 s, then ask (`askYes`) before an incomplete file is written; backups skip a cycle only while samples are still loading (lead's correction: ones gone for good must not stop backups). localStorage autosave unchanged (references by design). `tests/save-safety.test.js`. Still silent: song presets and impulse responses that cannot be found (`fullSmp`, `irsFull`). `navigator.storage.persist()` was already called and shown in Settings. This also answers the port plan's task A9.
- **Device recovery** (`DEV_RECOVER`). `devRecover(why)` from `devicechange`, an unexpected `statechange`, `sinkchange`, a wake (wall clock jump on an `idleTick`, `visibilitychange`, `focus`, and the desktop shell's new `powerMonitor` `thunder:power` event through `thunderDesktop.power.on`), and at the end of `resetAudio()`. Resumes a stopped context (never under `bgSus` or `rkBusy()`); a chosen sink that is gone → `setSinkId('')` with `aset.sink` kept (`DR.fb`), back when it returns. Logs only when something was wrong (lead's correction). …waves follows the output (`thunderWaves.sink()`, `followSink()`). `tests/device-recover.test.js` (fakes only: **nothing ran on hardware or in Electron**). Reset audio engine still cannot rebuild the context (Latency / Rate changes reload the page): a truly stuck stream may need a reload.
- **Synced delays pitching during holds** (`HOLD_CLK_FLAT`). Found: the effects' beat clock is automation on `clock.offset` booked per beat event; after `rsHold` the ramp to the next beat took `rowTime + d`, `clkSpb` measured a lower slope, the tfx modules' tempo dropped (Simple delay reads `bfTempo(ctx)` with a 60 ms glide) and the repeats bent. Now the clock waits flat for `d` (`PB.rsGap`) and then ramps at the song's slope; `clkSpb` treats flat as stalled. `tests/hold-clock.test.js` (tempo within 1 %; error up to ×2.8 before). Native TFX delays were never affected. A runaway (engine run too fast) still sounds wrong: that is the kick.
- **Background play restart** (`BG_RESTART`, Android). The element had no listeners; a system pause while playing went unnoticed. Now restarted at once, again after 1 s, then on the next return or touch; 3 a PLAY; not after the app's own pause or a Media Session pause / stop. `tests/bg-restart.test.js`. Whether Android allows `play()` from a hidden page: only the phone can say.
- **Audio-thread stalls.** `WASM_PRECOMPILE`: tfx, pump and pump-synth modules compiled on the page and handed over (`processorOptions` / `{pumpModule}`), bytes path kept as fallback; `chains-worklet.js?v=44`, `pump-synth-worklet.js?v=8`. `RESUME_PREP` / `RESUME_SPREAD`: the page sends `{op:'prep'}` before its own replay and the core chases 1 ms a block (`chaseRun`), PLAY finishes what is left; any message that changes what the sequencer reads drops the prepared result. `CORE_REV 198luua`. Checked by the agent in headless Chrome: first 48 rows after RESUME identical, inline vs prepared, at rs rows 9, 20, 30, 35 of ELECTRO; `started` 4–8 ms → 0–1 ms (the 260–650 ms case needs a longer song: unmeasured).
- **Engine goldens** (port unit U01). `tools/engine-golden.js`, `tests/engine-golden.test.js`, `tests/golden/engines.json`: 1037 factory voices fingerprinted (sha256 + peak / RMS); `npm test` renders a fixed subset (about 19 s), `ENGINE_GOLDEN=all` everything. Not covered: pump synths (own test), LXR, Plaits / Elements, waves, SoundFont, sample / grains layers, live paths, effects. Noted: `layertype|dust` is silent at its defaults.
- **RESOLVED the same afternoon (see the line after this one):** `node tools/seq-golden.mjs` (and `--core`) failed 3 of 4 songs (`electro`, `electro-mid`, `electro-cmds`) on `beta` as it was BEFORE this batch (6586f96).** Cause unknown (an earlier change to the event stream, or an intended change without a golden update). `npm test` does not run these (they need headless Chrome). To investigate next; the port's unit U07 depends on them.
- **Sequencer goldens: stale, not a regression; regenerated; now in CI.** A Sonnet agent diffed the old stream (tool run at 61d1355, 2 Nov, where the goldens were last written) against `beta`: the only difference is `fxc` `sw0` events on the master track beside `rtg`: commit 28d2218 (8 Nov) made a new RTG switch its slot on (`sw` 1, and −1 after its release length), the owner's decision of 9 Nov ("a new RTG switches its slot on"). electro +2 events, electro-mid +1, electro-cmds +2; rows, times and notes identical. (fb04b13 the same day had briefly added a bogus `rlo` to every note from `T.rel`; fixed in 28d2218.) The lead regenerated the three goldens (`node tools/seq-golden.mjs --update`); all four pass in both modes. `tools/seq-golden.mjs` now prints what differs on a failure. **New `.github/workflows/tests.yml`: `npm test` and both golden runs on every push to `beta` and every PR**, so this cannot go unnoticed again. The owner's LIMIT / CALM / TONE answer (10 Nov): they stay on buses and the master.
- **MOD, scoped (nothing built):** LFOs run only while their own track has a cell on the current row (so never on the master without an MST phrase); pump targets and engine targets do not exist; `cut` / `res` / `dec` targets only reach notes starting on that row. Owner's choices (10 Nov): LFOs always run while the song plays; engine targets as one generic set (cutoff, resonance, filter env, attack, decay, sustain, release) mapped per engine; tracker commands CUT / RES / ATK / REL / DEC reach the live engines in the same pass; pump targets wherever pump effects exist. Correction to what the owner was told: pump EQ, COMP, BAND, WIDE, HEAT, METER and the creative pump effects are already on every track (`pumpAnyTrack`); only LIMIT, CALM, TONE (lookahead) and MASTER are bus / master only.

## 10 Nov (midday): the owner's Fold reports on build 550; runaway kick experiment; Reset audio settings (build 551)

**The reports (Fold, Page sequencer, a simple song with a delay; `chains, cpu, master, tfx` loaded):** screen on 85 s: 1.00 s in 1.00 s, 2 underruns. Screen off: 31 s at about 0.98 with underruns 3 → 10 (the dropouts heard), then the context ran away: +4.6 s a second while hidden and about +14.3 s a second for 12 s after the screen came back (ctx t 136 → 303 in 14 s of real time), then healthy with no more underruns. The resync held the song throughout (95× / 8.9 s hidden, then 459× / 147.8 s; least lead −1.09 s), so the owner heard the song "relatively stable or corrected quickly", but the sound during the runaway was wrong ("low pitched and a little clickty", the delay "pitching": the engine itself runs too fast and most of its output is dropped; whether `rsHold` moving `mx.beat` also moves a tempo-locked delay's time was not checked). Stopped and hidden: no runaway, clock 0.64–1.00 (slow is harmless stopped), the engine suspended at 20 s and woke in about 0.25 s both times. The header read `latency 21 + 391 ms` two seconds after a wake (43 before): maybe momentary; ask whether pads feel late after a wake.

- **RUNAWAY KICK (TEST), an experiment** (owner: "do the experiment"). Lead's design, Sonnet agent, every line read by the lead. `rsHold` adds each hold to `PB.rk.sum`; `rkTick` (each pump tick, after `rsTick`) takes the surplus per real second over `RK_WIN` 0.5 s windows; `RK_N` 3 windows in a row over `RK_RATE` 2.0 (so about ×3 for 1.5 s) → `ctx.suspend()`, and the pump tick resumes it `RK_PAUSE` 150 ms later (no timer: hidden pages throttle them; the pump-timer Worker keeps ticking while suspended). At most `RK_MAX` 4 a PLAY, `RK_GAP` 8 s apart. `rkKick` guards `audio()` and `audioInterrupted()` so the pause is not resumed early or taken for an interruption; `bgSus` is not set. Lead's corrections: `rsTick` keeps its learned band across the pause (`rkBusy`; learning again takes 0.4 s, about 5 s of song let through at 14×); a resume that does not take is tried again at 1 s and handed to `audioInterrupted()` at 3 s. Kill switch `RUNKICK`; Settings › Audio › RUNAWAY KICK (TEST) (`rkOn`, `thunderplus-runkick-v1`, default on). The report gains `kick: audio clock ran ×N …`, `kick: resuming …`, `kick result: clock +X s in 2.00 s after the kick` and `kicked K×` on the page line. `tests/runkick.test.js` (10 cases on a fake clock). **Whether a suspend / resume breaks Chrome's runaway is unknown until the owner's next screen-off report**: read the `kick result` lines; if X stays near 28, the kick does not work and should come out (or try a longer pause before giving up).
- **Reset audio settings** (owner asked, to clear test settings): Settings › Audio, beside Reset audio engine. `audResetAset` + the handler on `#audDefaults`: Latency, Rate, Latency comp, Live engines, Sequencer, Worklets, Runaway kick, Keep screen on, Background play, Low power display back to default; reloads through `applyUpdate()` (song saved first) when Latency, Rate or Worklets differ from the running audio. Not reset: output device, level, playhead offset, input / MIDI engines, Link, metronome. `tests/audio-reset.test.js`. Sonnet agent, diff read by the lead.

## 10 Nov (midday): bigger cells on touch (build 550)

Owner: "let's do bigger cells on touch" (Arrange / Song only, as a setting). `bigCellsOn` / `setBigCells` (`thunderplus-bigcells-v1`, Settings › Screen, default off). On touch (`!arrDeskZ()`) `arrSize()` raises the zoom to `28 / cwFit` (`arrBigFloor`) and the row height to at least 24; the zoom the user chose is kept in `arrBigPre` and comes back when the switch goes off (`arrZoomDone` saves that one). Headless: 360 px 14.8×16 → 28×26, 884 px and desktop unchanged, no page overflow, cursor box equals a cell's, Off restores exactly. Not checked: pinch gestures, the outline boxes numerically, the row in the Settings sheet by tapping. Sonnet agent, diff read by the lead.

## 10 Nov (later morning): the port work plan; recorder settings key (build 549)

- **Work plan for the native port** (owner: "write up the work plan"; wants the web app untouched until a native beta works, on a branch, and a plan a less capable managing model can run with Fable called in for heavy work): `docs/port/WORKPLAN.md` (rules, roles, stages N0–N4 as tasks with a builder, a reviewer and a pass / fail gate each, gate commands, the brief template, a status table, known gaps), `docs/port/CORE-BRIDGE.md` (the bridge's message list, draft until N0), built on two Sonnet inventories: `docs/port/ENGINE-INVENTORY.md` (about 16,960 lines of JS DSP to port; 37 port units) and `docs/port/BRIDGE-TODAY.md` (every message crossing a boundary today). Nothing built; N0 still waits for "start the native spike".
- **What the inventories changed:** the mixer has no messages today (direct Web Audio writes), so the bridge gains `mix.set`; song renders run on the main thread, not in the render Worker; the audio-thread core reaches into other processors by object access; 28 track / pad effects live in the shared …chains / …waves JS block and are needed in N1, not N3; the engines have no golden renders (U01 first); `.thunder` files are self-contained (samples inline as 16-bit PCM base64) but SoundFonts are not in them, and a layer still loading from IndexedDB at save time is written as a reference only (unverified whether Save waits).
- **Bug fixed (found by the bridge inventory, confirmed by the lead):** `RECK` (REC / metronome settings) and `RCD_K` (Record a sample's settings) were both `thunderplus-rec-v1`; `recSave` wrote only its own fields, so the recorder's input, mode, latency and per-device channel choices were lost at the next reload. `RECK` is now `thunderplus-recset-v1`, reading the old key once.
- **The work plan was tested cold:** an Opus agent played a brand-new manager told "start the native spike" and listed where it had to guess (2 blockers, both "the plan is not on `beta` yet"; 11 risks). All taken: a pre-check before A1, where the plan and status table live (`native`), who pushes and opens PRs (the manager; `gh api` REST only), the files a port session may touch outside `native/`, an N0 brief wrapper, Sonnet writing audio-thread code allowed on `native` under gate and review, two N0 checks the To-do named but the plan lacked (A8 libremidi backends, A9 saved songs carrying their samples).
- PR #71 (build 548) merged into `beta` on the owner's word after CodeRabbit's second pass (one more finding taken: 26 further failure messages marked to always show; 57 call sites in all).

## 10 Nov (morning): audio-review three, quick wins, archive, native port strategy (build 548)

Owner asked the lead to plan the listed fixes, delegate them to low-cost agents and verify, and to write the native port strategy. Four Sonnet agents in separate worktrees (one brief each, a tool-call budget, no pushing); the lead read every diff (the audio ones line by line), made two corrections, and reran `npm test` on the merged tree. One PR from `lead/build-548` into `beta`.

- **First PLAY stall (was "for audio review").** The `.then` handlers of `tfxReady` (`rtTfxLoad`) and `chainsReady` (`rtChnLoad`) run `rtAllFx`; on a computer with PLAY as the first touch that landed in the first bars. `rtPending` keeps each handler's promise on `rt.pend`; `tStart` waits for them through the existing `warmTok` warm-up, capped at `FIRST_PLAY_WAIT_MS` 900 ms in all (600 as before when only a live engine is pending); nothing pending: synchronous as before. Skipped for Link, MIDI-clock slave, takes / REC, phones (`wkLazy`) and hand-overs (`playWaitFor`). Kill switch `FIRST_PLAY_WAIT`. Headless, long tasks in the first 2 s of the song: off 773–954 ms; on 0 ms in 4 of 6 runs, 531 and 1197 ms in 2 where the cap hit on a loaded 2-CPU box. F9 cases in `tests/sweep/stability-batch1.mjs`. Not run in Electron or with Link.
- **MIDI notes after STOP while hidden (was "for audio review").** Hidden, `msend` bypasses the outbox (timers throttled). `MOLED` now remembers every note-on handed to a Web MIDI port ahead of time while hidden (and what `mobFlush(true)` sends as the page hides: the lead's addition); `moledCut` in `moRelease` calls the port's `clear()` if it has one, sends each a note-off 1 ms after its start, then All Notes Off now and after the last. Native helper ports are not ledgered (their queue clears). Kill switch `MOLED_ON`. `tests/midi-ledger.test.js` (fake port). **Limit:** Web MIDI cannot recall a message, so gear that ignores note-off (drum machines) still sounds the booked hits; only the native shells fix that. No hardware run.
- **…waves' second engine (was audit C).** Embedded only (`LAZY_ENGINE`): no AudioContext or worklet at load; `engNote` starts the engine on the first key, on-screen key or MIDI note and queues notes meanwhile; the context suspends on `pause()` and when the page hides; five editor timers skip while `napping()` (two sit in the shared editor block, so `chains/index.html` got `napping = () => false` through `tools/sync-shared.py`). Lead's correction: a voice-count change while the engine is still starting no longer starts a second one. Headless: 0 embed contexts on opening the screen, 1 after the first key, suspended and 0 timer calls a second after leaving (13 before). Not checked: hidden-tab suspend, the MIDI path, a key released before the engine is ready. Still open: the embed ignores the chosen output device (the host exposes no sink id: a one-line getter on `thunderWaves`, then `setSinkId`).
- **Battery quick wins (audit findings 1, 4, 5).** `cpuMeter` writes only when its shown key changes, no width transition; `stageSpec` (Mixer) draws once when nothing is live and skips until its idle key changes (MOD and Song needed nothing); `paintMeters`, `mixFaders`, `draw()` write only changed values. DOM mutations a second, before → after: stopped Phrase 9.5 → 2.3, stopped Mixer 13.8 → 5.7, playing Mixer about 2890 → 715–1000, playing Phrase 443 → 365. CPU time not measured; no phone numbers. Left: findings 2 and 3 (engine sleep while visible, `clocked` worklets: audio review), 6, 8, 11.
- **Retest's three.** Pad press sets `arrActT` (Arrange holds its page like after a key); the pad keys' click after its own pointerdown is ignored for every key (`k._pd`, cleared on `pointercancel`); Tracker | Synth no longer shrinks with two buttons (CSS; the exact "Synt" clip was not reproduced headless). `tests/retest-three.test.js` is source-pattern only: none of the three was confirmed on a phone.
- **Usability quick wins** (touch only; 1440 px unchanged): File rows 40 px without shortcuts; "Help & keys" first in File at 480 px and under; FX opens on SLOT 1 when the cursor is on TRACK; 40 px header buttons and tabs, invisible hit areas on stars, Arrange zoom and PIN, 40 px Settings rows (those not measured); Pad presets… / Engine presets…; unsaved dot on File's corner on the phone; tour footer wraps. Then the owner's answers the same morning (a fifth Sonnet agent, diff read by the lead): Help & keys moved to the bottom of File; FX opens on SLOT 1 only the first time (`fxOpenedOnce`, cleared by `resetCursors`); Settings Done (`#gsetDone`, touch only); toasts placed under the tab strip on `body.mob` (`toastPlace`) with POP-UP MESSAGES in Settings › Screen (`toastsOn`, `thunderplus-toasts-v1`; `toastErr` / `toast(msg, ms, 'err')` always shows, 41 call sites marked). Bigger cells on touch not built (see the punch list). Skipped from the audit: U4 Close / Done, U6, U12, U14, U15, U16.
- **Handover archive.** Everything dated before 8 Nov moved word for word to `docs/HANDOVER-ARCHIVE.md` (this file 678 KB → about 120 KB); the What's new routine, Licensing rule and Conventions stay here. Done by the lead with a script (cheaper than briefing an agent).
- **Native port strategy:** `docs/NATIVE-PORT-STRATEGY.md` and "Native port" in the To-do. Plan only.
- **CodeRabbit on PR #71 (5 findings, all taken):** the ledger's cap raised 256 → 4096 (an entry dropped at the cap lost its own note-off; the test now releases all 400 booked notes); embedded …waves drops note-ons while napping (they waited in the port and burst out on resume) and starts or restarts only through `engStart` (a note during a voice-count restart started a second engine; a count changed during start-up is applied when it finishes); CLAUDE.md says a question about native apps never starts N0; the strategy's "sounds the same everywhere" line names its exceptions.

## 10 Nov (night): unique per track, first time only (build 547)

Owner: "only apply the first time a pattern is entered on a track. I want to be able to rearrange the order of existing patterns without generating a bunch of new patterns"; on an empty track new patterns should still be unique. `blockMixed` now says "copy" only when another track holds the block and this track does not hold it in another row. `mselApply` (selection drag-move) records the cells it empties in `uniqVac` so a move along a track never copies, and a move to another track copies only if the block also stays on its old track. Master-column guard and setting-off unchanged. Tests: `tests/uniq.test.js` (logic only; not run in a browser). Sonnet agent, diff read by the lead. A first agent ran four hours and produced nothing; the second, with a two-step brief and a tool-call limit, took four minutes.
Not changed, for the owner: bringing pattern A from another track onto a track twice makes two separate copies (the second time the track holds the copy, not A), as it did before; to repeat it, copy the track's own copy (review suggested remembering source → copy; not built, ask first). Alt-drag copy in Arrange / Song (`sxApply`) writes the grid directly, so a copy dragged onto another track shares the pattern with no unique copy (existing behaviour); cut then paste back onto the same track still copies in an older song where another track shares that pattern; `songRowsInsert` / `songRowsRemove` were not read.

## 10 Nov (night): phone resync, "hold the song" (build 546)

Owner asked for a resync safety net. Phones and tablets, Page sequencer, only while a worklet module is loaded on the context (`wkUsed(ctx).size`: without one the phone's clock held 1:1, so those songs are never touched), not during takes, Link or MIDI clock (`rsScope`). `rsTick` (called in `pumpRun` before the moved-on rule) watches the offset between the audio clock and real time over the last 0.6 s: the clock moves in steps of the output buffer, so the offset saws inside a band; a sample more than `RS_LEAP` 0.06 s over the band's upper edge is a leap, held in the same tick; the lower edge rising more than `RS_CREEP` 0.09 s is sustained racing, held too; a clock running slow (underruns) is followed, never corrected. `rsHold(s, d)` moves the song's timeline on by d as the moved-on rule does, plus `tp.t0`, `tp.st`, `stgT0`, `mx.beat`, `mx.t0`, the MIDI LFOs, ARP returns, CC ramps and echoes; notes already booked can't move. For 4 s after a hold the look-ahead stays short even hidden (`tp.rsUntil`). Kill switch `RESYNC`. The report's page line ends `held N× (S s)` and logs `held the song: audio clock leapt +D s`.
Built by a Sonnet agent from the lead's design; the lead rejected its first detector (drift against a threshold with a step estimate: it left about 38% of a sustained rush on a stepped clock) for the envelope one, then gated it on loaded worklets and widened the creep margin from 0.04 (8 false holds in 30 s with jittered ticks, each a small pause in a healthy song) to 0.09 (0). Simulated on a faked clock, with the fix / without: 1× in 0.2 s steps with jitter, 30 s: 0 holds; 1.5× stepped, 20 s: +0.09 s / +9.6 s; 2.3×, 10 s: 0.00 s (peak 0.28) / runs away; +4 s and +32 s leaps: held in the same tick; the phone's pattern (1×, 1.4× with pauses, a +7 s burst): −0.46 s after 45 s (the pauses are followed on purpose) / +2.8 s; hidden page 1.5×: −0.25 s / +10.4 s. Scripts were in the session scratchpad (`vtest.mjs`). **Never run on a real phone:** the thresholds are the first thing to adjust from a report (false holds on a healthy clock would show as `held` counts while the clock lines read 1.00 s in 1.00 s).

## 10 Nov (night): worklets confirmed as the phone clock's cause; build 545

**The owner's Worklets test (Fold, Page sequencer, build 544), which settles the cause:** Worklets ON: by the end the device had played about 83 s (`out t`) while the context had rendered 208 s (`ctx t`, `stats total` with it), 137 underruns, output delay grown from 43 to about 190 ms; it began in the first second on screen before anything played (+1.16 s in 1.01 s, 14 underruns) and the worst burst was just after STOP with the screen on (+32 s in 1 s); PLAY to STOP the song ran 61.7 s in 40.4 s of real time. Page ticks were healthy throughout. Worklets OFF: 45.35 s of clock in 45.31 s, 38 s of it hidden, 0 underruns. So with any worklet module loaded, Chrome on this phone renders more than the device takes and drops the surplus; the song, booked on that clock, rushes with pieces missing.

**Build 545 (PR from `overnight/build-545`; work by Sonnet agents from the lead's plans, each verified by the lead: diff read, tests rerun on the merged tree):**
- **Phones and tablets load a worklet module only when needed** (`wkLazy`, `wkNote`, `wkUsed`, `wkFxAsk`, `wkLiveEnsure`, `coreWanted`; `seqHandHere()` devices only; computers unchanged: same 7 modules and 14 nodes at first touch). A phone song of pads and samples with no effects makes zero modules through start-up, PLAY, hide, show, STOP (measured headless). Effects load tfx + chains (tfx needs chains' shared code) when a slot is filled and on, or off with a switching command in a pattern (FXO, FLX, FLY, GAT, RTG) or at the first such switch while playing; live engines (one group: bass, pump synth, waves) when the song plays a live pad; the audio-thread sequencer only if picked on that device (`aset.corePick`; the saved default plays on the Page on a phone: lead's decision, the owner uses Page there). The safety limiter stays its stand-in until another module loads, then crossfades to the worklet limiter over 40 ms. The report header lists the modules loaded (`worklets none` / a list). Not traced: whether Perform scenes, macros or MOD targets that switch a slot on reach `rtFx`.
- **Playback fixes (audit A):** PLAY then STOP inside the first-PLAY warm-up (`warmTok`); one throwing event is skipped, not the song stopped (`playEv`, over 20 a second still stops); `'interrupted'` contexts resume and PLAY gets the 2.5 s fallback; the hand-over no longer runs `moStop`; timers booked ahead are void after STOP (`seqGen`); tStop runs each step through `G` so the state flip and the background-play / wake-lock release always happen; `specAnalyser` follows the new master; `mutVoice` shares the base layers' sample objects (no base64 round trip; bit-identical on 16-bit-grid data, under 2e-5 otherwise). Browser checks: `tests/sweep/stability-batch1.mjs`.
- **Audio-thread fixes (audit B), output bit-identical on finite signals, goldens untouched:** `Auto.at` / `prune` without a closure per call (scavenges over 53 s of audio 636 → 74); `tpPeak` without an array literal per sample (18.6 KB → 0.15 KB a block); the master replaces non-finite or beyond ±64 input samples with silence and ignores non-finite settings; BassCore clamps a partial's increment to Nyquist (NaN after a big upward pitch jump; renders that had a partial above Nyquist while fading change, they were aliased or NaN before). `master-worklet.js?v=6`.

**Still open from the audits (not built; reports in the session's scratchpad are gone after it ends, so the essentials are here):**
- *For audio review:* first PLAY of a session runs 0.5–0.6 s main-thread tasks from the module loaders' `.then` handlers (`rtAllFx`) while the first bars play (moved on 1–4×); fix idea: a capped wait for the loaders the song needs before the first PLAY, or load at first touch on computers. MIDI out while hidden bypasses the outbox (`msend`), so notes booked 1.2 s ahead survive STOP. `varBuf`'s shaped-buffer cache is capped by count (256), not bytes. RESUME on the audio-thread sequencer replays up to 4096 steps in one message (260–650 ms at song row 60+). tfx, pump and pump-synth wasm compile on the audio thread at first use (pass compiled modules as waves live does). Processors assume 128-frame blocks (tfx throws at 256). VoxCore, PitchFx reverb and others allocate per block; `Rd` ring is 64 KB per shaped note. chains' FlexCore (17 patterns) and StepGateCore are older copies than the track effects'. psyn has no try / NaN guard around its wasm.
- *Engine and devices (audit C):* embedded …waves starts its own AudioContext + worklet at load with no EMBED guard, never suspended when hidden, ignores the chosen output (fix: start lazily on first key, suspend on hide). Reset audio engine keeps the same context and device stream (could reopen the sink). No recovery when the chosen output disappears or after sleep / wake (`devicechange` always on, Electron `powerMonitor`). Android may pause the background-play element on focus loss; nothing restarts it. Log `playbackStats` average / min / max latency beside `heardLat()` to settle the playhead-ahead question on the Fold.
- **Songs with effects on a phone still load worklets, so they can still race**: the resync (hold the song at real-time speed when the clock leaps) is being built on `fix/phone-resync`; the lead sent its first version back (it left about 38% of a sustained rush on a stepped clock) for an envelope detector. The cure is the native engine (Longer-term priority).

**The three audit reports are in the repo: `docs/audits/2026-11-10-retest-of-9-nov-fixes.md`, `…-battery.md`, `…-usability.md`** (each ends with a ready work list; screenshot paths in them pointed at a session scratchpad that no longer exists). A Sonnet session should start from those lists.
**Step 3 battery (desktop numbers; phone main-thread numbers were unusable under the CPU throttler):** stopped and visible on Phrase the main thread burns 235–357 ms/s, almost all of it the CPU meter (a `width .3s` transition and DOM writes every tick even when unchanged: about 45 with `transition:none`, about 10 with the meter hidden); the audio thread uses 373–445 ms/s while stopped and visible because the engine only sleeps when hidden or in Low power (0–15 after `ctx.suspend()`); playing, `frame()` takes 150–305 ms/s with 36 DOM mutations a frame on Phrase and 180 on Mixer, and Arrange row measuring adds about 100 layout reads a frame. Fixes in order: write the CPU meter only on change with no transition; skip idle redraws of the stage canvases (Mixer, MOD, Song); skip unchanged meter and Mixer text writes; *for audio review:* sleep the engine while visible after 60–120 s of silence, and let `clocked` effect worklets sleep when stopped with silence shortcuts in master, chains and core. Also …waves and …chains timers keep running after leaving their screens.
**Step 1 retest (63 items of the 9 Nov fixes, headless):** 44 fixed, 14 not exercised, 4 need the phone, 1 NOT fixed: after a pad press on the phone Arrange pages after about 1.4 s (FOLLOW on; should hold 4 s) or 2 s (FOLLOW off; should wait a minute): `arrFollowPage` ignores `workT`, which the pad's pointerdown sets. New: pad keys can fire twice on a slow press (the click fallback is ignored only within 800 ms of pointerdown, `ptrJust`; the `k._pd` flag used for SAVE would fix it); the Tracker | Synth switch is clipped to "Synt" at 360–412 px.
**Step 4 usability (nothing blocks a core job; 8 slow one, 9 cosmetic):** Tab never moves focus on desktop and Alt+F / Alt+E do nothing (making Tab leave the tracker would change an owner decision: ask); folded, Help is at the bottom of a 3,500 px Settings sheet (the `?` is gone below about 400 px); File menu rows are 30 px on touch and show Ctrl shortcuts (a CSS specificity clash, line 100 vs 1002); many touch targets under 40 px (header Play 32×34, screen tabs, Arrange zoom 24×24, preset stars, Settings switches); on FX the cursor starts on the TRACK row so EDIT does nothing while the hint says "EDIT adds". Not walked: Pattern view on touch, Synth page unfolded, pump windows. These, the retest's three, and the quick wins are Sonnet work (see "Which model for which work").

## 10 Nov: phone clock re-read; Worklets test switch, clock log while hidden, slow-work timing (build 544)

Owner asked what the stutter work had missed. Three things (read-only audit, then build 544; draft by the lead, finished and tested by a Sonnet agent, verified by the lead):
- **The clock evidence was misread.** The build 529 Balanced report shows the clock racing with the song STOPPED and the screen off (7.40 s of audio time in 5.95 s), so it is not the song or the sequencer. The "8% fast while playing" (527, Safest) happened once, right after the engine started with a 302 ms buffer, and never again: more likely a one-time buffer fill than a rate error. The 528 clock log stopped 5 s after STOP, so it never covered a racing stretch. It now runs while hidden with the engine running and for 10 s after coming back (`pbCkOn`).
- **Untested suspect: worklets.** Even a sample-only song on Page + Render loaded 7 worklet modules and made 14 worklet nodes at first touch (CPU probe, safety limiter, bass / pump-synth / waves modules, chains, tfx). Chrome renders a context on a second thread once it uses AudioWorklet, and on Android keeps adaptive buffering (Chrome 136 dropped both on desktop only). Hypothesis, NOT confirmed in current Chromium source: when that thread is held up with the app hidden, each device callback queues a render sized from a growing buffer target, so the context over-renders and the surplus is dropped: dropouts plus a leaping clock, and afterwards more hidden delay (the playhead ahead of the sound). Test: Settings › Audio › **Worklets: Off (test)** (`aset.noWk`, `NOWK`, `wkOff(ac)`; applies on reload). The page context then makes zero `addModule` and zero `AudioWorkletNode` (measured headless, Pixel 7 profile, Page and Audio-thread settings); effects, chains, live engines and the core are silent or unused, the limiter is its stand-in, exports are untouched. Not guarded: the recorder's `thunder-rec` and the helper's `thunder-io-in` (recording only), so don't record during the test.
- **Stutter that stops under LOOP: a real candidate.** `varBuf` built the variation's voice again for every note until its Worker render landed (21 builds per key in a 20-note test, now 1); for a MUT on a pad with a sample, `mutVoice` base64-encodes and decodes the whole sample each time (still does once per key: `voiceToJSON(base, false)`; worth changing if the report shows it). Also first use of a CUT / RES / DEC value copies and filters the sample on the page, and a new phrase rebuilds the grid. The report now logs `slow <what> N ms … row R` (4 ms or more, while playing) for varVoice, varBuf, shapeData, stretchData, rtBufPlain, ensure, and `long frame` lines (120 ms or more) with the top functions.
- Also found, not changed: embedded …waves starts its own AudioContext and worklet when its screen opens (`waves/index.html` `startEngine`, no EMBED guard). Held back on purpose until the Worklets test is done: sleeping the engine sooner when hidden and stopped on phones.
- Reading the test: Off holds 1:1 where On races → build the phone-safe audio mode (plan step 2). Both race → the clock can race without page worklets, which does not clear them of adding to it when On: compare how much each gains, and look at `out t`, `stats total` and `bg` in the new hidden lines as in "8 Nov, end of session".
- Sources: Chromium `audio_destination.cc` (older revision: one render task posted per device callback, FIFO not re-checked), "New in Chrome 136: WebAudio Lower Latency" (fugu-dev), Chromium issue 40133762.

## 9 Nov: LFO scopes on MOD (build 543)

Owner: show the LFOs visually. One scope per filled row, from the real `lfoAt`, depth sets the height, and a sum scope for LFOs sharing a target. `modScSync` lays out a persistent layer in `#tScr` (`pointer-events:none`, under the rows when wide, beside each row when folded); `modScDraw` caches each waveform; `modScTick`, from the existing `frame()`, moves only the dot at about 18 fps and only on MOD while playing. Dot time is the grid playhead's (`fxcBeat`, `heardLat`, `moFxLat`, `phOff`). Low power display: static shapes, no dot. Measured headless only: dot about 20 ms ahead of the playhead on average; not checked against real speakers.
## 9 Nov: chains rail feed view (build 542)

Owner: a track that feeds a chain should show it, with the rest greyed, to see what's feeding a chain and what happens to that feed. `chainFeed(t)` finds the track's Track In modules, walks the cables forward (`chainReach`, cycle-safe) to the first Chain Out, and uses `chainMods(outTrack)` for the module set. `dkChnRail` takes `feed`: this track's number in the IN tag stands out, off-path blocks and cables are grey (`dim`, a second `path.off`), `+ CONTROLS` stays off because controls belong to the output track. Only when the track has no chain of its own. Tests: `tests/chains-feed.test.js`. IN lines are spaced across the tag, not under each number.

## 9 Nov: acid Mutate intensity (build 541)

Owner: Mutate should make changes from small to large that relate to the line, keeping hand tweaks. In `acidLine`: "Mutate intensity" (`acidSet.mut`, 1–100, default 25, session only) and Original (O), as in `mutatePhrase`. `acidMutate(…, inten)` picks exactly round(L × intensity / 100) distinct steps (notes first when TIME is off; a press retries up to 8 times if nothing changed). `acidNear` moves a sounding note within the scale, at most `acidMutDeg` degrees, small moves likelier. `acidWriteMut` writes only the picked rows and only what changed: the old path rewrote every step row, losing hand volumes, instruments, note-offs, other poly columns and PSL parameters. Generate is unchanged. A slide re-rolls as "slide into the picked step", so writes stay on picked rows. Tests: `tests/acid-mutate.test.js`.
## 9 Nov: chains rail lines; MOD target steps (build 540)

Owner: lines between the blocks in the chains rail, in an accent color, spread out when few. `dkChnRail` records which cable joins which tag or block (`rail._dk`, from `S.chainPatch.cables`), `dkcDraw` does one layout read and draws on an SVG layer behind them (`.dkclines`, no pointer events; straight lines, the owner's choice after trying curves and right angles, and he may go back to curves), `dkcWire` redraws on a size change only, never on a tick. The IN tag gets one line per Track In module (12 at most); other pairs one per cable, 2 at most. Gaps grow up to 34 px when there's room; with many modules, today's 7 px and cut-off. Accent on the 909 skin is the weakest against the rail (contrast 1.89); darken it there if the owner minds.
MOD: Ctrl/⌘+arrows on TARGET step `tgtPicker`'s flattened list (▲▼ eight at a time), keeping SHAPE, RATE and DEPTH; on an empty row the first press adds this track's first target.

## 9 Nov: MOD screen, first version (build 539, PR from `feat/mod-screen`)

Owner: four LFOs per track for now; internal FX and instruments, …pump later; build stepwise and verify before the harder parts, knowingly past the feature freeze. Sonnet subagents built it, the lead reviewed.
- **Data:** `S.perf.lfo[t]` is now an array of up to `PLFO_MAX` (4) `{id, sh, rt, dp}`, no holes; `normPerf` still reads the old single object. Perform's lock sheet edits entry 0 only.
- **Sequencer (`makeSeq`, regenerated into `core/seq-core.js`):** LFOs on the same target add around its one value; point counts as before (4 a row live, 2 in renders, 1 for cut / res / dec). Checked headless: A+B equals A and B summed within rounding (800 points), main and core events identical.
- **Screen:** `mod` in `VIEWS` between `fx` and `project`, sharing the FX screen's track (`cFxT`, master included). Helpers `modPick`, `modDel`, `modBump`, `modEdit`, `lfoAdd`. "Add LFO…" on FX rows (`cmdCtx`) and the FX bar (`valMenu`'s `o.lfo`), never …pump. `~` mark (`.lfom`) on modulated FX screen rows only, not the FX bar.
- **Keys:** Alt+7 is now MOD; PROJECT has no Alt key (Alt+8 stays …waves, which `viewKey` takes first). Owner may want this changed.
- **Instrument targets are what existed:** vol, pan, and cut / res / dec, which update once a row on the next rendered note; on live engines cut / res are only an output low-pass and dec doesn't reach them.
- **Not tested:** touch hold-and-drag on a real device; the owner's ear. `tools/seq-golden.mjs` fails 3 of 4 songs before and after this work with the same counts (the recordings look stale, 1–2 extra events), not touched.

## 9 Nov: builds 531–535 (on `beta`, not on `main` yet)

Written up from the commits on 9 Nov; none of it has been tried on a real Fold.
- **531:** Pad: Save beside Mute; Loop, Play and play-song light up.
- **532:** Low power display option (off by default): limits playback drawing to about 24 fps; positions are still drained every frame, so timing and follow are unchanged. A black, high-contrast Low power skin.
- **533:** With Low power display on and the app on screen, the audio engine sleeps after 30 s of silence. Not while playing, rendering, recording or with MIDI input connected; it stays awake while a pad is sounding. Off keeps the old behaviour.
- **534:** Low power display picks the Low power skin; it applies even if storage fails, and a stale saved skin is cleared.
- **535:** Phone: less layout work while playing; the playback report logs stalls.

## 9 Nov: control bar Play plays this screen (build 538)

Owner's call: `#tSong`, labeled Play, runs `playCtx` in the tracker (the phrase on Phrase, the song on Song), so the phrase stays one tap on a
phone now that the pad's PLAY has the song. Outside the tracker it still plays the song. It now also shows on a folded phone (it used to be hidden there because the pad's PLAY did its job); the top bar is tightened under 400 px to fit. Under 700 px in PAD mode the COMMANDS row drops its ▶, since the pad's PLAY sits right below it (owner chose this over hiding REDO; saves the Song screen a third row). `tests/play-keys.test.js` guards all this. If it goes unused it can be removed.

## 9 Nov: pad PLAY matches Space, OPTION is ALT (build 537, same PR as the wording audit)

Owner's call after a pad-vs-keys comparison. On every input the same rule now holds through `playKey(shift)`: PLAY or Space plays
the song (on the Song screen, `playCtx`, so LOOP SEL's rows still loop), SHIFT+PLAY or Shift+Space plays this screen. This reverses
the 27 Oct note that the on-screen PLAY kept the pad's meaning. The pad's OPTION is labeled ALT to match the keyboard; the internal
name is still `opt`. Pad SHIFT+arrows (screens) and ALT+arrows (other phrase or block) were deliberately left as they are.

## 9 Nov: wording audit (build 536, branch `fix/wording-audit`, PR into `beta`)

The owner asked for an audit of awkward menu and notification text, then for the fixes. About 180 strings were rewritten and the
longest pop-ups tightened. Owner's naming decisions: American spelling in visible text (code identifiers and CSS stay as they
were); no dots on any name (Thunder, Thunder Plus, pump, chains, waves); say "desktop app", not "helper", to users;
"A voice per track" became "A kit slot per track" (the internal `vtrk` name is unchanged). Audio thread / Page stay as the
sequencer names. The installed app's productName was already Thunder Plus; `electron/package.json` only had its
descriptions and shortcut name changed. Write new user-facing text in plain sentences: say what is true now, no "as before", no colon-joined clauses.

## 9 Nov, end of session: what to test on the phone (build 530, on `beta`)

Nothing from builds 529–530 has been tried on a real Fold, only in emulation. Start a session by asking the owner for these.

**Most useful first:**
1. Play the song through twice with Arrange showing: does the stutter ease on the second pass? (That would mean sounds being prepared the first time they play.)
2. Playhead against the sound. If still off, try mastering off.
3. Tap an empty block in Arrange, enter a note, change instrument, press LOOP: it should stay and play.
4. Acid line on an empty track: lands in that block, audible, also soloed.

**Feel:** 5. arrows reach every column (FX numbers, INST, poly middle columns); 6. FOLLOW PATTERN while editing holds, Arrange pages about 4 s after letting go, selecting patterns doesn't fight; 7. PHRASE / ARRANGE ON STOP at STAYS keep the cursor (if it jumps to the top: did "Back to the top of the song" show? That's STOP tapped twice within 450 ms); 8. LEFT timer lands on the song's end after heavy LOOP use.

**Sounds:** 9. CUT / RES on a synth track, ATK / REL on a rendered sound (nothing on live engines yet); 10. RTG on a slot saved off switches it on; 11. Balanced and Safest on headphones (owner's next test): report any run that sounds quieter, with Copy report and which headphones.

**Look over:** pad layout (Save left of SOLO / MUTE), messages at the top, M.PAT, the resize bar by finger.

**Owner's decisions (9 Nov):** 4 s follow hold (will test); Save and MUTE unfolded only; a drag starting on a pad may sound it; a new RTG switches its slot on.

**Findings so far.**
- Stutter that stops under LOOP: the owner had Arrange showing, but the Arrange redraw is ruled out in emulation (4× CPU throttle: about 10 ms per pattern change; spikes only at PLAY). Next suspect: sounds prepared the first time a new pattern plays.
- Safest level drop: not Thunder. Build 529 reports, phone speaker: 4 s into PLAY Balanced −9.7 dB, Safest −9.2 and −7.8 dB (single peak readings). Output delay Balanced 55 ms; Safest 87–104 ms, but 302 ms in the build 527 report, so Android doesn't give the same path each time. Theory, unproven: the level depends on that path. Need a report from a run that sounds quiet.
- Audio clock: 1:1 while playing in these reports (Balanced and Safest), about 8% fast in the build 527 Safest report; still fast while hidden (8.43 s in 7.00 s, Balanced, after STOP). Cause still unknown.

## 9 Nov: the owner's phone bug list (build 529 on, branch `fix/mobile-list`, PR into `beta`)

Delegated to Sonnet agents, each diff reviewed by the lead: arrow columns (`move()` stepped the phrase twice), pad and title bar, Arrange and master, battery and retrigger labels.
Lead: playhead now takes off `moFxLat()` (master lookahead and PDC; the picture was ahead of the sound by it). FOLLOW holds `FOLLOW_HOLD` 4 s after a key or a press on grid, pad or Arrange (`workT`); a follow mode pages Arrange after that hold, FOLLOW OFF keeps its minute. `curToPlay` clears `.pg`. Empty cell beside Phrase: `phraseToCursor` (pendPlace), which is why acid lines and notes went into the last open phrase. LEFT: `songLenTab`, re-anchored on each row and LOOP repeat. Page sequencer: lead and re-anchor scale with output latency (Safest). ATK / REL appended to FXC. `liveCut` for CUT / RES on live engines.
After CodeRabbit: the ATK / REL state is `T.cAtk` / `T.cRel` (`T.rel` is the track clock); ATK uses the note's `fdi` fade, so stereo is kept. With ON STOP at STAYS, tStop puts the cursor back where FOLLOW found it (`fpHome`). A new RTG switches its slot on (`sw` 1), back as saved on release or RTG 00. Owner: the stutter was with Arrange showing and stops under LOOP; next look at what a pattern change redraws. Playback report logs the level 4 s into PLAY (compare Balanced and Safest).
**Not reproduced or not proven on a device:** note and instrument gone after LOOP (not reproduced stopped; suspected the empty-cell bug), the stutter that stops under LOOP (headless profile didn't separate them; `apPanel` layout read cached during playhead redraws), volume drop after switching to Safest (cause not found; the lead / re-anchor change addresses the stutter part only). Ask the owner to retest these, and what exactly was wrong with PHRASE / ARRANGE ON STOP.

## 8 Nov: dialog sliders on touch (build 515)

Sliders in `.ask` dialogs (every generator, Record a sample) couldn't be moved on a phone: CSS takes touches off sliders on
`pointer:coarse` and the shared drag skipped `.ask`. `sliderOk(r, touch)` now lets touch through for dialogs (a mouse keeps the
native slider), and `.acidr` / `.acidf` carry `touch-action:pan-y`, or the browser cancels the drag as a pan. Checked with
emulated touch drags on a Pixel 7 profile: no movement before, correct distance after, on Acid line and a generic `genPanel`.

## 8 Nov, end of session: where things stand (build 523)

- **Mobile hangs, partly fixed (build 516).** Owner's symptom on the Fold, Page sequencer, Render, Safest: stutter, slowdown, clicks, speed-up, a few
  notes after Stop, mostly when navigating. Waiting on his phone test: does it come with SHIFT+arrows or plain arrows, and is Arrange lighter? In
  simulation plain arrows were cheap and SHIFT+arrow screen changes were not, which doesn't quite match his report. Next: the per-screen cost of
  `go()` / `draw()`; on the phone, whether `PW.ps` is false (pump synths then render on the main thread under Render). Notes after Stop: suspected
  output buffer under Safest, not measured on a device; Stop kills voices at `currentTime`, not heard time.
- **Owner's phone test of 523 (8 Nov):** no difference between plain arrows and SHIFT+arrows, and it definitely hangs and stutters in the background (Page sequencer).
  So the simulation's arrows-vs-screens split doesn't hold on the Fold. Build 524 keeps the old 0.5 s re-anchor while hidden, since 516's 0.15 s could add gaps to a hidden page's bursts.
  Background trouble is older (31 Oct, and 6 Nov's 0.83× measurement). Headless can't reproduce it; the next evidence is the Playback report from the phone.
- **Owner's playback report, build 523:** with the app hidden, `ctx.currentTime` got ahead of `performance.now()` by 1.41 s in 30 s and by 12.85 s in 26 s, and never while visible; the context stayed running.
  Either the page clock stopped (a sleeping CPU) or the audio clock ran fast (dropped output). Build 525 adds a `wall=` (Date.now) to each report line and, at each screen change, the Page tick count, longest gap, least lead and re-anchors, to tell which.
  Stop: voices 0 and stray rows 0, level −21 to −26 dB 1 s after, −48 to −52 dB at 3 s, with output latency 183 ms under Safest, so the spill looks like buffer plus effect tails.
- **Report from 525 settles it (8 Nov, phone, latency 21 + 43 ms, Page):** hidden for 15.38 s of wall clock (`performance.now` agrees), `ctx.currentTime` advanced 23.62 s; then 3.46 s visible, it advanced 19.98 s, until STOP, after which it was back to 1:1. Page ticks were fine throughout (longest gap 0.05–0.07 s, least lead 0.08–0.21 s, no re-anchor). So **the sequencer isn't starved; the audio clock runs ahead while playing**, and the song follows it, which is the speed-up. Likely (not proven) Chrome rendering extra blocks to recover from underruns on an overloaded audio thread, so the clock counts audio that was never heard. Next: the owner's test with a very light song (is it CPU?), `AudioContext.playbackStats` in the report if this Chrome has it, then lighter processing or a resync when the clock races.
- **Simple-song reports and code audit (8 Nov):** it isn't song weight: a very simple song raced too (+7 s in 17 s hidden; +16.4 s in the 1 s after STOP, silent). One run was mild, so it comes and goes. Audit found nothing in the app touching the context while playing (`bgIdle` is the only live `ctx.suspend`, and it's busy-guarded); `bgEl` (Background play) is a separate 8 kHz stream, never connected to the graph, and could only affect Android's session. Why Chrome's clock races is unknown (burst-draining output, stream restart/fallback, or discarded render).
  Build 528 logs, when the clock moves over 0.1 s from real time, `getOutputTimestamp`, latencies, sample rate, `playbackStats` and `bgEl`. Read it this way: `bg` time racing too → Android/HAL; output `t` or `total` at 1:1 while `currentTime` leaps → render side; both leap → sink draining; `underrun` growing → starved render thread.
  Toggle test wanted: simple song hidden ~20 s with Background play off, then Keep screen on off. A defensive change is sketched but not built: phones only (`seqHandHere()`), when clock minus wall jumps over 0.3 s, shift `s.time/base/btT` forward by the jump and drop `tp.ahead` to 0.25 s; notes already booked can't move and would start at once. Build it only if the log shows it can help.
- **Mobile setting found off (8 Nov).** The owner's phone showed the desktop top bar. Nothing that day touches the setting (`platform` in localStorage,
  default only if nothing is saved). Cause unknown; if it recurs after an update, look at storage.
- **CodeRabbit on 515–522** (review-only PR #30, closed): two findings, both real and fixed in 523: the pad's MUTE muted picked Song blocks instead
  of the track and stayed unlit (`muteKey(true)` is track only), and the stale 1.3.1 in the helper's next-steps above. Its docstring warning was ignored.
  Reviews are skipped on PRs into `beta` and refused on closed PRs, and a new push to the head cancels one in progress, so for a merged run use a
  review-only PR from a snapshot, as #30 did, and don't push until it finishes.
- **Publishing:** `beta-web.yml` fails if `main` moves while it runs. Wait for it after merging into `beta`, before merging `beta` into `main`.

## 8 Nov: pad MUTE and SOLO (build 519)

Owner's request: a new column before SEMI+/GEN. `press` calls `muteKey()` / `soloKey()`, so a second tap undoes it, and each button lights for
the current track (set in `draw`). Build 520: only when the pad's container is 600px or wider (folded keeps its old layout); pink / green off, black on.

## 8 Nov: SHIFT+▲ goes to Arrange on Mobile (build 518)

Build 526 also: Mixer OPTION+arrows change track, as FX does (OPTION+▲▼ on the FX row still turn effects on and off, owner may want that changed). Build 526: with the Arrange panel showing, SHIFT+▲ gives it the keys (`apFocus('arr', true)`) and stays on the screen; otherwise as below. Owner's request: with the Mobile setting on, SHIFT+▲ calls `go('song')` from every screen (Project stays on SHIFT+◀▶ and its tab).
Desktop keeps Project / back. Build 517 added ` · beta` beside the build number on the beta copy.

## 8 Nov: mobile stalls during playback (build 516; partly fixed)

Owner: stutter, slowdown, clicks, then rushing on the Fold, on the Page sequencer too. Profiling a Pixel 7 profile at 4x CPU throttle
put most main-thread time in `drawArr` on every played row. On touch screens (`COARSE`) a playhead-only Arrange redraw now runs
about 4×/s, `isSendT` asks `busPlan` once per `drawArr` (`busPlanM`), and the page sequencer re-anchors at 0.15 s behind instead of
0.5 s. Measured, SHIFT+arrow screen changes while playing, 2 runs each: notes booked late 18 and 25 → 4 and 7. **Not fixed:** the
screen changes themselves are still long tasks (6–8 s of 10 s at 4x). Next: what `go()`/`draw()` costs per screen. Unmeasured on a real
phone. Notes heard after Stop are suspected to be the output buffer under Safest (Stop kills voices at `currentTime`, not heard time).

## 8 Nov: FLY label and same-row order (build 515)

The FX bar capped the Flex param at 255, so every FLY showed as `FLX FF`. FLX and FLY on one row now follow column order: the
rightmost plays (owner's go-ahead; the sequencer used to emit FLX then FLY, so FLY always won).

## 8 Nov: instrument big step can't overshoot (build 515)

EDIT+▲▼ (and the other big-step routes through `bump`) on an instrument now does nothing if the 8-step would pass 00 or the top,
instead of clamping: a slip from ▶ onto ▼ was sending low instruments to 00 (owner's choice). Single steps still stop at the ends.

## 8 Nov: …waves live per-bus running; "Live engines" setting (build 513, beta only)

Owner: make the live/rendered switch's name cover …waves live, and cut phone CPU by sharing one running patch across pads.
- **Sharing a patch was NOT done, on purpose:** every voice sums into every Out, so one shared WavesEngine would play pad A's
  notes through pad B's bus too (doubled or wrong sound). Separating them needs per-voice output, and mono sections after the
  voices can't be split at all.
- **Done instead, same saving when pads use different buses, same sound:** `WavesEngine.setOnly(slot)` / `_cull()` (waves-worklet.js,
  outside the shared block): walks back from the bus's Out modules and runs only those (`only`; null runs everything, the
  default, so the editor, renders and goldens are untouched). WavesLiveProc calls it from `busOf()` at start and on `{set}`;
  `_cullDirty` is set by `_sync`, `loadPatch` and a `slot` setParam. Always kept: `def.playsNotes` (the Sequencer, which calls
  noteOn with no cable). Runs everything if an Out's slot is modulated, or if no voice audio exit would be left (voice sleep).
  Two pads on the SAME bus still run two copies; idle copies already sleep after 2 s.
- Measured in Node: 3 pads on 3 buses (Plaits + VCO + reverb each), one held note each: 14.0 s → 4.9 s of CPU for 8 s of audio.
  Not measured on a phone.
- CodeRabbit on PR #17 (review-only, base `review/base-513`; it needs `@coderabbitai review` off the default branch), fixed in build 514: the row always shows (it was hidden without PsynCore), text names all …pump live engines, a test for the no-exit fallback. Owner wants future Thunder Plus changes to go through PRs.
- Settings → Audio row is now **Live engines** (`aset.psLive` unchanged, so saved settings carry over); tooltips and the fallback
  toast name …waves live. `waves-worklet.js?v=34`, cache `thunderplus-v513`. Tests in tests/waves.test.js.

## Older entries: in the archive

Everything dated before 8 Nov 2026 (and the older plan sections, such as "Possible future work: automation lanes", "search and tidier groups in the sound and effect pickers", "Plan: live performance mode" and "3 Nov (48): sharing songs between devices") is in [`docs/HANDOVER-ARCHIVE.md`](docs/HANDOVER-ARCHIVE.md), word for word. A reference above to an entry "below" with an earlier date means that file: search it for the heading. To archive again later: move whole `## ` sections older than about a week to the top of that file's entries, keep the three standing sections below here, and leave this pointer in place.

## Keep doing with every build: What's new

- `CHANGES` (beside `whatsNew`, after `welcome`) lists what changed, newest first: `[build, date, [lines]]`. Add a line
  for every build someone would notice, in plain words, when bumping `BUILD`. After an update the app shows the entries
  newer than the build this device last ran (`thunderplus-seen-build`) once; a first run shows the welcome instead (a
  device that had run the app before the list existed sees the 30 Oct entries); Help's header has What's new.

## Licensing rule (the owner may sell the app later)

Only **MIT / BSD / ISC / zlib / Apache** code goes in. **Never GPL/LGPL**: no Surge, Vital, Dexed, Helm,
OB-Xd, Odin2, ZynAddSubFX, Dragonfly or Calf, and not the DaisySP-LGPL add-on (DaisySP's main library is MIT;
its comb filter and ReverbSc are LGPL, which is why the comb resonator is our own code). Mutable Instruments'
STM32 modules are MIT; its AVR ones are GPL. Every addition gets:

1. a credit in `THIRD_PARTY_NOTICES.md`;
2. a **generic name** in the UI, with "based on X" in its credit (FX_CREDIT in `index.html`, help text
   in `chains/index.html`). Product names are trademarks even when the code is MIT.

Verified this session: Beads is MIT in upstream `pichenettes/eurorack`, and so is Cloud Seed Core. The VST 3
SDK is MIT since 3.8 (Oct 2025), for a future VST port; iPlug2 or DPF are permissive frameworks; JUCE needs
a licence.

## Conventions worth knowing

- **Ports/cables:** audio = teal, `cv`/modulation = purple dashed, `gate` = pink dashed. Any slider
  has a purple modulation jack; a cable into `p:<param>` modulates it.
- **Adding a …chains module:**
  1. Define it in the worklet and register it in `MODULES`.
  2. Mirror it in the editor's `MODULES` table with identical `params`, plus `ui`.
  3. Add a `PALETTE` entry and help text in `PAGE_ABOUT` (top level since 21 Oct; shared modules use `SHARED_ABOUT` or
     their `about`), and a one-line example in `MODULE_USE` (the shared block, then `python3 tools/sync-shared.py`).
  4. Bump `chains-worklet.js?v=` and the `sw.js` cache.
  5. Use `fx:true` only for `tfx.wasm` effects.
- **`README.md`:** the top part is Thunder Plus's; the rest is …Thunder's (the base app).
- **Two engines:** `waves/waves-worklet.js` (from the waves repo) and `chains/chains-worklet.js`.
- **Mobile vs Desktop:** anything that should differ uses `isMobile()` in the page; there is no second build.
- **Engine block:** code that renders pads goes between the `===ENGINE===` markers, with no DOM, `window` or
  `localStorage` at top level, because the render Worker runs that block as-is.
