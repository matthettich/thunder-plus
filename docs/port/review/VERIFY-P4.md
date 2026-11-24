# VERIFY U37 P4 (MIDI in the live host)
Independent review of `966f165..8f903c9` (branch `n1/u37-p4`). Reviewer: Claude (Fable 5.2). No code was changed; probes were a scratch test (`/home/claude/verify-p4-probe.cpp`, not committed) built against the branch.

## Verdict: MERGE (finding 1 should be fixed before P5 ships a shell on it; finding 2 needs a lead decision before P5)

The direct path is byte-equal to the posted path where the done-test looks, and my extra probes (stop at a quantum, stop with `at`, non-zero FX latency, future t0, play over play, play from HELD) found no wrong audio-side MIDI inside the claimed envelope. Two real defects sit just outside it (1 and 2).

## Results of the runs
- GCC Release, `-j2`: `live_midi` 109 checks 0 failed (20 songs, 1421 records byte-equal); `live_fx` 158/0; `midi_api` 0 failures; `midi_glue` 264 passed; `rt_harness` passes (pass 4: 848 MIDI records, 212 note-ons, 0 begin failures, "no allocation in process").
- ASan + UBSan (`-DTHUNDER_SANITIZE=ON`, targets `test_live_midi test_live_fx test_midi_api test_midi_glue rt_harness`): all pass, no sanitizer report. `live_midi` ran its reduced matrix (5 songs, 73 checks), as designed. My probes B, E, F, I, J also ran clean under the sanitizers.
- Not run: full ctest (231), clang, MSVC, wasm, TSan, `live_threads`.

## Findings

### 1. WRONG MIDI (narrow): a refused publish leaves the scheduler on the newer song
`live_host.cpp` songPublish (about line 157-165): `thunder_midi_set_song` is posted BEFORE the host's own steps, and the later steps can still fail (`SongAck` push when the command ring is full: line about 213; `songSlot_.publish` full; `sess_->make` memory). Nothing takes the scheduler's song back. The builder notes this ("a later failure leaves the scheduler with the newer song; the retry posts it again") but not what it does if there is no retry.
Failing run (probe E, idle host, command ring filled with `bpm()` until E_BUSY, then `songPublish(B)`; B = A with every `"mch":0` changed to `"mch":9`): second publish returns E_BUSY (-8); `play()` then plays A (host's `songCur_`), but all 5 note-ons come out on channel 10 (B's MIDI fields), none on channel 1. Control (ring not full, publish succeeds): channel 10 too, as it should (B is current).
Fix options: post the MIDI song only after the last failure point, or have `play()` re-post `songCur_`'s text, or on failure re-post the old text.

### 2. CONTRACT MISMATCH (timing): record `frame` is the engine frame, live.h says host timeline frame
Confirmed. `midi_begin(t0 = qStart / sr)` stamps `qStart` frames; `off_` is never added; `midiDrain` is a plain `thunder_midi_drain`. Probe H (tempo-clock, 600 quanta, re-anchor +480000 at quantum 300, records drained after each call): `record.frame - host frame at end of call` is [2760, 2880] before the re-anchor and [-477240, -477120] after it. A shell that trusts live.h ("the host timeline's frame") will see every record after a re-anchor 10 s in the past (notes already booked ahead keep their old frames, new ones continue in engine time). Either change live.h to say engine frames (and say the CLOCK records are the map), or add `off_` in `midiDrain` (then records drained before a re-anchor and after it differ by design). Needs a decision in P5.

### 3. NOTE (UX/robustness): at most 6 song publishes without an audio quantum, then E_BUSY
Probe F: 10 `songPublish` in a row with no `process`: results `0 0 0 0 0 0 -8 -8 -8 -8`. The scheduler adopts songs only in `midi_begin`, so a suspended or not-yet-started audio device means the 7th edit is refused. A shell that lets the user edit while the device is suspended must retry after audio runs. Document it in live.h (the notes mention it only in "Decisions").

### 4. NOTE (dead code / misleading comment): the `~LiveHost` stop quantum is unreachable
`~LiveHost` runs `midiOp(false)`, `midiBegin`, `midiEnd` and then `thunder_midi_free`. The comment says "so that a `thunder_live_midi_drain` made before destroy ... sees the stop it owes": a drain before the destructor cannot see what the destructor makes, and the handle is freed at the end of the same body. It is harmless (the sanitizer run is clean: members used are still alive), but the stop is never delivered. The real duty is the shell's `thunder_live_stop` + one `process` + drain before destroy (live.h line ~168-171). Fix the comment or drop the code.

### 5. NOTE (documented difference, quantified): ring order against a MIDI command posted after the play
Probe C (tempo-clock, `spp 8`, ports configured AFTER `play`/`thunder_midi_start`): posted path: `FA` Start at frame 3840 with no SPP (the port clock was not on when `start()` ran, so `spp_` was zeroed); live path: `F2 08 00` SPP at frame 1440 then `FA`. Streams differ from record 0. The live behaviour (SPP honoured) is arguably better, and the header already names this, but a shell that configures ports after calling play gets a different stream from the web.

### 6. NOTE: stop with `at`
Probe B (`stop(Q*128 + {0,1,64,127})`, tempo-clock / loop / echo): byte-equal to a posted stop before quantum Q in all 12 cases. So the MIDI stop lands at the quantum top, up to 127 frames (2.6 ms at 48 kHz) before `at`, while Players stops at `at`. Harmless; header already says so.

### 7. TEST WEAKNESS: explicit stop while PLAYING is compared on only 2 songs, and the reference does not stop the renderer
In `doneTest` 18 of 20 songs end by themselves before `stopQ`, so the explicit stop runs from IDLE for them. The posted-path reference also never stops the `SongRenderer`, so after `thunder_midi_stop` it keeps stepping the song. Probe A (explicit stop at quantum 20, 61, 150 for every song, 60 cases): 57 byte-equal; 3 differ, all `lfo-cc`: the reference emits CCs after the Stop, the live host (which drops its Seq) does not. The live side is the correct one; the harness is what is unfaithful. This is why a mid-play comparison is not in the done-test for lfo-cc-like songs. Suggest `postedPath` stop feeding the renderer silence/stop stepping at the stop quantum.

### 8. TEST WEAKNESS: the fx-latency path was not compared
The `fx-latency` song runs with no effects flags on either side (the builder says so). I ran it: probe D, ELECTRO with `"mo":2`, F_EFFECTS + F_MASTER + F_MIDI vs `SongRenderer` with `setEffects`, `setMasterChain`, `setPlan` (PDC auto and on), latency 2704 frames (56 ms) on both sides: no stop: 128 of 128 records byte-equal; stop at quantum 300: the first 66 records (everything up to and including the Stop) are byte-equal, the rest differs only because the reference keeps stepping. Clean. The check should still be added to `live_midi`.

### 9. NOTE (doc): `midi_begin < 0` handling
live.h says "the first one posts `thunder_midi_panic` once"; the code calls the scheduler's `panic(t0)` directly (correct for the audio thread; builder disclosed). Fix the header text. Also `midi_end`'s `alive` argument is unused by `MidiSched::book` (`(void)seqAlive`), so it cannot be wrong audibly.

## Check-by-check

### 1. Wrong MIDI: clean for the claimed envelope
`test_live_midi.cpp` compares every field of every record by `memcmp` of the 16-byte struct (`firstDiff`), asserts `!ref.empty()`, and the reference is not the live code: `SongRenderer` with its own hook plus `thunder_midi_start`/`_stop` posted, the 0.3 s rule written in the test. Both sides share `MidiSched` and `midi_spp_for` (by design: the thing under test is the driving, not the scheduler), so a scheduler bug would cancel. Mutations (each rebuilt, `live_midi` run, restored):

| mutation | result |
|---|---|
| M1 first try: Start also before `midi_begin` (double Start) | 1 check failed only (state-machine "second Start"): the byte-equal test did NOT see it (a second Start is idempotent) |
| M1b Start only before `midi_begin` (not after) | CAUGHT: 4 failures (start-midway byte-equal, SPP checks, state machine) |
| M2 drop the ENDED-timer Stop | CAUGHT: many songs not byte-equal |
| M3 Start `now` + one quantum | SURVIVED, equivalent mutant: `start()` only stores `now_` and `book()` overwrites it |
| M3b Stop `now` + one quantum | CAUGHT (22 failures) |
| M3d timer Stop one quantum late | CAUGHT (21) |
| M5 no MIDI Stop from IDLE | CAUGHT (19) |
| M6 drop `onStep` | CAUGHT (33) |
| M7 `alive` always true | SURVIVED, equivalent (argument unused) |
| allocation (`malloc`/`free`) inside `midiBegin` | CAUGHT by rt_harness pass 4 ("malloc inside the audio callback") |
Note on M1/M1b: the done-test streams are insensitive to where the Start sits relative to `midi_begin` when no FX figures change at that quantum; only SPP and the state-machine checks guard it.

### 2. Cases the done-test does not cover: see findings 5, 6, 8 (and 7 for a mid-play stop). Also probe J: `play(t0 = 96000)`: first records `FA`, note, `F8` all at frame 96000 (the Start follows the Seq's time, not the quantum). Probe I (play over play, play from HELD, 5 songs, 15 runs, CLEAR applied): every note-on has its note-off, no hung notes.

### 3. Real-time safety: clean
`midiOp`: fixed 16-entry array, `memmove` of at most 15 entries. `midiBegin`: no allocation; loop bounded by 16 ops; `midi_begin` pops at most `kMidiCmdCap`. `midiEnd`, the LATE additions in `afterBlock` (three counters read from `stats()`) and `applyOne`'s additions: no allocation, locks or blocking. The error-record path uses the existing `post`. `rt_harness` pass 4 does exercise MIDI (every instrument `mo:2`, port A, clock on, songs republished, `midiLead` changed between blocks, three re-anchors, play/stop/hold/ENDED timer) and the allocation guard is live (see the mutation above).

### 4. Threads and lifetimes: findings 1, 3, 4
`thunder_midi_set_song` is single-producer control (debug `CtlGuard`); `LiveHost::songPublish` and `midiLead` are on the control side, so no race I could find. The retire ring (8) cannot overflow with the cap of 6. Play over play, stop then play in one quantum, HELD, stop from IDLE: covered by the test and by my probe I, no hung notes. Stop from IDLE repeats the scheduler's stop (CLEAR again), as the header's table says.

### 5. Lead cap 1.2 s: clean
`kMaxLeadSec` is used in the C API check and `setLead`. Rings: 8192 records / 4096 pending / 4096 future. Probe G (7 songs including ELECTRO, 4 ports with clock, midi lead 0.06 and 1.2, drain every call and every 64 calls, stop at quantum 900): ring_full, pend_full, fut_full, held_full, echo_full, drops, may_hang all 0; `max_pend` is lower at 1.2 s than at 0.06 s. Caveat: the ledger songs are sparse (max_pend 65); I did not build a pathological 32-LFO-lane song, so the margin is argued (pending is bounded by the Seq's look-ahead, not the MIDI lead; the ring holds at most the lead's worth), not stress-tested.

### 6. Record frames: see finding 2.

### 7. Portability: clean as far as read
No `<unistd.h>`, sleeps, threads, `dynamic_cast`/`typeid`/`throw` in the diff; no "lxr" (case-insensitive) in the diff; the existing `lxr` files in `native/core` are untouched by it. `live_host.cpp` is built with exceptions on purpose (existing `try`/`catch` in `songPublish`; the new one in `play` is in the same file). No stack arrays above 8 KB added (`Rec buf[512]`, `char me[128]`). wasm builds only `core.cpp` and friends, not the live host. Not compiled with MSVC, clang or wasi-sdk.

## What I did not check
Full ctest suite, clang, MSVC, wasm, TSan, `live_threads`, a stress song for the ring caps, the P5 shell, `thunder_live_midi_drain` C wrapper (not in this diff).
