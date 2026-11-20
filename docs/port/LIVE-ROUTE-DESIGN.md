# LIVE-ROUTE DESIGN: live engines into the mixer (the lead's rulings, 19 Nov 2026, 20:30)

**IN THE MIDDLE OF: rulings written; the worker's JS table (LIVE-ROUTE-NOTES.md part 1) is next; nothing is built.**

Queue item 2 of PICKUP's START HERE (19 Nov, 17:00). Basis: the lead's read of `render/render.h|cpp`, `engine/live_shell.h`, `sf/sf_live.h`,
`seq/runner.h`, `core/core-processor.js` :662-725 (`lnode`, `liveNote`, `liveStart`, `stopAll`), DISPATCH-NOTES rows 4-6, WIRING-DOSSIER (R11, seams, PDC).

## What "the live routes" are
- **The live-engine route:** a pad whose `PadRoute` is `Live` sounds. Today `SongRenderer::dispatch` counts it (`notesLive`) and it is silent.
- **The SoundFont live route:** nothing new to build in the pool. `SfPool` already renders into `mix_.inputs` from `SongRenderer`; the live host (U37)
  drives the SAME `dispatch` from a `SeqRunner` instead of the renderer's own loop. So this item's second half is a condition on this design:
  **the dispatch code and the three pools (sampler, SoundFont, live) must be usable by a host that is not the offline loop** (see L7).

## Rulings
- **L1. One `LivePool`** (`engine/live_pool.h|cpp`), a member of `SongRenderer` next to `pool_` and `sfpool_`, adding into `mix_.inputsL/R()` before the
  mixer runs. The page's separate live nodes, their AudioParam overrides (`tcOver`, `ov.p`, `ov.until`, `release`) and the hand-back to the page do
  not exist natively: the pool IS the player. What is ported is what the listener hears: which node a note picks, takeover, choke, poly picking
  (`sameCol`, free, oldest `lastOn`), syllable counting, legato / slide, accent, velocity, gain, pan, bends (`pitchMods`), `off` / `cut` / `rel`,
  `stp`, `stopAll`, the ENGINE TARGETS (`eng` events, `engPut`).
- **L2. Nodes are made on the control thread, never at a note.** The host declares a pad live (`thunder_render_set_pad_live`: kind + the pad's
  parameters, copied). The pool holds a fixed number of nodes per live pad and binds one to a (track, pad[, j]) key at the first note that needs it
  (a binding is an assignment and a core reset: no allocation). When none is free, the node whose voice ended longest ago is re-bound; if every
  node still sounds, the note is silent and counted (`liveNoNode`), and it still takes the track over. The worker proposes the counts from the
  JS (mono: one a track that plays the pad; polyVox: POLY; a pump synth: one node of four lanes) and states the memory per node.
- **L3. Timing: the U26 rule stands** (a note event at its exact sample, control blocks counted from the last note event, the same output for any
  host block size). The page's 128-frame quantisation is NOT copied.
- **L4. No latency is reported; the pool runs each node ahead instead. THIS REPLACES the recorded condition "U26's latency goes into the PDC plan".**
  A node whose engine is `lat` samples late (31 bass, 15 vox / acid / mach, 127 waves, a pump synth's own figure) is processed `lat` samples ahead of
  the mixer's clock and read `lat` behind, so its sound lands on the note's own sample and sampler voices on the same track stay aligned with it
  (a per-track PDC entry would have moved them). This works because the sequencer hands events over LEAD (0.25 s) ahead. An event that arrives
  for a time already inside the look-ahead (a pad played by hand in U37, a STOP) takes effect at the node's clock, at most `lat` samples late:
  counted (`liveLate`), documented, accepted. An offline render never has one (the first quantum processes `lat` extra samples of each node).
- **L5. R11: a note-off always wins.** `LiveShell` keeps its 256-event queue. When it is full: a Rate / Bpm / Wob event is dropped and counted;
  a NoteOn is dropped and counted; a NoteOff / End is never dropped: it sets a forced-off time, every queued event at or after that time is
  discarded, and the core gets `off()` at that sample. A stuck note is impossible; the cost is the notes that were queued after it (counted).
  The same rule for every other node type's queue.
- **L6. Voices of the three pools are ONE list per track** (the JS has one): takeover, choke across tracks, the poly cap of four (`capPoly`'s serials)
  and `voices()` / `peakVoicesAll` include live voices. A sampler or SoundFont note takes the track's live voices over as `liveNote`'s kill does
  (a release, `v.syn` true), and the other way round.
- **L7. Shape for U37.** `dispatch`, `capPoly`, `takeAll` and the three pools move behind one small class (working name `Players`) that owns the
  pools and takes `(event, arp, bpm)`; `SongRenderer` keeps the loop, the mixer and the master. No behaviour change; the existing render tests
  are the gate for that move. U37's live host then owns a `Players` and a `SeqRunner`.
- **L8. Stats (appended to `RenderStats` and the C stats, nothing renamed):** `notesLive` stays every note of a live pad; new `notesLivePlayed`,
  `liveNoNode`, `liveLate`, `liveQueueDropped`, `liveForcedOff`, `liveFailed` (a core that refused: `bad()` / `failed_`).
- **L9. Steps, each its own PR, each verified independently:**
  1. `Players` (L7) + `LivePool` with the four U26 cores (bass, vox, acid, mach): mono, polyVox, `off` / `mod` / `stp` / `stopAll`, bends, gain / pan,
     L5, the C call, stats. `eng` events (ENGINE TARGETS) if the table shows they are small; otherwise step 2.
  2. Pump synths (four lanes, velocity, `PumpSynth` is process-global: one audio thread) and the xeng kinds the page plays live.
  3. …waves as a live engine (U30's conditions: Expression compiled at preparation; Marbles' re-make must not allocate at arm).
  LXR's live lanes stay with U04's seam, after these.
- **L10. Gates for step 1.** (a) The seam, as the SoundFont route's: a tap on the mixer inputs equals a second sequencer driving the cores by the
  U26 rule directly, bit for bit, every track and quantum, at host blocks 1 / 128 / 480 / 4096. (b) Against the JS: `tools/engine-u26-dump.mjs`
  `seqRender` fed the events of a small song (the tool that makes the sequencer goldens gives the events), bit for bit per node before gain / pan.
  (c) Mutations by hand for every takeover / choke / poly / off rule (each must fail a test). (d) `rt_harness`: no allocation in `block()` with
  live pads. (e) Release + ASan / UBSan ctest.

## What the worker's table must settle before any C++ (LIVE-ROUTE-NOTES.md part 1, line-referenced)
1. `coreKitV()`: what `kv.live` and `kv.lk` hold for each kind (`kind`, `vox`, `ps`, `vel`, anything else), and every kind that exists.
2. Each live worklet (`BASS_PROC`, `VOX_PROC`, `XPROC`, the pump synth's, the waves one): its params, where `ov.g` and `ov.pn` act (the pan law, mono
   or stereo), what `ov.post` means for the sound.
3. `kill(v, at, tc)` for a live voice (gate, release, what `stopAt` / `ended` become), `off`, `mod`, `stp`, `pitchMods`, `engPut` / `eng`, `chc`.
4. The page's OFFLINE player for live pads (`bassNode`, `bassNote`, `polyVox`, `polyPs`, index.html near :22013): every place it differs from the
   core's `liveNote` / `liveStart`. The lead rules on each difference; the default is the core's behaviour (as for the SoundFont route).
5. Per rule: is it in step 1, and the test that pins it.
