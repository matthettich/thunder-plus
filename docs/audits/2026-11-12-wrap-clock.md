# Wrap-around clock instability: measurement (12 Nov 2026, build 572)

Measurement only. No app file was changed (index.html, core/, worklets, sw.js, HANDOVER.md untouched). Branch
`measure/wrap-clock`, tip of `beta`.

## The question

Owner, Galaxy Z Fold 6, Chrome on Android, Mobile setup, core sequencer, app in view, build 568: "When playback looped
around on my track, there was a little clock instability. Resolved after a few patterns." HANDOVER "Bugs still open"
suspects sounds prepared the first time a pattern plays. Since 568: 569 `arrFast` cheap Arrange cursor redraws, 570 song
wrap restarts all tracks together (no `fxreset`), 571 `pvPlayMark`. What does timing do at the first wrap versus later wraps, and why?

## Verdict

**Not reproduced.** Across 31 runs the first wrap was never worse than wraps 2 to 4. Where timing broke, it broke at the
same heavy mid-song section on every pass (or on every redraw-heavy screen on every pass), not at the wrap. The core's own
counters (late notes, skips, worst lateness) stayed at zero in every run. Nothing here confirms or rules out a phone-only
cause (real audio device, real CPU, Android scheduling).

## What was and was not measured

Measured: headless Chrome (/usr/bin/google-chrome via Playwright and a raw CDP client), Fold profile (open 884x1020, cover
412x915, DPR 2.5, touch, Android UA so `seqHandHere()` and `wkLazy()` take the phone path, Mobile setup), CPU throttle 4x and
6x plus 1x control. Songs: ELECTRO to its end with no LOOP (56 rows, about 105 s a pass), a 24-row cut, an 8-row cut (about 14.7 s a pass, 5
passes) and a LOOP range rows 2 to 5. Both sequencers, screens Song/Arrange, Pattern view, Mixer, FOLLOW on and off.
Per pass (pass 1, wrap 1 +-1 s, pass 2..4): event lead and lateness, core counters, `PB` counters, audio clock against wall
clock every 100 ms, outputLatency/baseLatency, long tasks and long animation frames, main-thread and worklet CPU profiles
(1 ms sampling), underruns, first-note-per-pad cost, per-function timing wrappers (calls of 1.5 ms or more).

Not measured, and why it matters:
- No real phone, no real audio device. Headless Chrome has a null sink, 44.1 kHz, 10 ms callback, constant outputLatency
  (32 ms, never moves). So outputLatency drift, Bluetooth, and Android audio-thread priority are invisible here.
- `Emulation.setCPUThrottlingRate` slows the main thread only. The audio thread is not throttled, so core-mode audio-thread
  risk on a slow phone is understated.
- "Underruns" (`ctx.playbackStats`) in this sink track machine contention, not device glitches.
- The VM was shared with another agent's Chrome/npm jobs (0 to 60 percent of 2 CPUs). Full-ELECTRO runs were contended
  (see "others" rows); short-song runs were mostly quiet. Contention is logged per run and judged per run. Early contaminated
  runs were discarded and rerun.
- Only builds 572 behaviour; the owner's report was build 568, so 569 to 571 changes are included in what was tested.

## Method and scripts (all under tests/sweep/)

- `wrap-clock.mjs`: one run, one Chrome. Options `--seq=page|core --screen=song|pv|mixer --follow=on|off --throttle=N
  --song=s8|sN|full [--loop=y0-y1] [--passes=5] [--size=open|cover] [--prof=none|main|worklet|both]`. Injects wrappers via
  `__seqTest.ev` and writes `out/wrap-<tag>.json`. A pre-PLAY simulation gives expected wrap times; wraps are confirmed from row
  decreases in the stepped rows stream and mapped to wall time through the 100 ms clock samples.
- `wrap-report.mjs`: raw to markdown (per-pass tables, `--summary`, `--pool`, `--log`, `--first`, `--cpu=w1,w3`).
- `wrap-step-cost.mjs`: steps `makeSeq` on the page for N passes and times each step (wrap step against ordinary step).

Segments: pass k = wrap k + 1 s to wrap k+1 - 1 s; wrap k = due time +-1 s.

## Per-pass results (representative runs; full tables via `node tests/sweep/wrap-report.mjs <file>`)

### Page sequencer, Song, FOLLOW on, 6x, 8 rows, open (quiet machine)

| | pass 1 | wrap 1 | pass 2 | wrap 2 | pass 3 | wrap 3 | pass 4 | wrap 4 |
|---|---|---|---|---|---|---|---|---|
| events booked | 319 | 74 | 384 | 74 | 384 | 74 | 384 | 74 |
| late (lead < 0) | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| least lead ms | 155 | 182 | 184 | 179 | 196 | 202 | 80 | 200 |
| long tasks n / max ms | 3 / 123 | 1 / 63 | 4 / 68 | 3 / 73 | 1 / 55 | 0 / 0 | 6 / 134 | 0 / 0 |
| clock rate (ct s per wall s) | 1.00 | 1.00 | 1.00 | 1.00 | 1.00 | 1.00 | 1.00 | 1.00 |
| largest 100 ms clock step error ms | 6.9 | 5.2 | 6.1 | 3.1 | 8.7 | 4.3 | 6.4 | 4.8 |
| underruns | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| first-time pad notes n / cost ms | 1 / 2.9 | 1 / 0.2 | 0 | 0 | 0 | 0 | 0 | 0 |
| machine busy % (others) | 66 (0) | 68 (0) | 67 (0) | 66 (0) | 68 (0) | 66 (1) | 70 (3) | 68 (1) |

### Core sequencer, Song, FOLLOW on, 6x, 8 rows, open

| | pass 1 | wrap 1 | pass 2 | wrap 2 | pass 3 | wrap 3 | pass 4 | wrap 4 |
|---|---|---|---|---|---|---|---|---|
| core notes seen at page | 131 | 27 | 153 | 27 | 153 | 27 | 153 | 27 |
| core late notes / worst lateness | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| core least lead ms | 247 | 247 | 247 | 247 | 247 | 247 | 247 | 247 |
| core process() ms per block avg / worst | 0.04 / 0.08 | 0.09 / 0.16 | 0.06 / 0.09 | 0.06 / 0.08 | 0.05 / 0.10 | 0.06 / 0.08 | 0.05 / 0.09 | 0.05 / 0.06 |
| least lead at the page ms (rows) | 217 | 204 | 182 | 170 | 188 | 231 | 177 | 235 |
| long tasks n / max ms | 2 / 67 | 2 / 62 | 4 / 111 | 2 / 111 | 4 / 57 | 1 / 57 | 5 / 66 | 1 / 64 |
| clock step error ms (max) | 6.0 | 4.5 | 6.9 | 4.4 | 6.7 | 4.0 | 6.3 | 4.5 |
| underruns | 0 | 0 | 2 | 0 | 0 | 0 | 0 | 0 |

The wrap step itself costs 0.1 to 0.8 ms against a 0.1 ms median step (`wrap-step-cost.mjs`, s8 at 1x and 4x, full at 1x):
the song wrap is not a CPU event in the sequencer.

### Full ELECTRO to its end, 4x, FOLLOW on, Song (contended: others 28 to 48 percent of 2 CPUs, treat as stress)

| | page: pass 1 | page: wrap 1 | page: pass 2 | page: wrap 2 | page: pass 3 | page: wrap 3 | core: pass 1 | core: wrap 1 | core: pass 2 | core: wrap 2 | core: pass 3 | core: wrap 3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| late events | 136 | 0 | 134 | 4 | 83 | 0 | 1 | 0 | 2 | 0 | 0 | 0 |
| least lead ms | -157 | 80 | -145 | -4 | -159 | 161 | -133 (core own 33.6) | 241 | -47 (core 247) | 244 | 19 (core 247) | 182 |
| long tasks n / max ms | 365 / 1494 | 7 / 669 | 365 / 729 | 10 / 316 | 285 / 728 | 4 / 69 | 324 / 711 | 8 / 286 | 308 / 950 | 7 / 246 | 295 / 899 | 7 / 142 |
| underrun events | 21 | n/a | 21 | 0 | 183 | 0 | 272 | 0 | 73 | 0 | 46 | 0 |
| clock step error ms (max) | 29.7 | 1.1 | 18.3 | 5.3 | 54.3 | 5.1 | 91.5 | 4.9 | 20.4 | 4.6 | 18.8 | 5.5 |
| machine busy % / others | 99.8 / 48 | n/a | 99.9 / 47 | 99.6 / 33 | 90 / 28 | 73 / 0.3 | 97 / 31 | 100 / 38 | 95 / 31 | 99 / 45 | 95 / 29 | 99 / 38 |

Reading: in the full song the wraps are the quietest windows, because the end of the song is lighter than the mid-song
section. All the trouble is in the passes.

## First wrap against later wraps (pooled over the runs; cell = least lead ms / late events / longest long task ms / underrun events)

Pooled groups from `wrap-report.mjs --pool`. Selected lines (full list: `wrap-report.mjs --summary tests/sweep/out/wrap-*.json`):

| run | pass 1 | wrap 1 | wraps 2+ | passes 2+ |
|---|---|---|---|---|
| page song on 6x s8 | 155 / 0 / 123 / 0 | 182 / 0 / 63 / 0 | 179 / 0 / 73 / 0 | 80 / 0 / 134 / 0 |
| core song on 6x s8 | 247 / 0 / 67 / 0 | 247 / 0 / 62 / 0 | 247 / 0 / 111 / 0 | 247 / 0 / 111 / 2 |
| page song on 1x s8 | 204 / 0 / 0 / 0 | 219 / 0 / 0 / 0 | 215 / 0 / 0 / 0 | 212 / 0 / 0 / 0 |
| page song on 4x s8 cover | 199 / 0 / 82 / 0 | 216 / 0 / 0 / 0 | 191 / 0 / 61 / 0 | 134 / 0 / 107 / 8 |
| core song on 4x s8 cover | 247 / 0 / 97 / 0 | 247 / 0 / 57 / 1 | 247 / 0 / 50 / 0 | 247 / 0 / 70 / 0 |
| page song on 4x loop 2-5 | 145 / 0 / 106 / 0 | 141 / 0 / 107 / 0 | 125 / 0 / 163 / 0 | 104 / 0 / 182 / 2 |
| core song on 4x loop 2-5 | 247 / 0 / 284 / 0 | 247 / 0 / 106 / 0 | 247 / 0 / 87 / 0 | 247 / 0 / 146 / 2 |
| page pv on 4x s8 | 219 / 0 / 0 / 0 | 222 / 0 / 0 / 0 | 165 / 0 / 0 / 2 | 167 / 0 / 136 / 5 |
| page mixer on 4x s8 | 113 / 0 / 141 / 0 | 176 / 0 / 103 / 6 | 35 / 0 / 217 / 0 | -44 / 4 / 290 / 1 |
| core pv on 4x s8 | 247 / 0 / 450 / 0 | 247 / 0 / 349 / 2 | 247 / 0 / 254 / 0 | 247 / 0 / 746 / 1 |
| core mixer on 4x s8 | 247 / 0 / 310 / 0 | 247 / 0 / 219 / 4 | 247 / 0 / 343 / 49 | 247 / 0 / 333 / 8 |
| page song on 4x full | -157 / 136 / 1494 / 21 | 80 / 0 / 669 / 0 | -4 / 4 / 316 / 0 | -159 / 217 / 729 / 204 |
| core song on 4x full | 247 core (page -133) / 0 / 711 / 272 | 247 / 0 / 286 / 0 | 247 / 0 / 246 / 0 | 247 / 0 / 950 / 119 |

Per-wrap long tasks (n / max ms) in a few runs: page song 4x s8, 1/53, 0/0, 1/53, 5/94 for wraps 1 to 4; page mixer 4x s8,
11/103, 13/138, 15/217, 17/205 (rising, so later wraps are worse); core song 4x s8, 6/131, 4/86, 1/52, 5/88; page song 1x, 0 at
every wrap. Wrap 1 was not worse than wraps 2 to 4 in any run, and in the mixer run the trend is the other way round.

Clock against wall: the audio clock rate is 1.00 in every segment (1 s windows 0.99 to 1.01 on short songs; 0.91 to 1.09
and 0.95 to 1.04 only in contended full-song passes). Step errors at wraps are 1 to 9 ms, equal to the rest of the song. In the
quiet runs there is no jump of the audio clock at a wrap. Longest gaps between 100 ms clock samples (main thread starved) were 100 to 220 ms on quiet runs, 1 to 2.5 s only in contended full-song runs.

## Ranked causes (what the numbers support)

Because the symptom did not reproduce, these are ranked by size of the measured timing effect and by how well they match
"a little clock instability that goes away after a few patterns". None is wrap-specific.

1. **Main thread saturated by redraw, making page-played events and core rows late at the page.** Pattern view and Mixer at
   4x: 13 to 15 long tasks per 2 s (up to 217 to 290 ms), page-mode least lead falling from 113 to 35 to -44 ms and 4 late
   events by pass 2+ in the Mixer run. Costliest: `draw`, `drawArr0` (index.html 31229), `frame` (32251), `followPlay` (24312),
   `arrFollowPage` (24371), `pvFollow` (34587). In the 8-row Song view at 6x the main thread is 34 to 40 percent busy with
   0 late events, so this needs a heavy screen or a slow device to bite. Fix location: page-side display (throttle or skip
   redraws while playing, bound per-frame cost). Core mode is unaffected for audio (core lead stays at 247 ms) but the page cursor and
   row display go late.
2. **Heavy mid-song section drives audio-thread underruns that fall with pass number.** Core full runs: 272, 73, 46, 14
   underrun events in passes 1 to 4 (578, 176, 7 in the profiled run); audio-thread CPU is roughly 10 percent higher in pass 1
   than pass 2 for the same section, uniformly across functions (cold JIT or first-touch memory; cause not determined).
   Page full run was reversed (21, 21, 183), and contention (others 28 to 48 percent) confounds both, so this is a weak signal. It does match
   "resolved after a few patterns", but it happens mid-song, not at the wrap. Fix location: audio/sequencer (voice and effect
   cost in the worklet, warm-up before PLAY), not the wrap code.
3. **First-note buffer builds on page mode.** `rtBufPlain` (21560) / `ensurePlay` (8667) cost 1.5 to 16.5 ms at 4x on a pad's first
   note after a render install (first-time pad notes 8 / 50 ms in the full run). They recur through pass 2, so they are not strictly pass-1-only. Core
   mode never takes this path on the page. Fix location: page-side (prebuild before PLAY, extend `liveWarm`).
4. **PLAY-time one-off costs.** `songLenTab` (26471, full-song simulation at PLAY) and the `liveUses`/`liveWarm(512)` scan
   (22078, 22087, 22318). These land before the first pass and are pass-1-only, but they are on the Play press, not at a wrap.
   Fix location: page-side (spread over frames).
5. **Live pad 10's node is created at its first note (about 30.5 s, step about 534), beyond the 512-step `liveWarm` horizon.**
   Pass 1 only, single occurrence, measured at 2.9 ms in the 6x run. The nearest match to the HANDOVER suspect, but small and
   not at the wrap. Fix location: page-side (`liveWarm` horizon or by usage, not step count).
6. **Harness artefact (not a cause).** Attaching the worklet profiler adds 1.5 to 2.3 s at the start of `--prof=worklet` page runs
   ("stall", "moved on 1x"); ignore those pass-1 numbers.

Ruled out by measurement: the wrap step (0.1 to 0.8 ms), the song wrap in `makeSeq` (`settle`, "one wrap for the whole song",
core/seq-core.js about 292-325 and 711-720: zero late notes and zero skips at every wrap), `bgRender`, `varWant`, `wkFxAsk`,
`coreSong`/`coreDiff` (no call at or above 1.5 ms near wraps), audio-clock rate or jumps at a wrap, and FOLLOW on or off (no difference beyond noise).

Dependence: screen (Pattern view and Mixer worse than Song); throttle (nothing below 4x; 6x similar to 4x on short songs); sequencer (core keeps its own
lead at 247 ms while the page's view of it degrades; page mode degrades directly); size (cover and open similar); FOLLOW (no effect seen).

## Caveat on the owner's symptom

The owner heard it on a core-sequencer run. In emulation the core never booked late. The most plausible reading is that
what the owner heard is either mid-song audio-thread load (cause 2) or main-thread contention reaching the audio path
through something not emulated (real device clock, Android scheduling), neither of which headless Chrome can show.

## What to ask the owner to capture on the phone

Settings > Audio > Copy playback report, build number shown, taken twice:
1. Right after the first full wrap of the song (the moment the instability is heard).
2. After the third pass, when it is gone.
Also note: screen (Song, Pattern view or Mixer), FOLLOW on or off, song length in rows, LOOP on or off, unfolded or cover.
In the report look for: "page ticks" longest gap, "moved on", "held", "kicked"; "clock +x s in y s" lines;
"slow first buffer" and "long frame" lines; core "skips" and "latest note N ms late". Pass-1 and pass-3 reports can then
be compared line by line, and the time of the first odd line against the song position tells wrap against mid-song.

## Files

Scripts: tests/sweep/wrap-clock.mjs, tests/sweep/wrap-report.mjs, tests/sweep/wrap-step-cost.mjs. Raw data: tests/sweep/out/wrap-*.json
(gitignored, 31 runs; contaminated early runs in out/old/).
