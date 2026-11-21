# U37 P2 notes: LiveHost core (the real-time song host)

Branch `n1/u37-p2-livehost` (base `native` 56ffdc3 + the P0 contract files from `n1/u37-p0-live-h` a048b8a, PR #280; when #280 merges, merging `origin/native` is enough).

## State (update at every push)
- DONE, scope A: `native/core/src/live/live_host.cpp`: create / destroy, `process` (128-frame grid, re-anchor), command ring + 64-entry waiting list, song publish, pads (wrapping `PadsHost`, with the budget), transport (play, stop, hold, loop, bpm, lead, perf, stop_track), mix_set, the state machine (IDLE, PLAYING, HELD, ENDED, 0.3 s timer), both event rings merged by `seq` in `poll`, stats, host_stats, the step dump. `live/dump_plain.h` is the step dump's JSON writer.
- DONE, scope B: `PadsHost::setPadTake`, the byte counter (`bytesHeld` / `bytesRetired`, retired buffers included), R2-2 (`beginQuantum` holds a pending snapshot when no Seq is passed after one was seen, counted in `orphanAdopts`), R2-4 (`assert(pl.epochSlots() == nBoard_)`, plus the accessor `boardSlots()` for a test that also works under NDEBUG).
- DONE, tests (all in `native/tests`): `live_equals_render`, `live_step_dump`, `live_transport`, `live_events`, `live_pads`, `live_threads`; `rt_harness` runs `LiveHost::process` under the allocation guard (with and without the step dump); `live_pads_epoch` has checks for R2-2, setPadTake / the byte counter and R2-4.
- NOT in P2 (scope C): effects (`fx_plan`, `fx_commit`, `fx_param`: E_STATE), master chain (`master_config`: E_STATE), PDC (`pdc_set`: E_STATE), MIDI (F_MIDI refused at create), meters (F_METERS refused), SoundFonts and live-engine pads (`font_add`, `pad_sf`, `pad_live`: E_UNSUPPORTED), the C API (`thunder_live_*` wrappers, `thunder_live_alloc`), wasm. `create` rejects F_MIDI / F_EFFECTS / F_MASTER / F_METERS with a clear error.

## Where each method stands
create / ~LiveHost / collect / process: done. songPublish: done (IDLE ack, PLAYING successor snapshot, 17th waiting publish E_BUSY, session full flag). padSet / padTake / padKit / padRoute / varSet / padChop / padHits / padsPublish: done. play / stop / hold / loop / bpm / lead / perf / stopTrack / mixSet: done. poll / statsGet / hostStats / dumpStep: done. fxPlan / fxCommit / fxParam / pdcSet / masterConfig / midi* / metersWant / font* / padSf* / padLive*: stubs answering as live.h says for a host made without the part (P3, P4, later).

## Departures from the contract (each with its reason)
1. COMMAND ORDER. live.h step 1 and live_host.h said the waiting list is "ordered by at". Commands are applied in RING order within a quantum, and the waiting list is kept in ring order too: a stop at 3000 sent before a mix_set at 2950 must not be reordered. A 65th waiting command is applied at once and counted (`cmd_late`); the 65 bpm commands of `live_transport` end on bpm 163 and not 164 because of it (the 65th, bpm 164, went in at once).
2. ERROR(CMD). live.h names `THUNDER_LIVE_WHERE_CMD` but nothing posted it: one ERROR record per quantum in which a command was applied at once. 
3. RE-ANCHOR value. live.h says "value = frames skipped, may be negative" but `thunder_live_ev_error` has no value field: carried in `reserved`.
4. `stats.frames` is the frame the next QUANTUM starts at (whole quanta), not the end of the last process call.
5. Pad figures in `stats` (`pad_bytes_held`, `pad_bytes_retired`, `pad_buffers`, `pad_retired`) are control-side numbers refreshed by the control calls that change them (publish, collect, pad calls): after the audio side adopts a snapshot they move at the next `collect`.
6. SONG records: acks of IDLE publishes are coalesced to one record (the newest rev) per quantum, and a play that adopts the snapshot of the same quantum posts only the adoption's record.
7. ENDED timer: the 0.3 s runs from the quantum that finds the end. The ENDED record's `f` is the sequencer's end time, which can be up to the look-ahead earlier.
8. STEP DUMP. The line carries one more key than the contract's `{"t","bpm0","tks","evs"}`: `"clk":{"time","base","bt","btT","bpm"}` (the Seq's clock after the step), because the goldens' row hashes include the clock. The browser A/B ignores a key it does not know.
9. GOLDENS vs LIVE PLAY. The goldens are made with `once = !loop, sim = true`; a live play is `once = false, sim = false` (live.h has no `once` / `sim`). The events are identical under all four combinations (measured, 1792 rows). The row hash also has the clock, which differs only at the step where the song runs out (a looping Seq wraps, a `once` Seq stops), and for `seq-edit-cell-cleared` the golden's play ends at row 128 where the live play goes on. `live_step_dump` therefore compares each case to (a) a reference Seq with the live play's options (events and clock, EVERY row) and (b) the golden (every row hash and the sha256 for the four plain goldens; for the 25 clock goldens the longest equal prefix, at least 98 % of the rows). `seq-edit-bpm` and the other goldens that use the `shift` edit are not coverable: live.h has no shift command (only its first 130 rows, before the shift, are compared).
10. `live_equals_render` with a first `frame0` that is not 0: bit-equal over the first 12 s, and over the whole run at most 0.001 % of the samples differ, by at most 1e-6 (a sample position `(frame0 + x) / sr` rounds differently from `x / sr`: measured 3 of 3.8 million samples after 18 s). With frame0 = 0, all five call sizes are bit-equal over 40 s.
11. `live_host.h` was extended additively (state the control side and audio side need: `PlayPlan::sess`, the pad variation staging, ack / pos fields, the dump rings); `SongSnap` (session.h) gained `rev` and `ord`; `SnapSlot` (snap.h) gained `pending()` and `collect()` returning a count. All inside `native/`.
12. `-fno-sanitize=vptr` is on the new test targets only (thunder_core is `-fno-rtti`, where UBSan drops the vptr check itself; the tests that call into virtual classes of it get the same flag as `test_modfx` and `rt_harness`).

## Thread rules as built
CONTROL calls: one thread at a time. AUDIO: `process` only (it allocates nothing: `rt_harness`). CONSUMER: `poll` / `dump_step`, one thread. `statsGet` and `hostStats`: any thread (relaxed atomics). Rings: commands (256), fx params, events (1024), critical events (64), play results (8), song / pads / bus / pdc / master snapshots (16 each). The control side owns every allocation and every free; the audio side only adopts and hands back.

## Open questions
1. `thunder_live_play` with `spp = -1` steps a Seq on the control side (`midi_spp_for`): P4.
2. A `shift` command would let the browser A/B cover the shift goldens; the contract has none.
3. The ENDED timer's start (departure 7): if the page wants it from the sequencer's end time, it is one line.

## Mutations (tests that kill them) and results
(filled in at the end of the session)
