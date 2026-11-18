# WIRING DOSSIER (read-only review, branch `native`)

Basis: line-by-line read of the files named below. Nothing was built or run. Paths are relative to `native/core/` unless they start with `index.html`, `core/` or `docs/`. Line numbers marked "~" were taken from my notes and are approximate. Everything else was read.
Verify a line before editing it.

## 0. Bottom line

`src/core.cpp` is still the N0 stand-in. It does not use `Seq`, `VoicePool`, `Mixer` or `FxBank` (core.cpp:39, :204-227: only bpm/play/stop/echo/song.load).
The real engine is reachable today only through `SongRenderer` (`src/render/render.cpp`) and the CLI `tools/thunder-render/main.cpp`.
The wasm CI job builds only the stand-in (`.github/workflows/native.yml:87-121`). No `window.thunderCore` exists in `index.html`.
So "wiring" means three things:
1. Expose `SongRenderer` through the public C API (U34).
2. Build a real live core (Seq + VoicePool + Mixer + FxHost) behind `thunder_process` (U37).
3. Write MIDI scheduling from scratch (U35, no code and no `tests/midi-ledger*` exist).

## 1. Per file

### 1.1 seq (`src/seq/seq.h` 245 lines, `seq.cpp` 903 lines)
- **API:**
  - `Seq(const Song&, const SeqVars&, SeqMode, const SeqOpts&)`
  - `int step(SeqEvent*, int cap, SeqStep*)` (seq.cpp:616)
  - `alive()`, `setLoop` (clamped, seq.cpp:518), getters `time()/bpm()/tks()`, `arp(e)`, `setXf/setMac/setFill`, `dropped()`.
- **Ownership:**
  - `const Song& S_` and `const SeqVars& V_` are references, not copies.
  - `SnapSlot::adopt` (rt/snap.h) retires the old Song to the control thread, so a live Seq dangles after `song.patch`/`song.load`.
  - The JS edits S in place (`core/core-processor.js` `msg`).
- **Off the audio thread:** construction (seq.cpp:374-407) allocates `rv_` (up to 33*256*512 u32, ~17 MB worst case), chase tables and `arpPool_`.
- **On the audio thread:** `step()` only. A grep for `push_back`, `map`, `set` and `assign` found them only at line 407 or earlier, in the constructor and build functions.
- **Loops:** `settle` 4200 iterations (seq.cpp:109), arp 96, retrig 64.
- **Limits:** `kSeqStepEvents=12288` (about 5 MB), `kSeqArpMax=96`, `kSeqTracks=33`, `kSongNT=32`, `kSongRows=256`.
- **Deliberate differences:** `randomSeed` is fixed at 0x2545F491 (seq.h:61). The JS uses `Math.random`. This is a justified choice for the golden gate, but the live host should pass a time-derived seed.

### 1.2 voice (`src/voice/voice.h` 136 lines, `voice.cpp` 293 lines)
- **API:** `VoicePool(sr, maxVoices=64)`, `note()` (voice.cpp:142), `take`, `off`, `mod`, `stp`, `stopAll(at)`, `render(outL, outR, N, t0)`, `setTime`, `setPageTiming`.
- **Ownership:** the pool owns its rings (`kRdRing=16384` floats per voice, about 4 MB for 64 voices). Pad buffers are borrowed from the kit. Pad buffer lifetime is therefore the host's job.
- **Limits:** `kVoicePoly=4`, `kVoiceTrackCap=8`, `kRdRing=16384`, `kAutoR=128`, `kAutoGFP=32`.
- **Audio-thread allocation, lock or syscall:** none found. I grepped for `new`, `malloc`, `vector`, `mutex` and `std::function`, and read the render and note paths. One deliberate cost is that a `tw` (shaped) or reversed note memsets a 64 KB ring on the audio thread (voice.cpp:47). It is bounded.
- **Differences from the JS:**
  - There is **no voice stealing**. When `nFree_==0` the new note is dropped and `dropped_++` (voice.cpp:155). The JS has unbounded voices.
  - STUBs: SoundFont, MUT/GEN variation buffers, live engines (`kv.live`/`kv.lk`), SYNC `ev.st`, WATCHDOG.
  - The pan law matches the JS (`stBal` voice.cpp:24 against `core/core-processor.js:154`).

### 1.3 mix (`src/mix/mix.h` 208 lines, `mix.cpp` 401 lines, `pdc.*`, `delay.h`)
- **API:**
  - `Mixer(sr, maxBlock, maxDelaySec=0.99)`
  - `mix.set` (Vol/Trim/Pan/Mute/Solo/Out only; **Send returns false**, mix.cpp:267)
  - `render()` clamps N to maxBlock
  - `pdc_plan(BusPlan, PdcSlot[], n, sr, mode)`, `pdc_apply`, `pdc_delay_set`
- **Ownership:** the Mixer allocates 33 tracks x (post 2ch + pre 2ch + clock rd) delay rings at construction. My estimate is about 31 MB at 48 kHz and about 125 MB at 192 kHz. A second renderer doubles it.
- **Strip order:** inp(TRIM), chains hook, fx hook, pre, g, mg, pan, dl, master sum, out, master fx hook, mo (mix.cpp:304-342, 374-399).
- **Pan:** mono uses the equal-power law, stereo the balance law.
- **Audio thread:** no allocation found (same grep). `pdc_plan` allocates a `std::vector` and runs on the control thread. `ScopedFtz` is a no-op on wasm (rt/ftz.h).
- **Limits:**
  - `kAutoUi=16`, `kAutoSong=64`. A full lane takes "latest wins" (mix.cpp:232-242).
  - `maxDelaySec` 0.99 (the JS clamp, index.html `pdcSync` 21625+).
  - `bit32()` guards track 32.

### 1.4 fxhost (`src/fx/fxhost.h` 250 lines, `fxhost.cpp` 369 lines)
- **API:** `plan/planFromSong` (control), `commit` (audio), `psetAt`, `lane`, `reset`, `setRest`, `psetPump`, `pmidi`, `hold`, `setSources`, `setTapMap`, `process(L,R,n,t0,bp,bpn)`, `pop*` telemetry, `slotLatency/slotBypass/slotCount`.
- **Limits:**
  - `kFxSlots=8`, `kFxBlock=128`
  - `kFxMaxCall=4096`: a longer call gives silence (fxhost.cpp:273)
  - `kFxQueue=256`: an FXV is dropped when full (fxhost.cpp:209)
  - `kPumpMidi=1024`, `kPumpQueue=32`, `kPumpMaxParams=280`
- **Audio thread:** no `new` or lock found in `process`.
- **State:** `ModuleState` is `thread_local` in the dsp layer. The p1 anti-denormal loop writes L[k+1] for odd n and relies on +2 slack.

### 1.5 fxpump (`src/fx/fxpump.cpp` 317 lines, `pumpfx.h`)
- **Audio-thread allocation:** `PumpSlot::renew` calls `pump::destroy` and `pump_new` (fxpump.cpp:143-147). It runs from :231 (runaway output) and :266 (rest wake). `remakes_` counts them. This is the lead's decision of 16 Nov (keep the JS behaviour); it is not a bug, but it breaks an allocation guard.
- **Gate:** `PumpGate` is two seq_cst atomics, so the audio side takes no lock. The control side spin-waits with `std::this_thread::yield` under a `std::mutex` (`CtlGate`, ~:98). If the control thread holds the gate, pump slots pass the sound for that block (a click when an effect is added mid-song).
- **Global state:** `pump_io` and the handle table are process-global, so only one audio thread may run pump. This is not enforced.
- **Memory:** rest delay lines are 2x8320 floats per slot (:126).

### 1.6 core.cpp (273 lines) and public headers
- **API** (`include/thunder/core.h`, 51 lines): `thunder_create(sr, max_block)`, `thunder_process`, `thunder_post(JSON)`, `thunder_poll`, `thunder_put`, `thunder_put_info`, `thunder_host_stats`, `thunder_destroy`.
- **Missing from the API:** a render API, `mix.set`, `note.on/off`, `kit.load`, `buf.use`, `play` with options, `render.*`, `meters.want`, `midi.cfg` (CORE-BRIDGE.md `BRIDGE_V=1` requires all of them).
- **Command rings:** cmds `Spsc<Cmd,256>`, evts `Spsc<Evt,1024>`, `kPend=64` timed commands (core.cpp:49, 63-64).
- **Audio path** (:123-169): clean.
- **Control path:**
  - `thunder_put` stores into a `std::vector`, so it allocates on a non-audio thread. `kMaxPutFloats` is 2^27 (:244).
  - JSON is parsed by a hand-rolled `find_key` (:183).
  - Frame counter is int64. `thunder_create` refuses rate <8000 or >384000 (:88).
- **Headers:** `rtlog.h` uses atomics only. `lxr_seam.h` is a C function-pointer table, with `live_open`/`live_process` PROVISIONAL.

### 1.7 render (`src/render/render.h`, `render.cpp`; the pieces U34 builds on)
- **`SongRenderer`:** sequencer, then voices, then mixer, in 128-frame quanta (`kRenderQuantum`, `quantum()` :80-97). LEAD is 0.25 s, with at most 3 steps per quantum.
- **API:** `block`, `setPlan(PdcPlan, PdcMode, extra)`, `latencyFrames()`, `setMasterChain`, `setEffects`, `setMeters`, `renderTrimmed` (:64-68), `songOver()`.
- **`kRenderPads=256`.**
- **`dispatch()`** (render.cpp:20-40) ignores:
  - `SH_VK` (MUT/GEN)
  - `kv.live`/`kv.sf`
  - events `eng`, `chc`, `tempo`
  - MIDI events (`default: return`, :41)
- **Output:** block-size independent, because it buffers 128-quanta.

## 2. Findings

### DEFECT (3)
- **D1: Seq cannot start at a live frame, and has no tempo change.**
  - JS: `play` sets `seq.time = d.t0` (core-processor.js:423), RESUME shifts base/time/btT, and `case 'bpm'` sets `seq.bpm` (in `msg`, ~:1211).
  - C++: `time_`, `base_` and `bpm_` are private with no setter (seq.h). Live `play` at frame F>0, RESUME, `bpm` and the catch-up skip are impossible.
  - The catch-up skip is JS `process1`: `if s.time < currentTime-0.03` shifts base/time/btT. EXACT STOP `hold/held` is also absent.
- **D2: `dispatch` plays the wrong sound for some events.**
  - JS `mine()` (~core-processor.js:600) routes MUT/GEN to a variation buffer, live engines to the live shell and SoundFont to sf. A kit entry that is null is silent.
  - C++: `SH_VK` plays the base pad. `kv.live` and `kv.sf` are dropped. Any pad with a buffer plays (render.cpp:20-40).
- **D3: `noShape` is never set.**
  - JS `varRec` returns the unshaped pad for instruments with slc>1 or sync.
  - C++ would shape them, because no host sets `noShape` (grep found no writer).
  - This can make sliced or synced instruments differ on ELECTRO-like songs.

### RISK (11)
- **R1: Song and Seq lifetime against `SnapSlot` retirement** (see 1.1). A reference-holding Seq dangles after `song.load`/`song.patch`. The wiring must rebuild the Seq or hold a shared_ptr-like copy.
- **R2: No voice stealing** (voice.cpp:155). Notes drop silently at 64 voices. The peak census voice count was not measured.
- **R3: 64 KB memset on the audio thread** for shaped or reversed notes (voice.cpp:47). Bounded, but a burst of notes in one block costs time.
- **R4: `pump_new` on the audio thread** (fxpump.cpp:143-147, :231, :266). It trips `rt_harness`'s allocation guard if a runaway occurs. U30's Expression compile and Marbles re-make are the same class.
- **R5: Fixed `randomSeed`** (seq.h:61). Unseeded RND repeats every play.
- **R6: Mixer delay-ring memory** (about 31 MB at 48 kHz, about 125 MB at 192 kHz, estimates). Two renderers (live plus stems) double it. Wasm heap limits are unverified.
- **R7: The wasm CI target excludes the real core** (native.yml:87-121). `<thread>` and `std::mutex` in fxpump.cpp under wasi are unverified.
- **R8: `mix.set` Send unsupported** (mix.cpp:267). The web app has no send level, so this is harmless until the bridge sends one.
- **R9: Fixed FXV queues** (256 in fxhost.cpp:209, 32 pump) drop silently.
- **R10: `thunder_put` allocates** (core.cpp:244 region) and `kMaxPutFloats`=2^27. Control thread only, but the host must never call it from the worklet's `process`.
- **R11: U26 shell drops a NoteOff when its queue is full.**
  - `live_shell.h:115` is `if (cnt_ >= kPend) { dropped_++; return; }` for every event type.
  - `kPend=256` (live_shell.h:78).
  - The condition in PICKUP.md and the U26 notes, "a NoteOff never dropped", is therefore not met. A dropped NoteOff leaves a stuck note.
  - It matters once pads and the sequencer can fill the queue.

### QUESTION (5)
- **Q1:** Peak simultaneous voices on census songs versus 64: is the drop at voice.cpp:155 ever reached?
- **Q2:** `ev.st` stretched notes are a STUB. Is a stretched pad in the render gate?
- **Q3:** Should a pad whose `kv` is null ever play? The JS says no (renderOut filters `kit.voices[e.pad]`, index.html ~24390).
- **Q4:** Behaviour at host block size 1 or very large blocks. `SongRenderer` is block-size independent (128-quanta). `Mixer::render` clamps N, and `FxHost::process` gives silence above 4096 (fxhost.cpp:273). The live wrapper must chunk.
- **Q5:** `CtlGate`'s spin-wait under single-threaded wasm.

### Clean, with what was checked
- **seq `step()`, voice render and note paths, mix render, `FxHost::process`, core audio path:**
  - searched for allocation, lock, syscall, `std::function`, exceptions and unbounded loops;
  - loops are bounded by the constants above;
  - frame counters are int64 or double seconds (core.cpp:123-169), so no int32 overflow at long positions was found;
  - sample rates 22050 to 192000: `thunder_create` accepts 8000 to 384000 (core.cpp:88).
- **Pan law:** matches the JS.
- **`pdc_plan`:** bit-exact against `pdcPlan` (index.html:20125), 26 cases.
- Off-by-one in sample timing, swing, retrig and note-off ordering were not independently re-derived. The seq goldens are exact, and that was relied on.

## 3. Seams the wiring must join

| Seam | Entry points | Needs from the host | Latency | Condition in its notes |
|---|---|---|---|---|
| U18 pump hosting | `FxHost::plan/commit/process`, `PumpGate` (fxpump.cpp ~:98) | one audio thread; plan on the control thread | per slot, probed (`FxHost::slotLatency`, fxhost.cpp:142-146) | process-global `pump_io`; renew allocates |
| U25 Plaits/Elements | `pe_prepare()`, `pe_thread_ready()` (engine/layer_pe.h:20-21) | call `pe_prepare()` off-thread on every rendering thread; assert `pe_thread_ready()` | none | else ~1.8 MB allocation on the first render (layer_pe.h:6-7) |
| U26 live shell | `LiveShell` (engine/live_shell.h:78-124) | feed events with `at < n` per call; chunk of 128 (`kMaxBlock`) | 31 samples (bass), 15 (vox/acid/mach) (`latency()` = kBlk-1, :83) into the PDC plan | NoteOff never dropped (**not met**, see R11) |
| U30 ...waves | `waves/live.cpp` | Expression compiled at preparation | 127 samples | Marbles re-make at arm must not allocate (**not met**, see R4) |
| U33 pump synths | `xeng_render_voice` (engine/xeng.h:139, xeng_voice.cpp:238) | pump synths are not wired into it | n/a | unwired |
| U04 LXR | `lxr_seam.h` | host glue and live lanes | n/a | not wired; `live_open/process` provisional |
| Marbles / Tides | not located in this pass | | | not determined |
| U31 ...chains (`n3/u31`) | `ChainsLive`, `ChainsNode` | chunks of 128 | 127 samples | WIP branch, not merged |
| U32 SoundFont (`n3/u32`) | `SfPool` (`sf/sf_live.h`) | events handed in before their time; frame time = (n - n%128)/sr + (n%128)/sr | not stated | no "Conditions for the wiring" section yet |

## 4. PDC plan draft

**Latency sources in the core (value, where reported):**
- Pump tfx slots: probed at plan time (`pumpLatencyOf`, `PumpSlot::probeLatency`, fxhost.cpp:142-146). Live telemetry arrives as the `plat` post every 0.5 s (`popLatencies`).
- Master chain: `mchain_->latency()` (suppressor and limiter look-ahead), plus the safety limiter look-ahead and the trim, all summed in `SongRenderer::latencyFrames` (`extra_` + mchain + trim).
- U26 shell: 31 / 15 samples. U30 waves and U31 chains: 127.

**JS today:**
- `pdcPlan` (index.html:20125) counts only track pump chains. Bus chain latency is not compensated ("A bus's latency isn't compensated anywhere").
- `pdcSync` (index.html:21625+) sets per-bus delay nodes, clamped 0..0.99 s. Live it glides over 50 ms, 10 ms when `pdcNow`, and goes to 0 when stopped in auto mode. In a render it is set at once.
- `pass` (index.html:24225+): `lat` = mastering latency + master pump latency + `round(pdcPlan(sr).T*sr)` + the safety limiter's latency, and the output is trimmed by `lat`.
- Each effects node's beat clock is delayed by `pl.c`.

**Native mixer has:** `pdc_plan` and `pdc_apply` (bit-exact), `pdc_delay_set` (the same glide values), auto-idle when stopped.
**Native lacks:**
- live-engine latencies in the plan (U26 31/15, waves 127, chains 127);
- a master slot latency in `pdc_plan` (handled through `extra`);
- a reporting path from the shell to the plan.

## 5. Proposed order of work

1. **U34a: C API for an offline song render** (song bytes, pad buffers, a progress callback and cancel; wraps `SongRenderer::renderTrimmed`).
   - Gate: `render-compare.sh` ELECTRO within -80 dB, and `--fx` and `--master`.
   - Builder: Sonnet. Review: lead, for the API shape.
   - Exists: `SongRenderer`, `thunder-render/main.cpp`.
2. **U34b: stems.** Per-track render with the `keep` set and the bus feeds (`pass`, index.html:24225-24260), and trim by `lat`.
   - Gate: the sum of stems against the mix, then `render-compare.sh`.
   - Sonnet. Exists: `thunder-render`.
3. **D2/D3 fixes** (`dispatch` routing, `noShape`) before any live use.
   - Gate: `seq-golden --core`, `render-compare.sh`.
   - Lead review.
4. **Seq setters** (time, bpm, shift, hold) and the Song lifetime design (R1).
   - Gate: `seq-golden`.
   - Lead.
5. **U34c: pad renders through the core.**
   - Gate: the existing render goldens.
   - Sonnet. It needs the voice synth path, which is not in the core; the scope is unclear (see 6).
6. **Full wasm target** (seq, voice, mix, fx, dsp, pump) plus a worklet host and `window.thunderCore`, default off (U37).
   - Gate: `seq-golden --core`, `ctest`, `rt_harness`.
   - The wasm build is a Sonnet task; the worklet host needs lead review.
7. **U35 MIDI scheduling**, from scratch. Gate: `midi-ledger` (the case file does not exist yet; it must be written first). Lead.

## 6. Not determined

- The Marbles / Tides entry points.
- `ENGINE-INVENTORY.md` U35 line numbers (the grep hit is at line 248 only).
- The `tests/rt_harness.cpp` coverage in detail.
- `tools/seq-golden.mjs` contents. It was not read, and the path was not confirmed (`ls tools` did not list it, and neither does `tests/`).
- Whether `midi-ledger` exists anywhere. The grep found `docs/port/ENGINE-INVENTORY.md:248` naming `tests/midi-ledger.test.js`, but `ls native/tests` shows no such file.
- The index.html `makePlayer` and `sfRender` bodies, and the MIDI send scheduling (`msend`, `moClock`).
- Peak voices on census songs (Q1).
- Real memory use of the Mixer and `Seq` (estimates only; nothing was built).
- Whether `n3/u31` and `n3/u32` merge cleanly (they were read as WIP).
