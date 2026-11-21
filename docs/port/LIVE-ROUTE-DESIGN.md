# LIVE-ROUTE DESIGN: live engines into the mixer (the lead's rulings, 19 Nov 2026, 20:30)

**IN THE MIDDLE OF: rulings and the worker's table are done (LIVE-ROUTE-NOTES.md); the build of step 1 is next (decision-log golden first, then PR A the `Players` move, then PR B).**

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

## Rulings on the worker's table (the lead, 19 Nov, after LIVE-ROUTE-NOTES.md ff8ea16). Where these differ from L1 to L10, these win.
- **C1 accepted.** `reset()` on the four cores and the shell, `seek(clock)` on the shell; neither allocates. Nodes are made, and their parameters set
  (`VoxCore::set` allocates), inside `set_pad_live`; a bind at a note is `reset()` + `seek()`.
- **C2 accepted: L5 moves to the pool.** Each node has its own timeline in the pool (the future events; the shell sees one 128-chunk at a time) with
  `cancelFrom(t, mask)` used on EVERY NoteOn and kill, as `cp:535-536` and `cp:723-724`. L5's overflow rule (a note-off always wins, counted) applies to
  that timeline. `LiveShell::enqueue` also never drops a NoteOff (for hosts that call `process` directly).
- **C3 / D4: follow the JS** (page and core agree): a note-on at the sample of a booked note-off takes the off back; the note restarts without a
  release. Gate L10(b) keeps the one `seqRender` sequence that differs as a listed, expected difference.
- **C4: when a live voice counts and ends.** A live voice is in `voices()` from its note-on. It has ENDED when its gate is off and either its node's
  output has stayed under 1e-5 for 4096 samples or 4 s have passed since the off (the JS's `stopAll` window), whichever is first. When the sequencer
  ends (`songOver()`), `Players` releases every live voice still held at the song's end time, so an unknown-length render ends. (The page cuts
  instead; with `use_frames` the host's length wins as before.) Poly cap: a live voice is in the track's one list (L6) by its serial.
- **C5 accepted:** vox is 5 nodes a track (4 poly + the mono node). **C6 accepted:** the pool takes `SEV_TEMPO` (bass `bpm`, later pump and waves).
- **C7 / D7: follow the shell, which is the page:** a slide cut by the next note glides on to that note's sample. (The core holding the FROM pitch
  is taken as a core fault; reported to the owner.)
- **C8 / C9 / C10 noted, no change:** waves and pump synths keep their own queues and latencies under L4 in steps 3 and 2; `stopAll` is built now for U37.
- **D5 and D6: follow the PAGE** (as DISPATCH-NOTES: where the two players differ and the page's is the fuller behaviour, the offline reference is
  the page). D5 (the CUT / RES low-pass on the nine kinds outside `ENG_CUT`) concerns step 2 and 3 kinds only: build it there. D6 (`mod` with `wob`
  moves the bass wobble) is step 1. Both reported to the owner as suspected "Audio thread" sequencer bugs in the web app.
- **D1, D2, D3, D8, D9, D10:** the port's behaviour stands (L3, L4). **D11, D12:** as the table says.
- **`eng` (ENGINE TARGETS):** the last commit of step 1 ("1b"), in its own PR if step 1 is ready before it.
- **The decision-log golden (`tools/live-route-dump.mjs`) is accepted as gate L10(f)** and is built FIRST (Node only, no compiler).
- **Vox idle skip:** never skipped until the bit-compare test in the notes passes.
- **The C API, the track mask, the `Players` layout and the build order are accepted as proposed.** Two PRs: (A) the `Players` move alone, gate: every
  existing render test bit-identical; (B) the rest of step 1.

## Rulings P1 to P6 (the pre-wiring read)

P1 (PR A, `Players`). Do NOT hold `const Song&` for the routing reads (`insts[ii].mo / slc / sync`). `Players::event` takes the Song it should read as an argument (or a `setSong(const Song*)` called by the owner each quantum): a live host adopts a new `SongSnap` while playing and the old Song is freed. `SongRenderer` passes its own `S_`. Same for pads: `Players` reads `RenderPads` through a pointer the owner can re-point between quanta (`setPads(const RenderPads*)`), never a stored reference. No behaviour change offline.

P2 (PR B, takeover on a poly track). In the JS, bass / acid / mach `liveNote` is the MONO branch even when `ev.poly` is true: it kills every other voice on the track. `VoicePool::take` / `SfPool::take` do nothing for a poly event (voice.cpp ~:141). So the live route's takeover of the sampler and SoundFont voices must not go through `takeAll(e)` unchanged for those kinds: pass an event copy with `poly = false` (or add an explicit `takeMono`), and pin it with a test (a poly track, a sampler chord, then a bass note: the chord is released) and a mutation.

P3 (PR B, the poly cap). Check the JS before coding it: live notes do NOT run the `while (T.length >= POLY) kill(T.shift())` cap themselves (`liveNote` has no such loop: polyVox / polyPs pick a node instead), but a live voice IS in `T`, so a later sampler or SoundFont poly note can shift a live voice out. Reproduce exactly that asymmetry, state it in the notes with the lines, test it.

P4 (PR B, tempo). `SEV_TEMPO` is emitted only when a step changes bpm or tks (seq.cpp ~:993), AFTER that step's notes, never at the start and never on `Seq::setBpm`. So: `Players::event` receives `bpm` on every call (it already does): the pool keeps the bpm lane current from that argument at each note (a Bpm event at the note's time when it differs from the node's last), and handles `SEV_TEMPO` by its own time. Events of one step are applied in TIME order to a node's timeline, not arrival order.

P5 (PR B, replaces part of L5). An ARP books up to 97 rate points for one note; "drop a Rate event on overflow" would recreate the stuck-pitch bug the sampler fixed (N1 review B5). The node's RATE lane is therefore not in the general event queue: use `AutoN<kAutoR>` with the same folding as voice.cpp ~:108-118 (read how the sampler does it and reuse it), sampled by the shell feed at each control block. L5's rule stays for the note / bpm / wob / eng timeline.

P6 (PR B, small ones). (a) `seq.arp(e)` is valid only until the next `step()`: copy what you need at the event. (b) `SEV_MOD` carries `wob` (seq.cpp ~:937): that is D6's input. (c) `SEV_ENG` is change-only (up to 4 points): keep the per-TRACK engine state in the pool so a node bound later starts from it (the JS's `engSt` + `engPut` at liveStart). (d) One seconds-to-sample rule, the sampler's: the first sample i with `tb0 + i*dt >= t` (read voice.cpp's start rule and use the same function), so a live note and a sampler note at the same `ev.t` start on the same sample; test exactly that.

## Correction after the verifier (verify-265 F6): L2 as built
L2 said "bind at the first note, re-bind the oldest, `liveNoNode` when all busy". What is built, and accepted by the lead: ONE NODE PER MASKED TRACK (vox: five), made in `set_pad_live`, none at a note, so nothing allocates at a note; `liveNoNode` counts a note on a track outside the pad's mask (or a vox track without nodes). The pool calls `seek()` at a node's first block; `reset()` exists for a later live host. Memory per pad per track: bass 57 KB, acid 38 KB, mach 38 KB, vox 235 KB. The idle skip that the up-front nodes make necessary goes into the `eng` PR.
