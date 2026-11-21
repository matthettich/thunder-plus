# U37 design draft: bridge and host switch, `core.wasm` behind the Settings switch

DRAFT by a Sonnet design agent, all 11 sections. **The lead's rulings of 21 Nov are in section 12 at the end and win over the text above them.** Nothing here is built. Some file:line cites in sections 0 to 2 were not re-checked by the author: check a cite before editing code from it.
Written 21 Nov 2026 by a Sonnet design agent from `native` at db76547 and the web at `beta` cdd9439 (build 660).

**How to read the line numbers.** `native/...` and `docs/...` are in this branch. `web:index.html:N`, `web:core/core-processor.js:N`, `web:master-worklet.js:N`
are `beta` at cdd9439 (the pinned commit in `native/tests/web-ref/BETA-COMMIT`); the `index.html` at the root of `native` is an OLDER snapshot
(about build 615) whose line numbers differ, so it is not used here. Where a doc and the code disagree, the code is trusted and the disagreement is named.

## 0. The five findings that shape everything (read these first)

1. **There is no live host in C++ yet.** `native/core/src/core.cpp` is still the N0 stand-in (a 16-step click pattern; `core.cpp:1-4`, `:39`);
   the wasm CI job builds only that stand-in plus four support files (`.github/workflows/native.yml:101-116`). The real engine runs only inside
   `SongRenderer` (`native/core/src/render/render.h:92`), which is an OFFLINE renderer: it owns its `Seq`, holds the Song and pads by
   `const&` (`render.h:177-178`), has no `SeqRunner` (no catch-up skip, no EXACT STOP, no `setBpm`), no command ring and no snapshot swap.
   `SeqRunner`, `SnapSlot`/`seq_adopt`, `Players::setPads` and the `Seq::takeOver` machinery all exist (`seq/runner.h:50`, `rt/snap.h:40`,
   `render/players.h:32`) but **nothing assembles them**. U37 therefore contains a new C++ class (called `LiveHost` below) and a new C API, not only a bridge.
2. **A chains host is not wired into any song path.** `Mixer::setChain` is called only from tests (`grep -rn setChain native/core/src` finds `mix.h:111` and `mix.cpp`);
   `SongRenderer` never calls it, so no bus exists natively (`render.cpp:17-18`, `render.h:136-139`). `chc` events are counted as ignored (`render.cpp:36`).
   Pump mastering effects are not made for a song either: `fxbank.cpp` has no pump slot (the only hit for "pump" in `fx/fxbank.*` is a comment, `fxbank.h:32`)
   and `render.h:97-98` says effects "of the other kinds are left out". A song that uses chains, bus tracks, or pump effects cannot sound the same through the core today.
3. **The page already has an audio-thread JS core with its own Settings switch** (`web:index.html:8712` `aset.core` = `'core'` or `'page'`; segment at
   `web:index.html:15953-15956`; the module `core/seq-core.js` loaded as worklet `thunder-core`, `web:index.html:24971-24982`). U37's switch is a THIRD value, not a new row.
   And that JS core does NOT replace the page's mixer: it feeds notes and some mix values into the page's Web Audio graph (outputs `NT + 5`, `web:index.html:24978`;
   `coreWire` `:24990-24999`). The native core replaces the whole graph (mixer, effects, master chain, safety limiter), so the page's analysers, meters, recorders
   and hand-played pads need an answer (section 2.5).
4. **No cross-origin isolation anywhere** (no COOP/COEP header in `electron/main.js`, no `SharedArrayBuffer` or `crossOriginIsolated` in `web:index.html`, `web:sw.js`,
   `web:master-worklet.js`; GitHub Pages cannot set headers). So the web host is one wasm instance in one AudioWorklet, no shared memory, no wasm threads (section 3).
5. **The web copy the owner tests is built from `beta`, not `native`** (`.github/workflows/beta-web.yml:2-3, 23-31`; `tools/make-channel.sh:13` refuses any directory but `beta` or `stable`).
   Nothing today publishes `native`'s `index.html` anywhere the owner can open. Without a publish route the switch cannot be tried (open decision D12; owner question O1).

(Also relevant: the reason for the whole port is that Chrome on the owner's phone runs `ctx.currentTime` ahead of real time once any AudioWorklet module is loaded
(`docs/port/WORKPLAN.md:13-15`; `web:index.html:24848-24853`, `:8836`). `core.wasm` in an AudioWorklet is exactly that situation. The web switch is for the desktop web copy and Electron
(WORKPLAN `:160-164`); it does not fix the phone, the Android shell (N2) does.)

## 1. Scope in ten lines

1. U37 delivers: a native live host (`LiveHost`, new C++) behind a new C API, built to `core.wasm`; an AudioWorklet that hosts it; the page-side `window.thunderCore` object
   (CORE-BRIDGE section 2, web flavour); the Settings switch (third value of the existing Sequencer segment, default off); the fallback to the JS engine.
2. Plays through the core when the switch is on: the song's notes (sampler, SoundFont, the four live engines bass / vox / acid / mach), the mixer, the tfx effects, the master chain,
   MIDI out records (as data for the page to send), tempo / loop / mute / solo / mixer edits during play.
3. Does NOT deliver U34 (render through the core): U34a (render C API, `render.h`) and U34b (stems) are merged, but **pad renders stay with the page's Worker**
   (`web:index.html:8888-8895`, `renderAsync`), and so do song / stem exports (`renderOut`, `web:index.html:25895`, `OfflineAudioContext`). U34c (pad renders in the core) is not built.
4. Does NOT deliver U35 / U36 beyond using them: `MidiSched` (U35) and the port table + pump (U36 PR A/B, merged) exist; the Android sink is PR #275 (open); CoreMIDI / WinMM sinks are N4.
   For the web, the sink is Web MIDI on the page (section 6).
5. Does NOT deliver the N2 shells (Android `WebMessagePort`, native audio out) or N4 shells (WebView2 / WKWebView). The bridge object is written so those shells can implement the same `send` / `put` / `on`.
6. Does NOT deliver live route step 2 (pump synths and the other `xeng` kinds live) or step 3 (…waves live) (`docs/port/LIVE-ROUTE-DESIGN.md:L9`). Pads of those kinds stay on the SAMPLER route
   and play their rendered twin (`render.h` comment on `set_pad_route`, `include/thunder/render.h`). U04's LXR seam is not wired.
7. Does NOT deliver the stretch (`sx`, OLDSCHOOL, GRANULAR, LOOP play unstretched natively: `players.cpp:60`, `WEB-FOLLOW-20NOV-NOTES.md` item 4). A song that uses it must fall back (section 2.4).
8. Does NOT deliver MIDI in, clock in, Link, recording, input metering (U36 phase 2), nor hand-played notes (`note.on`): keys, previews and MIDI-thru keep sounding through the page's own graph.
9. Needs, and this draft lists as work, two things the other docs treat as done or later: the chains host (finding 2) or a rule that refuses chain songs; the pads + chop snapshot with deferred free (F2 + F3).
10. Ships behind the switch, default off; `npm test` on `beta` must be unchanged with it off (WORKPLAN `:160`).

## 2. The pieces

### 2.1 The wasm module and its exports

Existing facts. Flags that the real core needs are already fixed by the stand-in job: `-O3 -ffp-contract=off -msimd128 -fno-exceptions -fno-rtti -std=c++17`, reactor model
with `-Wl,--export=_initialize` (`native.yml:107-114`). The page instantiates its other wasm with only four WASI stubs, `fd_close fd_seek fd_write proc_exit` (`web:index.html:18853`, `:20512`, `:20582`);
the core must link against no more than those (an import that is not provided makes `new WebAssembly.Instance` throw in the worklet). The DSP the core links (tfx, pe, mods, pump) is built
by `native/core/CMakeLists.txt:46-75` for native; `native/tools/build-wasm.sh:20` builds them as SEPARATE wasm files with CMake targets `thunder_wasm_*`. **Linking them statically into one core module has not been done** (open: whether
every one links with wasi-sdk 25 without `<thread>` / `<mutex>`: `fx/fxpump.cpp:97-101` uses `std::mutex`, `std::this_thread::yield`, and the test CMake says wasi without `-pthread` has no threads, `native/tests/CMakeLists.txt:856-858`; not verified, listed in section 11).

Proposed C API (new header `include/thunder/live.h`; names are proposals). Everything the page can ask for must map to one of these, with the thread rule of the header it wraps:

| Group | Exports (proposed) | Wraps | Thread (in the worklet: which call site) |
|---|---|---|---|
| life | `thunder_live_create(sr, flags, err)`, `_destroy`, `_process(L, R, frames)`, `_poll(buf, cap)`, `_stats` | `LiveHost` | create / destroy: message handler; process: `process()` |
| memory | `thunder_alloc(n)`, `thunder_free(p)` (or export `malloc` / `free`) | the module's allocator | message handler |
| song | `_song_publish(bytes, n, vars, nvars, rev)` | `SeqSession::publish` (`seq/session.h:51`) | message handler (control side) |
| pads | `_pad_set(pad, l, r, frames, sr)`, `_pad_kit`, `_pad_route`, `_pad_chop(loop)`, `_pad_hits`, `_var_set`, `_font_add`, `_pad_sf`, `_pad_live`, then `_pads_publish()` | the `thunder_render_set_*` family of `include/thunder/render.h`, but into a SNAPSHOT (F2 + F3), not a running handle | control |
| transport | `_play(mode, row, opts, at_frame)`, `_stop(at)`, `_hold`, `_loop`, `_bpm`, `_lead`, `_perf`, `_lxp`, `_stop_track(track, at)` | `SeqRunner` + `Seq` setters, `Players::stopTrack` (`players.h:47`) | command ring, applied at block top |
| mixer | `_mix_set(tr, key, v, ramp, at)` | `Mixer::set(MixSet)` (`mix.h:132`) | command ring; audio side only |
| effects | `_fx_plan(track, json)` then `_fx_commit` (ring), `_fx_param(...)` | `FxHost::plan` / `commit` (`fxhost.h:128`, `:134`) | plan: control; commit: audio |
| master | `_master_config(json)` | `MasterChain::configure` (`render.cpp:58-59`) | control, swapped like a plan (not built: `SongRenderer` does it before the first block only) |
| midi | `_midi_set_song`, `_midi_port(...)`, `_midi_start(spp)`, `_midi_stop`, `_midi_release`, `_midi_panic`, `_midi_drain(out, cap)` | `include/thunder/midi.h` | as that header (section 6) |
| meters | `_meters_want`, meters in `_poll` | `MeterBank` (`render.h:155`) | not in any C API today |

"Not in any C API today" is the honest state for: every transport call, mixer, effects, master, meters (they exist as C++ classes only), song publish, and the pad snapshot.
`core.h` has `thunder_post` with a JSON command and a 256-slot ring, which could carry the small commands (the rest of this draft proposes it does), but its command set is N0's (`bpm play stop echo song.load`, `core.h:28-30`).

### 2.2 The AudioWorklet that hosts it

- File: `native/web/thunder-native-worklet.js` (new; the page loads it with `ctx.audioWorklet.addModule(url)` as it loads `core/seq-core.js`, `web:index.html:24975`). It registers `thunder-native`.
  **The served path is a problem:** `native/` is not in `beta`'s tree and the beta publish script copies the tree of `beta` only (finding 5), and `web:sw.js:3-8` lists every file the offline cache holds (a new worklet and a new wasm must be added there). Both are edits outside `native/` (WORKPLAN `:75-80`): B3's exception covers `index.html`; `sw.js` needs the same ruling (D12).
- The module reaches the worklet the way tfx / pump already do: the page fetches the bytes, compiles ONCE on the main thread, and sends the `WebAssembly.Module` by `postMessage` (`WASM_PRECOMPILE`, `web:index.html:20949-20951`, `:18861`); the worklet only instantiates (`web:index.html:20549`, `:20580-20583`). A phone compile of a 2 to 4 MB module on the audio thread would glitch.
- Memory: the module's own linear memory (default, growable). The page does NOT see it (no shared memory, finding 4). Everything crosses by copy through `postMessage` (section 3). Growth: wasm memory growth detaches the `ArrayBuffer` of every typed-array view; the worklet must re-make its views after any call that can allocate (`thunder_alloc`, publish, plan). Pattern already in use: `pumpIO` re-checks `P.memory.buffer` (`web:index.html:20586`).
- Block size: the worklet's `process()` gets 128 frames. The core's quantum is also 128 (`kRenderQuantum`, `render.h:30`), `Mixer::render` silently cuts above its `maxBlock` (`mix/mix.cpp:377` per prewire-19nov) and `FxHost` gives silence above 4096 (`fxhost.h:155`). Rule: `LiveHost` is created with `max_block = 128`, `process()` runs whole quanta, and a call of any other size (a future variable render quantum) is cut into 128s by the host class, as `SongRenderer::block` does (`render.cpp:83-92`). No intermediate buffer is needed for 128-frame calls.
- Denormals: `ScopedFtz` is a no-op on wasm (`rt/ftz.h:28`); the wasm build must not rely on it (the web's JS core did not either).
- Stack: `FxHost::commit` puts about 12 KB on the audio stack and `lfoEvs` 7 KB (prewire-19nov F6); set `-Wl,-z,stack-size=` explicitly and test the deepest path. Not measured here.
- Failure inside wasm: built with `-fno-exceptions`, a failed allocation ABORTS (`include/thunder/render.h`, OUT OF MEMORY paragraph): a trap in `process()` raises `processorerror` on the node and the node outputs silence from then on. The page must listen for it (section 2.4).

### 2.3 The page-side bridge object

`window.thunderCore` as CORE-BRIDGE section 2 (`send`, `ask`, `put`, `take`, `on`, `v`, `CORE-BRIDGE.md:30-38`), implemented over the worklet node's `port`. Rules from that file that bind it:
times are frames on the core's timeline (never seconds / `performance.now()`); `put` chunks at most 1 MB; unknown message types are ignored and logged once; `hello` returns `caps`.
**One difference from the document, code wins:** CORE-BRIDGE section 3 lists message names (`song.load`, `song.patch`, `buf.use`, `kit.load`) that the page does not speak today; the page speaks the JS core's ops
`load song patch vars kit buf vbuf sf vdrop prep play hold stop loop stp bpm perf lxp lead ack` (`web:core/core-processor.js:327-363`, `CORE_WANT = 7`, `web:index.html:24971`). Section 4 maps every op to the new call.
Proposal D4: the web bridge ACCEPTS the JS core's op names as its input, so `corePost(...)` (`web:index.html:25016`) is the only call site that changes and the diff of `index.html` stays small; the bridge object translates.

### 2.4 The switch

- Where: the existing segment `segCore` (`web:index.html:15953`: `[['core', 'Audio thread'], ['page', 'Page (old)']]`), a third value `['native', 'Native core (test)']`, saved in `aset.core` (`web:index.html:8712`, `asetSave`). Default stays `'core'`/page-wise exactly as today; `'native'` is never a default.
  `coreWanted()` (`web:index.html:24969`) is read at load by the Settings row, and phones need `aset.corePick` (the lazy-worklet rule): the native value must follow the same `corePick` rule or it will load a worklet on the phone at start-up.
- Default off. A saved `'native'` on a device whose browser lacks `WebAssembly` + `AudioWorklet`, or whose module failed to load last time, is treated as off and the Settings row says why.
- Toggled while playing: **take effect from the next PLAY**, as `updCore` does (`web:index.html:15955-15956`, toast "from the next PLAY"). Switching mid-song would mean a hand-over between engines; the existing `seqHandover` (`web:index.html:25205-25216`) shows how costly that is. Not proposed.
- Per-song readiness check at PLAY (new, `nativeCan(S)`): the song is native-playable only if it uses none of: chains (`S.chainOn` any, or a bus plan), pump effects on any track or the master, stretch (`sync` 2 or 3, `sx`), a live-engine kind other than bass / vox / acid / mach unless the page's rendered twin is acceptable (decision D8), an LXR pad, or any effect not in `caps.fx`. Otherwise PLAY uses the JS core (or page) and a toast says which feature caused it. This is the mechanism for finding 2.
- Fallback on any failure: (a) module compile / instantiate fails, or `thunder_live_create` returns an error: toast "Native core didn't load: playback uses the Audio-thread sequencer" (same style as `web:index.html:24981`), `aset` untouched, next PLAY uses the JS core; (b) `processorerror`, a heartbeat message missing for 1 s while playing (the JS core's `ack` / "DEAF CORE" logic is the model: `web:index.html:25272-25285`): STOP, `coreDrop`-like teardown, toast, and the song is NOT restarted by itself (a restart would repeat the failure); (c) out of memory while loading pads: the core refuses (`E_LIMIT`) → PLAY on the JS core with a toast naming the cause. The page writes each into the playback report (`pbLog`, `web:index.html:25125`) so the owner can paste it.
- The owner is told by a toast (3 to 5 s) and a line in Settings → Audio under the segment ("Native core: fell back at <time>: <reason>"). Wording to be settled with the owner (O2).

### 2.5 What the page keeps when the core plays

Not replaced by U37: the Web Audio graph for hand-played pads and keys, previews, the metronome (`metRun`, `web:index.html:25143`-ish), the editors' own engines, recording (`RCD`). The core's stereo output connects to the page's `master` gain
(`web:index.html:8807`: the device-volume node before `ctx.destination`), NOT to `rt.out` / `rt.mo` / the page safety limiter, because the core already contains MASTER VOL, the master chain and the safety limiter (`render.cpp:58-59`);
a second limiter would double the processing. Consequences to settle: track and master meters and the spectrum read page analysers (`web:index.html:24041-24045`, `rt.man`) which would see only page-played sound, so they must be fed from the core's `meters` message (U13 is built, `render.h:155`, no C API: section 2.1); the recorder that taps the graph would miss the song (O3).


## 3. Threads and data ownership in the web host

### 3.1 The one fact that changes the C++ thread rules

The headers speak of a CONTROL thread (builds snapshots, plans effects, parses, frees) and an AUDIO thread (`seq_adopt`, `pump`, `commit`, `process`): `core.h:12-14`, `snap.h:1-9`, `fxhost.h:25-29`, `midi.h:21-26`.
In the web host there is **one thread for both**: a worklet's message handlers run on the rendering thread between `process()` calls, and the page cannot share memory with the module (finding 4).
So:
- **The thread rules hold trivially** (nothing runs concurrently; "pump never on two threads" is true by construction: `pump_io` is process-global per wasm INSTANCE (`fx/fxpump.cpp:4-6`, `render.h:20-27`), and there is one instance in the worklet).
  A second instance (an offline render in a Worker) has its own memory and its own globals, so a render and the live host cannot corrupt each other in the web. This is a property of the web host only; the shells need the serialising rule of prewire-19nov ("Pump is process-global").
- **The cost model inverts.** Everything the headers put on the "control thread" now steals time from the 2.67 ms quantum (128 frames at 48 kHz): parsing the song (a parse tree about 8.6 times the file, 15.5 times for a hostile one: `include/thunder/render.h`, MEMORY), building a `Seq` and its `SeqSession` pages (up to 17 MB for song mode: `docs/port/WIRING-DOSSIER.md` 1.1), `FxHost::plan` (makes effect objects, `fxhost.h:25-27`), copying a pad into the snapshot. The web already learned this: "Posting the whole song after every save made the audio thread unpack hundreds of KB in its own time: a glitch each time" (`web:index.html:25056-25059`), which is why it sends diffs. **C++ has no patch**: `Song::load(text, len, err, pageS)` is the only entry (`song/song.h:89-92`; `grep patch native/core/src/song native/core/src/core.cpp` finds nothing but that comment), so a native edit is a whole-song re-parse. Nobody has measured the time of that for ELECTRO or a census song (section 11).
- Options for the stall (D2): (a) accept it, debounce edits to 250 ms or more, republish only at PLAY and on settled edits, and measure with `AudioContext.renderCapacity` (`web:index.html:8806` already subscribes to it; its `underrunRatio` is the gate); (b) a C++ `Song::patch` (the JS diff format of `coreDiff`, `web:index.html:25060-25069`) building the new `Song` from the old by sharing unchanged tables; (c) wasm threads + `SharedArrayBuffer`, which needs cross-origin isolation: Electron could add the two headers in its `app://` handler (`electron/main.js:135-140` already wraps HTML responses with a CSP), the Android shell can add them when it serves assets, GitHub Pages cannot (a service-worker header rewrite is a known trick; not tried here, needs a reload on first visit, and `web:sw.js` is the file that would carry it). **Recommendation: (a) now; (b) only if the measured stall is over about 2 ms; (c) is the shells' design anyway.**

### 3.2 What lives where

| Thing | Page (main thread) | Worklet (audio thread, one wasm instance) |
|---|---|---|
| The song `S` | the JS object; edits; `core.mir` mirror (`web:index.html:25060`) | `SongSnap` = `shared_ptr<const Song>` + `SeqVars` + `Seq` (`seq/session.h:90-96`) |
| Sequencer | none while native plays (a stand-in `tp.seq` object keeps `time / bpm / tks`: `web:index.html:25101-25106`) | `SeqRunner` + `Seq` (`seq/runner.h`), stepped LEAD ahead |
| Pad renders | `cache[i]` (Float32Array, `data2`, `sr`), the render Worker (`web:index.html:8888`, `:25054`) | the pads snapshot (3.4): its own copy |
| Pad chop data | computes `loopSecs` and `slicePoints` / `findHits` (`web:index.html:23703`, `:23711`, `:23722`) | `SeqChop` inside the pads snapshot (`seq/chop.h`) |
| Voices, mixer, effects, master | none (the page graph keeps the hand-played sound only) | `Players`, `Mixer`, `FxBank`, `MasterChain` |
| MIDI out | Web MIDI ports, the outbox, the hidden-page ledger (`web:index.html:17928-17990`) | `MidiSched` produces timed records; the worklet drains them |
| Meters, playhead | draws them | produces `clock`, `pos` / `rows`, `meters` events |
| Wall-clock pair | `getOutputTimestamp()` (section 6) | `currentFrame` / `currentTime` only |

### 3.3 What crosses, and how

- **Small messages** (the JS core's ops, section 4): `postMessage` of plain objects, as `corePost` does (`web:index.html:25016`). Song text: ONE string, not an object tree (structured clone of a big object tree is slow on the page; `JSON.stringify(S)` is cheap by comparison and the C++ side wants text anyway: `Song::load`).
- **Pad audio**: transferable `ArrayBuffer`s, in chunks of at most 1 MB per CORE-BRIDGE (`CORE-BRIDGE.md:37-38`); the Android shell's existing `thunder_bridge.js` (`native/android/app/src/main/res/raw/thunder_bridge.js:36-47`) is the pattern (header message, then the buffer transferred). The page copies each pad before transferring because it keeps its own (`web:index.html:25054-25055`, `c.data.slice()`); the worklet copies it AGAIN into wasm memory; the snapshot call copies it a THIRD time (`include/thunder/render.h`: set_pad "COPIED"). Peak while loading is therefore three copies of the pad in three heaps. Proposal: the snapshot call takes OWNERSHIP of a block the worklet allocated with `thunder_alloc` (one copy fewer, no second peak). Section 7 has the numbers.
- **Worklet to page**: `postMessage` at the rates CORE-BRIDGE section 4 gives (`clock` 10/s, `pos` 30/s, `meters` 30/s, `rows` per tracker row), MIDI records batched into one transferable `ArrayBuffer` per drain, only when non-empty.
- **No `SharedArrayBuffer`.** Finding 4. The only memory the two sides share is nothing.

### 3.4 The prewire F2 + F3 design applied to this host

`prewire-19nov.md` F2 (a pad buffer has no lifetime rule: `Voice::data` is borrowed, `voice.cpp:186`; replacing a pad while it sounds is a use-after-free) and F3 (the `SeqChop` comes from the session's opts, `seq.cpp:397`, `SongSnap` holds none, and `setHits` mutates vectors the audio thread reads, `chop.h:32-44`) are ruled to need ONE design: an immutable pads-and-chop snapshot with deferred free. Applied:

1. **`PadsSnap`** (new, immutable once published): per pad a `shared_ptr<const PadBuf>` (so a pad that did not change is shared between two snapshots, not copied), the kit entries (`choke syn rel`), the route and the live params, the prepared SoundFont presets, the variations, and a fresh `SeqChop`. Built in `LiveHost`'s control side (the message handler in the web), published through a second `SnapSlot<PadsSnap>` (`rt/snap.h`), adopted at the top of a quantum, ALWAYS before `SeqRunner::pump` of that quantum (prewire "What U37's host must do").
2. **The chop pointer.** `Seq` keeps `const SeqChop*` from its opts (`seq/seq.h:70`: "must outlive every Seq built from these opts"). A pad change must not rebuild the `Seq`. Proposed: `Seq::setChop(const SeqChop*)`, called at adopt time between steps (a one-line change in `seq.cpp`; the lead reads it). Alternative: every pads publish makes a successor `Seq` through `SeqSession::publish` on the same `Song` (cheaper than a parse, but it allocates the roll pages again and the lead's `takeOver` rules apply every time).
3. **`Players::setPads`** already exists for the re-point (`render/players.h:32`, ruling P1). The `LiveHost` calls it at the same moment as the `Seq` re-point.
4. **Deferred free (D5).** Three mechanisms:
   - (a) **Epoch.** Each `PadsSnap` has an epoch; each voice (booked or sounding, every pool) stores the epoch of the buffer it was started on; the audio side publishes `minLiveEpoch` (an atomic, the minimum over active voices) once a quantum; the control side frees a retired snapshot only when its epoch is below it. Cost: one scan of at most 64 + 32 + live voices a quantum. Works for threaded shells and for the web.
   - (b) **Refcounted buffers** (`shared_ptr` copy at note start): atomic increments on the audio thread; the last release may free ON the audio thread unless the release goes to a retire list. More moving parts.
   - (c) **Web-only shortcut**: with one thread the handler can ask `Players` synchronously whether any voice uses the old buffer, and free at once. Correct in the web, wrong in the shells.
   **Recommendation: (a).** It is one design for every host, which is what F2 + F3 asked for; (c) would be a second design to test. It also bounds a bad case: a 5-minute sample sounding keeps ITS buffer (not the whole snapshot's other pads) alive, so the snapshot must free per pad, not per snapshot. Say so in the brief.
5. **Pump on one thread.** One `LiveHost` per wasm instance; no `thunder_render_*` handle in the same instance while it plays (the render API's comment on pump, `include/thunder/render.h`, is true only because a render never makes pump slots; the live host does). Offline renders get their own Worker instance. `pe_prepare()` (Plaits / Elements need it "on every rendering thread", `engine/layer_pe.h:20-21`) is called once when the worklet instance is made, off `process()`.
6. **The live route's idle skip is in** (`LivePool::setIdleSkip`, on by default: `engine/live_pool.h:116`, merged with #272 in db76547); a vox pad on 33 tracks no longer renders silence at 3 times real time. Its memory (about 235 KB a vox track, `live_pool.h:10`) counts in section 7.
7. **`FxHost::plan` and `process` share the host's RNG** (prewire F5, `fxhost.cpp:152`, `:287`): with one thread this cannot race; a threaded shell must fix it first. Listed, not done here.
8. **`PumpSlot::renew`** frees and makes a pump instance on the audio thread (prewire F4, `fxpump.cpp:143-150`, `:231`, `:266`): accepted by the 16 Nov ruling; in the web it is an allocation inside `process()` (a `memory.grow` is possible there). Counted by `FxHost::audioRemakes()`; the page should show the count in the playback report.

### 3.5 The block loop of `LiveHost` (what the lead writes or reads)

One quantum, in this order (`SongRenderer::quantum`, `render/render.cpp:93-120`, is the template; the live version differs where marked):

1. apply the command ring (`play`, `stop`, `hold`, `loop`, `bpm`, `lead`, `mix.set`, `fx commit`, `stopTrack`, `perf`); each command may carry `at` in frames (`core.h:28-30` semantics, `CORE-BRIDGE.md:106-114`) and waits in a 64-slot pending list;
2. `seq_adopt(songSlot, runner)` then adopt `PadsSnap` (`seq/session.h:122-134`); NEW: re-point `Players`, `Seq` chop;
3. `players.setTime(t0, frame)`, `mix.setTime(t0)`;
4. MIDI `midi_begin(h, t0, 128/sr, latencySec, audibleMask)` (`render.cpp:101`);
5. `runner.pump(t0, lead, evs, cap, &step, onStep)` (catch-up skip, EXACT STOP, at most 3 steps: `seq/runner.h:50-73`) with `onStep` doing `midi_sched.onStep(...)` and then `dispatch` of each event (the same order as `render.cpp:106-108`); NEW versus the renderer: the runner, `hold`, `held` reporting, `ended` reporting;
6. `midi_end(h, t0, seq.time(), seq.bpm(), seq.alive())`;
7. `Players::songOver(at)` once when the song ends (`render.cpp:111`);
8. `players.render`, `mixer.render`, `meters_feed`;
9. then, AFTER the block: `clock_map` (if the sink is the core's, section 6), drain MIDI records, post events.
Seed the sequencer tempo from `seq.bpm()` at PLAY: `SEV_TEMPO` is not emitted at the start (prewire "What U37's host must do"; `seq.cpp:993`). Give the `Seq` a time-derived `randomSeed` (default is the fixed golden seed, `seq/seq.h:67`; WIRING-DOSSIER 1.1): the page sends `Date.now()` with `play`.

## 4. Every state change the page can make during playback

"Today" is what the page does with the JS core on (`web:` lines). "Native" is the call that carries it. **No API** = no C or C++ entry exists for it today (and so it is U37 work); **C++ only** = the class has it, no C API yet.
"Takes effect" is when the listener hears it. Side: P = page, W = worklet (control side of the host, between quanta), A = audio side (inside a quantum).

| # | Change | Today (JS core on) | Native carrier | Side | Takes effect | API today |
|---|---|---|---|---|---|---|
| 1 | Song edit (note, phrase, chain, song row, instrument, perf, any `S` field) | `coreSong()`, 100 ms debounce, `coreDiff` ops `patch` (`web:index.html:25080-25086`, `:25060-25069`); the JS sequencer reads `S` in place | whole song text → `SeqSession::publish` → `SongSnap` → `seq_adopt` + `Seq::takeOver` (`seq/session.h:51`, `:122-134`) | P sends text; W parses and publishes; A adopts | at the top of the next quantum after publish; a note already booked is not re-timed (the web has the same limit: "the dual-sound bug", WORKPLAN §9) | C++ only; no patch (3.1) |
| 2 | Chain-patch edit (cables, CHN on/off, bus roles) | page `busSync` (`web:index.html:22924`), `chainsPush` (`:24534`); in the JS core `rebus()` on `load` / `song` / `patch` (`web:core/seq-core.js:279`, `:1209`; `core-processor.js:334`) | same as 1: a new `SongSnap` (its `Seq` builds its own `busT_`; `takeOver` no longer copies it: `WEB-FOLLOW-20NOV-NOTES.md` "Goldens" table, `seq-edit-bus`) PLUS `Mixer::setPlan(BusPlan)` (about 1.5 KB: a slot, not a ring entry: prewire) | P → W (`bus_plan` allocates: control side) → A | next quantum; the mixer plan at the same block boundary as the Seq | `bus_plan` C++ only; `Mixer::setPlan` audio-only; **the chains hook (`setChain`) has no host: finding 2** |
| 3 | Tempo (BPM drag, TAP, TPO command, Link, MIDI clock) | `tp.seq.bpm = v` → `corePost({op:'bpm'})` (`web:index.html:25103-25104`, `:18376`) | `bpm` command → `Seq::setBpm` (clamped, `seq/seq.h` `setBpm` per SEQ-LIVE-DESIGN A1); `MidiSched` reads it each block | W ring → A | first sample of the next block (`at` allowed) | C++ only (`Seq::setBpm`) |
| 3a | Tempo while pads have envelopes in lines or tempo-synced LFOs | page `lnTempo`, 300 ms quiet, 0.5 % tolerance (`web:index.html:8905-8913`), then `prerender()` → new render → `install` (`:8917`) | the page renders the pad (U34c is not built) and sends it: row 6 | P | after the render lands, row 6 | page side done; see D7 of section 5 |
| 4 | Pad sample change (new render lands, new sample, engine change) | `install` → `coreBuf(i)` (`web:index.html:8917`, `:25054`) | `PadsSnap` publish (3.4), per pad | P → W → A | next quantum; voices already sounding keep the OLD buffer (deferred free) | **No API** (`set_pad` exists only before the first read of a render handle: `include/thunder/render.h`, set_pad "Only before the first read") |
| 5 | Chop change (slice count, SMODE, loop length, hits) | page `applyChop` (`web:index.html:23768`) with `loopSecs` / `findHits` (`:23703`, `:23711`); the JS core's `applyChop` is a STUB (`seq-core.js:5`) | `SeqChop` in the pads snapshot; `Seq::setChop` (proposed) | P computes, W builds, A adopts | next step the `Seq` takes | `set_pad_chop` / `set_pad_hits` exist on the render handle only; **No API** for a running host |
| 6 | Kit change (a pad's kind, choke, release, live or rendered, SoundFont) | `coreKit()`, re-checked 5 times a second (`web:index.html:25017-25024`, `coreKitV` `:25026`) | `PadsSnap` (route, kit, live params) | P → W → A | next quantum; a note already sounding keeps its pad | render handle only |
| 7 | Mute / solo | `mixChanged()` → `rt.pl.applyMix()` (`web:index.html:24566`): page `AudioParam` writes; the core never sees it except through `S.mix` in a song patch (and `mix_audible` for MIDI gear) | `mix.set {tr, k:'mute'/'solo', v}` → `Mixer::set` (`mix/mix.h:132`; `set` returns false for Send) AND the same change inside the next song text (the `Seq` reads `S.mix` for TVA / TPN, `seq/seq.cpp:211-212`, `:279`, `:619`); `thunder_midi_set_audible(mask)` each block from `Mixer::audibleMask` | W ring → A | page law: 12 ms approach; MUTE crossfade per CORE-BRIDGE `ramp` | C++ only |
| 8 | Mixer (volume, trim, pan, master VOL, OUT) | same `applyMix` | `mix.set` (same) | W ring → A | 12 ms approach; MASTER VOL jumps (`mix.h` header comment) | C++ only |
| 9 | FX param edit (a slider on a track effect) | `rtFx(t)` (`web:index.html:24454`) → the effects node `set(chain)` / `pset` | `FxHost::psetAt(at, slot, p12)` for tfx parameters; `psetPump` for pump (`fx/fxhost.h:136`, `:143`) | A (a ring entry) | first block starting within 64 samples of `at` | C++ only; the whole-chain form is row 10 |
| 10 | FX add / remove / reorder, effect type change | `fxChanged(t)` → `rtFx` (`web:index.html:24464`); `chainsPush` | `FxHost::plan(entries)` on the control side (makes the objects), `commit(plan)` on the audio side; the old plan returns for deletion (`retired_`, `fxhost.h:108`, `:128-134`) | W plan; A commit; W frees | next block; a pump slot may pass the sound for one block (the gate, `fxpump.cpp:97-101`: "a click when an effect is added mid-song") | C++ only; `FxBank` wraps it for a whole song at load only (`render.cpp:69-70`) |
| 11 | Master effects / mastering / limiter edit | `mstSync`, `rtRoute` (`web:index.html:24437`) | `MasterChain::configure(cfg)` | W builds, A swaps | next block | `SongRenderer::setMasterChain` is "control thread, before the first block (the processors start from rest)" (`render/render.h:128`): **a live swap does not exist** |
| 12 | Reset track | `resetTrack(t)` (`web:index.html:21523-21541`): page `stp` + a song patch + `corePost({op:'stp', track})` (`:21538`) | song republish (row 1) THEN `Players::stopTrack(track, t)` (`players.h:47`: both pools and the live pool, `kill(.., 0.004, stop = true)`) | W then A | the voices let go at `max(now, t)` over 4 ms; booked-later voices never sound | `Players::stopTrack` C++ only |
| 13 | PLAY from another row while playing (seek) | `tStart` calls `tStop(true)` then starts again (`web:index.html:24578-24640`; `lk.restart`); RESUME replay (`corePrep`, `:25087`) | `play` with a new `Seq` (`SeqRunner::start`), a `thunder_midi_start(spp)`; the previous voices ring out except through `stopAll` as the page does (`tStop` → `rt.pl.stopAll`, `:25375`) | W ring → A | at `t0 = now + 0.08` (section 6) | C++ only; no `LiveHost::play` |
| 14 | Loop (LOOP key, pattern loop) | `tp.seq.setLoop(l)` → `corePost({op:'loop'})` (`web:index.html:25105`) | `loop` command → `Seq::setLoop` (clamped, `seq.cpp:518` per WIRING-DOSSIER 1.1) | W ring → A | at the next step | C++ only |
| 15 | STOP (user, song end, hold / EXACT STOP) | `tStop` (`web:index.html:25355`): `core.node.port.postMessage({op:'stop', at})`, `coreWire(false)` unplugs the outputs, `rt.pl.stopAll`, `moStop` | `stop {at}` command: `SeqRunner::stop`, `Players::stopAll(at)` (`players.h:52`), `thunder_midi_stop`, one more quantum, then the host may be dropped; EXACT STOP: `SeqRunner::hold()` and the `held` report (`runner.h:37`, `:62-66`) | W ring → A | at `at`; silence never waits for the core: the page also mutes the core node's output at once (as `coreWire(false)` does, `web:index.html:24988-24990`) | C++ only |
| 16 | PERFORM macros, XF, FILL | `perfPush` (`web:index.html:22261`) → `{op:'perf'}` | `Seq::setXf / setMac / setFill` (`seq.h`, WIRING-DOSSIER 1.1) | W ring → A | next step | C++ only |
| 17 | Live transpose (`lxp`) | `{op:'lxp'}` (`web:index.html:17735`) | none: LXR is not wired (U04 seam) | | | **No API** (LXR pads fall back, 2.4) |
| 18 | MIDI settings (ports, channels, gear delay, clock boxes, OUTPUT edits) | `midi.outIds`, `mLatOf`, `moRelease` on edit (`web:index.html:17885-17900`, `:18317`) | `thunder_midi_set_song(json)` on an instrument edit; `thunder_midi_set_port(...)`; `thunder_midi_release(playing)` (`midi.h:84-95`) | W | next block | C API exists |
| 19 | Lead (screen off) | `coreLead()` 0.25 s, 0.6 s for 1.5 s after PLAY, grows to 1.2 s hidden (`web:index.html:25074-25079`, `:25101`) | `lead` command → `SongRenderer::lead` equivalent (`render.h:173`: 0.05 to 2 s); `thunder_midi_set_lead` is capped at 0.5 s (`midi.h:88`) | W ring | next block | C++ only; **the MIDI cap (0.5 s) is below the web's hidden lead (1.2 s)**: see D6 |
| 20 | Latency comp mode (AUTO / ON / OFF) | `pdcSync` (`web:index.html:21625` region, `pdcModeOf`) | `pdc_plan(BusPlan, PdcSlot[], n, sr, mode)` on the control side, `Mixer::setDelays` on the audio side (`mix.h:117-125`) | W plan, A apply | glide over 50 ms | C++ only; the live engines add none (L4) |

Reading the table: **rows 4, 5, 6 (pads), 11 (live master swap), and every transport / mixer / effect call have no C API today**; rows 1, 2 depend on a decision about patching (D2/D3); row 2 depends on finding 2.

## 5. Every duty from PICKUP.md's "Next, in order" item 2, one row each

Source: `docs/port/PICKUP.md` "START HERE (21 Nov, 09:15)", "Next, in order" 2; the detail is in `reviews/verify-274.md` F1, F4, F6 and `reviews/verify-271.md` F4. Test names are PROPOSED (they do not exist); each is a test in `native/tests` unless it says "browser".

| # | Duty | Where it is done in this design | The test that proves it |
|---|---|---|---|
| D-1 | `setMidi` + `setSeqStart(0.08)` with `thunder_midi_start` (verify-274 F1 point 1 and 2: the `Seq` starts `t0_of_first_block + 0.08` when `thunder_midi_start` is posted in the same block; start posted BEFORE that block) | `LiveHost::play(mode, row, t0)`: `Seq::setTime(t0)` with `t0 = (page's ctx.currentTime + 0.08) as frames`; `midi_start(spp)` posted first. The page already computes it: `t0start = ctx.currentTime + 0.08 + ci + (warmNew ? 0.15 : 0)` (`web:index.html:24622`) and sends `t0` in `play` (`:25098`). `SongRenderer::setSeqStart` cannot do it live (it refuses after the first block, `render.h:151`): the live `Seq` is made per PLAY. SPP from `midi_spp_for` (`src/midi/midi_spp.h`) | `live_midi_start`: the live host's MIDI stream (Start, SPP, first Gear-delayed notes) equals the web's `runBlocks` stream for the 18 ledger songs (`native/tests/golden/midi-ledger-expect.json`, `test_midi_ledger.cpp`), the same check verify-274 F1 asked for |
| D-2 | Post `thunder_midi_stop` when STOP is asked or 0.3 s after `songOver()` (spec 2.5; the renderer never stops the scheduler by itself: verify-274 F4 "Song end"), then one more quantum before destroying the renderer so CLEAR / note-offs are booked (verify-274 F1 point 3, F4 "Lifetime") | `LiveHost::stop` and the quantum of step 7: a `songOver` timer of 0.3 s (the web's `frame()` rule, `render.h:147` note (3)); the host never destroys itself with a quantum owed: `destroy` runs one `midi_begin` / `midi_end` after `thunder_midi_stop`, then the drain | `live_midi_stop`: STOP at 20 positions of a song with notes held: every note-on has its note-off or a CLEAR; stats `may_hang` = 0; song-end case (`song-end` of `test_midi_glue.cpp`) |
| D-3 | `clock_map` once per audio callback AFTER the block, with the shell's own (frame, host time) pair | Depends on D6: with the page-side sink, the pair is the page's `mapUpd` pair (`web:index.html:17909-17924`) and the map lives in JS; with the worklet-side pump, the page posts `{contextTime, performanceTime, outLatency}` every pump tick and the worklet calls `thunder_midiout_clock_map` right after `process` (`midiport.h:119`). Section 6 | `live_clock_map`: records get host times within 1 ms of the expected for a synthetic pair; a stale pair (no update for 2 s) is counted (`THUNDER_MIDIOUT_STALE_NS`); first-map hold (the verify-274 F6 hold burst test, `native/tests/verify_274.cpp`) |
| D-4 | `Players::setDropLate(true)` | `LiveHost` constructor, once (`players.h:41`). The web plays late core notes at once (`web:index.html:25308-25310`); native skips notes over 0.15 s late: "pads and gear agree" (told to the owner, PICKUP "Decisions made") | `live_late`: a stall of 0.4 s then a burst: no note in the burst, `notesLate` counted, a late OFF still applied (`test_players.cpp` `testPlayersLate`, verify-274) |
| D-5 | `Players::stopTrack` for reset track | Row 12 | `stptrack` (merged) through the live host: republish then stopTrack; a voice booked 1 ms after is silent; the live pool also stops (verify-271 F1, `stptrack_live`) |
| D-6 | Republish the Seq snapshot on every chain-patch edit (the web's `rebus`) | Row 2. The page's `coreDiff` already reports `chainPatch` / `chainOn` changes as ops; the bridge turns ANY op on those keys into a song publish. `takeOver` copies `hearT_` from the PLAY's, so no further call for a cable edit (`WEB-FOLLOW-20NOV-NOTES.md` "For the lead" 4) | `live_rebus`: the `seq-edit-bus` golden (rebuilt from the web's own `rebus`, `seq-golden-web.mjs --edit`) through the live host: events equal from the row after the edit |
| D-7 | `eng_env_lines_fit` with the song's bpm and lpb before a render, and again on a settled tempo change (verify-271 F4 (a) to (d)) | **Not done in the core in U37: pad renders stay with the page** (scope 3). The page already does it: `renderAsync` posts `songBpm()` / `songLpb()` (`web:index.html:8892`), `envLinesFit` runs in the render (`:5065`), and `lnTempo` re-renders after 300 ms quiet / 0.5 % (`:8905-8913`), feeding row 3a → 4. The C++ function (`engine/env_lines.cpp`) has no caller and gets none until U34c. F4 (d) (`relOf` gating: only `on && level > 0 && mode ADSR` layers; `max(1, gRel || 1)`; `max(30, r)` for blip / x-engines) is the page's `relOf` (`:5013-5015`) and stays there | browser: a lines-mode pad song, tempo changed during play: after 300 ms a new pad arrives, the snapshot is republished, the old buffer is freed only after its voices end (epoch test), the sound after equals the page's own re-render (when U34c comes the same test runs core-side) |
| D-8 | verify-274 F4 list: (a) first block, (b) quantum with no steps, (c) end of song, (d) `midi_begin` failure, (e) lifetime | (a) first block: records are HELD until the first clock map, then note-ons / CCs over 150 ms old are dropped (`midiport.h` BEFORE THE FIRST MAP); with the page sink the first `clock` message plays that role. (b) a quantum with no step still runs `midi_begin` / `midi_end` (`render.cpp:101`, `:109`): keep it in the live loop (step 4 and 6 are unconditional). (c) song end: the scheduler is not stopped by the renderer; D-2. (d) `midi_begin` can only fail for a block length outside 0..1 or a non-finite `now` (verify-274 F4): the live host asserts in debug, and in release counts a new `midi_begin_failed` stat and posts `thunder_midi_panic` once on the first failure (the return values are ignored silently today). (e) lifetime: `midi_` is a raw non-atomic pointer: the handle outlives the host; `setMidi(nullptr)` from the control side while the audio side runs is a race in the shells and impossible in the web; the live host owns the `thunder_midi*` itself (created and freed with it, never given to the page) | `live_midi_life`: create, play, destroy in every state (playing, stopped, after song end, mid-first-block); ASan/UBSan; TSan thread test for the shell builds |
| D-9 | prewire F2 + F3 (snapshot + deferred free), pump on one thread, the live route idle skip | 3.4 | `live_pads_epoch` (a pad replaced while a long sample sounds: no read of freed memory under ASan, the sound continues); `rt_harness` no-allocation over `LiveHost::process` with pads republished; ThreadSanitizer for the shells' build |
| D-10 | Seed the tempo from `seq.bpm()` (`SEV_TEMPO` is not emitted at the start); fixed 128 quanta; `Mixer::set / setPlan / setChain` audio-side only; fx `plan` control / `commit` audio with the old plan returning (prewire "What U37's host must do") | 3.5, rows 2, 7, 8, 10 | `live_tempo_seed`, `live_block_sizes` (the host called with 1 / 64 / 128 / 480 / 4096 frames gives the same samples), `live_fx_swap` |
| D-11 | `eng_env_lines_fit`'s siblings from WEB-FOLLOW: a host that loads a sample into a pad follows `assignSample` / `smpOwnTune` (`web:index.html:16219-16220`) | stays in the page (it owns sample loading) | page tests on `beta` (unchanged) |
| D-12 | The pump holds records until the first map and drops stale note-ons; the gone-TOKEN fix | D-3, D-8 | as D-3 |
| D-13 | "Then the full wasm target" | 2.1, work breakdown P3 | `parity-u37` (section 8) |

## 6. Timing

**The audio clock.** In the worklet: `currentFrame` (an integer, always a multiple of 128 at a block start), `currentTime` (= `currentFrame / sampleRate`), `sampleRate`. These are the only clocks it has. `Date.now()` exists there (the JS core uses it for its own timing: `web:core/core-processor.js:310`, `:410`) but is the wall clock in whole milliseconds, a different epoch from `performance.now()`; `performance` itself is not exposed in an `AudioWorkletGlobalScope` in Chrome as far as I know: **unverified here, test it in Chrome before relying on either** (section 11).
Proposal (D11): the core's frame counter IS `currentFrame` (set from it at each block's start, never counted separately), so "frames on the core's timeline" (CORE-BRIDGE rule) equals the context's, and the page converts a context time to a frame with `Math.round(t * sampleRate)`. A suspend / resume of the context does not advance `currentFrame`, so the timeline has no hole. The alternative (the core counts from 0 at creation and the page learns an offset from the first `clock` event) works but puts one more conversion in every message.

**`setSeqStart(0.08)`.** The page starts its sequencer 80 ms after PLAY (`web:index.html:24622`: `ctx.currentTime + 0.08 + ci + (warmNew ? 0.15 : 0)`; the 0.08 gives SPP its 50 ms and a Gear delay its room, `render.h:147-151`). The live host takes `t0` from the page in the `play` message and starts the `Seq` there, as the JS core does (`seq.time = d.t0`, `core-processor.js:423`). The page's own extras (the count-in `ci`, the Link plan `lkp`, the first-PLAY warm-up 0.15 s) stay the page's: they only change `t0`.

**`clock_map` and the host time pair.** `thunder_midiout_clock_map(frame0, host_ns0)` wants "core frame `frame0` is AUDIBLE at host time `host_ns0`" (`midiport.h:115-119`). Candidates in a browser:
- `ctx.getOutputTimestamp()` → `{contextTime, performanceTime}`: main thread only; jitters by a few ms call to call and returns zeros before the device has started (the page already handles both: `web:index.html:17909-17913`, `:17908` comment). Its `contextTime` is the sample frame being output now (latency included), so the pair is already "audible". The page smooths it (`amap`, 2 % per call, a jump over 50 ms taken at once: `:17921-17922`) and adds a floor of `baseLatency + outputLatency` (`:17914-17920`, the 7 Nov fix: "never sooner than reported"). The core's `clock_map` does the same smoothing (2 % per 25 ms of host time) and has `set_out_latency` for the floor (`midiport.h:114-119`).
- `performance.now()`: main thread (and Workers); not in the worklet. Resolution is clamped without cross-origin isolation (about 100 microseconds in Chrome; unverified): enough for MIDI at millisecond scale.
- In the worklet only `currentTime` / `currentFrame`: no host time at all.
So the pair must be MADE on the page and carried to wherever the map lives. The page's pump tick already runs every 25 ms from a Worker timer (`pumpTimer`, `web:index.html:24709-24716`; it keeps its rate in a hidden page, `:24697-24700`) and calls `mapUpd` (`moClock`, `:18204`). Two designs (D6):
- **(A) the map and the queue stay in the page** (recommended): the worklet drains `thunder_midi_drain` records after each block and posts them; the page maps `frame / sr` to a performance time with its existing `a2p` (`web:index.html:17925`) and sends through its existing outbox (`msend` / `mobPut`, `:17928`, `:17952`), which can hold a message back until 60 ms before its time (`MOB_AHEAD`, `:17951`) and drop it on STOP (`mobDrop`), and the hidden-page ledger (`moledCut`, `:17975`) that undoes a note Web MIDI cannot recall. `thunder_midiout_*` (port table, token table, queue) is not used in the web. `clock_map` as a function is then replaced by JS that already exists; D-3 is satisfied by `mapUpd`.
- **(B) the core's pump runs in the worklet**: the worklet calls `thunder_midiout_pump` once per block (the audio thread is also the sender thread: one thread in each role, which the headers allow: `midiport.h` THREADS), its `sink.now_ns` extrapolates the last page pair with `currentFrame`, `sink.send` posts `{dev, bytes, at_ns}` to the page, and the page calls `output.send(bytes, ts)` straight away. Advantage: the tested U36 queue, CLEAR rules and stale-drop. Disadvantage: with `CAP_TS` the pump hands a message over 20 ms early (`THUNDER_MIDIOUT_SEND_AHEAD_NS`, `midiport.h:46`); a main-thread stall of more than 20 ms (the app has these: `web:index.html:25126`ff "SLOW WORK") sends it late, and Web MIDI cannot recall what was handed over, so a STOP's CLEAR cannot cancel messages already past the page. The page outbox (A) holds 60 ms in the page, which is the same exposure with a longer window but under the page's control.
  **Recommendation: (A) for the web host, (B) design kept for the shells.** The cost of (A): the page keeps its own copy of the "late" rules; but `MidiSched` already drops late notes and ticks (`late_ticks`, `late_notes`, `midi.h`: "B7: dropped, never sent in a burst"), so the page only has to send.
Either way the MIDI record's `frame` EXCLUDES the device latency (`midi.h:6-8`); in the web the map adds it (the floor above), so the Gear delay setting keeps its meaning.

**MIDI out through Web MIDI from the core's scheduler.** One drain per block in the worklet (`thunder_midi_drain`, consumer role; `midi.h:106-108`), a transferable `ArrayBuffer` of 16-byte records only when non-empty, at most once per block. Records carry `port` (A to D), `cable`, `flags` (`THUNDER_MIDI_F_CLEAR`). The page: port letter to `midiOut(p)` (`web:index.html:17910`ish `const midiOut`), `F_CLEAR` drops what the outbox holds for that cable's outputs (the helper's `clear()` semantics; Chrome has none, so the ledger covers the rest). The scheduler needs, from the page: the song's MIDI instruments (`thunder_midi_set_song`, at PLAY and on instrument edits), the four ports (`set_port`: connected, clock box, Gear ms, cable id = output id so two letters on one cable are one stream), `set_fx_latency` and `set_audible` every block (done inside the host from the mixer, `render.cpp:101`), the lead (capped 0.5 s: the page's hidden lead is 1.2 s, `:25079`; with a 0.5 s MIDI lead a hidden page loses what the web sends up to 1.2 s ahead; D6 notes it: either raise the core's cap to 1.2 s or accept 0.5 s hidden).

**`setDropLate`.** On, always (D-4). `Players` skips a NOTE more than 0.15 s behind the quantum start; the web plays such notes at once (burst) for pads but the gear path sent them. The owner has been told the new rule (PICKUP "Decisions made for the owner").

**Hidden page.** The page's timers are throttled; the pump Worker is not (`web:index.html:24697-24700`). The audio thread does not depend on the page at all while native plays: it steps the song itself. What needs the page while hidden is only MIDI out (records sent as they arrive: the page's hidden path sends them with their timestamps straight away, `:17933-17934`) and Link / clock-in, which are outside U37.

## 7. Sample and instrument data

**How decoded audio gets into wasm memory.** The page owns the decoded pad: `cache[i]` = `{data: Float32Array, data2: Float32Array | null, sr}` (`web:index.html:25054`; a stereo pad keeps its right channel in `data2`). The path: page copy (`slice()`) → transfer to the worklet → `thunder_alloc(n)` + a copy into wasm memory → the pads snapshot (ownership taken, 3.3) → the old buffer freed by the epoch rule (3.4). A MUT / GEN variation goes the same way (`coreVar`, `web:index.html:25050`; keyed instrument : variation : pad; the core's `set_variation` / `RenderPads::setVar`, `render.h:67-77`); a pad's buffer change drops its variations, so "set the pad first, then its variations" (`include/thunder/render.h`, set_pad). A SoundFont pad: see D9.
**Memory growth.** A grow detaches every `Float32Array` view over the old buffer: after any call that allocates, the worklet re-makes its views (the `pumpIO` pattern, `web:index.html:20586`). `memory.grow` can fail; `malloc` then returns 0.

**Figures** (all from `include/thunder/render.h` MEMORY unless stated; Release GCC x86-64 measurements, NOT wasm, NOT the phone):
- The mixer's delay rings dominate: ELECTRO with no pads about 53 MB at 48 kHz (143 MB at 192 kHz); with effects and the master chain about 94 MB (218 MB at 192 kHz). The rate in the web is `ctx.sampleRate`: `aset.rate` 0 / 44100 / 48000 (`web:index.html:8802`, `:15947` segment), the Fold uses the device rate.
- Song parse tree about 8.6 times the file (ELECTRO 0.86 MB), up to 15.5 times for a hostile one; the vars up to 18.6 times. Each republish (row 1) makes a new one while the old is alive: at least double during the swap.
- Pads: 4 bytes a sample per channel, copied; the render API's default limit is 512 MB for pads and variations together (`THUNDER_RENDER_MAX_PAD_BYTES`), one pad at most 2^28 frames.
- A `SeqSession` is capped at 8 MB of roll pages and counters (`seq/session.h:62`, `kSeqSessionMaxBytes`); it only grows with the edits of one PLAY, so the host must start a fresh session when `sessionFull()` (a PLAY-start restart point: the lead chooses where; SEQ-TAKEOVER-BUILD-NOTES "Choices for the lead").
- Live-engine nodes are made up front, one per masked track: bass 57 KB, acid and mach 38 KB, vox 235 KB per pad per track (`engine/live_pool.h:10`); a vox pad on 33 tracks is about 7.7 MB.
- `SeqEvent` buffer 12288 x 456 B = 5.6 MB once (`seq/seq.h:81`); voices' rings about 4 MB for 64 voices (WIRING-DOSSIER 1.2).
- The wasm module itself: not measured (the stand-in is small; the real one links the effects, pump, engines, SoundFont decoder). Unknown.

**Limits on phones.** The owner's phone is a Galaxy Z Fold 6 (memory file; `docs/port/RESULTS-N0.md` ran on it). Not found anywhere in this repo: the Fold's Chrome per-tab memory ceiling, or V8's wasm memory limit on that build. A wasm32 module can address 4 GiB; the tab usually dies well before that on a phone. I could not measure it. Proposal D7: a hard budget the bridge enforces BEFORE the first `put`: 256 MB of pads and variations on a computer, 128 MB when `seqHandHere()` is true (`web:index.html:25189`), the figures to be replaced by the Fold's measured numbers; the sum of the figures above must fit under it with the effects on.

**Out of memory.** The wasm build has no exceptions: an allocation that fails inside the core is an `abort()` / `unreachable` trap (`include/thunder/render.h` OUT OF MEMORY; `render_api_noexc.cpp`), the node raises `processorerror` and stays silent. So the rule is "never get there": (1) the budget check above refuses a `put` that would pass it (reply `E_LIMIT`, fall back, toast with the size); (2) `thunder_alloc` returns 0 when `memory.grow` fails and the bridge treats 0 as a refusal; (3) the host's own allocations in the control path use `new (std::nothrow)` and return an error code, NOT `bad_alloc` (needs a lead rule for every `std::vector` growth in `LiveHost`); (4) `processorerror` and the missing heartbeat (2.4) catch the rest; (5) on any of these the page stops the transport, drops the node and the next PLAY uses the JS core. The page's own copies (cache, mirror strings) are the larger risk on a phone; not measured.

## 8. Equality testing

**What exists** (all found in this branch):
- The sequencer: goldens made by the WEB's own `makeSeq` (`native/tools/seq-golden-web.mjs`, reading the pinned web at `native/tests/web-ref`, `BETA-COMMIT` = cdd9439): `seq-electro*.json`, the live-clock set (`seq-electro-live-{time,bpm,shift}.json`), 24 `seq-edit-*.json`, `seq-chop-*.json`; ctest `seq`, `seq_takeover`, `seq_edit_current`, `seq_chop`, compared row for row (`WEB-FOLLOW-20NOV-NOTES.md` "Tests"). Exact.
- The core's own timing vs the page's: the `render-compare.sh` gate: ELECTRO within -80 dB of Chrome's offline render (-87.4 dB measured), with `--fx` and `--master` (`native/tools/render-compare.sh:1-20`); the reference is the page's `renderOut` steps run in headless Chrome (`native/tools/song-ref.mjs`). The voices are timed as the page's player for that gate (`page_timing`), NOT as the core's: "a render made by the page and one made by the core differ in waveform ... each is right by its own rule" (`WORKPLAN.md` §12 "Voice timing", the owner: "I like sample exact").
- The mixer and master against real Web Audio: `mix-webaudio-check.mjs`, `master-webaudio-check.mjs` (Chrome + playwright; not in ctest).
- The live route: `live-route-dump.mjs` decision-log golden and `test_live_*` (`LIVE-ROUTE-DESIGN.md` L10).
- MIDI: `test_midi_ledger.cpp` + `golden/midi-ledger-expect.json` (the 18 ledger songs; every difference from the web an exact rule); `test_midi_glue.cpp`, `test_midiport*.cpp`, `verify_274.cpp` probes.
- A browser harness pattern: `tools/seq-golden.mjs` and its `--core` mode run in CI under Chrome via playwright (`.github/workflows/tests.yml:31-33`); the page exposes `window.__seqTest.run` and `.runCore` (`web:index.html:38418-38433`, only with `?seqtest`). **WORKPLAN's gate** ("`seq-golden --core` equal across Page, Audio-thread and core.wasm", `WORKPLAN.md:160`) needs a third `runNative` and a way to get EVENTS out of the native host: **the live host emits audio, not rows** (the JS core posts rows because the page plays the non-core ones; native plays everything). A test-only export (`thunder_live_dump_step`) that returns each step's events would do; it is not on the audio path.

**What a browser-side A/B needs and does not have:** (1) the `LiveHost` and `core.wasm`; (2) `__seqTest.runNative` and the event export; (3) a way to run the worklet deterministically: an `OfflineAudioContext` accepts `audioWorklet.addModule` in Chrome, and a worklet node in it processes 128-frame blocks as fast as it can; **unverified for a module that receives a pre-compiled `WebAssembly.Module` by `postMessage`**; (4) pads: the page renders them (its Worker) and the harness reads them from `cache[i]`, so the same samples feed both routes; (5) `page_timing` on the live host for the JS comparison (the existing `VoicePool::setPageTiming`, which "is never on in an app").

**Proposed gates** (tolerances are the project's own, `WORKPLAN.md:205ff`, `render-compare.sh`):
1. **wasm vs native, in Node, no browser** (`native/tools/parity-u37.sh`, in the pattern of `parity.sh u02/u03/u18`): the same `LiveHost` message script run through `core.wasm` (in Node, a harness that mimics the worklet: 128-frame calls, the same message order) and through the native library: event streams EXACT (all four electro goldens, the live-clock, edit and chop goldens); audio within -100 dB of the case's peak (libm `sin` / `pow` differ between wasi-libc and glibc; `js_exp` is already the core's own; WORKPLAN §9 "pow and sin to match V8"); bit-exact where no libm function is involved (the render tests already show which).
2. **worklet glue** (browser, `OfflineAudioContext` or a real-time context captured with a recorder worklet): the page's glue (message order, the `t0` conversion, `put` chunking, snapshot republish during play) over ELECTRO and three census songs against gate 1's Node output: bit-exact expected (same module, same messages) — it proves the glue, not the DSP.
3. **JS engine vs wasm route** (browser, the existing `song-ref.mjs` reference B vs the live host with `page_timing = 1`): within -80 dB (the existing gate); with core timing, report only, as `render-compare.sh` does.
4. **Edits during play**: the 24 `seq-edit-*` cases through the live host (publish at the same step as the golden's patch): events equal; plus a pad replaced while a long sample sounds (epoch test).
5. **MIDI**: the ledger songs' stream from the live host equals the web's `runBlocks` stream plus the ledger's expected rules.
6. **N1 exit** (WORKPLAN `:163-166`): ELECTRO and three census songs through `core.wasm` within -80 dB; CPU of `core.wasm` on the reference song not above today's in headless Chrome; then the owner listens to three songs with the switch on (desktop web copy).
Where the native Seq chops, the JS audio-thread core does not (`seq-core.js:5`, the owner's open "slices" bug); gate 3 must therefore compare against the PAGE sequencer for any song with `slc > 1` or `sync`, not the audio-thread core.

## 9. Work breakdown

PRs in order. Each is sized for one Sonnet builder (about one session) and has a done-test. "Lead" = the lead writes it, or reads it line by line before merge, as WORKPLAN §2 says for F-review units.

| PR | What | Done when | Lead |
|---|---|---|---|
| P0 | **Rulings** on section 10 and a frozen header `include/thunder/live.h` (the C API of 2.1, with the thread rule of each call in its comment) | the header compiles in C and C++ (`test_render_c.c` pattern) | WRITES the header and the rulings |
| P1 | Pads snapshot + deferred free: `PadsSnap`, `Seq::setChop`, `Players::setPads` use, epoch scan in the pools (`voice.cpp`, `sf_live.cpp`, `live_pool.cpp`) | `live_pads_epoch` under ASan; every existing render / dispatch / players test bit-identical (the move gate of `LIVE-ROUTE-DESIGN` L7) | READS line by line (audio-thread code; touches `voice.cpp`) |
| P2 | `LiveHost` core: command ring, `SeqRunner` + `seq_adopt` + pads adopt, quantum loop of 3.5, play / stop / hold / loop / bpm / lead / stopTrack / mix.set, songOver timer, stats (no effects, no master, no MIDI yet) | `live_*` tests: ELECTRO through `LiveHost` equals `SongRenderer` bit for bit at block sizes 1 / 128 / 480 / 4096; `rt_harness` no allocation over `process`; the 24 edit goldens at a publish | WRITES or READS line by line (the quantum order, adopt order, EXACT STOP, takeover) |
| P3 | Effects and master live: `FxHost` plan / commit ring, `FxBank` for a running host, `MasterChain` swap, PDC (`pdc_plan` on the control side, `setDelays`), meters | `live_fx_swap`; `render-compare.sh --fx --master` equal through the live host | READS (`fxhost` commit, master swap = new code) |
| P4 | MIDI in the host: `setMidi` wiring, D-1, D-2, D-8, `midi_begin_failed` stat, drain, the first-map rules for the page sink | `live_midi_start`, `live_midi_stop`, `live_midi_life`, the ledger stream | READS (it is U36's F-review area) |
| P5 | C API `live.h` implemented + C test + the no-allocation and sanitizer runs | `test_live_c.c`; ctest under ASan + UBSan; TSan for the shells' threaded build | READS the API layer (argument checks) |
| P6 | wasm target: CMake `thunder_wasm_core` linking tfx, pe, mods, pump, engines statically; exports; stack size; CI job; the Node harness and `parity-u37.sh` (gate 1) | the wasm CI job builds it; gate 1 passes | READS the CMake and the export list; decides the `std::thread` question (section 11) |
| P7 | Worklet + bridge object (`native/web/`): module load by pre-compiled `Module`, block loop, message translation of the JS core's ops, `put` chunking, `thunderCore` over the node port, heartbeat | a Node + jsdom-free unit test of the translator; gate 2 in Chrome | READS the worklet (audio thread, memory views) |
| P8 | Page glue behind the switch (the B3 exception in `index.html`): third segment value, `corePost` routing, `nativeCan(S)`, fallback and toasts, meters / playhead from events, output wired to `master`, MIDI sink (A) | `npm test` on `beta` unchanged with the switch off; `seq-golden` (page, core) unchanged; gates 2 and 3 | READS the `index.html` diff (it is the app's file) |
| P9 | `sw.js` precache entries and the publish route for a web copy of `native` (D12) | a page at an agreed address serves the module | the lead and the owner |
| P10 | Browser A/B in CI (`tests.yml`-style job, playwright): `runNative`, gates 2, 3, 4 | CI green | READS the gate definitions (a gate that is wrong is the lead's) |
| P11 (separate unit) | **Chains host** and pump effects in the song path (finding 2), only if D1 says so | the web's bus / chain songs match the page within -80 dB | WRITES the design (it is a new hook family) |

What the lead must write or read line by line, in one list: `live.h`; P1 (voice lifetime); P2 (quantum order, adopt order, EXACT STOP); P3's master swap and fx commit; P4 (MIDI start / stop / life); the worklet (P7: views after growth, the pair, the heartbeat); the `index.html` diff (P8). Everything else is builder work with a numeric gate.

## 10. OPEN DECISIONS for the lead

Each: the question, the options (trade-off in one line), my recommendation.

**D1. Chains, bus tracks and pump effects in songs.** The core has no chains host and no pump slots in a song (findings 2). (a) Ship U37 without them: such songs fall back to the JS core with a toast; fast, honest, the owner's chain songs do not test the core. (b) Build the chains host (P11) before the switch exists; the switch means something for the owner's real songs; adds a large unit. (c) Ship (a) now, P11 in parallel. **Recommend (a) then (c)**, and ask the owner how many of his songs use chains (O4).
**D2. Control work on the audio thread.** (a) one instance, accept and measure; (b) add a C++ `Song::patch`; (c) wasm threads with cross-origin isolation (Electron and shells only). **Recommend (a), (b) only if a measured stall exceeds about 2 ms, (c) is the shells' design.** Needs a first measurement of `Song::load` for ELECTRO and a census song in wasm (nobody has).
**D3. How an edit reaches the core during play.** (a) whole song text, debounced 250 ms or more; (b) diff ops translated to a patch (D2 b); (c) republish only at PLAY and on a "settled" edit (nothing mid-note). **Recommend (a).**
**D4. Message names at the page boundary.** (a) accept the JS core's ops (`load song patch kit buf play stop ...`), translate in the bridge; (b) switch the page to CORE-BRIDGE's names (`song.load`, `buf.use`). (a) keeps the `index.html` diff to `corePost` and `coreWanted`; (b) is the contract the shells use. **Recommend (a) for the web, with the bridge object exposing (b) on `window.thunderCore` so the shells share the API.**
**D5. Pad buffer lifetime.** (a) epoch + `minLiveEpoch`; (b) refcounted buffers; (c) a synchronous check, web only. **Recommend (a)** (one design for web and shells; per-pad, not per-snapshot).
**D6. MIDI sink in the web host.** (A) page-side outbox fed by `thunder_midi_drain`; (B) the core's `thunder_midiout_pump` inside the worklet with JS sink imports. **Recommend (A) for the web, (B) for the shells.** Sub-question: raise `thunder_midi_set_lead`'s 0.5 s cap to 1.2 s for a hidden page, or accept 0.5 s hidden (**recommend raise**: it is one constant, `midi.h:88`).
**D7. Pad memory budget.** (a) 512 MB (the render API's), (b) 256 MB desktop / 128 MB phone, (c) measure on the Fold first. **Recommend (b) now, replaced by (c)'s numbers.**
**D8. Live-engine kinds with no native live player** (pump synths, xeng kinds, waves, LXR). (a) play their rendered twin on the SAMPLER route (what `thunder_render_set_pad_route` says a host may do); (b) refuse the song. (a) sounds slightly different from the page's live node; (b) loses more songs. **Recommend (a), with one toast per PLAY naming the pads.**
**D9. SoundFont pads (the GM font is 24 MB; a preset decodes to float32 at 28 MB an octave, 191 MB the full piano).** (a) fall back in the first release; (b) decode in a second wasm instance in a Worker and hand over PCM (no such API: `set_pad_sf` takes font bytes); (c) decode in the host's control side in the worklet (a long stall on the audio thread). **Recommend (a), then (b) as its own PR.**
**D10. Stretch (`sync` 2 / 3, `sx`).** (a) fall back; (b) play unstretched with a warning (what the core does today, `players.cpp:77`). **Recommend (a): the page's stretch is audible and the owner has used it.**
**D11. Timeline.** (a) the core's frame counter equals `currentFrame`; (b) counts from 0, the page learns an offset. **Recommend (a).**
**D12. How the owner gets to try it.** (a) a web copy of `native` published by a new `native-web.yml` to a Pages folder (needs a ruling because `make-channel.sh` refuses other names and the beta job commits to `main`); (b) a desktop beta built from `native` through `desktop.yml`'s pattern (Electron `app://`; headers available for D2 c); (c) local only. **Recommend (b) first** (the owner already installs desktop betas: PICKUP "A desktop beta is: `desktop.yml` ..."), (a) later. `sw.js` and `index.html` are both edited under the B3 exception.
**D13. Where the core's output is wired.** (a) the page's `master` gain (`web:index.html:8807`); (b) straight to `ctx.destination`. (a) keeps the device volume and the page analysers; (b) is cleaner. **Recommend (a).**
**D14. After a failure, restart on the JS core or stop?** (a) stop and tell (the JS core's DEAF CORE handling does this); (b) restart the song on the JS core from the same row. **Recommend (a).**
**D15. Meters, spectrum and the recorder while native plays.** (a) feed the page's meters from the core's `meters` events and leave the recorder out (O3); (b) leave them dead. **Recommend (a).**
**D16. Hand-played pads and keys while native plays.** (a) they go through the page graph and do NOT take over the song's tracks (the JS core's `coreTake` interplay, `web:index.html:25322`, is lost); (b) wait for `note.on` in the core. **Recommend (a) for U37.**
**D17. The sequencer's random seed.** (a) a `Date.now()`-derived seed from the page per PLAY (the JS uses `Math.random`); (b) the golden seed. **Recommend (a); tests pass the golden seed explicitly.**

### For the owner (plain words; only he can answer)
- **O1.** Where do you want to try the switch: in a desktop beta of the app, in a web page, or only on your computer from a folder? (This decides D12.)
- **O2.** If a song uses something the new engine does not play yet (chains, pump effects, stretched samples, SoundFont sounds), is it fine that the old engine plays that song, with a short message saying why? Or would you rather it refuse?
- **O3.** While a song plays on the new engine, the level meters, the spectrum and recording from the mixer come from the new engine, and keys you play by hand still sound through the old one. Is that acceptable for a test?
- **O4.** How many of your songs use chains or the pump effects? Which three songs do you want to listen to for the first test?
- **O5.** Late notes after a stall are skipped, not played in a burst (told on 20 and 21 Nov): still OK?
- **O6.** The phone: switching on the new engine in the web page will not fix the phone's clock problem (it still runs inside Chrome's audio worklet); only the Android app will. Do you still want a phone test of the switch, or desktop only?

## 11. Risks, and what I could not find or am unsure of

**Risks**
1. **The edit stall (3.1).** A whole-song re-parse on the audio thread, never measured. If it exceeds a quantum a click follows each edit during play. Gate: `renderCapacity` underruns in an edit-while-playing run.
2. **Chrome's phone clock (the reason for the port) applies to the web switch** (`WORKPLAN.md:13-15`): `core.wasm` in a worklet is "a module loaded". The switch proves the engine; it does not fix the phone.
3. **Three copies of a pad during load** and the page's own caches: a phone could run out before the core does. Unmeasured.
4. **Linking the DSP into one wasm.** Separate modules build today (`build-wasm.sh`); one module with tfx + pe + mods + pump + engines + SoundFont decoder has not been built. `fxpump.cpp:97-101` uses `std::mutex` and `std::this_thread::yield()` in a spin (`CtlGate`): harmless on one thread (the audio flag is 0 between blocks) but it must compile and link without pthread (the test CMake says wasi has no threads, `native/tests/CMakeLists.txt:856-858`). The dossier raised it as Q5 and R7; the wasm job still builds only the stand-in.
5. **No chains host, no song-level pump effects, no stretch, no LXR** (findings 2, scope 6, 7): real songs will often fall back until P11.
6. **`LiveHost` is new code on the audio path** with the lead's line-by-line review owed: the order of adopt, runner, MIDI and dispatch; EXACT STOP; the takeover state.
7. **`Seq` start offset and the first-PLAY warm-up**: the page's `t0` can be later than the page's 0.08 when live nodes are being made (`warmNew`, `web:index.html:24622`); a `t0` already past (a stalled page) must be handled as `SeqRunner`'s catch-up skip does (`runner.h:21-22`, `:53`).
8. **Two engines at once**: the page graph still exists and can sound (previews, keys); levels and the limiter are not shared with the core's master chain; peaks can exceed what either limiter sees. Not analysed.
9. **The published copy question** (finding 5) could block the whole test.
10. The two docs that disagree with code: `verify-274.md` F1 point 2 says "`clock_map` before the block (F6)" while `PICKUP.md` and `render.h:149` and `midiport.h` say AFTER the block (the header says order is not needed for correctness: nothing is lost before the first map). The code and headers win: after the block. Also `WIRING-DOSSIER.md` is older than the live route, `Players` and `LivePool` (it still lists the live engines as unwired and R11 as open: both changed); and CORE-BRIDGE section 3 names (`song.patch`, `buf.use`) are not what the page speaks (2.3).

**What I could not find (searched, not there)**
- Any C++ or JS glue for a web host in `native/`: `grep -rIl "worklet\|wasm\|bridge" native` lists only tests, tools, the LXR module and the Android shell; the Android `thunder_bridge.js` (`native/android/app/src/main/res/raw/thunder_bridge.js`) is the only `window.thunderCore` implementation and speaks JSON strings (N0), not the CORE-BRIDGE objects.
- A `docs/port/U34C-*` or any note on pad renders through the core (U34a / U34b notes exist: `U34A-NOTES.md`, `U34B-NOTES.md`).
- Any live (non-offline) C API: no `live.h`; `core.h` is the N0 stand-in's.
- Any measurement of `Song::load` time, of `LiveHost`-like block cost in wasm, of wasm memory on the Fold, of the wasm module's size with the real core.
- Any COOP / COEP / `SharedArrayBuffer` use (searched `electron/`, `web:index.html`, `web:sw.js`, `web:master-worklet.js`).
- Whether `performance` is exposed in `AudioWorkletGlobalScope`, whether an `OfflineAudioContext` can host a worklet fed a `WebAssembly.Module` by `postMessage`, and what a variable render quantum (`renderSizeHint`) would do to a 128-frame assumption: Chrome behaviour, not verified here (no browser was run).
- `FxBank` pump-slot handling: only the one comment in `fx/fxbank.h:32`; I did not read `fxbank.cpp` end to end.
- The index.html at the root of `native` is not the `beta` one; line numbers `web:` are `beta` cdd9439 fetched shallow (`git fetch --depth=1 origin beta`).
- `seq.cpp:993` (`SEV_TEMPO`), `voice.cpp:186`, `fxpump.cpp:143-150` and `mix/mix.cpp` details are quoted from `prewire-19nov.md` (line numbers of c97da6f): re-check before editing.

## 12. The lead's rulings (21 Nov, 11:20; Fable, lead of 21 Nov)

Checked by the lead in the code before ruling: finding 1 (no `LiveHost`, no `live.h`: `native/core/include/thunder/` has `core.h lxr_seam.h midi.h midiport.h render.h rtlog.h` only) and finding 2 (`Mixer::setChain` has no caller outside `mix/`; `render.cpp:17`, `:36` say no chains host). Both are true. The pump-effects-in-a-song claim of finding 2 was NOT checked by the lead (the author did not read `fxbank.cpp` end to end): P3's builder confirms it first.

**The order that follows from finding 1.** `LiveHost` and `live.h` are needed by the web switch AND by every shell (N2 Android first). So P0, P1, P2, P4, P5 are the shared road; P6 to P10 (wasm, worklet, page glue, browser A/B) are the web host only. The web switch does not fix the phone (section 0, last paragraph); the Android shell does. The lead decides after P5 whether the web host or the N2 shell is wired first, with the owner's answer to O1 / O6.

| # | Ruling |
|---|---|
| D1 | (a) then (c): U37 ships without chains / bus tracks; such songs fall back to the JS core with a message. The chains host is its own unit (with U31, the …chains engine, which it needs), started when a compiling slot is free. |
| D2 | (a). Measure `Song::load` for ELECTRO and one census song (native and wasm) in P6 before anything else is built on it; (b) only above about 2 ms; (c) is the shells'. |
| D3 | (a) whole song text, debounced 250 ms. |
| D4 | (a), with the CORE-BRIDGE names on `window.thunderCore` for the shells. |
| D5 | (a) epoch + `minLiveEpoch`, freed PER PAD, not per snapshot. One design for web and shells. |
| D6 | (A) for the web, (B) for the shells. The `thunder_midi_set_lead` cap goes from 0.5 s to 1.2 s in P4, with a test at 1.2 s (ring sizes checked for the longer lead). |
| D7 | (b) 256 MB computer / 128 MB phone until measured on the Fold. |
| D8 | (a) the rendered twin on the sampler route, one message per PLAY naming the pads. |
| D9 | (a) fall back; (b) later as its own PR. |
| D10 | (a) fall back. |
| D11 | (a) the core's frame counter is the host's (`currentFrame` in the web, the callback's count in a shell). |
| D12 | The owner's (O1). Until he answers: nothing is published; P0 to P5 do not need it. |
| D13 | (a) the page's `master` gain. |
| D14 | (a) stop and tell. |
| D15 | (a) meters from the core's events; the recorder is left out of the test. |
| D16 | (a). |
| D17 | (a); tests pass the golden seed. |

Asked of the owner on 21 Nov: O1 (where to try the switch), O2 (fall back with a message, or refuse), O4 (how many songs use chains or pump effects; three songs to listen to). O3, O5, O6 are decided as the recommendations unless he says otherwise (O6: desktop only).

Started 21 Nov: P1 (pads snapshot + deferred free) with a Sonnet builder on `n1/u37-p1-pads`. P0's `live.h` is the lead's and is written while P1 builds.
