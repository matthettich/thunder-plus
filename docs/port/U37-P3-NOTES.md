# U37 P3 notes: effects, master chain, PDC, meters in LiveHost (+ R1, R2)
Branch `n1/u37-p3` (base native ff3a494). Builder: Claude (Sonnet), 24 Nov.

## Status
DONE locally: effects (track + master, FXV / lane events), master chain (at play and live `master_config`), PDC (at play, `pdc_set`, idle at stop), latency report, meters, R1, R2.
Done-test met: `live_fx` 177 checks, 0 failed (output, METERS and latency_frames bit-equal to SongRenderer, ELECTRO with all slots, four variants, call sizes 128 / 480 / 4096 / 333, frame0 0 / 37 / 1000 / 2^31). `live_r1r2` kills R1 (mutation: state store removed -> fails) and R2 (check removed -> fails). `rt_harness` has a third LiveHost pass (effects, master, PDC, meters, plans / params / pdc_set / master_config / meters_want between blocks): "no allocation in process".
Resume: build `cmake -S native -B build -DTHUNDER_DEPS_DIR=/home/claude/deps; cmake --build build --target test_live_fx test_live_r1r2 rt_harness -j2`; run `build/tests/test_live_fx` (about 60 s).

## Decisions (where the design was silent or I departed)
1. FxBank gets ONE additive method, `commitLive(track, plan, mixer, now)` (fxbank.cpp:29): commit + set / clear the track's mixer hook + flush waiting FXC. Hooks are set at commit, not at create: the mixer's clock reader (Mixer::clockBlock) exists only for hooked tracks, so SongRenderer's "hook from frame 0" equals "hook at the play's quantum" for a first play.
2. `play()` plans master first, then tracks 0..31 with one `modfx::SeedCounter` (FxBank::load's order) LAST, after every allocation that can fail. FxHost::plan changes the control view at plan time, so every plan MUST be committed: a play that fails part way sends the plans already made as plain FxCommit commands (room checked first: 34 ring places, 33 return places); a play cancelled before it started (Stop, a newer play) still commits its plans (`dropPlay`); a Stop / play clearing the waiting list commits any FxCommit in it (`clearWaiting`).
3. PDC at play from IDLE is set AT ONCE (pdc_apply live=false, as a render); over a running play it glides 10 ms; `pdc_set` glides 50 ms; stop / the ENDED timer re-applies with playing=false (AUTO: delays glide to 0; mode ON keeps them). I tried to keep the contract's "10 ms glide" for every play, but at-once is what SongRenderer does and nothing of the new play sounds for 80 ms. `latencyFrames()` counts the PDC's T only while it is in force.
4. Ordering of configuration: `PlayPlan::cfgOrd`, `LivePdcMsg::ord`, `LiveMasterMsg::ord` come from one counter; the audio side ignores a message older than what is in force (a pdc_set sent before a play must not overwrite the play's plan at step 2).
5. `fx_param` drains after the commands in step 1 (so a param sent after a commit meets the new chain); it uses `FxHost::psetAt(max(at, now))`. A param is NOT ordered against a commit sent before it with `at` in the future (two rings).
6. METERS records: head.seq is the ring's order (poll merges by it); the bank's own count is in `reserved[0]` (live.h comment changed). Meters are fed / advanced only after `meters_want(rate > 0)`; the bank's events are posted each quantum.
7. F_PAGE_TIMING now also sets `Mixer::setPageClock(true)` (render_api.cpp does; needed for bit-equality of page-timed effects; P2 tests unaffected, no effects there).
8. R1 fixed by storing `state_ = PLAYING` BEFORE `giveBack(p)` in `tryStartPlay` (not a control flag). Test seam `setTestAudioSeam` runs after giveBack.
9. live.h edits: METERS seq text, `test_once` / `test_sim` E_ARG text, master_config "built" text. P5 must set THUNDER_LIVE_CAP_MASTER_SWAP.
10. A staged (uncommitted) fx_plan makes `play` E_STATE, as live.h says; at most 64 commits outstanding (E_BUSY).

## Audio-path functions added or changed (native/core/src)
live/live_host.cpp: `tryStartPlay` (play: effects, master configure, PDC apply, R1 store), `commitFxLive`, `commitPlayFx`, `dropPlay`, `setMasterHook`, `masterHook`, `applyPdc`, `applyFxParams`, `clearWaiting`, `latencyFrames`, `postMeters`, `applyOne` (Stop: dropPlay + idle PDC; FxCommit; Meters), `applyCommands` (clearWaiting), `quantum` (applyFxParams), `adopt` (pdcSlot_, masterSlot_), `dispatch` (SEV_FXC / SEV_FXP), `render` (meters), `afterBlock` / `publish` (latency, fx counters), `songEnded` (idle PDC). fx/fxbank.cpp `FxBank::commitLive`. Line numbers: `grep -n` the names (file changes often).
Control side: `play` (R2, planning), `fxPlan`, `fxCommit`, `fxParam`, `pdcSet`, `masterConfig`, `meters`, `noteFxPlanned`, `collect`, `~LiveHost`.

## Known / open
- FxHost::plan (control) and FxHost::process (audio) share the host's `rand_` (prewire F5): a benign data race with real threads when a plan is made while that host processes. Not changed (changes audio). test `live_threads` has no effects.
- A pump slot that wakes from rest allocates in the audio thread (accepted FX1 rule): rt_harness pass 2 uses ELECTRO with its pump reverb turned into a tfx reverb; `live_fx` runs the real ELECTRO (0 remakes seen).
- Not done: pump FXV / `fx_param` for pump slots (contract: out), chains (C2), MIDI latency feed (P4 reads `latencyFrames()` at step 4 / 6).
- Unkilled mutations: PDC glide flavour at play-over-play (10 ms `now`), `dropPlay` path only smoke-tested ("three plays and a stop").

## Test results
See the end of this file (appended when the full ctest finished).

### Results (24 Nov, GCC Release, local)
`ctest -R "live|rt_|fxsong|fx|pdc|master|mix|meter|render_api|render_dispatch|players|midi_glue|seq_|beat"`: 100% tests passed, 0 failed out of 82 (472.82 s), including live_fx, live_r1r2, live_equals_render, live_transport, live_events, live_pads, live_threads, live_step_dump, rt_core, fxsong, render_api*. The full 231-test suite was NOT run (2-core machine); CI decides.
`test_live_fx`: "live_fx: 177 checks, 0 failed". R1 mutation (state store removed): `FAIL test_live_r1r2.cpp:19 ... song_pending 0`; R2 mutation: `FAIL ... R2: test_once / test_sim without F_DUMP_STEPS are E_ARG`.

## 24 Nov, fixes after verify-301 (findings 1 and 8): effects plans commit in planning order
Rule: the effects plans of a track are COMMITTED IN THE ORDER THEY WERE PLANNED (FxHost::plan reuses the objects of the previous plan).
- live_host.cpp `commitWaitingFx(qStart, track = -1)` (new): commits the FxCommits waiting in `pend_` in list order, returns each plan through fxBack_, marks the entry done (p = nullptr) instead of removing it, because applyCommands iterates `pend_` by index (`w` / `i` compaction) while calling applyOne. applyCommands' loop skips a done entry; a kept entry moved down nulls its old slot (no second live copy); the entry being applied is nulled before applyOne; clearWaiting skips done entries (cmdsCleared_ counts only non-FxCommit commands, as before).
- Called: `tryStartPlay` before `commitPlayFx` (all tracks), `dropPlay` before `commitPlayFx`, `applyOne` FxCommit (same track, before this commit). FxCommits still in the ring behind the current one are in order already.
- live.h: fx_plan comment text only (FX1 hosts pump effects; pump slots have no FXV / fx_param yet).
- Tests (test_live_fx.cpp, 193 checks): (a) stale waiting commit (at 2^40) before play: bit-equal to SongRenderer and to a host without the edit; (b) commit at quantum 40 then a NOW commit, same track: bit-equal to both committed in order at quantum 0 (call sizes 128, 4096); (c) cancelled-play block asserts audio against `ref`; (d) the fx edit is audible (> 1000 samples differ from the unedited run); (e) latency after pdc_set exact (`== ref.latency && > 0`); (f) the `keep` plans are deleted at the end.
- Without the fix (live_host.cpp/.h from 12d454c, new tests): `FAIL test_live_fx.cpp:176 ... a stale waiting fx_commit before the play: 94805 + 94782 samples differ ... (first at 1169)` and `FAIL test_live_fx.cpp:195 ... a commit at quantum 40 then a now commit (call size 128 / 4096): 89711 + 89711 samples differ, first at 6289`; "live_fx: 193 checks, 3 failed".
- With the fix: "live_fx: 193 checks, 0 failed"; `ctest -R "live|rt_|fx|render|pdc|master|meter"`: "100% tests passed, 0 tests failed out of 76" (Total Test time 777.37 sec).
- Open: P3b (own unit): a private rand state per effects plan, made on control and copied in at commit (the `rand_` race, finding 2). Ring pushes onto fxBack_ are assert-only (room is guaranteed by the 64-outstanding rule).
