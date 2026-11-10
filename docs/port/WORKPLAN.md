# Native port: the work plan (for the session that manages it)

Written 10 Nov 2026 by the lead (Fable) for the owner, who wants to hand the port to a managing session, possibly a
less capable model (Opus), that calls on Fable for the heavy work. **Nothing here starts until the owner says so**
("start the native spike" starts stage N0 only). Why and what: [`../NATIVE-PORT-STRATEGY.md`](../NATIVE-PORT-STRATEGY.md).
Facts this plan is cut from: [`ENGINE-INVENTORY.md`](ENGINE-INVENTORY.md) (what there is to port, units U01–U37),
[`BRIDGE-TODAY.md`](BRIDGE-TODAY.md) (today's messages), [`CORE-BRIDGE.md`](CORE-BRIDGE.md) (the new contract).

## 1. The one rule that protects the owner's music

**The web app on `beta` / `main` keeps working exactly as it does, the whole time.** The owner makes music with it.

- All port work happens on the long-lived branch **`native`** (made from `beta` when N0 starts). Merge `beta` into
  `native` at the start of every session; never the other way without the owner.
- New code lives only in new folders the web app never loads: `native/core/` (C++), `native/android/`,
  `native/desktop/`, `native/ios/`, `native/tests/`, `native/tools/`. No edits to `index.html`, `core/`, `chains/`,
  `waves/`, `dsp/`, `electron/`, `io/`, `link/` on `native`, with two exceptions: task B3 (the `thunderCore` switch,
  off by default) and test-only additions.
- **Test-only work that helps the web app goes to `beta` by the normal PR route** (the golden harness U01, new parity
  goldens): it makes today's app safer and the port needs it there.
- Bugs found in the web app while porting: write them under "Bugs still open" in `HANDOVER.md` on `beta`; do not fix
  them on `native`.
- The owner tests only at stage exits. No stage exit is declared by opinion: each has numbers (section 5).

## 2. Who does what

| Role | Model | Does | Never does |
|---|---|---|---|
| Manager | Opus (or Fable) | Keeps this plan's status table, writes briefs from the template (section 6), spawns workers, runs the gate commands, merges PRs into `native`, reports to the owner at stage exits | Judges audio quality or real-time safety by reading; changes a gate; skips a failed gate |
| Worker | Sonnet | One unit per session: translate, build, make the gate pass | Touches anything outside its unit's folders; edits goldens to make a test pass |
| Audio lead | Fable | Designs marked **F** below; reviews every PR marked **F-review** line by line (real-time safety, timing, numerics); decides when a gate itself is wrong | |
| Chores | Haiku | Branch sync, CI reruns, release notes, status table updates | |

How the manager calls Fable: spawn an agent with `model: "fable"` if the session's agent tool offers it (untested from
an Opus session); otherwise add the item to "For Fable" at the end of this file and tell the owner, who opens a Fable
chat with: "Pull thunder-plus, branch native, and clear the For Fable list in docs/port/WORKPLAN.md". A spawned agent
starts cold: the brief must name the files to read.

**Because the manager may be less capable than the work it manages, every gate is a command with a pass / fail
result.** If a worker says "done" and the gate command fails, it is not done. If a gate cannot pass and the worker
argues the gate is wrong, that is a "For Fable" item, not a manager decision.

## 3. Rules for every unit (put these in every brief)

1. Read the unit's rows in `ENGINE-INVENTORY.md` and the JS they point at. The JS is the specification: port
   behaviour, including its quirks; do not improve the sound. A suspected bug in the JS is reported, and ported as is.
2. C++17, no framework types in `native/core/` headers, no exceptions or RTTI on the audio path. On the audio thread:
   no allocation, no locks, no file or log I/O (NATIVE-DESKTOP-PLAN 4.1). Each unit adds its files to the no-allocation
   test (task A5's harness runs the audio callback under an allocator that aborts).
3. Parity before anything else: write (or extend) the golden from the JS first, commit it, then port until the C++
   matches. Tolerance is the gate's, normally exact for event streams and −80 dB for audio.
4. One unit, one PR into `native`, with the gate's output pasted in the PR. `@coderabbitai review` on each.
5. `index.html` is 3.2 MB with very long lines: never read it whole; grep with bounded context.
6. Budget each worker (tool calls, and "stop and report after N failed attempts"). A worker that produced nothing in
   an hour is stopped and the unit is cut smaller (8 Nov's lesson: a four-hour agent that produced nothing; the
   second, with a two-step brief, took four minutes).
7. Licences: only permissive code in `native/core/`. LXR is GPL-3.0-only and stays a separate optional module (U04).
   `pump` sources come from the owner's `matthettich/pump` repo (U03: ask the owner how to bring them in: submodule
   or copy). Pin every upstream commit.

## 4. Order of work

Unit numbers (U..) are defined, with their inventory rows and done-tests, in `ENGINE-INVENTORY.md` "Suggested port
units". Tasks A, B and H are new here. **F** = Fable designs or builds; **F-review** = Sonnet builds, Fable reviews
line by line; no mark = Sonnet builds, the gate decides, manager merges.

### Stage N0: spike, the go / no-go gate (owner says "start the native spike")

| Task | What | Who | Gate |
|---|---|---|---|
| A1 | Make branch `native`; `native/` skeleton; CMake builds an empty `thunder_core` static library for the host machine | Sonnet | `cmake --build` green in CI on Linux |
| A2 | CI targets: wasm (wasi-sdk as `dsp/build.sh`), Windows x64, Mac universal, Android arm64 (NDK); artefacts uploaded | Sonnet | all four jobs green on a push to `native` |
| A3 | Android shell v0: Kotlin app, WebView loading the real `index.html` from app assets, miniaudio (already in `io/vendor`) or Oboe opening the output; a C++ test core that plays a sequenced sample pattern at a set tempo from its own sample clock; foreground service | Sonnet, **F-review** of the audio callback | Debug APK attached to a GitHub pre-release; installs and makes sound on the Fold |
| A4 | Bridge transport proof on Android: `thunderCore.send` / `on` round trip at 60 messages a second for 5 minutes with no loss; `put` of a 53 MB buffer by both candidate paths, timed | Sonnet | a results table in `native/tests/RESULTS-N0.md`; Fable picks the path |
| A5 | Real-time test harness: runs the core's callback offline under an allocator that aborts on allocation, and a clock-drift logger (frames rendered vs wall time, underruns) the app shows on a debug screen | **F** | harness fails on a deliberate `new` in the callback; passes on the test core |
| A6 | Owner test on the Fold: 5 minutes screen on, 5 screen off, 5 in the background, with A5's logger | Owner | **Clock reads 1.00 s in 1.00 s (±0.1 %) in all three, 0 underruns after the first second; output latency recorded.** If not: stop the port and report |
| A7 | Usage census: a Node tool that reads the owner's `.thunder` files and counts which engines, layer types, effects, pump types, chains / waves modules and commands they use | Sonnet | a table in `native/tests/CENSUS.md`; the manager reorders units inside a stage by it (most used first) |

**Owner decision at the end of N0: go or stop.** Also confirm: Android before desktop; permissive parts, not JUCE.

### Stage N1: the playback core (pads still rendered by the page)

| Step | Units | Who | Gate (besides each unit's own done-test) |
|---|---|---|---|
| 1 | U01 golden harness for engines (to `beta`), U02 build of existing C++ with pinned upstreams, U03 pump sources, U04 LXR module | Sonnet | `npm test` on `beta` fails on a one-bit change of any factory voice; CMake wasm hashes equal today's `tfx.wasm` |
| 2 | B1 bridge design review against N0 results; B2 freeze `BRIDGE_V = 1` in `CORE-BRIDGE.md` | **F** | document change only |
| 3 | U05 core runtime (callback contract, queues, snapshot swap, `Auto`) | **F-review** | C++ port of `tests/core-auto.test.js` passes; A5 harness green |
| 4 | U06 song / kit reader | Sonnet | loads `sounds/ELECTRO.thunder` and every test song; unknown fields round-trip |
| 5 | U07 sequencer | Sonnet, **F-review** | the four `seq-electro*.json` goldens match exactly |
| 6 | U08 sampler voices, U09 mixer, U10 latency compensation | Sonnet, **F-review** each | renders within −80 dB of today's for ELECTRO and two census songs |
| 7 | U11 master, U12 master EQ and safety limiter, U13 meters | Sonnet, **F-review** U11–U12 | `tests/golden/master.json` within −80 dB; EQ within 0.01 dB |
| 8 | U14 track-effect host, U15 tempo-locked effects, U18 pump hosting | Sonnet, **F-review** U14 | effect hashes match; pump parity passes |
| 9 | U16, U17, U19, U20, U21: the JS effects (28 of them live in the shared …chains / …waves block), in census order | Sonnet | golden entries match. An effect not yet ported is listed absent in `caps.fx` and shows "bypassed in this build" |
| 10 | U34 (song / stems render through the core; pad renders stay with the page), U35 MIDI scheduling | Sonnet, **F-review** U35 | rendered ELECTRO WAV within −80 dB; `midi-ledger` cases pass on a fake port |
| 11 | B3 / U37: `window.thunderCore` on the web copy (core.wasm in the worklet) behind Settings › Audio › Engine: Native core (test), **default off** | Sonnet, **F-review** | `seq-golden --core` equal across Page, Audio-thread and core.wasm; `npm test` on `beta` unchanged with the switch off |

**N1 exit (numbers):** every gate above green in CI; A5 harness green over the whole core; ELECTRO and three census
songs render within −80 dB through `core.wasm`; CPU of core.wasm on the reference song no higher than today's engine in
headless Chrome. Then the owner listens to three of their songs with the switch on (desktop, web copy).

### Stage N2: Android beta

| Task | What | Who | Gate |
|---|---|---|---|
| H1 | Android shell: bridge per `CORE-BRIDGE.md`, audio out, device change and headphones, audio focus (calls, other apps), foreground service + notification with Play / Stop | Sonnet, **F-review** of the audio and focus code | BrowserStack run installs and plays ELECTRO; A5 logger 1.00 ±0.1 % for 10 minutes |
| H2 | MIDI through AMidi (USB, Bluetooth), mapped onto `midi.cfg` / `midi.in` | Sonnet | loopback test on a device with a USB MIDI interface (owner) |
| H3 | Songs / samples folder (Storage Access Framework, permission kept), `.thunder` open from a file manager, newest-first list, changed-underneath warning | Sonnet | open, edit, save, reopen on desktop: identical song JSON |
| H4 | Signed release APK from CI, in-app new-build check against GitHub Releases | Sonnet / Haiku | install over the previous build keeps songs and settings |
| H5 | The Fold: fold / unfold, back gesture, keyboard, screen-off behaviour of the web view (interface may stop; audio must not) | Sonnet | checklist in `native/tests/ANDROID-CHECKLIST.md` ticked on BrowserStack, then by the owner |

**N2 exit:** the owner's normal song with effects plays 10 minutes screen-off on the Fold with no rush, no gaps and 0
underruns; battery drain per hour no worse than the web app on the same song (owner measures both).

### Stage N3: live engines, the editors' engines, pad rendering in the core

U22–U25 (engine foundation and layer types), U26–U27 (bass, vox, acid, machine, blip; live and rendered), U28–U29
(rendered-only engines), U30 …waves engine, U31 …chains engine, U32 SoundFont player (decoder licence check first),
U33 pump synths and LXR glue, the rest of U34 (pad renders in the core), U36 Link / recording / inputs.
Sonnet builds each; **F-review** on U26, U30, U31, U36 (live paths and graph runners). Order inside by the census.
**N3 exit:** U01's goldens pass for every factory voice from the C++ core; no JS DSP is loaded when the switch is on;
`caps.engines` and `caps.fx` list everything; then, and only with the owner's word, the switch becomes the default on
the web copy and the JS engine is deleted in a separate PR.

### Stage N4: Windows and Mac shells

| Task | What | Who | Gate |
|---|---|---|---|
| H6 | Shell skeleton: WebView2 (Windows) / WKWebView (Mac), the app served from `app://thunder/`, `thunderCore` bridge, `window.thunderDesktop.*` with today's 31 channels (BRIDGE-TODAY section 4) | Sonnet | every channel answers; `tests/sweep/menus.mjs` equivalent passes in the shell |
| H7 | Audio and MIDI: move `io/src` device, clock and MIDI code in; Settings › Audio (driver, device, buffer, rate, channels); WASAPI shared and exclusive (ASIO only if free to use) | Sonnet, **F-review** | A5 logger 1.00 ±0.1 % for 30 minutes at 128 and 256 frames; MIDI loopback jitter recorded |
| H8 | Files, single instance, `.thunder` association, updater (Sparkle / WinSparkle or the current GitHub check), Mac menu, signing (Apple Developer membership needed here) | Sonnet / Haiku | NATIVE-DESKTOP-PLAN phase 4 parity checklist ticked |
| H9 | WKWebView pass: every screen on a Mac, pointer / keyboard / fonts / canvas differences listed and fixed behind feature checks | Sonnet | `tests/sweep/layout.mjs` and `keys.mjs` equivalents pass in WKWebView |

**N4 exit:** the checklist ticked on the PC and a Mac, the owner has made music on it, then Electron is retired
(owner's word).

### Later: N5 (plugin hosting, audio tracks, the synth-engine plugin) and N6 (iOS) get their own plans when reached.

## 5. Gate commands (the manager runs these; a stage is not done until they pass)

To be created by the named tasks; until a command exists, the stage that needs it cannot exit.

| Command | Made by | Proves |
|---|---|---|
| `npm test` (on `beta` and on `native`) | exists | the web app is unchanged |
| `node tools/seq-golden.mjs` and `--core` | exists (needs headless Chrome) | sequencer event streams equal |
| `native/tools/parity.sh <unit>` | U01, extended by each unit | the unit's C++ output equals its JS golden within tolerance |
| `native/tools/rt-check.sh` | A5 | no allocation or lock in the audio callback |
| `native/tools/render-compare.sh <song>` | U09 | a whole-song render within −80 dB of today's |
| the debug screen's clock log | A5 | 1.00 s in 1.00 s on a device |

## 6. Brief template (the manager fills one per unit)

```
You are porting one unit of Thunder Plus's sound engine to C++. Repo /home/claude/thunder-plus, branch native
(run: git fetch origin && git checkout native && git merge origin/beta). Work only in native/.
UNIT: <U.. name>. Read first: docs/port/ENGINE-INVENTORY.md rows <…>; docs/port/WORKPLAN.md section 3 (the rules);
<CORE-BRIDGE.md sections, if the unit touches the bridge>.
SOURCE OF TRUTH: <files and function names>. index.html is 3.2 MB: never read it whole, grep with bounded context.
DO: 1) extend the golden for this unit from the JS and commit it. 2) Port. 3) Make the gate pass.
GATE: <exact command> must print PASS. Paste its output in your report.
DO NOT: change files outside native/ (except the golden under tests/golden via its tool), edit a golden to match
your output, change behaviour you think is a bug (report it instead), push, or open a PR.
BUDGET: <N> tool calls. After <M> failed gate runs with no progress, stop and report what differs and where.
REPORT (under 300 words): commit hashes, the gate output, anything ported "as is" that looks like a bug, anything
you could not port and why.
```

## 7. Status (the manager keeps this current)

| Stage | State | Notes |
|---|---|---|
| N0 | not started: waiting for the owner's "start the native spike" | |
| N1 | not started | U01 may start early on `beta` if the owner wants better engine tests now |
| N2 | not started | |
| N3 | not started | |
| N4 | not started | |

## 8. Known gaps in this plan (found by the inventory; settle before the stage that needs them)

- `GAIN-STAGING.md` predates latency compensation and the bus plan: U09 / U10 must take the code, not the document, as
  the specification, and update the document.
- The engines have no golden renders today: U01 comes before any engine or effect port.
- `core/seq-core.js` is generated from `index.html`; the sequencer goldens need headless Chrome, so `npm test` alone
  does not prove sequencer parity.
- A sample layer still loading from IndexedDB at save time is written as a reference only; whether Save waits for it
  was not verified. Check before promising cross-device songs (H3).
- SoundFonts are not in the `.thunder` file (the GM font is a 24 MB app file; user fonts live in IndexedDB): U32 and
  H3 must say how a song finds its font on another device.
- The strategy's N1 size (6–10 weeks) was set before the inventory: about 17,000 lines of JS DSP are to be ported,
  6,150 of them in the …chains / …waves block whose 28 effects N1 also needs. Re-estimate after N1 step 3, from
  the real time the first five units took.

## 9. For Fable (items waiting for the audio lead)

- (none yet)
