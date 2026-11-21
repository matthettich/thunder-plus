# U37 P0 notes: `live.h` and `LiveHost`, the contract (draft for the lead)

Branch `n1/u37-p0-live-h` (base `native` 56ffdc3). Files: `native/core/include/thunder/live.h` (the C API), `native/core/src/live/live_host.h` (the class declaration, no bodies),
`native/tests/test_live_h.c` and `native/tests/test_live_h_cpp.cpp` (compile and size checks, registered in CMake). Design: `U37-DESIGN-DRAFT.md` sections 2.1, 3.4, 3.5, 4, 5, 6, 7, 12 (the rulings win).
`live_host.h` includes `render/pads_snap.h` (PR #276, merged into `native`; this branch is rebased onto `native` 56ffdc3). The two tests are in CMake (`test_live_h`, `test_live_h_cpp`; compile and size asserts only).

**Compile results** (all rc = 0, no warning):
- `cc -std=c99 -Wall -Wextra -pedantic -fsyntax-only -Inative/core/include native/tests/test_live_h.c`: clean. The same file also builds and runs (`test_live_h: ok`) and compiles with `-std=c11`.
- `g++ -std=c++17 -fno-exceptions -fno-rtti -Wall -Wextra -fsyntax-only -I native/core/src -I native/core/include native/tests/test_live_h_cpp.cpp`: clean.
- `live.h` alone as C++17 with `-pedantic`: clean. The sizes and offsets of the test file also hold for `clang --target=wasm32` and `--target=i686-linux-gnu` (checked with `-ffreestanding`): the structs have explicit padding and no pointer or `size_t`.
- The C file uses a C99 spelling of a static assert (a negative array size) when `__STDC_VERSION__ < 201112L`: a literal `_Static_assert` under `-std=c99 -pedantic` is a warning. Documented sizes: opts 56, play_opts 80, stats 240; event records head 16, clock 48, pos 32, rowent 8, rows 24 + 8n, meters 304, state 24, song 24, pads 24, error 32, late 40.
- My first version of the test asserted a wrong offset (`max_pad_bytes` at 8) and the build failed: the asserts do catch drift.

Counts: 45 functions: life 6 (`abi caps create destroy process stats_get`) + memory 2 (`alloc free`), song 1, pads 15 (`pad_set pad_take pad_kit pad_route var_set pad_chop pad_hits font_add font_remove pad_sf pad_sf_clear pad_live pad_live_clear pads_publish collect`), transport 8 (`play stop hold loop bpm lead perf stop_track`), mixer 2 (`mix_set pdc_set`), effects and master 4 (`fx_plan fx_commit fx_param master_config`), MIDI 3 (`midi midi_drain midi_lead`), events 3 (`host_stats poll meters_want`), test 1 (`dump_step`).

## 1. Where the draft's proposal did not fit the code

1. **`play` cannot be only a ring command.** The `Seq` is built per PLAY from a `SeqSession` on the control side (`session.h:37`, `:51`), and a `Seq` allocates. So `thunder_live_play` is a CONTROL call that allocates (session, starting `Seq`, the song's effects plans, master configuration, PDC plan) and queues a small command carrying a `PlayPlan*`; at the top of the quantum the audio side adopts the new snapshot itself (`seq_adopt` inside `applyPlay`) before `runner.start`. The draft's step 1 / step 2 order is kept for every other command; play adopts early. `play` has no `at`: `t0` is its time.
2. **`process` takes `frame0` and `wall_ns`** (draft: `process(L, R, frames)`). Ruling D11 says the core's counter is the host's; the one exact way is to pass the frame of the first sample on every call (a re-anchor when it is not the expected one). `wall_ns` exists because a wasm build may not import a clock (the four WASI stubs only, draft 2.1) and `core.cpp` reads `steady_clock` inside `thunder_process`; here the host passes it (0 in the web).
3. **`create` takes an options struct** (house style of `render.h`), with `max_pad_bytes` (the ruling D7 budget), `max_song_bytes`, `flags` and `max_voices`. `max_block` is only an upper bound on a process call (E_ARG above it): the mixer and effects are always 128.
4. **The pads draft names do not map one to one onto `PadsHost`.** (a) `PadsHost` has no live-engine pads: `LivePool::addPad` is a control call that must not overlap the audio thread (`live_pool.h:71`) and `Players` owns its `LivePool` by value (`players.h:61`). `pad_live` therefore needs a swappable pool, or it is E_STATE while playing (cap bit `THUNDER_LIVE_CAP_LIVE_SWAP`). (b) `PadsHost` has no variation call: variations travel inside `setPad` (`PadVarIn`), so `var_set` re-copies the pad (P1 open question 3). (c) `PadsHost` counts buffers, not bytes (still so as merged): the D7 budget needs a byte counter that includes RETIRED buffers (voices still read them), so an E_LIMIT can be transient. (d) The draft's budget "enforced by the bridge before the first put" is moved into the core as a create-time limit with E_LIMIT, as the lead asked.
5. **Taking ownership works without a redesign, so both calls exist.** `PadBuf::mem` is `new float[]` freed with `delete[]` (`pads_snap.cpp` `freeBuf`), "L then R in one allocation". `thunder_live_alloc` returns exactly such a block; `PadsHost::setPadTake` (new, about 30 lines) adopts it. Cap bit `THUNDER_LIVE_CAP_TAKE`; until it exists `pad_take` is E_UNSUPPORTED and the block stays the caller's.
6. **The bus plan has no call of its own.** It is built from the song's chain patch inside `song_publish` and published on a second slot beside the song snapshot; the two slots can be adopted one quantum apart (documented). `Mixer::setPlan` is audio-side (`mix.h:110`). The plan matters today only for solo's chains-aware audible mask: no bus exists without a chains host (ruling D1).
7. **`fx_plan` takes the song's slot-list JSON, not `FxEntry[]`** (`FxHost::planFromSong`, `fxhost.h:130`), and at most one plan may be staged per track: `FxHost::plan` reuses the objects of the previous plan (`fxhost.h:128`), so a plan that is never committed would leave a dangling reuse. `play` itself plans and commits the song's own effects (the live counterpart of `FxBank::load`, which commits "now", offline: `fxbank.h:25`).
8. **`FxBank::pdcSlots` cannot be used by the control side**: it reads `FxHost::chain_`, the audio thread's view (`fxhost.h` `slotLatency`). The PDC plan is built from `FxPlan::entry(i)` (`lat`, `bi`, `on`) on the control side.
9. **No pump variant of `fx_param`.** `psetPump` needs up to 280 floats (1.1 KB) per command (`fxhost.h:143`); pump effects are out of U37 by ruling D1. The draft's row 9 "psetPump" is NOT COVERED.
10. **MIDI: the draft's D-1 "post `thunder_midi_start` first" is replaced by "the audio side calls `MidiSched::start` / `stop` directly".** `thunder_midi_start` is a control call on a single-producer ring; a stop that must land at `at`, and the stop 0.3 s after the song's end, can only be timed on the audio side (`MidiSched::stop` is the function the ring's own `apply` calls: `midi_api.cpp:71`). Start is called right after `midi_begin` of the same quantum (step 4), before the step loop, so the Start never follows the first step. The draft's list `_midi_set_song, _port, _start, _stop, _release, _panic, _drain` is cut to `midi` (the owned handle), `midi_drain`, `midi_lead`: the shell makes `set_port`, `release`, `panic` ... on the handle itself (the same control thread), and the host makes `set_song` (inside `song_publish`), `start`, `stop`, `set_audible`, `set_fx_latency` itself.
11. **Two leads.** The draft's "`lead` -> `thunder_midi_set_lead` capped at 0.5 s" mixes two things: the SEQUENCER's look-ahead (`SongRenderer::lead`, 0.05 .. 2 s: `thunder_live_lead`) and the MIDI outbox's (`thunder_midi_set_lead`, 0.005 .. 0.5 now, 1.2 after P4: `thunder_live_midi_lead`). The page sends both.
12. **Session full cannot be handled in the middle of a PLAY.** The draft says the host starts a fresh session when `sessionFull()`; a fresh session means a new starting `Seq` (a restart at the row, with a possible click: SEQ-TAKEOVER-BUILD-NOTES "Choices for the lead", options a, b, c). The contract implements (b): the publish still succeeds, `stats.session_full` and a record say so, the next play starts fresh.
13. **An IDLE `song_publish` builds nothing for the audio side** (no session, no `Seq`): it keeps the Song for the next play and only queues a SONG acknowledgement. Only a PLAYING host publishes successor snapshots.
14. **`destroy` cannot deliver the owed quantum's MIDI records** (nobody is draining and the `thunder_midiout` must be freed first because it points at the host's handle). So D-2's "one more quantum" is a SHELL duty (stop, one more process call, drain / pump, free midiout, destroy); destroy still runs one quantum for a host that was left playing, and drops the records.
15. **The draft's file:line cites that are stale on `native` b89ac30** (checked): `session.h:122-134` (`seq_adopt` is `:100-112`); `render.h:92` (`SongRenderer` is `:105`), `render.h:30` (`kRenderQuantum` is `:33`); `fxhost.h:136` (`psetAt` is `:135`; `:136` is `lane`). The cites for `render.cpp:93-120`, `:101`, `:106-108`, `:111`, `:58-59`, `:69-70`, `mix.h:111/:117/:132`, `players.h:32/:41/:47/:52`, `runner.h:37/:50/:62-66`, `fxhost.h:108/:128/:134/:143`, `snap.h:40`, `live_pool.h:116`, `midi.h:88`, `midiport.h:119`, `song.h:89-92`, `fxbank.h:32` are right. The draft's claim that `core.cpp` is "a 16-step click pattern" is still true. I did not re-check the `web:` cites (they are `beta`, not in this tree).
16. **The record layouts are mine.** The draft gives only the names and rates of CORE-BRIDGE section 4; the fixed layouts, the `seq` counter and the `bytes` field (so that a reader skips an unknown type) are new, as is the rule "a full ring refuses the NEWEST" (core.h's `evtDropped`). `rows` and `held` share one layout.

## 2. Coverage table

Section 4 of the draft (the 20 state changes):

| Row | Change | Function(s) of live.h |
|---|---|---|
| 1 | Song edit | `song_publish` (+ `collect`); SONG record |
| 2 | Chain-patch edit | `song_publish` (bus plan inside). The chains hook has no host: NOT COVERED (ruling D1: fall back) |
| 3 | Tempo | `bpm` |
| 3a | Lines tempo re-render | page renders; `pad_set` / `pad_take` + `pads_publish` (row 4) |
| 4 | Pad sample | `pad_set` or `pad_take`, `var_set`, `pads_publish`, `collect` |
| 5 | Chop | `pad_chop`, `pad_hits`, `pads_publish` |
| 6 | Kit | `pad_kit`, `pad_route`, `pad_sf`(+clear), `pad_live`(+clear: E_STATE while playing without `CAP_LIVE_SWAP`), `pads_publish` |
| 7 | Mute / solo | `mix_set` (MUTE, SOLO) + the same change in the next song text; the audible mask goes to MIDI inside the host |
| 8 | Mixer | `mix_set` |
| 9 | FX param | `fx_param` (tfx). Pump parameters: NOT COVERED (D1; 1.1 KB a command) |
| 10 | FX add / remove / type | `fx_plan` + `fx_commit`; `pdc_set` for the plan. Pump slots: NOT COVERED (D1) |
| 11 | Master chain edit | `master_config` returns E_UNSUPPORTED until P3 builds the live swap: NOT COVERED in C++ (the contract is written) |
| 12 | Reset track | `song_publish` then `stop_track` |
| 13 | Seek (PLAY from another row) | `stop` then `play` (a `play` while playing does not cut the voices; the page stops first, as `tStop` does) |
| 14 | Loop | `loop` |
| 15 | STOP / hold / song end | `stop`, `hold`; STATE and HELD records |
| 16 | PERFORM | `perf` |
| 17 | Live transpose `lxp` | NOT COVERED: LXR is not wired (U04 seam; "NOT IN THIS API") |
| 18 | MIDI settings | `midi` (handle: `thunder_midi_set_port` ... by the shell), `song_publish` (instruments), `midi_lead` |
| 19 | Lead | `lead`, `midi_lead` |
| 20 | Latency comp mode | `pdc_set` (and `pdc_mode` of `play`) |

Section 5 (the duties):

| Duty | Carried by |
|---|---|
| D-1 `setMidi` + start 0.08 s + SPP | `play` (`t0`, `spp`; audio side `MidiSched::start` before `midi_begin`) |
| D-2 stop + 0.3 s after song end + one more quantum | `stop`; the song-end timer inside the host (STATE ENDED then STOPPED); the "one more quantum" before `destroy` is a SHELL duty (note 14) |
| D-3 `clock_map` after the block | not a live.h function: the shell calls `thunder_midiout_clock_map` on the handle of `midi` after `process` (design B); in the web (A) the page's own `mapUpd`; the CLOCK record carries the (frame, wall) pair |
| D-4 `setDropLate(true)` | the constructor, once; `stats.notes_late`, the LATE record |
| D-5 `stopTrack` | `stop_track` |
| D-6 republish on a chain-patch edit | `song_publish` |
| D-7 `eng_env_lines_fit` | NOT COVERED by ruling (U34c is not built): the page renders and sends the pad |
| D-8 (a) first block, (b) quantum with no step, (c) song end, (d) `midi_begin` failure, (e) lifetime | (a) `midiport.h` holds records until the first map (a shell); the web's first CLOCK record plays that role; (b) steps 4 and 6 are unconditional; (c) STATE ENDED + the 0.3 s stop; (d) `stats.midi_begin_failed`, one panic, an ERROR record; (e) the host owns the handle: `midi`, destroy order (free midiout first) |
| D-9 snapshot + deferred free, pump on one thread, idle skip | `pad_*`, `pads_publish`, `collect` (P1's rule); pump effects are not in this API, so the process-global pump rule does not bind it; `setIdleSkip` stays on |
| D-10 tempo seed, fixed 128 quanta, audio-side `Mixer` calls, fx plan / commit | `play` (seeds the tempo consumers); THE QUANTUM; `mix_set`; `fx_plan` / `fx_commit` |
| D-11 `assignSample` / `smpOwnTune` | page only |
| D-12 | = D-3 and D-8 |
| D-13 the full wasm target | not an API: P6 (the export list is this header) |

## 3. Questions for the lead (most important first)

1. **Who starts and stops the MIDI scheduler.** (a) The audio side calls `MidiSched::start/stop` itself, in step 1, before `midi_begin` (what I wrote); (b) the control side posts `thunder_midi_start/stop` as the draft says, and the host times the 0.3 s stop by a control timer. (a) is exact for a stop at `at` and for the song-end stop, and keeps the control ring single-producer; (b) uses only the public C API. **Recommend (a).** It changes P4's scope (a direct call into `midisched.h` from `LiveHost`).
2. **Live-engine pads while a song plays** (`pad_live`). (a) E_STATE while not IDLE; the page changes live pads only between plays; (b) a swappable `LivePool` published with the pads snapshot, live voices cut at the swap (about 120 to 180 lines in `players.h` / `live_pool.cpp`, an edit in P1's area). **Recommend (a) for P2 with the cap bit in the contract, (b) as its own PR before the web switch is offered** (an owner who edits a bass pad's sound during play would otherwise hear nothing until the next play).
3. **Session full in the middle of a PLAY.** (a) the publish succeeds, the flag and a record say it, the next play starts fresh (written); (b) restart at the next pattern edge with `SeqOpts::at` (needs a pattern-edge counter in the runner: a new feature); (c) refuse edits with E_LIMIT. **Recommend (a).**
4. **Out of memory in a build without exceptions** (wasm). The control paths that cannot catch: `Song::load`, `SeqVars::load`, the `RenderPads` copy in `PadsHost::publish` (P1 open question 1), the SoundFont parse. (a) accept: the budgets and `max_song_bytes` are checked BEFORE the allocation (E_LIMIT), a trap is fatal and the page falls back (ruling D14); (b) build those few files with `-fexceptions` for wasm (code size, wasi-sdk C++ exception support, untested); (c) rewrite them `nothrow` (large). **Recommend (a)** for U37, and a one-line warning in the page's report.
5. **`pad_take` and the byte counter in `PadsHost` (P1's file).** `PadsHost::setPadTake` (about 30 lines) and a byte counter that counts retired buffers (about 20) are additive. (a) P2 adds them; (b) P1 adds them before PR #276 merges. **Recommend (a)**: do not churn a PR under review; the cap bit lets the page use `pad_set` until then.
6. **Variations cost.** Each `var_set` re-copies the pad (P1 open question 3): a 5-minute pad (57 MB) with 50 variations is 2.9 GB of copying. (a) `LiveHost` stages the variation list on the control side and calls `PadsHost::setPad` ONCE at `pads_publish` (the staged variation samples exist twice for a moment); (b) split variations into their own refcounted object in `PadsHost`. **Recommend (a)** (contract unchanged; no P1 change).
7. **Two leads and the 1.2 s MIDI cap.** Confirm: `lead` (sequencer) and `midi_lead` (outbox) are separate calls and the page sends both (a hidden page: 1.2 and 1.2). Alternative: one call that sets both. **Recommend separate.**
8. **PDC is explicit.** `pdc_set` is not recomputed by `fx_commit` or `song_publish` (only `play` builds it). (a) explicit (written); (b) the host recomputes after every commit that changes a pump slot. Pump slots are out of U37, so tfx never change the plan: **recommend (a)** until pump effects come.
9. **The song's own effects at PLAY.** `play` plans and commits the song's `tfx` / `mfx` itself (F_EFFECTS). Alternative: the page always sends `fx_plan` + `fx_commit` for every track before `play` (more messages in the web, the page's tfx JSON is the only source). **Recommend the song's own** (a native shell and the A/B tests need no extra step), the page's explicit calls being the edit path.
10. **Re-anchor and `max_block`.** A `frame0` that is not the expected one re-anchors the grid (drops the partial quantum, counts it); `frames > max_block` is E_ARG. Alternative: accept any size and ignore `frame0` after the first call. **Recommend as written** (it makes a lost callback visible).
11. **`play` while PLAYING** does not cut the voices or stop the MIDI scheduler (the page stops first, row 13). Alternative: `play` implies `stop(NOW)` first. **Recommend as written**; the caller already does `tStop`.

## 4. C++ that does not exist yet (size guesses, lines of new code, bodies and tests excluded from "contract")

| Piece | Where | Lines | PR |
|---|---|---|---|
| `LiveHost` core: create / destroy, rings, `applyCommands` / `applyOne` / `applyPlay`, `adopt`, `quantum`, `stepLoop`, `dispatch` (copy of `SongRenderer::dispatch`), `process` with re-anchor, `song_publish`, play / stop / hold / loop / bpm / lead / perf / stop_track / mix_set, state and stats | `live/live_host.cpp` | 700 to 900 | P2 |
| Event records: `post`, `afterBlock` (clock, pos, rows, held, state, song, pads, error, late), `poll` merging the critical and the big ring by `seq`, rates | `live_host.cpp` | 230 | P2 |
| Pad wrappers: budget check, `padSet` / kit / route / chop / hits, variation staging (ruling 6: one `setPad` at publish), `pads_publish`, `collect` | `live_host.cpp` | 200 | P2 |
| `PadsHost::setPadTake`, a byte counter that includes retired buffers (ruling 5: P2), and two small verifier fixes in `beginQuantum`: when `!seq && seqSeen_` do NOT adopt (leave the snapshot pending) instead of only counting; and `assert(pl.epochSlots() == nBoard_)` | `pads_snap.cpp` | 60 | P2 |
| Fonts: `fontAdd` / `fontRemove` / `padSf` / `padSfClear` with the budget (a port of `render_api.cpp:406-500` onto `PadsHost::setSf`) | `live_host.cpp` | 150 | P2 |
| Live-engine pads: `LivePool::addPad` path with the E_STATE-while-not-IDLE rule | `live_host.cpp` | 40 | P2 |
| **The swappable live pool** (`Players::live_` -> a published `LivePool*`, live voices cut at the swap, `CAP_LIVE_SWAP`): its OWN PR before the web switch ships | `players.h`, `live_pool.cpp`, `live_host.cpp` | 150 to 200 | separate PR |
| Effects live: staging and commit ring, `fxBack_` return, `FxBank` attach without `load`, `planFromSong` of the song's slots at play, PDC plan from `FxPlan` entries, `pdc_set`, `fx_param` ring | `live_host.cpp`, small `fxbank.h` accessor | 250 | P3 |
| Master chain: live `configure` at a quantum top, its latency change in the stats and PDC; `master_config` | `chain.cpp`, `live_host.cpp` | 150 to 250 (does not exist today) | P3 |
| Meters: `MeterBank` in the host, `meters_want`, METERS records | `live_host.cpp` | 60 | P3 |
| MIDI: handle ownership, `song_publish` call, start / stop in `applyOne`, the 0.3 s timer, drain, `midiLead`, the 1.2 s cap in `midisched.cpp`, `midi_begin_failed` | `live_host.cpp`, `midisched.cpp` | 120 | P4 |
| The step dump and its JSON writer (`tests/seq_plain.h` moved or copied under `src/live/`, test builds only) | `live/live_dump.cpp` | 150 | P5 |
| The C shell: argument checks and the error mapping of every function of `live.h` | `live/live_api.cpp` | 450 to 600 | P5 |

Total about 2400 to 3000 lines for P2 to P5, of which the C shell is the largest mechanical part.

## 5. The lead's rulings (21 Nov) and the edits made for them

Applied in `live.h`, `live_host.h` and here; compile checks re-run (below).

1. **MIDI start / stop: the audio side calls the scheduler, on one condition.** I looked in the scheduler. A posted `thunder_midi_start` / `_stop` is applied in `blockBegin` (`midi_api.cpp:83-90`): first `setFxLatency` / `setAudible` with the block's figures, then every posted command in ring order (`apply()`: `OP_START` -> `MidiSched::start(spp, now)`, `OP_STOP` -> `MidiSched::stop(now)`, `midi_api.cpp:70-71`), then `setBlock`. My first draft put the direct calls BEFORE `midi_begin` (step 1): that is NOT the same point (the figures `stop` reads for the FX latency, and the commands posted earlier, would not be in force yet). I moved them to IMMEDIATELY AFTER `midi_begin` returns, before the step loop, with the same `now` (the quantum start): the same figures are in force and every posted command has been applied. **Answer: same point only up to two differences I cannot remove, so NOT provable from the code:** (1) ring order against another MIDI command posted AFTER the play (popped before the direct Start here, after it in the posted path); (2) stop's `at` (the posted path has none). So I wrote P4's done-test into the header and `live_host.h`: the stream must be BYTE-EQUAL to the posted path for the 18 ledger songs, and **the posted path is the fallback P4 must use** (`play` / `stop` post `thunder_midi_start` / `_stop` themselves, before queuing their command; the 0.3 s song-end stop is posted by the control side when `collect` sees the ENDED record).
2. **`pad_live` while playing: E_STATE while not IDLE, cap bit as written.** The swappable live pool is its own PR before the web switch ships (work list above). Added caveat in `live.h`: a threaded shell must also have no process call in flight, because `Players` renders its live pool every quantum even when IDLE; only the cap makes the call safe against a running audio thread.
3. **Session full: as written.**
4. **Out of memory without exceptions:** the header's OUT OF MEMORY paragraph now says a trap is fatal and falls back (D14) and that P6 must measure the largest song and pad set that stays inside the limits on a phone before the limits are trusted.
5. **`pad_take` and the byte counter are P2 work in `PadsHost`**, not PR #276 (work list).
6. **Variations:** `LiveHost` stages them and calls `setPad` once at `pads_publish`. `var_set`'s comment no longer says "each call re-copies"; it says what the caller may rely on: one pad copy at publish (the staged variation samples exist twice until then and count against the budget at once).
7. Two leads, separate calls. 8. `pdc_set` explicit. 9. `play` plans and commits the song's own effects. 10. `process(frame0, wall_ns)`, re-anchor, `frames > max_block` = E_ARG. 11. `play` while playing does not cut voices. All as written.

Edits:
- **a. `pe_prepare`.** `create` / `process` now say: the first `thunder_live_process` call on a thread prepares that thread's workspace and is the one call that may allocate; the web does it from its first message handler with `frames = 0`; a shell accepts it in its first callback, before any song plays. New stats field `thread_prepares` (takes the first of the four reserved words; `evt_crit_dropped` takes the second; the struct is still 240 bytes, asserts unchanged and passing).
- **b. The EVENTS paragraph.** Decided: the CRITICAL records (STATE, HELD, ERROR, SONG, PADS) have their OWN ring of 64 records (`evCrit_`), the rest share the 1024-record ring; `poll` merges the two by `seq`, so the reader sees post order. A full ring refuses the newest record of its kind and counts it (`evt_dropped` for all, `evt_crit_dropped` for the critical ones; the next CLOCK carries `evt_dropped`); the gap in `seq` shows it. A consumer polls at least every 100 ms. Nothing is dropped silently.
- **c. The state machine** is one table in `live.h` (after the state constants). States IDLE, PLAYING, HELD, ENDED; STOPPED is a record (the move to IDLE), not a state. Summary: `play` from any state -> PLAYING (record PLAYING; voices not cut; the ENDED timer cancelled); `stop` from PLAYING / HELD / ENDED -> IDLE with STOPPED arg 0 (from IDLE: none, MIDI stop repeated); `hold` only arms the runner (no state change); a step at an edge with hold armed: PLAYING -> HELD (HELD record); the sequencer running out: PLAYING -> ENDED (ENDED record, `songOver`, 0.3 s timer); 0.3 s after ENDED: ENDED -> IDLE with STOPPED arg 1 (the MIDI stop). HELD keeps MIDI running until `stop`.
- **d. NOT IN THIS API** has the line: this header is used by the web host AND by every shell; nothing in it may assume a page.

Compile checks after these edits: `cc -std=c99 -Wall -Wextra -pedantic -fsyntax-only` on `test_live_h.c`: rc 0, no warning, and the program runs ("test_live_h: ok"); `g++ -std=c++17 -fno-exceptions -fno-rtti -Wall -Wextra -fsyntax-only` on `test_live_h_cpp.cpp` (with the P1 tree first on the include path): rc 0, no warning.

## 6. Round 3 (21 Nov): rebased onto the merged `PadsHost` (PR #276, ace20e6)

The branch is now on `native` 56ffdc3. What changed in the `PadsHost` text of `live.h`, `live_host.h` and these notes:
- **Create order:** `Players` first, then `PadsHost::create(const Players&)` (it binds to the Players' voice pools, publishes an empty snapshot, and returns nullptr when the pools exceed `kBoardMax` 512).
- **Quantum step 2:** `seq_adopt`, then `seqGone()` if the play was stopped or its Seq dropped (BEFORE that quantum's `beginQuantum`), then `beginQuantum(players, runner.seq())` every quantum (board, adopt, chop re-point); sessions use `opts.chop = nullptr`.
- **Free rule:** exact per buffer and per voice slot (the board), no longer "per pad"; no `slot` / `kEpochSlots`.
- **P2 work list:** `PadsHost::setPadTake`; the byte counter including retired buffers; verifier fix 1 (`!seq && seqSeen_`: do not adopt, leave the snapshot pending); verifier fix 2 (`assert(pl.epochSlots() == nBoard_)`).
