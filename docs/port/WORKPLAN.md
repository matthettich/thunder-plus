# Native port: the work plan (for the session that manages it)

Written 10 Nov 2026 by the lead (Fable) for the owner, who wants to hand the port to a managing session, possibly a
less capable model (Opus), that calls on Fable for the heavy work. **Nothing here starts until the owner says so**
("start the native spike" starts stage N0 only). Why and what: [`../NATIVE-PORT-STRATEGY.md`](../NATIVE-PORT-STRATEGY.md).
Facts this plan is cut from: [`ENGINE-INVENTORY.md`](ENGINE-INVENTORY.md) (what there is to port, units U01–U37),
[`BRIDGE-TODAY.md`](BRIDGE-TODAY.md) (today's messages), [`CORE-BRIDGE.md`](CORE-BRIDGE.md) (the new contract).

## 0. Start here (a session that has never seen this project)

**What this is, in four lines.** Thunder Plus is the owner's music app: one big web page (`index.html`) that is a
tracker, synths and effects, used on a Galaxy Z Fold 6 and on Mac / Windows (an Electron shell). On the phone, Chrome's
audio clock misbehaves whenever an effect is loaded, and no patch can fix it. The port moves the sound engine into one
C++ library that the same interface talks to from thin native apps. The interface is not rewritten.

**Lines the owner can paste to start a session:**
- Manager (Opus): "Pull thunder-plus. Read docs/port/WORKPLAN.md sections 0 to 3 and its status table, then act as the
  manager for the next unfinished task. Follow the session loop in section 0."
- Audio lead (Fable): "Pull thunder-plus, branch native, and clear the For Fable list in docs/port/WORKPLAN.md."
- To begin the port at all: "start the native spike" (stage N0; nothing before that phrase).

**Read, in this order, and no more than this before acting:** (1) this file, sections 0–3 and 7; (2) the stage you
are on in section 4 (for N0 also section 10, where each task is written out); (3) for a unit, its row in `ENGINE-INVENTORY.md` "Suggested port units" and the rows it names;
(4) `CORE-BRIDGE.md` only if the task touches the bridge. `HANDOVER.md`'s To-do section has the owner's standing
decisions about the app itself; the strategy document is background, not instructions. `CLAUDE.md` says to read
`HANDOVER.md` first: for a port session that means its To-do section only (the file is long and mostly about the app).
Where `ENGINE-INVENTORY.md` says `core/` for new C++ (unit U02), it means `native/core/`; the existing `core/` folder
is the app's JavaScript and is never edited.

**Before the very first task (A1), check two things.** (a) `git show origin/beta:docs/port/WORKPLAN.md` works: the
plan must be on `beta` before `native` is cut from it; if it is not, stop and ask the owner to merge the plan's PR.
(b) You have a tool that spawns agents. If you do not: do an unmarked or **F-review** task yourself by following its
brief exactly (then ask for the review); an **F** task goes to section 9 either way.

**Where things live once `native` exists.** `docs/port/` (this plan, its status table, the bridge spec, results) is
edited on `native` only; `beta` keeps the copy from the day the branch was cut. `HANDOVER.md` is edited on `beta`
only. The files a port session may change outside `native/` are exactly: `docs/port/**` and
`.github/workflows/native*.yml` (new workflow files; never an existing workflow). Anything else outside `native/`
needs the owner.

**Who pushes.** Workers commit in their worktree and never push. The manager creates and pushes `native`, pushes each
unit's branch, opens every PR, and reads CI. In these sessions `gh pr …` commands fail (GitHub's GraphQL is blocked):
use the REST form, for example `gh api repos/matthettich/thunder-plus/pulls -f title=… -f head=… -f base=native`,
`gh api repos/matthettich/thunder-plus/actions/runs?branch=<branch>` for CI, and
`gh api repos/matthettich/thunder-plus/issues/<n>/comments -f body="@coderabbitai review"`.

**The manager's session loop (one task or one unit per session, started fresh):**
1. `git fetch origin && git checkout native && git merge origin/beta` (N0 task A1 creates `native`; before that, work
   from `beta`). Run `npm test`: it must pass before anything else. If the merge conflicts, stop and report: nothing
   on `native` should touch the app's files.
2. Read the status table (section 7). Take the first task that is not done and whose prerequisites are done. Never
   start a stage whose previous stage has not exited.
3. If the task is marked **F** (Fable designs or builds): do not attempt it. Spawn a Fable agent with the brief, or add
   it to section 9 and tell the owner. Stop there for that task; pick another only if it does not depend on it.
4. Otherwise write the brief (units: the template in section 6; N0 tasks: the wrapper at the top of section 10 around
   the task's own text), spawn
   one Sonnet worker in its own worktree, and wait. One worker per unit; at most two units at a time, and only when
   they touch different folders.
5. When the worker reports: run the gate command yourself. PASS: open the PR into `native`, comment
   `@coderabbitai review`, and if the task is **F-review**, get the Fable review (as in step 3) before merging. FAIL:
   send the worker back once with the gate's output; a second fail goes to section 9 with what differs.
6. Merge only when the gate passes, CodeRabbit's real findings are fixed, and any F-review is done.
7. Update the status table, add one dated line to `HANDOVER.md` on `beta` (what landed, what is next, what waits on
   the owner), and stop. Do not roll into the next task in the same session.

**When to stop and ask the owner (and nothing else needs asking):** a stage exit; a gate that Fable says is wrong;
anything needing money, an account or a device (section 11); a choice listed as open in section 12; any wish to
change a file outside `native/`.

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
| Worker | Sonnet | One unit per session: translate, build, make the gate pass. On `native`, inside `native/`, a Sonnet worker may write audio-thread code: the gate and, where marked, the Fable review are what make that safe. (The rule in `HANDOVER.md` that cheaper models never change audio code is about the app on `beta` and still holds there) | Touches anything outside its unit's folders; edits goldens to make a test pass |
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
   or copy; a submodule keeps one copy). Owner, 10 Nov: asked whether the pump VSTs could just be copied over. The
   built VST files cannot (desktop-only binaries; a song would not play on the phone), but the pump DSP is already C++
   and is compiled in as it is: nothing in pump is ported, only the hosting glue around it (U18). Pin every upstream
   commit.

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
| A8, A9 | Checks written out in section 10: libremidi's Android / iOS backends; whether a saved song always carries its samples | Sonnet | findings in `native/tests/RESULTS-N0.md` |
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
**Feedback cables (web build 589):** a cable with `fb:true` is left out of the graph sort and read one block late through the guard `fbAdd` (untouched in −1..1, `1 + tanh(|x| − 1)` above, non-finite → 0); the C++ graph runners must do exactly the same (`tests/feedback.test.js` is the reference).

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
| N0 | not started: waiting for the owner's "start the native spike" | A1 ☐ A2 ☐ A5 ☐ A3 ☐ A4 ☐ A7 ☐ A8 ☐ A9 ☐ A6 ☐ (tick each with its PR number) |
| N1 | not started | Steps 1–11 of section 4; list each unit here with its PR as it lands. U01 may start early on `beta` if the owner wants better engine tests now |
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

## 10. Stage N0, written out (so the first sessions need no design work)

**Brief wrapper for an N0 task** (the manager sends this with the task's paragraph below pasted in as TASK):

```
You are building one task of the Thunder Plus native spike. Repo /home/claude/thunder-plus, branch native
(git fetch origin && git checkout native && git merge origin/beta; npm test must pass before you start).
READ FIRST: docs/port/WORKPLAN.md sections 1, 3 and the top of section 10 (layout and C API)<, and CORE-BRIDGE.md
sections 2 and 4 for A4 / A5>.
TASK: <the task's paragraph from section 10, whole>
YOU MAY CHANGE: native/**, docs/port/** results files named in the task, .github/workflows/native*.yml. Nothing else.
DO NOT: read index.html whole (3.2 MB), edit any existing workflow, add a dependency that is not pinned, push,
or open a PR. Commit in your worktree on a branch named n0/<task>.
LOCAL CHECK: whatever of the gate can run here (cmake configure / build / ctest for A1, A5; node for A7). The CI part
of the gate is checked by the manager after pushing.
BUDGET: <the task's number> tool calls. After 3 failed attempts at the same step, stop and report.
REPORT (under 250 words): commits, local check output, what the manager must verify in CI, anything unclear.
```

Builds happen in GitHub Actions, not in the session's sandbox (no Android SDK there, and package installs may be
blocked): a task is "built" when its CI job is green and its artefact is attached. Folder layout from A1:

```
native/
  CMakeLists.txt            top-level; options THUNDER_BUILD_TESTS, THUNDER_WITH_LXR (off)
  core/include/thunder/     public C API (core.h): create, destroy, process(float** out, int frames), post(msg), poll(evt)
  core/src/                 the core; one folder per unit as they land (seq/, voice/, mix/, master/, fx/, engines/ …)
  core/third_party/         pinned upstreams (miniaudio and libremidi are reused from io/vendor by path, not copied)
  tests/                    C++ tests, RESULTS-N0.md, CENSUS.md, checklists
  tools/                    parity.sh, rt-check.sh, render-compare.sh, census.mjs
  android/                  Gradle project (app/, src/main/cpp/ JNI glue, assets/ filled by a copy step from the repo root)
  desktop/  ios/            empty until N4 / N6
```

The core's public surface is a small **C** API (not C++ classes) so Kotlin / Swift / wasm bind to it the same way:
`thunder_create(sample_rate, max_block)`, `thunder_process(core, out_l, out_r, frames)`, `thunder_post(core, bytes, len)`
(a command, JSON as UTF-8 for N0), `thunder_poll(core, buf, cap)` (next event or 0), `thunder_put(core, id, meta,
data, frames)`, `thunder_destroy(core)`. `thunder_process` is the only function called on the audio thread.

**A1. Branch and skeleton** (Sonnet, about 30 tool calls). Create `native` from `beta`. Add the layout above with a
`thunder_core` static library whose `thunder_process` writes silence, one test that calls it, and
`.github/workflows/native.yml` running CMake configure, build and `ctest` on `ubuntu-latest` for pushes to `native`
and PRs into it. Gate: the workflow is green. Do not touch any existing workflow.

**A2. CI targets** (Sonnet, about 50). Extend `native.yml`: wasm with wasi-sdk (download a pinned wasi-sdk release in the job; `dsp/build.sh` shows the compiler flags the
repo's wasm is built with but does not fetch the SDK, it expects `SYSROOT` / `RESDIR` in the environment; output
`thunder_core.wasm` as an artefact, not committed), Windows x64 (MSVC), macOS universal (arm64 +
x86_64), Android arm64-v8a (the runner's preinstalled NDK through CMake's Android toolchain file). Gate: four green
jobs with artefacts. If a runner lacks a tool, install it in the job with a pinned version; never vendor a toolchain
into the repo.

**A5. Real-time harness and clock logger** (**Fable**; before A3 so A3 can use it). In `native/core/src/rt/`: a
debug allocator hook (replace global `operator new` / `malloc` in the test binary; a thread-local "in audio callback"
flag makes any allocation abort with a message), `native/tools/rt-check.sh` that runs a test rendering 60 s through
`thunder_process` with the flag set, and a clock logger in the core: counts frames rendered, takes the host's
monotonic time at each callback, and ten times a second emits `clock {f, wallNs, sr, outLat, under}` exactly as
CORE-BRIDGE section 4 defines it (the Android overlay in A3 prints one summary line a second from these).
Gate: `rt-check.sh` prints PASS on the test core and FAIL when a deliberate allocation is compiled in
(`-DTHUNDER_RT_SELFTEST=1`).

**A3. Android shell v0** (Sonnet, about 90; Fable reviews the audio callback and service). `native/android/`:
- Kotlin, one activity, minSdk 29, target the current SDK; a `WebView` with JavaScript and DOM storage on, loading
  `https://appassets.androidplatform.net/index.html` through `WebViewAssetLoader` (the repo's `index.html`, `chains/`,
  `waves/`, `core/`, `dsp` wasm, `fonts/`, `icons/`, `sounds/`, `wavetables/`, `pump/`, `lxr/`, `*.js` copied into
  assets by a Gradle task: copy, never move or edit). The service worker and "install" prompt are irrelevant here;
  leave the page as it is.
- Audio: miniaudio (the single header already in `io/vendor`) with the AAudio backend, device opened at the device's
  native rate, float output; the data callback calls `thunder_process` and nothing else.
- The N0 test core: holds one mono sample (a short click generated at start-up, no file), a 16-step pattern, a tempo
  set by `{"t":"bpm","v":120}`, started and stopped by `{"t":"play"}` / `{"t":"stop"}`; it schedules hits on its own
  frame counter. This proves "sequenced from native code"; it is thrown away in N1.
- A foreground service (type `mediaPlayback`) with a notification started on play and stopped on stop, so playback
  survives screen-off and backgrounding; `WAKE_LOCK` is not needed for audio, do not add it.
- A debug overlay in the native layer (not in the page) showing the last ten `clock` lines and total underruns, with
  a "Copy" button.
- CI: a job that assembles the debug APK and attaches it to a GitHub pre-release tagged `native-n0-<run>`.
Gate: the APK installs on a BrowserStack Android 14+ device, the page loads, and tapping the overlay's Play makes the
click pattern audible (BrowserStack session video / audio capture noted in the report). The real gate is A6.

**A4. Bridge transport proof** (Sonnet, about 60). In the same app: inject `window.thunderCore` (CORE-BRIDGE section
2) from Kotlin. `send` / `on` over a `WebMessagePort` pair (`WebViewCompat.postWebMessage`), JSON strings in N0.
A test page under `native/android/app/src/debug/assets/bridge-test.html` (not the app's page) that: sends 60
messages a second for 5 minutes and counts echoes and their round-trip time; then `put`s a 53 MB Float32Array by
(a) `WebMessageCompat` ArrayBuffer if `WebViewFeature.WEB_MESSAGE_ARRAY_BUFFER` is supported, (b) writing chunks to a
file through a `WebViewAssetLoader` POST handler or `addWebMessageListener`, timing both. Gate:
`native/tests/RESULTS-N0.md` holds a table (messages sent / received / lost, median and worst round trip, seconds per
path for the buffer, device model, WebView version). Fable picks the path and updates CORE-BRIDGE sections 2 and 6.

**A7. Usage census** (Sonnet, about 40; needs the owner's songs, section 11). `native/tools/census.mjs`: Node, no
dependencies; reads every `.thunder` under a folder given on the command line and counts per song and in total:
engines and layer oscillator types used by pads (`kit.voices[]`), track and master effect types (`S.tfx`, `S.mfx`),
pump types, whether …chains / …waves patches are present and their module types, tracker commands (`FXC` codes) in
the phrases, SoundFont use, sample count and total sample seconds. Field names are in ENGINE-INVENTORY section 8;
where a field is uncertain, print the raw key so nothing is silently skipped. Gate: `native/tests/CENSUS.md` with the
totals table sorted by use; a unit test on `sounds/ELECTRO.thunder`. The songs themselves are not committed.

**A6. The owner's test** (owner, about 20 minutes). Install the APK from the pre-release link (Android asks once to
allow installs from the browser). In the app: Play on the overlay. Five minutes with the screen on, five with it off,
five with another app in front. Copy the overlay's log and paste it to the manager. Pass: every `clock` line reads
frames / sample-rate within ±0.1 % of the wall time elapsed, and underruns stay at 0 after the first second. Also
note by ear: no rushing, no gaps. The manager records the numbers in `RESULTS-N0.md` and asks for go / stop.

**A8. MIDI library check** (Sonnet, about 25; no code in the app). Establish from libremidi 5.4.3's source in
`io/vendor` and its documentation whether it has working Android (AMidi) and iOS (CoreMIDI) backends at that
version; if Android is missing or experimental, the plan for H2 is AMidi through a small JNI layer. Gate: half a page
in `native/tests/RESULTS-N0.md` with the files and lines that show it, and the recommendation.

**A9. Does a saved song always carry its samples?** (Sonnet, about 30; read-only on the app, runs on `beta`.) The
inventory found that a sample layer still loading from IndexedDB at save time is written as a reference only. Find
the save path (`songOut` / the kit's sample serialising; grep `b64`), establish whether Save waits for loading
samples, and write a headless test that saves during a slow sample load and checks the file. Gate: the finding in
`RESULTS-N0.md`; if a save can drop a sample, an entry under "Bugs still open" in `HANDOVER.md` on `beta` marked for
the owner (it is an app bug today, independent of the port). Also note there that SoundFonts are not in the file.

Order: A1 → A2 → A5 → A3 → A4, with A7, A8 and A9 any time after A1. A6 last.

## 11. What the port needs from the owner, and when

| When | Needed | Why |
|---|---|---|
| Before A7 | A folder of the owner's own `.thunder` songs, attached to a chat or put in a private location the session can read | The census orders the work by what the owner actually uses; N1 and N2 exits name "the owner's songs" |
| A6 | 20 minutes with the Fold; installing an APK from a GitHub link | The go / no-go measurement |
| Before U03 | How to bring in `matthettich/pump` (submodule suggested) and the session given access to that repo | pump's C++ is compiled in, not ported |
| N1 exit | Listen to three songs with Settings › Audio › Engine: Native core (test) on, desktop web copy | Sound parity by ear, after the numbers pass |
| N2 | The Fold with a USB MIDI interface for H2; a battery comparison (same song, web app vs native, an hour each) | Things no emulator shows |
| N4 start | Apple Developer Program membership ($99 a year) | Signed, self-updating Mac app; required for iOS |
| N4 | A Mac and the Windows PC with the audio interface, for the checklist | Driver and device behaviour |
| Optional | Google Play account ($25 once) only if the app should be in the store | Not needed for APKs from GitHub |

## 12. Decisions: settled, and open with the default a session uses until the owner says otherwise

| Decision | State | Value / default |
|---|---|---|
| The web app stays fully working during the port | **Settled** (owner, 10 Nov) | `native` branch, `native/` folders only |
| A plan a less capable manager can run, Fable for heavy work | **Settled** (owner, 10 Nov) | Sections 0 and 2 |
| pump: compile its C++ in, do not port, do not load the VST files | **Settled** (10 Nov) | U03, U18 |
| Linux native build | **Settled** (owner, Nov: low priority) | CI builds the core on Linux for tests only; no Linux shell |
| ASIO on Windows | **Settled** (owner, Nov) | Not unless free to use; WASAPI shared and exclusive |
| Licensing | **Settled** | Permissive only in the core; anything with limits removable (LXR optional) |
| Go ahead at all | Open | Nothing starts before "start the native spike"; after N0, a second explicit go |
| Android before desktop | Open | Default: Android first (N2 before N4); swapping costs nothing, both wait on N1 |
| Framework | Open | Default: miniaudio + libremidi + system web views (option A); JUCE only if A's glue fails in N0 |
| Keep the web copy afterwards | Open | Default: yes, as demo, development copy and fallback |
| iOS | Open | Default: not before Android and Mac are settled |
| MOD's remaining engine-side steps | Open | Default: no new engine features on `beta` once N1 step 3 starts; build them in N3 |
| How `matthettich/pump` comes in | Open | Default: git submodule pinned to a commit |
| Distribution on Android | Open | Default: signed APK on GitHub Releases, no Play Store |

## 13. What can go wrong, and what the manager does

| Sign | Do |
|---|---|
| A worker's output passes its own tests but the gate fails | The gate wins. Send back once; then section 9 |
| A parity gate fails by a tiny, constant amount (denormals, `float` vs `double`, a different `sin`) | Section 9: Fable decides whether the tolerance or the port is wrong. Never loosen a tolerance yourself |
| `git merge origin/beta` conflicts on `native` | Something edited the app's files on `native`: stop, report, do not resolve by hand |
| `npm test` fails on `native` after the merge | The failure came from `beta` or from a rule-breaking change: stop and report |
| A unit turns out much bigger than "one session" | Split it in the status table into a, b, c with a gate each; do not let one worker run long |
| The JS behaves in a way that looks wrong | Port it as it is, list it under "Bugs still open" in `HANDOVER.md` on `beta`, carry on |
| N0's clock test fails on the Fold | Stop the port. Record the numbers. The decision returns to the owner and Fable |
| CI minutes or a missing runner tool block a build | Report; do not move builds into the session's sandbox |

## 9. For Fable (items waiting for the audio lead; kept last in the file so it is easy to find)

- (none yet)
