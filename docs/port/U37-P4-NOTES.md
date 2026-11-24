# U37 P4 notes: MIDI in the live host
Branch `n1/u37-p4`, on top of `n1/u37-p3` head `966f165`. Builder: Claude, 24 Nov.

## Status
DONE. **The DIRECT path shipped** (the audio side calls `MidiSched::start` / `stop` itself, right after `midi_begin`, before the step loop). The done-test found it BYTE-EQUAL to the posted path for all 18 ledger songs (and 2 more); the posted fallback was NOT needed and is not built.

## What was built (native/core/src)
- `live/live_host.{h,cpp}`:
  - `create`: `F_MIDI` is now accepted; the host makes and owns a `thunder_midi` (`thunder_midi_create`), frees it in `~LiveHost`.
  - `songPublish`: after the host's own parse succeeds and BEFORE anything is published, `thunder_midi_set_song(midi_, text)`; E_BUSY (six songs in flight / ring full, counted in `cmd_busy`), E_MEMORY, E_SONG map through. A refusal changes nothing.
  - `play`: spp = `o.spp`, or (`-1`) `midi_spp_for(song, vars, row)` for `row > 0` and 0 for the top (what the shells' `sppFor` does); computed on the control side before anything is built; carried in the `PlayPlan` and the command.
  - audio side: step 1 queues MIDI ops in `midiOps_[16]` (`midiOp`): `tryStartPlay` queues a Start(spp), the Stop command queues a Stop from every state (from IDLE too: the scheduler's stop runs again). Step 4 `midiBegin`: the ENDED timer's Stop (`stateA_ == ENDED && t0 >= endStopAt_`, the same test `songEnded` uses at step 7 for STOPPED arg 1), then `midi_begin(t0, 128/sr, latencyFrames()/sr, mixer audible mask)`, then the queued ops applied IN THE ORDER the commands were applied, with the quantum's `t0`. Step 5: `MidiSched::onStep` for each step before its events are dispatched. Step 6 `midiEnd`: `midi_end(t0, last Seq time, last tempo, alive)`. The Seq's time and tempo are cached after the step loop (`midiSeqT_` / `midiBpm_`) for the quanta where the runner has no Seq (HELD, IDLE); `alive` is false then.
  - HELD keeps MIDI running (nothing is queued). The ENDED -> IDLE timer stops it 0.3 s after the end, from the end's time (`max(t0, endT) + 0.3`).
  - `midi_begin < 0` (cannot happen with finite arguments): counted (`stats.midi_begin_failed`), first one calls the scheduler's own `panic(t0)` (NOT `thunder_midi_panic`: that is a single-producer CONTROL call, wrong on the audio thread) and posts a non-fatal ERROR record WHERE_MIDI; debug build asserts.
  - LATE record carries `midi_late_notes / midi_late_ticks / midi_may_hang`; `stats.midi_may_hang` filled; LATE posts when any of them changes (still at most 4 a second).
  - `midiLead(seconds)`: `thunder_midi_set_lead`; E_STATE without F_MIDI, E_ARG outside 0.005 .. 1.2, E_BUSY when the 256-slot midi command ring is full. New `midiDrain` (thunder_live_midi_drain's body; E_STATE without F_MIDI).
  - `~LiveHost`: runs NO quantum (round 2: the stop quantum of round 1 was unreachable dead code, verify-p4 finding 4); the shell's duty is `thunder_live_stop` + one more `process` + a drain before destroy.
- `midi/midisched.{h,cpp}`, `midi/midi_api.cpp`, `include/thunder/midi.h`: the lead cap 0.5 s -> 1.2 s (`kMaxLeadSec`); `MidiSched::setLead` clamps to it too. Rings (8192 records, 4096 pending) hold 1.2 s of the densest ledger song with no `ring_full`.
- `include/thunder/live.h`: one comment paragraph ("SHIPPED IN P4").

## Done-test result (ctest `live_midi`, test_live_midi.cpp)
Posted path = `SongRenderer` (hook `setMidi`, `setSeqStart(0.08)`, SeqOpts of the live play: `live=false, once=false`, Song loaded with `pageS=true` like the host) + `thunder_midi_start(spp)` posted before block 0, the explicit stop posted before a block, and the host's 0.3 s rule on the control side (`songOver() && t > seqTime + 0.3` -> `thunder_midi_stop`). Live path = `LiveHost` with `F_MIDI`, `play(t0 = -1)`, `process` in calls of 128 (and 512 / 4096 for three songs), `stop()` at the same quantum, records drained after every call. Compared: EVERY field of every `thunder_midi_msg` (frame, port, len, flags incl. CLEAR, kind, bytes, cable), in order, `memcmp` of the 16-byte records. Songs run from the first block to 30 quanta after the explicit stop; 18 of 20 end by themselves first (so the explicit stop then runs from IDLE: also compared).

| song | records | result |
|---|---|---|
| two-ports | 107 | BYTE-EQUAL |
| mono-notes | 17 | BYTE-EQUAL |
| tempo-clock | 223 | BYTE-EQUAL |
| echo | 19 | BYTE-EQUAL |
| lfo-cc | 174 | BYTE-EQUAL |
| bend-fine | 24 | BYTE-EQUAL |
| cc-ins | 11 | BYTE-EQUAL |
| cc-program | 37 | BYTE-EQUAL |
| chan-share | 9 | BYTE-EQUAL |
| column-switch | 7 | BYTE-EQUAL |
| modes-mixed | 8 | BYTE-EQUAL |
| muted-solo | 7 | BYTE-EQUAL |
| note-len | 13 | BYTE-EQUAL |
| overlap-poly | 29 | BYTE-EQUAL |
| slide-arp | 74 | BYTE-EQUAL |
| song-end | 51 | BYTE-EQUAL |
| velocity | 19 | BYTE-EQUAL |
| fx-latency | 37 | BYTE-EQUAL |
| start-midway (spp from row 2) | 207 | BYTE-EQUAL |
| loop (loop options, explicit stop) | 348 | BYTE-EQUAL |

(1421 records in all. No first-differing byte to report.) The 18 are the list of `test_midi_glue` case 9 / `glue_vs_runBlocks`.
Limits of the claim: the two paths are compared for a play at the first block, a stop at a quantum boundary and the timer; the two differences live.h names (ring order against ANOTHER MIDI command posted after the play; stop's `at`) are not exercised by ledger songs. Both are by design (the host owns the transport; the shell must not post start / stop). The fx-latency song is run WITHOUT effects flags on both sides (latency 0 both); the latency feed (`latencyFrames()` into `midi_begin`) is the same expression the renderer uses and is covered by `rt_harness` pass 4 only for "does not break"; a bit-equal MIDI-with-FX-latency comparison is NOT in this unit.

Other checks in `live_midi` (109 checks in all): HELD keeps the clock and does not stop MIDI, a later stop emits Stop + CLEAR, a stop from IDLE runs the scheduler's stop again (more CLEARs; the Stop byte is not repeated because the clock was already stopped: same as the posted path, proven by the compared streams); stop + play in one go keeps Stop before Start; play over play; the ENDED timer (the MIDI Stop 0.25 .. 0.5 s after the ENDED record); `midiLead` range / E_STATE / E_BUSY and a 1.2 s lead books >= 0.8 s further ahead than 0.06 s; spp from `row` and an explicit spp; bad song E_SONG; no F_MIDI: `midi()` null, E_STATE.

## Real time
Nothing new allocates, locks or blocks on the audio thread: `midiOp` / `midiBegin` / `midiEnd` are fixed arrays and the scheduler's own calls (already guarded in `rt_midi`). `rt_harness` got a fourth LiveHost pass (effects + master + meters + `F_MIDI`, every instrument OUTPUT 2 on port A with the clock on, the midi lead changed between blocks, records drained by the consumer): 848 records, 212 note-ons, "no allocation in process", 0 `midi_begin` failures. The audio thread reads `midi_sched(midi_).stats()` (its own) for the LATE record and publishes `mayHang` through `pub_[]`.

## Decisions and departures
1. Direct path (see above). One departure from the P3 scaffold comment: the ENDED timer's MIDI Stop is applied at step 4 of its quantum (after `midi_begin`), not after `midi_end` at step 6 (the comment said so in two places); that is the point the posted stop has and what made the streams equal.
2. (REPLACED in round 2, see below.) At most 6 songs are in flight in the scheduler: a shell that publishes 7 songs with no quantum between gets E_BUSY on the 7th (documented in live.h).
3. A queued MIDI op list of 16 per quantum; a 17th pushes the oldest out. Not reachable from a real shell (plays are capped at 8 outstanding).
4. The midi_begin-failure panic uses the scheduler's `panic()` directly (see above), not the C call the header text names.
5. `midiDrain` added as the method behind `thunder_live_midi_drain` (P5 wraps it).

## Round 2 (24 Nov, after VERIFY-P4: MERGE with follow-ups)
1. **Finding 1 (wrong MIDI after a refused publish): fixed by ORDER, option A.** `songPublish` now passes every host-side failure point first (allocations, `cmds_.space()` for the idle path, `songSlot_.room()` for the playing path), THEN posts `thunder_midi_set_song`, and only then does the pushes whose room was just checked (this thread is their only producer, so they cannot fail; `assert`ed). A scheduler refusal (E_BUSY six in flight / ring full, E_MEMORY, E_SONG) rolls back what the host made (`sess_->rollback()`, snapshot and bus message freed) and returns with nothing changed on either side. A host refusal never reaches the scheduler. So there is no window in which the two hold different songs after a returned error. Test `refusedPublish` (verifier's probe E: idle host, command ring filled with `bpm`, publish B = A with `mch` 9: E_BUSY, then play: A's channel only) FAILS on 8f903c9 (`0 on channel 1, 5 on channel 10`) and passes now; plus the control (a successful publish plays B) and the six-in-flight case (6 accepted, then E_BUSY, host and scheduler still on A).
2. **Tests added to `live_midi`** (204 checks, still about 3 s Release; sanitizer matrix: 5 songs in the done-test, 2 songs x 3 stops, one PDC mode):
   - (a) `fxLatency`: ELECTRO with every instrument OUTPUT 2 on port A (clock on), F_EFFECTS + F_MASTER + F_MIDI, PDC auto and on, against SongRenderer with setEffects / setMasterChain / setPlan: latency 2704 frames (non-zero) on both, 91 records BYTE-EQUAL, with a stop at quantum 450. Runs everywhere (the master chain alone gives the latency; the effects library only adds slots).
   - (b) `stopWhilePlaying`: lfo-cc, tempo-clock, two-ports, echo, slide-arp, mono-notes, stops at quanta 20 (before the sequencer's first step), 61 and 150: 18 cases, byte-equal. The reference now REALLY stops: from the stop quantum the renderer is no longer stepped and the scheduler is booked with `thunder_midi_process` (same begin / end, no step, the sequencer's last time). The done-test uses the same reference, so its explicit stops are real too; lfo-cc is byte-equal.
   - (c) the mutation "Start both before and after `midi_begin`" is an EQUIVALENT mutant: `MidiSched::start` reads only its arguments and the port table and the second call overwrites everything the first set (no stream, no counter can see it; `stats.starts` is counted in `book`, not in `start`). Not killable and not killed. What IS killed: "Start before `midi_begin` only" (done-test, start-midway, the SPP checks) and now also by a new check "ports set AFTER play(): the Start still carries its SPP" (verifier's finding 5 behaviour, documented in live.h). The scheduler's counters (starts / stops / ticks / sent) are compared too.
3. Comments / docs: `~LiveHost` (above; live.h `thunder_live_destroy` text now says destroy books nothing); live.h MIDI text on a `midi_begin` failure (the scheduler's own `panic`, not `thunder_midi_panic`); live.h song_publish: six publishes with no quantum, the seventh is E_BUSY, and the post-last ordering; live.h stop: the MIDI stop lands at the top of the quantum holding `at` (up to 127 frames before `at`).

## Open for P5
- Record `frame` (verify-p4 finding 2): the records carry the ENGINE frame (`now * sr`), live.h says "the host timeline's frame". After a re-anchor the two differ by `off_` (probe H: records 10 s in the past after a +480000 re-anchor). NOT changed in P4 (a P5 ruling): either say "engine frames, the CLOCK records are the map" in live.h, or add `off_` in `midiDrain` (records drained before and after a re-anchor then differ by design).
- Ring order against a MIDI command posted AFTER the play (finding 5): the live Start is applied after it (SPP honoured when ports are configured after play()); the web's would not be.

## Questions for the lead
- Record frames. live.h says the records' `frame` is "the host timeline's frame"; the code (like the scaffold comment at step 6, "engine frames only") gives ENGINE frames (`now * sr`), which equal host frames only while `off_ == 0` (first `process` call with frame0 = 0, no re-anchor). I kept the posted path's behaviour (engine frames, unmapped). If the shell must get host frames, `midiDrain` can add `off_` (a re-anchor then shifts only records not yet drained); decide in P5. The web's worklet maps with the CLOCK records.
- `thunder_live_midi_lead` vs the sequencer lead: separate, as written.

## Tests run (24 Nov, GCC Release, 2 cores)
`ctest -R "live|rt_|midi|render_api|players|seq_|fx|pdc|master|mix|meter" -j2` (85 tests, 479 s) on the head with the code as shipped: 83 passed. Two failed: `live_fx` (its check "F_MIDI is refused until P4" is obsolete: fixed in test_live_fx.cpp, now "F_MIDI is accepted", `live_fx: 158 checks, 0 failed`) and `android_midi_sink` (a wall-clock timing test, "stamp ... wanted ... +- 3" under -j2 load; it does not touch LiveHost; passes alone: 22 s, 100%). Then on the final head: `ctest -R "live_midi|live_fx|midi_api|rt_core|midi_glue|midi_ledger|live_r1r2|live_equals_render"` (see below).
`live_midi`: "live_midi: 109 checks, 0 failed" (2.2 s, 20 songs); `rt_harness`: LiveHost pass 4 "848 MIDI records drained, 212 note-ons, midi_begin failures 0, may_hang 0, no allocation in process". The full 231-test suite and the sanitizer build were NOT run here (reduced matrix compiled in under THUNDER_SANITIZE_BUILD: 5 songs).

Final head: `ctest -R "live_midi|live_fx|midi_api|rt_core|midi_glue|midi_ledger|live_r1r2|live_equals_render"`: 100% passed, 9 of 9 (rt_core, midi_ledger, midi_api, midi_api_threads, midi_glue, live_equals_render, live_fx, live_r1r2, live_midi).

### Round 2 results (head de1c4fd, GCC Release, 2 cores)
`ctest -R "live|rt_|midi|render_api|players|seq_|fx|pdc|master|mix|meter" -j2`: 100% passed, 85 of 85 (476.5 s), including live_midi (204 checks), live_fx, live_r1r2, live_equals_render, rt_core (rt_harness with the MIDI pass), midi_api, midi_api_threads, midi_ledger, midi_glue, android_midi_sink, live_h, live_h_cpp. Not run: the full 231-test suite, a sanitizer build, clang, MSVC, wasm.
Fails on 8f903c9 (old live_host.cpp / .h under the new tests): `refusedPublish` (0 notes on channel 1, 5 on channel 10).
