# U36 PR A: notes (branch `n1/u36a`, builder: Fable worker, 20-21 Nov 2026)

Spec: `docs/port/U36-SPEC.md` (section 8, the lead's rulings, first). PR A = T1, T2, T5, T6 and the pump half of T7, tests 1-8 and 12 of section 3.6, and the one-line note in CORE-BRIDGE.md.
Nothing in `native/core/src/render/` or `src/mix/` was touched; `midisched.cpp` and `test_midi_sched.cpp` / `test_midi_api.cpp` are not edited (PR #267).

## Checklist
- [x] T1 SPP helper: `native/core/src/midi/midi_spp.{h,cpp}` (`thunder::midi_spp_for`, returns -1 only on bad_alloc); `tests/midi_drive.h` `sppFor` is a one-line call to it. Case 12 `spp_for` compares it with the OLD body of `sppFor` (kept as the reference in `test_midiport.cpp`) on `start-midway`'s row and ten others.
- [x] T2 split: `src/midi/midi_host.h` (`midi_begin`, `midi_sched`, `midi_end`, `midi_last_now`), implemented in `midi_api.cpp`; `thunder_midi_process` = the same two halves (without the fx / audible overrides). `midi_api`, `midi_api_threads`, `midi_sched`, `midi_ledger`, `rt_midi`, `rt_midi_selftest` pass unedited. New check (`testT2` in `test_midiport.cpp`): `midi_begin` + `onStep` + `midi_end` give, for all 24 native ledgers, exactly the records (frame, port, bytes, kind, cable) and the CLEAR count of `runBlocks`.
- [x] T5 `include/thunder/midiport.h` + `src/midi/midiport.cpp`: device list, four letters, cable ids, hot-plug, time-ordered fixed queue (4096), per-cable CLEAR, F4 drop order, the pump (`CAP_TS` 20 ms send-ahead / spin 0.5 ms), stats, bad arguments.
- [x] T6 clock map (`thunder_midiout_clock_map`, seqlock, `mapUpd`'s 2 % smoothing, 50 ms jump, the `imp < fl` floor) and `thunder_midiout_set_out_latency`.
- [x] T7 (pump half): `rt_midi.cpp` runs a second handle with a `thunder_midiout` behind it; `thunder_midi_process`, `thunder_midiout_clock_map` and `thunder_midiout_pump` run inside the allocation / lock guard for 60 s with hot-plug, STOP / PLAY, live keys, raw ticks; `midiport.cpp` is in the source scan. `rt_midi --selftest` unchanged.
- [x] CORE-BRIDGE.md: one note on the `midi.cfg` row (arrays of four, `BRIDGE_V` stays 1).
- NOT in this PR: T3 `SongRenderer::setMidi`, T4 `Players::setDropLate`, cases 9-11, T9, T10, the `midi.ports` bridge event (it is the shells' / PR B's).

## Decisions (where the spec was wrong or silent)
1. **Cable id = a STICKY slot, not the index in the list.** The spec (3.2) says the cable id is the device's index in the current list and that "a re-plug that renumbers the list cannot re-route queued messages". Both cannot hold: after a renumbering a queued record's old index names another device. A device id gets a slot 0..199 the first time it is listed and keeps it for the session (when all 200 are used the slot longest unlisted is reused); a port with no device gets `200 + port` (as the spec says). A record's destination is its cable's device in the CURRENT list; a cable whose device is not listed is dropped (`dropped_gone`). Two letters on one id share the slot (one clock stream, one CLEAR), as the spec wants.
2. **Unplugging: the old cable's Stop does not reach the sink.** Spec 3.3 says the core sends Stop to the old cable and "the sink returns < 0". After `set_devices` without the device there is no sink index for it, so that Stop (and everything queued for the cable) is dropped and counted in `dropped_gone`. The other way round is covered: if the SINK fails first (the OS knows before the scan), the first failed send counts `send_errors`, marks the device gone until the next `set_devices`, and the rest of its messages are `dropped_gone` (no retry storm). Both orders are in test 5 (`list-loses-B`, `sink-fails-first`).
3. **CLEAR and what the OS already holds (CAP_TS).** With stamps a message is in the OS up to 20 ms before it is due, and a sink's `clear()` (MIDIFlushOutput) drops EVERYTHING the device holds, including messages BEFORE the marker's frame that must still sound (the output latency makes that a real window, 26 ms on the N0 phone). So the pump remembers the last 256 stamped-but-not-yet-due messages and, on a CLEAR that meets some, calls `sink.clear(dev)` and sends again those with `frame <= marker.frame` (same stamp, same order) and counts the rest in `dropped_cleared`. Without `CAP_TS` or without a `clear` nothing is recalled (nothing is in the OS earlier than 0.5 ms). Test 1 (`ts` mode) delivers exactly what `Drive::shell` kept for all 24 ledgers with a sink that models the flush; the first version without the re-send lost one message in three ledgers. Known gap: a message that the OS delivers between the pump's clock read and its `clear()` is sent twice (a window of microseconds).
4. **Clock map floor needs the engine position.** The web's `imp = ctx.currentTime - (ct + (now - pt))` needs "now" and the render position. The map call gets only `(frame0, host_ns0)`, so the floor reads `sink.now_ns` at the call (so `now_ns` must be callable from any thread; documented in the header) and the scheduler's last block start (`midi_last_now`, published by `midi_end`, a relaxed atomic). The error is at most one block (2.7 ms) and only while the floor is active. No floor set (`set_out_latency(0)`, the default) or no block yet: the pair is used as given. `imp > -0.5 s` is kept as in the web.
5. **Writers of the map.** "Any thread, wait-free": one writer at a time (an `atomic_flag`); a second concurrent call is skipped (the next one is 20 ms away). Readers use a seqlock with acquire / release data (no fences: TSan models them).
6. **Frames before the first map.** Read as "drained before the first `clock_map`" (not "frames earlier than the first `frame0`", which would drop legitimately late messages): those are dropped and counted in `dropped_nomap`. CLEAR markers still apply.
7. **Stale map** (not refreshed for 2 s, judged by the last raw `host_ns0` against the sink's clock at the pump): the last map is kept and every message sent while stale counts in `late`.
8. **Late ticks.** The pump drops a clock tick more than 20 ms behind its time (counted in `late` and `dropped_late_ticks`); anything else late is sent at once at the clock's time (`late` counts more than 2 ms). The core has already made the note verdict (B7).
9. **Queue full.** The queue is a sorted array of 4096 `thunder_midi_msg` (stable for equal frames; messages arrive nearly in order so the insert is at the end). When full the victim is the lowest class below the new message's (the core's own `prioOf` classes: tick, CC / bend / program, note-on / Start / SPP, note-off / Stop / All Notes Off), the latest in time among equals. A note-off, Stop or CLEAR is never a victim; if nothing is less important the new message is dropped, `queue_full` counts, and for a class-3 message `may_hang` is set (a field added at the end of the stats; the struct is size-prefixed).
10. **Stats** has two fields past the spec's: `dropped_late_ticks`, `may_hang`.
11. **`set_playing`** is a CONTROL call added as the spec asked (3.2); `set_out_latency` returns an int (E_ARG outside 0..2 s). Every control call re-syncs the scheduler; a full command ring returns `E_BUSY` and the next control call repeats the missing posts (the table is the truth).
12. **While playing a Gear change** is `set_port` with the values already applied; a cable change is `set_cable`, a clock box is `port_clock`; while stopped one `set_port`. The first sync of a port always uses `set_port`.
13. **`set_devices`** rejects (E_ARG, nothing changed) n > 32, a NULL list, an id without a NUL in 96 bytes, an empty id and a REPEATED id (a silently skipped duplicate would shift the sink's indices).
14. **Spin mode** spins on `sink.now_ns` at most 2^20 times (a stuck clock cannot hang the pump) and then sends. The tests' fake clock advances 1 us per call.
15. `thunder_midiout_*` constants `SEND_AHEAD` 20 ms and `SPIN` 0.5 ms are macros in `midiport.h` (ruling 5.10).
16. `midi_spp.cpp` is built with `-fexceptions` (it allocates a `Seq` and a vector on the control thread) and answers `-1` to a `bad_alloc`; `midiport.cpp` allocates only in `create`.

## Spec line citations that drifted (checked against `native` 5abe174 and `beta`)
`beta` index.html: `mapUpd` 17909-17923 and `MOB_AHEAD` 17951 are right; `nmScan1` is 18592 (spec 18622), `midiNames` / `midiRemap` 18662-18671 (spec 18661-18672, fine), the saved-key `MKEY` is 17610 (spec 17606), the "(not connected)" label is 17804 (spec 18801). Ids are `'n:' + name` with ` #2` confirmed (18599-18607).

## UNVERIFIED of spec section 7: what PR A could check
1. Live host structure / 128-frame quantum: not PR A (PR B / the lead).
2. `pdc_trim_frames` vs the web's "auto and stopped" term: not PR A (needs `render/`).
3. `Mixer::audibleMask()` vs the web for buses: not PR A.
4. AAudio / CoreAudio / WASAPI map recipes: need devices; the core side (any `(frame0, host_ns0)` pair, any floor) is tested.
5. Timestamp honoured over BLE / USB: needs a device. The queue's side (stamped hand-over 20 ms early, recall with a re-send) is tested with a fake OS.
6. Android manifest, `MidiDeviceInfo.getId()`: not PR A.
7. Windows MIDI Services: not read.
8. "The pump can meet 2 ms on Android without a real-time priority": needs a device; the pump is bounded (one drain of at most 128 x 128 records, one pass over the queue) and `next_ns` tells the shell when it must run.

## How to run
```
cmake -S native -B build-u36a -DCMAKE_BUILD_TYPE=Release && cmake --build build-u36a -j1 --target test_midiport test_midi_ledger test_midi_sched test_midi_api rt_midi
cd build-u36a && ctest -R 'midiport|midi_ledger|midi_sched|midi_api|rt_midi' --output-on-failure
```
`U36_DEBUG=1 ./tests/test_midiport` prints the hot-plug test's messages for the returning device. `./tests/test_midiport --threads` is the control / audio / sender test (TSan in CI).
