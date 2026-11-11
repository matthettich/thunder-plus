# Core sequencer profile: why it costs more audio-thread time, and the memory question (11 Nov 2026, build 563)

Measurement only. No app file was changed (`index.html`, worklets, `sw.js`, `HANDOVER.md`, `BUILD` untouched). New tools are under
`tests/sweep/`: `core-profile.mjs`, `cdp-lib.mjs`, `cp-analyse.mjs`, `cp-trace-analyse.mjs`, `core-offline-bench.mjs`,
`wide-outputs.mjs` (+ `.html`, `-worklet.js`). Raw output is in `tests/sweep/out/` (gitignored).

## 1. Answer in short

- **The gap is a real cost, not a measurement artefact.** On this machine the audio thread is busy about **57 %** of one CPU with the
  page sequencer and about **70 %** with the core (3 clean runs of 150 s each way, from `/proc`): **+13 points, about 0.38 ms more
  per 128-frame block**, against a budget of 2.90 ms (the headless Chrome here runs at 44.1 kHz, not 48 kHz: 128 / 44100 = 2902 us).
  Three independent measurements agree (thread CPU, a V8 profile of the worklet thread, Chrome's own audio trace, and an offline Node
  replay of the core).
- **The extra time is NOT `makeSeq` stepping.** `api.step`, `engEmit` and the rest of the sequencer logic total about 0.2 % of the
  thread. It is **mixing and per-sample automation that the core does in JS**, all of it caused by one weakness: `Auto.constFrom(t)`
  (`core/core-processor.js` line 60) only says "constant" when the timeline's LAST event is already over, and the core books events
  about 0.25 s ahead (`LEAD`), so a timeline almost always has a pending future event and the constant shortcut almost never applies.
  Every timeline is then walked per sample (`at(t)` x 128 per block).
- **Memory:** the core does not leak that I can find. In clean runs (10 min core with the soak's actions, 7 min page, 7 min quiet core with
  heap snapshots) renderer RSS, the page JS heap and the worklet JS heap are flat. The soak's "RSS growing 3.24 MB/min" is a
  **sawtooth** (510 to 840 MB, driven by the waves / chains frames being loaded by the screen tour and dropped by the song reload) that
  a straight-line fit turns into an arbitrary slope: the same 12-minute soak schedule gave **+5.3 MB/min on core and +2.7 MB/min on page**,
  with identical peaks and troughs. Not shown: a leak, and not shown: that there is none over hours.
- **Why 10x to 30x the dropouts for 13 points of CPU:** the audio thread is not being starved by other threads (run-queue wait is 0.015 %
  to 0.06 % of its time in both modes); it simply has less headroom. At 57 % busy the page mode is already near the knee on this slow
  2-vCPU VM, and the core pushes the 10 ms output callbacks over their deadline far more often (callbacks over 10 ms: 9 % vs 16 %,
  render quanta over 2.9 ms: 4.7 % vs 15 %). The ratio is a property of this machine's saturation; the +0.38 ms is not.

## 2. Method that worked

| Need | What worked | What did not |
|---|---|---|
| Per-thread busy fraction without perturbing | `/proc/<renderer pid>/task/*/stat` and `schedstat` (CPU ns, run-queue wait ns, preemptions) for the thread named `Realtime AudioW`; `Performance.getMetrics` for the main thread; `SystemInfo.getProcessInfo` for all processes | `ctx.renderCapacity` is not exposed in this Chrome (the app's meter falls back to its own probe: `globalCpuValue()` 0.54 to 0.59 page, 0.70 to 0.75 core) |
| CPU profile of the worklet thread | Raw CDP: `Target.setAutoAttach {flatten:true}` on the PAGE session gives a child target of type `worklet`; `Profiler.setSamplingInterval(250)`, `start`, `stop` on that session yields self time per function (`tests/sweep/core-profile.mjs --do=cpu`, `cp-analyse.mjs`). Playwright's `CDPSession` cannot talk to child sessions, so `cdp-lib.mjs` has its own tiny client and launches Chrome with the same flags playwright uses. `Debugger.enable` once beforehand names the blob scripts (`registerProcessor` names) | Browser-level auto-attach shows only the page and two dedicated workers, not the worklet. The profile's `(idle)` also contains native graph time, so it counts JS only |
| Per-node and per-quantum cost | `Tracing.start` with `webaudio,disabled-by-default-webaudio.audionode,v8` (`--do=trace`, `cp-trace-analyse.mjs`): per-quantum `RealtimeAudioDestinationHandler::Render` wall and thread-CPU time, self time per node type, `v8.callFunction`, `Glitch!` events, scavenge GCs. Heavy (it adds glitches of its own); use deltas, not absolutes | |
| Offline cost | Node vm replay of the real message stream (captured with `--do=capture`: the song, kit, 24 pad buffers, PLAY) into `core/seq-core.js` (`core-offline-bench.mjs`) | |
| Memory | `Runtime.getHeapUsage` after `HeapProfiler.collectGarbage` on the page AND the worklet target each minute, RSS and `smaps_rollup` of the renderer, `HeapProfiler.startSampling` on both, worklet heap-snapshot diffs (`--do=heap`, `--actions` repeats the soak's STOP/PLAY and screen tour) | |

Setup: `sounds/ELECTRO.thunder` (32 tracks, 4 driven nodes: 2 live engines "acid" / "vox" and 2 effect nodes), headless Chrome for Testing 153,
`--mute-audio`, no audio device, 2-vCPU Firecracker VM, sample rate 44100, callbacks of 441 frames every 10 ms, 128-frame quanta.

**A wasted first set of runs.** My first measurements were taken while a stray Chrome from my own probe was burning about one CPU (machine
98 % busy); I caught it from `machineBusyPct`, killed it and redid everything. `core-profile.mjs` now refuses to start when the machine is
more than 15 % busy before the run. Nothing from that set is used.

## 3. Page vs core

Clean runs, quiet machine (about 0.3 % busy before each run), 150 s after a 20 s warm-up. Machine busy during runs: 48 to 67 %.

| | page (3 runs) | core (4 runs) |
|---|---|---|
| Dropouts a minute (`glitch.n`, = `playbackStats.underrunEvents`) | 2.8, 1.2, 8.7 (median 2.8) | 90, 1108, 37, 33 (median 63; the 1108 run was a burst) |
| Audio-thread busy % (`/proc`, unprofiled) | 58.8, 55.9, 55.3 | 70.2, 72.7, 68.2, 69.0 |
| Audio-thread run-queue wait (share of time) | 0.014 to 0.016 % | 0.015 to 0.060 % |
| App CPU meter | 0.54 to 0.59 | 0.70 to 0.75 |
| Main-thread busy % (CPU of the renderer main thread) | 21.8 to 26.4 | 22.0 to 25.6 |
| Main-thread task time / script time (`Performance.getMetrics`) | 26 to 39 % / 5.8 to 8.3 % | 29 to 36 % / 5.9 to 7.6 % |
| Long tasks per 150 s | 3, 4, 100 | 6, 7, 66, 74 |
| Core's own `process()` wall time (its `coreStats`: `ms / blocks`) | n/a | 0.112 ms a block (3.9 % of the budget) |

Main thread: **no difference** (the long-task counts follow machine load, 100 in a page run and 6 in a core run). Core mode does not cause more
main-thread work (rows: about 17 a second, about the same render-side cost; `CrRendererMain` `v8.callFunction` 1345 vs 1363 in 5 s of trace).
Both threads' scheduling is fine: the audio thread waits for a CPU for well under 0.1 % of its time in both modes.

### Top 8 audio-thread functions (V8 profile of the worklet thread, 60 s each, 250 us sampling; % of the thread's wall time)

The profile itself perturbs (glitches 5x to 20x during it), so read shares, not absolutes. JS busy (not `(idle)`): **page 51.8 %, core 64.5 %**.
Same scripts either side except `seq-core.js` (0 % page, **13.7 % core**).

| # | page | self % | core | self % |
|---|---|---|---|---|
| 1 | `wasm-function[796]` (…pump FX wasm, via ThunderFx `pumpRun`) | 7.4 | `wasm-function[796]` | 7.1 |
| 2 | `wasm-function[783]` (…pump FX wasm) | 3.7 | **`__tcPost` seq-core.js:891** | **4.6** |
| 3 | `run` master-worklet.js:33 | 3.3 | **`process1` seq-core.js:1478** | **4.0** |
| 4 | `wasm-function[90]` (FX wasm, via `slotRun`) | 2.6 | `wasm-function[783]` | 3.7 |
| 5 | `process` ThunderFx (index.html `TFX_SRC`) :142 | 2.5 | `run` master-worklet.js:33 | 3.2 |
| 6 | `wasm-function[787]` | 2.1 | **`__tcOv` seq-core.js:875** | **2.5** |
| 7 | `process` chains-worklet.js :1202 | 2.0 | `wasm-function[90]` | 2.4 |
| 8 | `p1` ThunderFx :974 | 2.0 | `process` ThunderFx :142 | 2.4 |

The effect work is identical in both modes (ThunderFx `process` inclusive 39.9 % page vs 39.4 % core; chains, master, wasm the same), so the
song's DSP is not doing more in core mode: **everything extra is inside `seq-core.js`**.

### Per-quantum cost from Chrome's trace (5 s each, microseconds per 128-frame quantum; trace overhead inflates both modes)

| | page | core | difference |
|---|---|---|---|
| Whole `Render` quantum, wall / thread-CPU | 2017 / 1521 | 2501 / 2017 | +484 / +496 |
| `v8.callFunction` (all worklet JS) | 935 | 1294 | +359 |
| `AudioWorkletHandler::Process` (native part of worklet nodes) | 190 | 266 | +76 |
| `GainHandler::Process` (extra a-rate inputs from the mix outputs) | 43 | 63 | +20 |
| `ChannelSplitter` nodes | 17 | 32 | +15 |
| `AudioBufferSourceHandler::Process` (the page mode's native samples) | 15 | 0 | -15 |
| Gain, Panner, Delay, Analyser, Biquad nodes | same | same | 0 |

The core removes the page mode's native sample playback (15 us) and replaces it with JS (below).

### What the dropout counter does (light 60 s trace, `webaudio,audio,v8`; `Glitch!` and callbacks)

| | page | core |
|---|---|---|
| Output callbacks (441 frames) mean / p99 render time | 7.0 / 12.7 ms | 8.1 / 15.7 ms |
| Callbacks over 10 ms | 9.3 % | 16.0 % |
| Quanta over 2.9 ms | 4.7 % | 15.0 % |
| `Glitch!` events (trace overhead on) | 31 / min | 637 / min |
| Worklet scavenge GCs / total pause / worst | 213 / 77 ms / 3.1 ms | 304 / 124 ms / 4.6 ms |
| Glitches with a scavenge in the 15 ms before | 1 of 31 | 43 of 637 |

## 4. Where the core's extra time goes (ranked)

All numbers are shares of the audio thread's wall time in the core profile (13.7 % in `seq-core.js` is about 0.40 ms of a 2.9 ms quantum;
Chrome's trace says +0.36 ms of JS, the thread-CPU runs say +0.38 ms: they agree). Offline Node numbers are per block in microseconds, on 44.1 kHz,
after warm-up; Chrome's are roughly 1.6 to 2 times larger (cold caches between blocks).

1. **The per-block hooks `__tcOv` and `__tcPost` (7.9 % of the thread, 4.6 + 2.5 self + callees; about 0.23 ms a quantum).** They are defined in
   `core/seq-core.js` (lines 875 and 891) but called from the OTHER worklets' `process()` (the bass / vox / acid / mach module at index.html
   lines 21613, 21632, 21698, 21716, 21779, 21796; ThunderFx and chains at 18654 and 18888) whenever the core drives that node's parameters or its
   gain / pan. Profile callers: three live-engine processors (1.6 %, 1.6 %, 1.4 % of the thread) and ThunderFx. Per line (V8 `positionTicks`):
   - `__tcPost` line 900: `Math.cos` and `Math.sin` evaluated for EVERY sample even when the pan is a constant (63 % of its self time),
     line 896 (the per-sample `ov.g.at(t)`, `ov.pn.at(t)`) another 33 %. The constant shortcut `gc`, `pc` (`constFrom`) fails because the timeline has
     a pending event 0.25 s ahead.
   - `__tcOv` line 886: the per-sample loop `b[i] = ... A.at(t)` that fills an a-rate Float32Array per overridden parameter (64 % of its self time).
   - Offline (4 nodes driven): `__tcOv` 34 us + `__tcPost` 55 us per block.
2. **The core's own `process()` (about 5.8 % inclusive, 0.17 ms a quantum; offline 89 to 97 us, of which sequencing alone is 37 to 47 us; idle 2.4 us).**
   `process1` self 4.0 %: line 1530 (65 % of it) and 1539 (9 %) are the per-sample slow path of the sample voices (`const t = currentTime + i*dt`,
   `v.g.at(t)`, `Math.cos/sin` for the pan): a voice takes the fast path only when `constFrom` of its four timelines (`g`, `f`, `pn`, `r`) says
   constant, which, again, needs every event to be over, so a note with its release already booked takes the slow path for its whole life. Line 1504
   (12 %): the beat clock written per sample whenever it has events (one a step, always while playing). Then `prune` 0.8 %, `at` 0.7 %, `mixFill` 0.7 %.
3. **The node's width, about 0.05 to 0.08 ms a quantum.** `thunder-core` has NT + 5 = 37 outputs (131 channels). `tests/sweep/wide-outputs.mjs`
   (a do-nothing worklet): 1 output costs 9 us of `AudioWorkletHandler::Process` per quantum, 37 outputs with only #0 connected costs 57 us (48 us
   more, 5 x CPU the 1-output node, with nothing connected). In the app the delta is +76 us, plus +35 us for the mix-output `ChannelSplitter` / `Gain`
   inputs (3.9 % of the budget together with item 2's native share).
4. **GC.** The core adds about 1.5 scavenges a second on the worklet thread (+42 %) and its worst pause was 4.6 ms (a single pause longer than a
   block); 7 % of core glitches had one in the 15 ms before (3 % of page glitches). A tail contributor, not the mean. Garbage sources by reading (not
   measured per call site): `Auto.prune` builds `head` + `concat` + `slice` arrays whenever it folds (every block, every timeline with events),
   `const rows = []` and the row objects in `process1` and the `rows` message.

**Checked and found small or zero:** `makeSeq` stepping (`api.step` 0.1 %, `engEmit` 0.1 %, everything else 0.0 %); messages (`postMessage` 0.2 % vs
0.1 %; `port.onmessage` 0.0 %); the watchdog (phones only, returns at once on desktop: 0.0 %); `Date.now()` twice a block (negligible); idle nodes
(the page mode leaves nothing idle that the core wakes: ThunderFx, chains, master, wasm all equal); per-note node creation on the main thread
(`AudioBufferSource` page mode: 15 us of native audio-thread time a quantum, main-thread work equal in both modes); a main-thread penalty (none).

## 5. Fix check

A scratch copy (kept outside the repo, applied nowhere) with a block-aware constant test (diff in section 8) was tried two ways.

**Offline** (`core-offline-bench.mjs --src=`, 12 000 blocks, 3 runs each, mean per block): total 177 to 194 us -> **82 to 86 us** (-54 %): hooks 87 to 97 -> 15 to
16 us, `process()` 89 to 97 -> 67 to 71 us. **Bit-identical**: a lockstep run of the old and the new core over 8000 blocks compared every voice output
sample, every `__tcPost` result and every `__tcOv` parameter value by bit pattern: no difference (the only differing parameter, `trig` on the acid node,
differs the same way between two copies of the OLD core, so it is nondeterministic by itself).

**In Chrome, end to end** (the page served the scratch copy of `core/seq-core.js` through `core-profile.mjs --core=`): section 6.

## 6. Fixed core in the same A/B

Three runs each, interleaved (fixed, old, fixed, old, ...), 150 s, quiet machine, core sequencer, same song:

| | old core | scratch fixed core | page (earlier runs) |
|---|---|---|---|
| Dropouts a minute | 26.3, 35.5, 25.9 | 7.6, 6.0, 3.2 | 2.8, 1.2, 8.7 |
| Audio-thread busy % | 66.8, 67.4, 66.8 | 62.1, 62.4, 63.6 | 55.3 to 58.8 |
| App CPU meter | 0.79, 0.74, 0.70 | 0.66, 0.72, 0.67 | 0.54 to 0.59 |

The fix takes about 4.5 of the 10 to 13 points back (about 0.13 ms a quantum) and cuts dropouts about 5x, to near the page mode's range. (This interleaved
old-core set sits lower than the earlier core runs, 67 % vs 70 %: machine state differs between sets, so compare within the set.) The rest of the gap is items 2 to 4
of section 4.

## 7. Memory

| Run | RSS slope (MB/min, fit from minute 2) | page JS heap after GC | worklet JS heap after GC |
|---|---|---|---|
| core, 10 min, soak actions (STOP/PLAY each minute, tour every 2) | +0.48 (365.7 to 368.2 MB, minute 2 to 10) | -0.04 | +0.02 (6.1 to 7.0 MB, noise) |
| page, 7 min, same actions | +0.89 (355 to 362 MB) | -0.19 | -0.01 (4.0 to 5.0 MB) |
| core, 7 min quiet (heap snapshots at 1 and 7 min) | +0.5 after the snapshot's own 92 MB step (an artefact of taking it) | | snapshot diff: +170 KB in total, nothing growing by class |

- The core adds a constant, not a trend: about +10 MB RSS and +2 to 3 MB worklet JS heap (the song copy, pad buffers).
- Allocation sampling at the end of the 10 minute core run, worklet, live bytes: `process` master-worklet:319 194 KB, ThunderFx `slotRun` / `process` / `pumpRun`
  about 240 KB, bass module `process` 70 KB, `ThunderCore.process` 45 KB, `Auto fresh` 16 KB: nothing proportional to time. Page: bytecode, the `drawArr0`
  redraw, `pumpW.onmessage`. The 11 Nov build's open items (`tcChunks`, `Auto.prune` / `ins`, ThunderFx `modRun`, `sub()`) are GARBAGE sources (item 4), not retained memory: nothing
  of theirs shows in the live set and the heap after GC does not rise.
- **Soak, 12 minutes each way, same schedule** (`soak.mjs --minutes=12`): renderer RSS **core slope 5.3 MB/min (FAIL)**, **page 2.7 (PASS)**. The series are the same
  sawtooth. RSS at the end of each minute, core: 707 778 793 778 756 **551** 549 556 566 **837** 808 741; page: 719 767 772 770 772 **555** 556 542 777 **836** 757 560. The
  step down is the song reload at 362 s (the waves / chains frames are thrown away), the climbs follow the screen tours (those frames load again, about +200 to +280 MB
  each); peaks 837 / 836 and troughs 549 / 542 are equal in the two modes. A straight-line fit over a sawtooth returns whatever the phase at the window's two ends says
  (the 20 minute A/B's 3.24 MB/min against a bound of 3 is the same effect). JS heap after GC +0.055 and +0.05 MB/min, every app structure flat in both.
- Which side holds the swing: the frames (page side, same renderer process); the worklet heap does not move. I did not attribute the 200 to 280 MB to individual
  frames (not needed: it is mode-independent).

## 8. Sanity check of the measurement

- **The dropout counter is the same in both modes.** `glitch.n` reads `ctx.playbackStats.underrunEvents` (index.html `glitchScan`, line 31793) in both modes, and
  it equals Chrome's own `Glitch!` trace events and `underrunEvents` row by row. Core mode counts nothing extra; its only side effect is that `dropNote()` is
  called on a dropout in both modes.
- **`PB.late` and `PB.drop` are not comparable:** they are fed only by the core's rows. "No late or dropped notes" in the page mode is a definitional zero, not a measurement.
- **Headless, no device:** `--mute-audio` gives a null sink pulling 441 frames every 10 ms (each `Glitch!` is exactly 10 ms). I see no way it penalises one mode:
  both are rendered by the same thread into the same FIFO, and the run-queue wait is the same. What it does is run the song on a slow vCPU: the page mode alone is
  57 % busy here, which is why a +13-point increase turns into 10x the dropouts. On a normal desktop CPU both modes would be far below the knee; on a phone's
  big cores (page sequencer is what the phone uses) it would not be this machine's numbers either way.
- **Run-to-run spread is large** (core 33 to 1108 a minute at about the same CPU; page 1.2 to 8.7). The CPU numbers repeat to a few points; the dropout counts do not.
- **The soak harness itself loads the machine:** all-browser CPU 150 to 156 % of 200 % (queryObjects, forced GCs, 5 s sampling), against about 55 % for the
  harness here, so its absolute dropout counts are higher in both modes (12 minute soak: page 118, core 703).

## 9. Fixes, ranked by gain / risk (not applied)

1. **Block-aware constant test (`Auto.constBlk(t, t1)`) and hoisting the trig out of `__tcPost`'s constant case.** Gain: about half of the core's overhead
   (offline -54 % of the core's total per block, -97 us = 3.3 % of the budget; measured in Chrome: -4.5 points of audio-thread CPU, dropouts about 5x lower, section 6).
   Risk: low. Pure function of the timeline, bit-identical in the lockstep test above; `core-memory.test.js` and `core-auto.test.js` already pin the
   per-block outputs. Care points: `Math.fround` in `__tcOv`'s constant array so it equals the Float32 array the per-sample path stored; keep `constFrom`
   for the "all events over" decisions elsewhere. Sites: `Auto` (line 817), `__tcOv` (883 to 884), `__tcPost` (894 to 905), `mixFill` (1120), voice fast path (1512).
2. **Take the garbage out of the core's per-block path.** `Auto.prune` (`head`, `concat`, `slice`: fold in place when it can), the `rows` array and row objects
   in `process1`. Gain: fewer scavenges (the core adds 1.5 a second and a 4.6 ms worst pause): tail latency, maybe a few percent of dropouts, little of the mean.
   Risk: low to medium (bit-identical tests exist: `worklet-memory.test.js`, `core-memory.test.js`; `prune` has a long history, see its FLX CHECK 2 comment).
3. **Narrow the node.** `NT + 5` outputs, 131 channels cost 48 to 76 us a quantum before any work (and the `ChannelSplitter` / `Gain` mix inputs +35 us). Fewer
   outputs (only the tracks the song plays on, the four mix outputs merged) would give about 2 to 3 % of the budget. Risk: medium to high (the wiring in `coreWire` /
   `coreMixT`, rebuilding the node when the song's track set changes). Do after 1 and 2 and re-measure.

Not worth it on this evidence: the beat clock per sample (line 1504, about 0.5 % of the thread; a two-segment ramp could replace it, small gain), the messages, the watchdog.

### The scratch diff behind section 5 (core/seq-core.js; not applied)

```diff
+  constBlk(t, t1){ const A = this.ev; let v = this.v0, i = 0; for (; i < A.length && A[i].t <= t; i++){ const e = A[i]; if (e.k > 1) return null; v = e.v; }
+    if (i < A.length){ const e = A[i]; if (e.t <= t1 || e.k === 1) return null; } return v; }   // constant over [t, t1]: nothing moves it before t1
 __tcOv:  const c = A.from > currentTime + n * dt ? null : A.constBlk(currentTime, currentTime + n * dt);
          ... a1[0] = A.constFrom(currentTime) != null ? c : Math.fround(c);
 __tcPost: const gc = ov.g.constBlk(currentTime, currentTime + n * dt), pc = ov.pn.constBlk(currentTime, currentTime + n * dt);
          if (gc != null && pc != null && R){ /* the same loop with cos / sin evaluated once outside it */ ...; return; }
 mixFill:  const c = A.constBlk(currentTime, currentTime + N * dt)
 voices:   const gc = v.trm ? null : v.g.constBlk(tb0, tb1), fc = v.f.constBlk(tb0, tb1), pc = v.apn ? null : v.pn.constBlk(tb0, tb1), rc = v.r.constBlk(tb0, tb1);
```

## 10. What I could not determine

- Whether any of this matters on a real device: no audio hardware, one slow VM, 44.1 kHz, a null sink. The +0.38 ms is measured; the dropout ratio depends on the machine's headroom.
- The cause of the occasional 1 to 4 ms `process()` block in the offline replay (blocks that also step the sequencer; the positions differ from run to run and are not
  consistently a GC): likely JIT / deopt, not pinned. In Chrome, v8 calls over 2 ms: 118 core vs 88 page per minute.
- The 3.24 MB/min in the 20-minute A/B was not reproduced: no 20-minute run was made. Everything points at the sawtooth, but I did not attribute the 200 to 280 MB swing to
  individual frames, and a small real difference (the core's +10 MB) cannot be excluded.
- Per-function CPU time (not wall) on the worklet thread: V8's profiler is wall-clock sampled; the thread-CPU runs and the trace's `tdur` are the CPU-time cross-checks.
- The effect of fix 2 and 3 (not tried).
