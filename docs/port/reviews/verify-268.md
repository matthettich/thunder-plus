# Independent verification of PR #268 (U36 PR A: MIDI out port table, sender pump, clock map)

Branch `n1/u36a` @ 726d692 (shallow, 30 deep), diff base `n1/u36-spec`. Verifier: Fable worker, 21 Nov 2026. Not the author; the builder's notes were read last and checked, not trusted.
Web truth: `origin/beta` index.html (`mapUpd` 17909-17923, callers 16889, 18204, 18356, 18442, 18783). Spec: `docs/port/U36-SPEC.md` 3 and 8.

## VERDICT: MERGE AFTER FIXES

Fix before merge: F0 (Windows CI red), F1 (CLEAR loses messages past 256 stamped), F2 (seqlock retry exhaustion discards every message). Everything else is follow-up or note.
The core of the work is sound: the sticky-slot / token design, the L1 write-only-what-changed fix, the time-based smoothing and the `midi_begin` / `midi_end` split are correct as far as I could check. TSan is clean, all 24 ledgers go through the pump with exact times, and `thunder_midi_process` is exactly equivalent to before.

## Numbers
- Release, GCC, Linux x86-64, `-j1`: **full ctest 193 run, 192 passed, 1 skipped (`pumpsynth_refused`), 0 failed** (2112 s). Two of the 193 are mine (`midiport_c`, `midiport_verify`).
- TSan (`-fsanitize=thread -O1`, everything): `test_midiport --threads` and `test_midiport`: 0 reports.
- CI at 726d692: Linux GCC, Linux clang, sanitizers, macOS, wasm, Android NDK, APK, `test`: success. **Core (Windows x64, MSVC): FAILURE** (F0). CodeRabbit: "skipped" on 726d692 (it reviewed 09356e3 only).

## Findings

### F0 FIX BEFORE MERGE: `midiport_threads` fails on Windows MSVC (CI red)
Evidence: job log `Core (Windows x64, MSVC)` (run 44873587241, job 136753346642), test #15: `FAIL: threads (spin): 455 blocks, 95 pumps, 365 messages sent` and the same for TS (91 pumps). `test_midiport.cpp:456` requires `pumps > 100`; the sender thread sleeps `sleep_for(500 us)` (:442) and Windows' default timer granularity (about 15.6 ms) gives about 90 pumps in 1.2 s. Not a product bug in the core; it is the same limit the spec tells the Windows shell to remove (`timeBeginPeriod(1)`, U36-SPEC 3.5). Fix: lower the threshold (for example `pumps > 20`), or call `timeBeginPeriod(1)` under `#ifdef _WIN32` in the test. Reproduce: read the log above; cannot be run here.

### F1 FIX BEFORE MERGE: more than 256 stamped messages outstanding: a CLEAR loses messages that must still sound
Evidence: `midiport.cpp:23` (`kHand = 256`), `:343` (the ring overwrites the oldest entry whether or not it is still in the OS), `:168-176` (`applyClear` can only re-send what is in the ring, but `sink.clear` flushes everything the device holds). NOTES decision 3 lists only the microsecond double-send window as a known gap, not this. CodeRabbit's `midiport.cpp:343` comment is right (Major).
Reproduction: `test_midiport_verify` (F1): CAP_TS sink whose `clear` models MIDIFlushOutput; 300 note-offs handed over 15-20 ms ahead (all due BEFORE the STOP's marker frame), then STOP: **256 of 300 survive, 44 are lost** (their note-offs: hung notes). Mutant `kHand = 16` passes the whole builder test-suite (see Mutations).
Real-world reach: more than 256 messages within the 20 ms send-ahead window (dense USB bursts: 4 cables of ticks plus CC LFOs plus a chord); the shells with CAP_TS are Android USB and macOS. Silent, so worth a fix now.
Suggested fix: count the live entries (`live && at > now`); when none is free, do not hand the message over: leave it in the queue (`break`, next pass), so the OS never holds more than the ring can restore. Keep `kHand` and add a stat.

### F2 FIX BEFORE MERGE: `readMap` returns "no map" when the writer is mid-update; the pump then drops every drained record as `dropped_nomap`
Evidence: `midiport.cpp:287` (`if (a & 1) continue;` consumes a try without reading) and `:291` (`return r;` gives the initial `{valid=false}`). The comment says "the last read (a torn pair would still be a map from a few ms ago)": wrong, nothing was read. In `pump` (`:310`) a record drained with `!map.valid` is dropped, including note-offs, Stop and Start, and `:321` returns 0. Trigger: the audio thread preempted between the odd and the even `mseq` store (the pump sees an odd value 16 times in about 50 ns). Rare, but the loss is silent, hangs notes, and was already established earlier in the session. CodeRabbit `:291` is right.
What I checked is fine: a torn (offset, host) pair cannot reach the timing: the data stores are release stores after the odd store, the data loads are acquire loads before the second `mseq` load, so any value from a newer write forces a mismatch and a retry. `off` is one 64-bit atomic; `host` only feeds the stale test. Mutant "no re-check" passes the suite only because nothing reads the pair concurrently with a failing check; TSan is clean (it is a logic race, not a data race).
Reproduction: `test_midiport_verify` (F2): a valid map exists, `mseq` made odd, one note-off drained: `dropped_nomap 1, sent 0`.
Suggested fix: keep the last good `Snap` in the object (sender-only) and return it when the loop gives up; correct the comment.

### F3 FOLLOW-UP: the core ring's `may_hang` is not in `thunder_midiout_stats.may_hang`
Evidence: `midiport.h:88` says "a note-off, Stop or All Notes Off did not fit: the shell should send PANIC", but only the queue's loss sets it (`midiport.cpp:144`). The scheduler's own ring (8192; new records dropped by class, note-offs only at 0 free) sets `thunder_midi_stats.may_hang`. After a long stall the ring keeps the OLDEST records and loses the NEWEST (ticks below 512 free, note-ons below 256, note-offs at 0): the pump then plays the old ones out late.
Reproduction: `test_midiport_verify` (F3): 10000 ticks plus 1000 note-offs with no pump: core `may_hang` 1 (ring_full 2808), `midiout` `may_hang` 0. Fix: OR the core's flag into the stats at `stats_get`, or say in the header that the shell must read both.

### F4 FOLLOW-UP: with more than 200 distinct device ids in a session a slot is reused and a queued record goes to another device
Evidence: `midiport.cpp:92-101` (`cabOf` reuses the longest-unlisted slot) and the epoch sweep at `:314-320` only drops cables that are NOT listed: a slot that is listed again for another device keeps the old device's queued and in-ring records.
Reproduction: `test_midiport_verify` (F4): a note-off for "old" in the core ring, "old" unplugged, 200 other ids listed one by one: the note-off goes to the 200th id's token. Needs 201 ids ever seen (ids are by port name, so rare). Fix: never reuse a slot while `seenEpoch` has not passed its unlisting, or reject with E_ARG at 200.

### F5 FOLLOW-UP: `now` is read once per pump; a slow `send` makes the lateness decisions of the rest of the pass wrong
Evidence: `midiport.cpp:300` and `:333-343` (only the spin path refreshes it). A 25 ms `send` followed by a tick that was 1 ms late at entry: the tick is 26 ms late but is sent (the rule says drop at 20 ms), and `late` is under-counted. CodeRabbit `:338` is right (rated Major; I rate it Follow-up: needs a sink that blocks for 20 ms+).
Reproduction: `test_midiport_verify` (F5). Fix: refresh `now` after each send (one clock read per message).

### F6 FOLLOW-UP: the latency floor mixes observations of different ages when `clock_map` is not called from the audio callback
Evidence: `midiport.cpp:267-269`: `imp = t0*1e9 - (heardAt + (now - host_ns0))` with `t0` = the last block start, `now` = the clock at the call. The header allows "the shell's own timer". A call 5 ms after the pair was made shifts the map 5 ms later than needed (CodeRabbit's outside-diff comment is right). The web reads `ctx.currentTime` and `performance.now()` together. Fix: document "audio callback only", or let the shell pass the time of its pair.

### F7 NOTE: `midi_begin` applies the posted commands before it sets the fx latency / audible
Evidence: `midi_api.cpp:164-167`. A live key or raw message applied in block N is stamped with block N-1's latency (the first block: 0). That is the order U36-SPEC 2.3 prescribes, and CodeRabbit's `midi_api.cpp:167` comment is right that it is observable (up to one block, or the whole latency in the first block). Cheap to improve: set both before `blockBegin`.
Host that uses `thunder_midi_set_fx_latency` + `thunder_midi_process` only: unaffected, `thunder_midi_process` never calls `midi_begin` and does not write the fx latency or audible. A host that calls both has its posted values overwritten every block (documented in `midi_host.h`).

### F8 NOTE: header contract gaps (`midiport.h`)
a) Lines 9-20 say every CONTROL call re-syncs and returns E_BUSY; `set_out_latency` and `get_port` never sync (CodeRabbit `midiport.h` comment right, Minor).
b) `thunder_midiout_free` (:93) says "after the pump thread has stopped": the audio thread's `clock_map` must have stopped too (use after free otherwise).
c) The token: "e.g. a pointer or a counter". A pointer can be reused by the allocator for another device while a message that read the old token is in flight. Say "never reused in a session" and prefer a counter. Duplicate tokens are not rejected by `set_devices` (E_ARG would be cheap).
d) A transient `send < 0` marks the cable gone until the next `set_devices` (`:341`): if a shell only calls it on hot-plug events that is permanent. Also: if the control thread re-lists a still-listed id with a NEW token while the pump has a send in flight with the old one, the failed send sets `cabGone` AFTER `set_devices` cleared it.
e) `set_playing` is a second copy of the transport state the scheduler already has (from `thunder_midi_start/stop`); forget one and the cable change takes the wrong path.
f) Before the first `clock_map` everything drained is dropped (`dropped_nomap`), a Start included: the shell must call `clock_map` once before PLAY.

### F9 NOTE: behaviour after a stall (answers to check 1)
- 400 ms without a pump: ticks more than 20 ms late are dropped (`:338`), everything else is sent at once in order (note-ons 400 ms late sound as a burst). `late` counts it. `may_hang` not set. Tested (case 7).
- 10 s without a pump at normal load: the ring (8192) does not overflow (about 200 ticks/s on 4 cables); the pump drains all (128 x 128 per pass), 10 s of ticks are dropped as late (the clock stream has a hole, no burst), notes and CCs go out in one burst, ordered by frame. Nothing hangs.
- With a flood (more than 8192): see F3. The pump's own queue (4096) then evicts by class, latest first, never a note-off / CLEAR / Stop (`:139-146`; `may_hang` only if everything in it is class 3).
- Cost: `enqueue` scans up to 4096 entries and `qRemove` memmoves up to 64 KB per eviction: a backlog of 8192 into a full queue is O(10^7) operations (estimated about 100 ms, not measured). Bounded, not wait-free in the usual sense; fine for a sender thread.
- Spin: bounded by 2^20 clock reads per message, and in real time by SPIN_NS (0.5 ms) in total per pass; `now` that does not advance: it sends (F7 case in `test_midiport_verify`, 1.4 ms). A clock that goes backwards also ends at the cap.

### F10 NOTE: ABI
`thunder_midiout_dev`, `_port` and `_sink` have no size field; only `_stats` is size-prefixed. Adding a field later needs new functions. `_stats_get` with a size that is not a multiple of 8 writes half a counter (`:353`). Sizes pinned in `test_midiport_c.c`: dev 208, stats 88. `_port` is 216 on LP64 but 212 on i386 (double alignment): do not rely on it across ABIs.

### F11 FOLLOW-UP (tests): gaps and surviving mutants
- `test_midiport.cpp:203` `CHECK(y80 == 0 || y80 == 1, "2: n/a")` can never fail; `(void)x80` leaves a count unused.
- Not covered: hand-ring overflow (F1), seqlock retry exhaustion (F2), core-ring overflow and `may_hang` (F3), slot reuse (F4), a slow sink (F5), CLEAR before the first map, the 400 ms stall with a CLEAR in it, 44.1 / 96 kHz, host time going backwards, `dt = 0`. My `test_midiport_verify` covers the first five and F6 below.
- Mutations (one at a time, local, reverted): see the table.

## Mutations (all in `midiport.cpp`, never committed)
| # | Change | `test_midiport` | `--threads` | Verdict |
|---|---|---|---|---|
| M1 | stable insert `frame >` -> `>=` (`:148`) | 6+ FAIL (ledgers mono-notes, overlap-poly: order of same-frame messages) | ok | caught |
| M2 | `applyClear` no longer re-sends what the flush dropped (`:175`) | 3 FAIL (`ts` mode: start-stop, start-midway, loop each one message short) | ok | caught |
| M3 | `kHand` 256 -> 16 | all passed | ok | SURVIVES (F1) |
| M4 | drop the `imp > -0.5 s` guard of the floor (`:269`) | all passed | ok | SURVIVES: no case with the engine more than 0.5 s behind |
| M5 | seqlock reader without the second `mseq` check (`:289`) | all passed | ok | SURVIVES: nothing reads concurrently with a write that matters |

## What I checked and found correct
- Check 1: same-frame order is the core's (stable insert, drain order = book order); a note-off never overtakes its note-on (frames, then stable); `next_ns` = `at - lead` of the head, `INT64_MAX` when empty (tested); stale or missing map: see F2, F8f; the stale flag counts `late` (tested). CLEAR is applied in stream order before the due loop, so a message queued earlier and due now cannot go out after the marker's drain; the Stop record itself has frame == marker frame and the test is `>`, so it survives.
- Check 2: without CAP_TS or without `clear` only the queue is edited; nothing the OS holds earlier than 0.5 ms exists. With stamps, with `clear` and a ring that has room: re-send in send order, same stamps, after-marker ones dropped and counted (M2 proves the tests need it). Double send only in the documented microsecond window. Another cable on the same device: a cable is one device id, so only two ids sharing one token (a contract violation) can meet; those entries are re-sent, not lost, while in the ring.
- Check 3 (map): offset `host - frame*1e9/sr` = web `o = pt - ct*1000` (17914); floor = 17919-17920 (`imp < fl && imp > -0.5`, applied BEFORE smoothing, as the web does: CodeRabbit's "apply the floor after smoothing" would deviate from 17921 and is wrong for parity); jump `|o - off| > 50` -> `off = o` (17921) the same; smoothing `1 - 0.98^(dt/25 ms)` is the lead's ruling L3: the web's callers are 17921's single `* 0.02` per call at 40 Hz (:18204), 2 Hz (:18783), per MIDI-in tick (:18356), 50 Hz with Link (:18442), once at record start (:16889), so no single rate; 25 ms is the one that matters for out. First call takes the pair as is; `dt` is clamped to 0..2 s (host going backwards or `dt = 0`: no move, no NaN, tested in `test_midiport_verify` F6 at 44.1 / 48 / 96 kHz). Precision at 30 days (2.6e15 ns, below 2^53 = 9e15, ulp 0.5 ns): error at most 1 ns for a frame 4410 later (F6, all three rates). The double is exact up to about 104 days of uptime, 1 ns up to 208. `clock_map`: no lock, no allocation, no loop (`atomic_flag` try-lock, one `pow`), bounded; it calls `sink.now_ns` (shell's, documented). The web's `if (!pt)` fallback (17913) is the shell's job.
- Check 4: only atomics cross threads; `c.*` and `devs` are control-only; the pump reads `cabLive` (acquire) before the token (relaxed, written before the live flag, release); `st[]` has one writer; TSan clean on 3 threads in spin and CAP_TS, plus the L1/L2 tests. Token 0 is fine (live flag gates). Two devices with one token: both slots resolve to the same sink device, nothing in the core detects it (F8c). A token reused for another device: F8c. Sticky slots over 200: F4.
- Check 5: old and new `thunder_midi_process` compared line by line: same validation (the same eight conditions, before anything is applied), same pop/apply loop with the same cap, `setBlock` then `book` then `publish`; the only addition is the `lastNow` store. `midi_end` validates `now`, `seqTime`, `bpm` as the old function did and returns false with nothing booked. Equivalence is also tested by `midi_api`, `midi_ledger`, `rt_midi` unedited and by `testT2` (24 ledgers).
- Check 6: `midi_spp_for` is the old `sppFor` body plus a `bad_alloc` catch; the test compares it with a copy of the old body on 11 rows; control thread only is in the header (`midi_spp.h:1`).
- Check 7: `midiport.h` compiles as C99 `-pedantic -Werror` (`test_midiport_c.c`, new, ctest `midiport_c`), every function with NULL / out-of-range arguments gives what the header says; `stats` size-prefixed, 4-byte size refused.

## CodeRabbit (8 inline comments + 1 outside-diff; all on 09356e3, the review of 726d692 was skipped)
| Comment | Judgement |
|---|---|
| `midiport.h` retry guarantee only for calls that sync | RIGHT, Minor (F8a): `set_out_latency` and `get_port` do not sync |
| `midi_api.cpp:167` fx latency applied after the commands | RIGHT, Minor, and it is what U36-SPEC 2.3 says (F7); one block stale, first block 0 |
| `midiport.cpp:165` history keyed by list index | WAS RIGHT, FIXED by L2 (tokens): the history holds the token, a renumbered list cannot redirect a clear. (A token that changes for the same id leaves old entries that fail `send` and are dropped, no misroute) |
| `midiport.cpp:206` `set_devices` shows `-1` to the pump | WAS RIGHT, FIXED by L1 (only changed slots written, tested by `threadsDevs`) |
| `midiport.cpp:259` apply the floor after smoothing | WRONG for parity: the web applies it before (index.html:17920 then 17921), so does the port; the slow rise after enabling a floor is the web's |
| `midiport.cpp:276` keep the last valid map when the read fails | RIGHT, Major-ish (F2), reproduced |
| `midiport.cpp:323` refresh host time before each lateness check | RIGHT, Minor in practice (F5), reproduced |
| `midiport.cpp:328` retain every outstanding stamped message | RIGHT, Major (F1), reproduced; "increase kHand alone does not help" is also right |
| outside diff: time-aligned observations for the floor | RIGHT, only when `clock_map` is called from a timer (F6) |

## Reproduce
```
cmake -S native -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j1 --target test_midiport test_midiport_c test_midiport_verify
build/tests/test_midiport_c ; build/tests/test_midiport_verify          # F1-F5 print BUG PRESENT; F6 (precision / edge cases) and F7 print ok
build/tests/test_midiport_verify --strict                                # exit 1 while any finding is unfixed: use it as the done-test of the fixes
```
New files: `native/tests/test_midiport_c.c` (C-API gate, ctest `midiport_c`), `native/tests/test_midiport_verify.cpp` (white-box: it `#include`s `midiport.cpp`; ctest `midiport_verify`, exit 0 unless `--strict`), two entries in `native/tests/CMakeLists.txt`. Do not merge these into `native` with the verifier branch; take them with the fixes.
