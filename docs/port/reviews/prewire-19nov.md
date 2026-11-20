# Pre-wiring read for U37 and the LivePool (the lead, 19 Nov 2026, 20:40; read-only, nothing built)

Read: seq.h, runner.h, session.h/.cpp, chop.h, voice.h/.cpp, mix.h, fxhost.h; mix.cpp 196-401; seq.cpp 372-600, 655-760, 925-1001; fxhost.cpp 140-381;
fxpump.cpp 88-156, 262-294. **NOT read:** seq.cpp 600-655 and 760-925, mix.cpp 1-195, fxpump.cpp 156-262, core.cpp, fxbank, the JS `engPut`.
Line numbers are from `native` at c97da6f; verify a line before editing it.

## Must fix before U37 (the live host)
- **F1.** Routing reads the Song and the pads by stored reference (`S_.insts[ii].mo / slc / sync`, `pads_`, render.cpp `dispatch`). After a live song
  edit the Seq reads the new `SongSnap` while dispatch reads the old, freed Song. Ruled (P1, LIVE-ROUTE-DESIGN): `Players` is handed the Song and pads
  by its owner, re-pointable between quanta.
- **F2.** Pad buffers have no lifetime rule: `Voice::data` (voice.cpp:186) and `RenderPads::var` are borrowed; replacing a pad while it sounds is a
  use-after-free.
- **F3.** `SeqChop` comes from the session's opts (seq.cpp:397); `SongSnap` holds no chop, so PICKUP's "a pad change during PLAY publishes a new
  table with the snapshot" has no mechanism, and `setHits` mutates vectors the audio thread reads (chop.h:44).
  **F2 + F3 want one design: an immutable pads-and-chop snapshot with deferred free, published like `SongSnap`.**
- **Pump is process-global:** a live host and an offline render on two threads corrupt `pump_io`. They must be serialized.

## Should fix
- **F5.** `FxHost::plan` (control) and `process` (audio) both use `ModuleState(&rand_, ...)` (fxhost.cpp:152, :287): a race on the host's RNG state.
- **F7, F9, F10:** into the LivePool brief as rulings P2, P4, P5 (mono live kinds take over on a poly track; `SEV_TEMPO` is change-only and late in
  its step; an ARP's 97 rate points must not go through a droppable queue).

## Notes
- **F4 (= R4, stands):** `PumpSlot::renew` frees and makes a pump instance on the audio thread (fxpump.cpp:150, :272).
- **F6.** `FxHost::commit` puts about 12 KB on the audio stack (fxhost.cpp:203), `lfoEvs` 7 KB: check the wasm stack size in U37.
- **F8.** Live notes never run the poly cap themselves; only a sampler / SoundFont poly note can release a live voice (ruling P3).
- No allocation, lock or unbounded loop found in `Seq::step`, `takeOver`, `SeqRunner::pump`, the `VoicePool` calls, `Mixer::render / event / set`.

## The dossier's items now
- **Resolved:** D1 (seq.cpp:699-712; runner.h:53-65), D2, D3 (render.cpp), R1 (`SongSnap`, `seq_adopt`).
- **Stand:** R2 (no voice stealing, voice.cpp:171), R3 (64 KB memset, voice.cpp:47), R4, R5 (fixed `randomSeed`), R6 (mixer ring memory), R8, R9, R11
  (ruled in the live-route design). **Not checked:** R7, R10.

## What U37's host must do
- Song: publish, `seq_adopt` before `pump`, `collect()` on the control thread.
- `Mixer::set / setPlan / setChain` are audio-thread only: a command ring; a `BusPlan` (about 1.5 KB) needs a slot, not a ring entry.
- Fx: `plan` on the control thread, `commit` on the audio thread, the old plan travels back for deletion (`retired_`).
- Blocks: fixed 128-frame quanta on one grid, as `SongRenderer::block` (`SeqRunner::pump` plays 3 steps a CALL; `Mixer::render` silently cuts N above
  `maxBlock`, mix.cpp:377; `FxHost` reads lanes once a call and gives silence above 4096).
- Seed the tempo from `seq.bpm()`: `SEV_TEMPO` is not emitted at the start or on `setBpm` (seq.cpp:993).
