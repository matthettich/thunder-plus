# U37 design draft: bridge and host switch, `core.wasm` behind the Settings switch

DRAFT, partial push 1 of 2 (sections 1 to 3 written; 4 to 11 follow in the second push). For the lead to rule on; nothing here is built.
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
(Section 3 continues in the second push.)
