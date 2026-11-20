IN THE MIDDLE OF: nothing. The design is written and pushed (branch `beta-stretch-design`, 19 Nov 2026); waiting for the lead's rulings on section 11. No app file changed; the only code is under `tools/experiments/stretch/` (not shipped).

# Stretch design: a clean synced stretch, REPITCH as a first-class choice, and the old-school ramp stretch

Owner, 19 Nov: "I'd love a nice sync timestretch, like Ableton's, as well as options for pitch, and an oldschool time stretch that uses the
ramp LFO technique where you set a short loop length and use a synced ramp LFO to scrub through the sample." Then two corrections from the
owner, via the lead: (1) "options for pitch" means **pitching the loop to match the selected length** (repitch-to-fit, speed and pitch together),
offered in the same picker as the stretch modes, not an independent transpose; (2) "We could consider adding more of the stretch algorithms as
well. I'd be OK with a lot of them." So the mode list is an OPEN SET with one picker, and section 4 prices each candidate.

Line numbers are of `beta` at `ba32739` (build 647), where the brief's `:22311` / `:22333` have moved: `stretchData` is :23222, `applyChop` :23245.
Design is against today's `I.sync` / `I.beats` / `I.grain`. The sampler-pitch-sync branch (`beta-sampler-pitch-sync`, build 655) was not pushed when
I looked (`git fetch` says no such ref), so its field names are unknown; "What changes when it lands" is at the end of section 1.

Measured here, not recalled: the experiments in `tools/experiments/stretch/` (`node tools/experiments/stretch/oldschool.mjs`, `page-stretch-quality.mjs`).
They run the page's own `stretchData` text (cut out of `index.html` by name), so they cannot drift from what ships. I cannot listen; every
sound claim below is a number or is marked "to be heard".

---

## 1. What exists today

### 1.1 The modes (answer in two sentences)
`SYNC_MODES = ['OFF','REPITCH','STRETCH','LOOP']` (:23193), `I.sync` 0..3, `I.beats` 1..64 (integer beats, `BEATS` row), `I.grain` 0..4 (1/64 ... 1/4 beat, `GRAIN_BEATS`).
REPITCH plays the loop at `rate = loop / target` (confirmed in `applyChop`, :23250: `if (sy === 1) rate *= loop / target`), so speed and pitch move together.
STRETCH and LOOP render a buffer exactly `beats*60/bpm` long by sweeping a **linear ramp** through the loop: STRETCH lays Hann grains at 50% overlap, LOOP lays back-to-back hard grains.

### 1.2 The renderer, exactly (`stretchData`, :23222-23242)
- Input: the pad's rendered buffer (after layers AND pad effects, so effect tails are stretched too), the first `inN = min(len, loop*sr)` samples; `loop` from `loopSecs` (:23197: first enabled sample layer's length x (1 - start) / its tune rate + delay).
- Output length `outN = max(64, round(beats*60/bpm*sr))`, exact. Read ratio `k = inN / outN` (the ramp; grains are read at ORIGINAL pitch, never resampled).
- **STRETCH = plain granular overlap-add**: grain `W = round(GRAIN_BEATS[grain]*60/bpm*sr)` (min 64), hop `W/2`, grain *n* starts at output `s0` and reads source from `round(max(0,s0)*k)`, Hann window (sums to 1 at 50% overlap). No waveform alignment (no WSOLA / SOLA), no phase handling, no transient handling. Grain size in beats, so it **changes with tempo** (1/32 beat at 120 BPM = 62 ms; at 170 BPM = 44 ms).
- **LOOP = hard grains**: grain `W` at `s0 = 0, W, 2W...`; source start `round(s0*k)`; each grain plays `W` samples of source from there, so the source advances `W*k` per grain; a "crossfade" of fixed 1.5 ms (`xf = min(round(sr*0.0015), W>>2)`) where the grain *reads the loop circularly* (`(i % W + W) % W`).
- Stereo: each channel stretched separately with the same maths (`d` and `d2`); fine for the grain modes, not for a phase-vocoder (section 4).
- **Quality limits, measured** (`page-stretch-quality.mjs`, 220 Hz sine, 1.5x): STRETCH's level ripple over 5 ms windows is -2.6 / -4.4 / -1.2 / -4.6 / **-12.8 dB** for GRAIN 0..4 (ideal 0). So today's STRETCH is a granular texture, not a clean stretch: grains at unaligned phases cancel and add, worst with the long grains.
- **Finding for OLDSCHOOL:** LOOP's "1.5 ms crossfade" halves the click, it does not remove it. On a 220 Hz sine, 12 ms grains: hard cut max step 0.99 (63x the source's own max step); with the page's wrap-style crossfade at x = 5% or 50% the max step is still 0.79 (50x), at every grain boundary, because the loop's wrap point sits in the middle of the fade. A crossfade that reads the source straight on (`xmode 'straight'` in the prototype) gives max step 1.5x / 1.0x / 1.0x the source's (x = 5 / 25 / 50%) and zero boundary steps over 4x. So a real SMOOTH control needs the new overlap, and the old behaviour must stay as the "classic" entry.

### 1.3 The cache and when it is rendered
- `rtBuf(pad, rev, st)` (:23262): key `pad + 'r'|'f' + stKey(st)` with `stKey = ':mode:bpm:beats:grain'` (bpm rounded to 0.01); entry valid while `e.d === c.data` (the pad's render object, so any re-render of the pad drops it). Reverse is a **second full buffer** (`d.reverse()`). Cap: 96 entries, oldest first. No byte cap: worst case 96 x 3.8 MB (10 s stereo 48 kHz float) is about 370 MB.
- **It is computed synchronously on the main thread at the first note that needs it** (`getBuf` in `makePlayer`, :22934, called from the player's `play`). There is no pre-render. The app's own "slow work" log already times it (`stretchData = W('tempo stretch', ...)`, :24573), which is why it is a known stall. Measured (Node, desktop CPU, 10 s mono in, 8 s out): STRETCH 231 ms, LOOP 219 ms, per channel; a phone is several times slower (my estimate 3-6x, not measured): 1-2 s per channel on the Fold, in the middle of PLAY, on the first note.
- A **tempo change** makes a new key (bpm is in it), so a new synchronous stretch on the next note; old keys stay until evicted.
- Offline render and stems: `renderGetB` (:25153) calls `stretchData` the same way, per unique (pad, rev, st), on the main thread, in the same text; output identical to live.
- Pad renders themselves go to a Worker (`rwStart`, :8795: a blob made from the `===ENGINE===` block of the page plus glue, with the wasm modules handed in). Stretch does not.

### 1.4 Slices on a stretched timeline
`applyChop` maps every slice linearly: `ofsSec = s0 * scale`, `durSec = (s1 - s0) * scale`, `scale = target / loop`, on the stretched buffer. That is correct only because both ramps are linear. Any non-linear ramp (reverse, ping-pong, ease) or a hit-aligned transient mode breaks the mapping (section 7.5). `ev.rate` for a stretched note = `2^(semis/12)` only (the stretch already holds the tempo fit), `ev.cut = t + durSec / rate`.

### 1.5 The audio-thread sequencer: what it does today (and the open bug)
- `core/seq-core.js` is GENERATED from `index.html` by `tools/make-core.mjs`. Its line 5 is a **stub: `function applyChop(){}`**, `ensure` / `ensurePlay` return the core's own `coreBufs` (the pad render copied in by `coreBuf`). So on the audio thread `makeSeq` calls `applyChop` (seq-core.js:706) and nothing happens: no `rate` fit, no `ofsSec` / `durSec` slice, no `ev.st`. That is the open "slices on Audio thread" bug: a sliced or synced instrument plays unchopped there. (The native port found the same and ruled the page's sequencer the specification: `docs/port/PICKUP.md` on `native`, #245 `SeqChop`.)
- Even once `applyChop` ran there, **stretched notes could not be played by the core**: `core-processor.js:563` `mine(ev)` returns false for `ev.st` ("the page's stretch cache, for now"), and `loopSecs` needs `kit.voices[pad].layers`, which the core's `kit` stub (`{voices:[]}`) does not have.

### 1.6 The native port today
`docs/port/PICKUP.md` (on `native`): chop is ported (`seq/chop.h` `SeqChop`; loop seconds and hit points are HANDED IN by the host, `thunder_render_set_pad_chop / _hits`), and "**sync modes STRETCH and LOOP play unstretched** (counted in `notes_stretched`) ... until the stretch port exists", with slice offsets those of the stretched timeline, so slices land in the wrong place. REPITCH works (it is only `rate`).

### 1.7 What changes when `beta-sampler-pitch-sync` lands (build 655)
Expected from the brief, to confirm against its diff: `I.beats` (integer beats) gets an exact line length (fractional lines; a line is `60 / bpm / S.lpb` s), and a SYNC TO LINES button writes it. Consequences for this design: (a) every place below that says "target length" means that exact value (`T = lines * line * sr`), `stKey` must carry it (today it carries `beats`); (b) the BEATS row becomes a LINES row for all modes; (c) the SYNC TO LINES toast/info is where the pitch sentence goes (section 6); (d) the old `I.beats` stays readable for old songs (load: `lines = beats * lpb`).

---

## 2. Where pitch comes from for a synced / stretched note, and what "pitch independent of stretch" would hook into

Note pitch is assembled in `makeSeq` (:22329, seq-core.js:696): `nOf(x) = x + ctsp + I.tsp + T.tsp + nrnd + lx` (command transpose, instrument TRANSPOSE, track transpose, random, KEY FOLLOW live transpose; KEY FOLLOW snaps to scale) `+ pit` (pitch commands). `applyChop` receives `n` (the typed note) and `semis = note - n`, builds `ev.rate = 2^(semis/12)` (slice instruments also add `n - ROOT` only when the SLC command picked the slice), times `loop / target` for REPITCH. The layer's own tune is not in `rate`: it is baked into the pad render (and into `loopSecs`).

What a melody on a stretched sample does today: the stretch is rendered **once at the root** and a note repitches the stretched buffer by `rate` (`playbackRate`): pitch AND length move together, so an octave-up note plays the 8-beat loop in 4 beats and is off the grid. Slides (PSL) and vibrato/bends are `Auto` ramps on the same `rate` (`v.r`), so they scrub the stretched buffer faster and slower like tape. Arps retrigger the same buffer.

Hooks for "pitch independent of stretch" (all of them **later**, section 6): (1) engines that give it for free (Signalsmith: `setTransposeSemitones`, formant; OLDSCHOOL: its own grain pitch `r`), rendered into the cache so it is part of the key; (2) a KEEP LENGTH per-note re-stretch, which needs the sequencer to know the note's rate before the buffer is chosen (it does: `applyChop` sets `ev.rate`), so the cache key gets a quantised rate and the render is per (pad, params, semitone).

---

## 3. Where DSP of this weight runs today

| Module | Built | Loaded | Pinned |
|---|---|---|---|
| `dsp.wasm` (synth engines) | `dsp/build.sh`, wasi-sdk 25, -O3 -msimd128 -fno-exceptions -fno-rtti | base64 in `index.html`, instantiated on the page, in the pad-render Worker (`m.pl`) and in the worklets | sources named in the script; since 15 Nov the native tree pins them by full SHA in `native/core/third_party/PINS.md` (`fetch.sh` fails if the checkout is not that commit) |
| `tfx.wasm` (track effects) | `dsp/tfx/build.sh` | base64, one copy per track worklet; Worker gets it as `m.tx` | same |
| `dsp/mods` | `dsp/mods/build.sh` | embedded in `chains-worklet.js` / `waves-worklet.js` | same |
| pump | built from the owner's repo (`pump/pump-meta.js` carries its sha1) | `pump/pump.wasm`, 1.6 MB, a separate file | repo sha in notices |
| LXR (GPL) | `lxr/build.sh` | `lxr/lxr.wasm`, `lxr-engine.js`, optional folder; the app runs without it | commit in `lxr/NOTICE.md` |
| Link (GPL, desktop) | `link/build.js`, `link/fetch-sdk.sh` (pinned commit) | native addon | commit in `link/NOTICE.md` |

A stretch **can** run in a Worker: the pad-render Worker already shows the pattern (blob from page text + wasm module via `postMessage`, ArrayBuffers transferred back, `PW.jobs` promise map, `rwDown()` fallback to the page thread). It should be **its own Worker** (a file, `stretch/stretch-worker.js` + `stretch.wasm`, started on the first synced instrument), not another op of the pad-render Worker, because that one is single and sequential: a 1-2 s stretch would block the pad renders that auditions wait on, and songs that never use SYNC should pay nothing. The native tree already compiles the same C++ DSP for native and for wasm from one pinned source (`native/core/dsp/CMakeLists.txt`, `prepare.sh`), and has JS-exact maths (`rt/jsmath.cpp`, `rt/fdlibm.cpp`), which matters for goldens (section 9).

---

## 4. The engines, compared, and the price of adding each

Facts below come from each project's own repository or site, fetched 19 Nov (URLs after the table). Not verified (I could not fetch the page): the licence text of Signalsmith Linear (its FFT dependency), the current official SoundTouch release (the mirror I could read shows 1.9.2), the wasm sizes of anything (no build was made), Rubber Band R2 vs R3 differences. Each is marked "check" where it matters.

Honest ratio note: Signalsmith's README says **"time-stretching sounds best for more modest changes (between 0.75x and 1.5x)"**. A tracker's 0.5-2x is outside that on both ends, and 4x is far outside. Pitch shifting covers octaves. Our own break/loop use is mostly tempo fits (170 -> 140 BPM is 1.21x; 160 -> 120 is 1.33x), inside the good range; the big ratios belong to the CHARACTER modes (SMEAR, EXTREME, OLDSCHOOL), where artefacts are the point. Signalsmith's README also does not mention transient handling: for drums the DRUMS group (SLICED) is the answer, not a better vocoder.

### 4.1 Candidates

| # | Candidate | Licence | Character / good at | Mono / stereo | Formant / pitch | Maturity | Wasm + native from one source? |
|---|---|---|---|---|---|---|---|
| A | **Signalsmith Stretch** | MIT (README: "Released under the MIT License") | full-spectrum phase-vocoder-family stretch, clean on tonal material and full loops at modest ratios; no stated transient handling | any channels, stereo in examples, coherent across channels | pitch (`setTransposeSemitones`), formant shift + compensation (`setFormantSemitones`, needs a rough `formantBase`), tonality limit, custom freq map | active (npm 1.3.2 seen); has a Web Audio build in its `web/` dir; C++11 | yes: C++11, `presetDefault` / `presetCheaper`, optional FFT backends; depends on **Signalsmith Linear** (check its licence) |
| B | **Bungee (open edition)** | MPL-2.0 (file-level copyleft) | adaptive phase vocoder, all ratios incl. reverse and zero speed (scrub, tape-stop); "good quality" is the project's claim; the better "Pro" is closed and commercial | stereo; any count in principle | pitch yes; formant not mentioned | Ableton keeps a fork (`github.com/Ableton/bungee`, patches on release branches); open edition's web build not mentioned | C++, CMake; Eigen + PFFFT dependencies |
| C | **Rubber Band** | **GPL v2 or later, or commercial** ("No expiry date, no royalties"); its page says GPL code "can't legally [be] distribute[d] on the iOS or macOS App Stores" | the best-known engine; R2 resets phase at percussive transients | stereo+ | pitch via resample + stretch; formant: not on the page I read | very mature | C++; an unofficial wasm build exists (Daninet/rubberband-wasm) |
| D | **SoundTouch** | LGPL-2.1 | time-domain WSOLA-like; tempo range very wide; classic for rhythmic mono-ish material, artefacts on dense mixes | stereo | pitch via stretch + resample | very mature | C++ |
| E | own **phase vocoder** (SMEAR) | ours; algorithm from the literature | the "smeared" classic: any ratio, phasey and washy, transients smear | per channel or joint | none | new code | JS / C++ |
| F | own **WSOLA** (SPLICE) | ours | no phasiness; keeps waveform shape; repeats or skips periods; good on bass, mono lines, drums at small ratios | per channel with shared lags | none | new code | JS / C++ |
| G | own **PaulStretch-style** (EXTREME) | ours; the original PaulStretch is GPL (not read at its source; PaulXStretch's README says GPLv3 with an App Store exception, and credits Nasca for the algorithm) so **write it from the published description, as the app's Extreme stretch effect already did** | 10x-1000x ambient wash, random-phase FFT, huge windows | stereo | none | new code (and an existing effect to reuse maths from) | JS / C++ |
| H | own **SLICED / beats** | ours | slices at hits (the app's `findHits`), placed on the stretched grid, tails by cut / decay / loop / stretch | per channel | none | new code | JS / C++ |
| I | own **OLDSCHOOL** (cyclic ramp) | ours | section 7 | per channel | grain pitch | new code (proved in the prototype) | JS / C++ |
| J | **GRANULAR (classic)** = today's STRETCH | existing | kept bit for bit | per channel | none | existing | JS (+ C++ port for native) |
| K | **REPITCH** | existing | speed = pitch | n/a | n/a | existing | trivial |

URLs: Signalsmith, https://github.com/Signalsmith-Audio/signalsmith-stretch (README read through a mirror, https://gh.r-mj.com/Signalsmith-Audio/signalsmith-stretch, and the web README https://unpkg.com/signalsmith-stretch@1.3.2/README.md; API summary via https://docsearch.algolia.com/mcp/docs/repo/signalsmith-audio/signalsmith-stretch). Bungee, https://github.com/kupix/bungee ("permissively licensed under the Mozilla Public License Version 2.0"), https://bungee.parabolaresearch.com/ (open vs Pro), https://github.com/Ableton/bungee (MPL-2.0, Ableton's fork). Rubber Band, https://breakfastquay.com/rubberband/license.html and https://breakfastquay.com/rubberband/technical.html. SoundTouch, https://github.com/Bilibili/soundtouch (a mirror: LGPL-2.1, WSOLA-like, "SoundTouch uses WSOLA-like time-stretching routines that operate in the time domain"; the official site was not on this session's allowlist, so the current version is unverified). PaulXStretch, https://github.com/essej/paulxstretch/blob/main/README.md.

### 4.2 Recommendation: ONE default clean engine

**Signalsmith Stretch (A), MIT, compiled once to wasm and natively, used OFFLINE (a pure function that renders the stretched loop), exposed as the mode CLEAN.** Why: the licence is the app's permissive set (a notice, nothing else); C++11 with no exceptions needed on the surface that matters (to confirm with `-fno-exceptions -fno-rtti` in the spike); pitch and formant come with it for the "later" step; stereo is coherent; one source for web and native, which is what the owner asked for. **The one risk:** its own documented sweet spot (0.75-1.5x) is narrower than a tracker's ratios and it has no transient mode, so CLEAN on a full break at 0.6x or 1.8x will smear hits, and the first build's honesty rests on (a) the info line telling the owner when the ratio is outside the sweet spot and pointing at SLICED / SMEAR, and (b) a spike (section 10, PR 5a) that renders the owner's loops at the ratios he uses and measures size and time before the app takes the dependency.

**Second best: Bungee (B), MPL-2.0.** Weaker on documented quality than A, but reverse and zero-speed (scrub, tape stop, reverse stretch) are first-class, it also has a closed Pro tier whose upgrade path keeps the same API, and Ableton maintaining a fork is a maturity signal (I do not claim what Ableton uses it for). MPL-2.0 is file-level: files we modify must be published; the notice and the licence text ship. Dependencies are heavier (Eigen).

### 4.3 Cost of ADDING each (so the lead can pick a set)

"Notice" means a THIRD_PARTY_NOTICES.md entry. "Goldens" means the tests the native port needs (section 9). Sizes are my estimates unless stated; no wasm was built.

| Candidate | wasm size | Build and pinning | Native port work | Licence notice | Tests / goldens |
|---|---|---|---|---|---|
| K REPITCH | 0 | none | none (works today) | none | an exact-rate test (exists in part) |
| J GRANULAR classic | 0 (stays JS in the page) | none | C++ port of `stretchData` mode 2, bit-exact with `fdlibm` cos; about 120 lines | none | golden from the JS text (the `lib.mjs` cut-by-name method; `seq-golden --chop` does the same); both channels |
| I OLDSCHOOL | 0 (JS) or about 3 KB | none | about 80 lines C++, same `jsmath` trick; shares the reader with the live step | algorithms list: "cyclic / S950-style stretch, original code" | prototype's facts become tests: exact length, boundaries only on the grid, max step bound at x > 0, pitch ratio; classic parameters == the page's LOOP bit for bit (done: 0 differing samples, all 5 grains) |
| H SLICED | 0 (JS) | none | about 150 lines C++; `findHits` already handed in by the host on native (`set_pad_hits`) | none | slices and tail rules: golden per policy; no-click bound at joins |
| F WSOLA (SPLICE) | 0 (JS) or about 5 KB | none | about 200 lines C++ | cite Verhelst and Roelands 1993 in the algorithms list | length exact; periodicity of a sine preserved (zero-crossing rate); JS vs C++ within 1e-6 |
| E SMEAR (phase vocoder) | JS using the page's `SpecUtil` FFT, or C++ with native's `fftr.cpp` | none | about 250 lines C++ + the FFT the native core already has | cite Flanagan and Golden 1966, Laroche and Dolson 1999 (identity phase locking), Roebel 2003 (transients) | length exact; sine frequency preserved; steady-state level ripple under a bound (the same measurement as section 1.2); JS vs C++ tolerance |
| G EXTREME (Paul-style) | 0 | none | about 100 lines on top of the same FFT | "No PaulStretch code was used" (as the existing line) | length exact; deterministic RNG seed so goldens are stable |
| A CLEAN (Signalsmith) | est. 100-400 KB, **unmeasured**; separate `stretch.wasm`, loaded on first use | new `dsp/stretch/build.sh` (wasi-sdk 25 like the others); source fetched at a pinned commit by a script (as `link/fetch-sdk.sh` and `native/core/third_party/fetch.sh`), SHA recorded in `PINS.md` and the notice; fetch Signalsmith Linear at its own pin | the same `.cc` file in `native/core/dsp` CMake; wrapper = one pure function `stretch_render` (offline, no streaming) | MIT text and copyright line of both repos (read from LICENSE at the pin; the copyright holder is not stated on the pages I read) | wasm-vs-native **tolerance** golden (max abs and RMS error bound; float determinism flags documented), exact length, sine frequency, loop-seam test (render a loop twice-concatenated and compare the seam), ratio sweep 0.5..2 |
| B Bungee | unknown; Eigen makes it larger | pin + fetch; Eigen and PFFFT pins (PFFFT is already in the tree via pump) | C++ same as above | MPL-2.0 text, "source available" statement | same as A |
| C Rubber Band | unknown | a **separable module** like LXR: `rubberband/` folder with its source or a fetch script, GPL text, `NOTICE.md`; the app runs without it and the mode shows "not in this build" | native: excluded from the default build option like LXR (`native/CMakeLists.txt` option); never in an App Store build | GPL-2-or-later notice, source offer; commercial licence is a purchase from Breakfast Quay | wasm-vs-native tolerance; **what it would take if the app is sold**: remove the folder or buy the commercial licence (`docs/port/IF-SOLD.md` row for LXR and Link says the same for GPL parts) |
| D SoundTouch | est. small | LGPL needs the user to be able to relink: awkward when wasm is base64 inside `index.html` and when the native app is statically linked on iOS | possible | LGPL notice + relink statement | not needed: F covers what it does; not recommended |

### 4.4 Which set, when (a / b / c)

- **(a) first build:** REPITCH (with its sentence), GRANULAR (classic STRETCH), OLDSCHOOL (new, with LOOP as its classic), nothing needs a dependency. Owner's asked-for oldschool ramp is here.
- **(b) next:** CLEAN (Signalsmith), SLICED, then SMEAR, EXTREME, SPLICE (own code, no dependencies, each small).
- **(c) later:** live OLDSCHOOL in the voice, KEEP LENGTH / transpose + formant through Signalsmith, Bungee as a second clean engine if the owner wants a different flavour, Rubber Band only as a separable GPL module if his ears say A is not enough on full mixes.

**Default (one):** CLEAN, once it exists; until then SYNC stays as today (REPITCH is the plain "fit it" the owner already has). Recommendation for the pre-selected mode when SYNC is first turned on or SYNC TO LINES is pressed: CLEAN if the ratio is within 0.75-1.5x, else REPITCH with the sentence telling him what it does (section 5.3); the owner can overrule in one tap and I would ask him (section 11).

---

## 5. The modes, as the owner will see them

### 5.1 One picker, grouped; each mode shows only its own rows
The SYNC row stays the single entry (an `opts` row, :26712). Its list is grouped, in this order (the group name is shown as a heading or as a prefix in the info text, whichever the existing options UI supports; to check in the PR):

```
OFF
REPITCH            Repitch: speed = pitch, loop fits the length
CLEAN  (Signalsmith)       full loops and tonal sounds, pitch kept (best 0.75x-1.5x)
SLICED  (beats)            drums: cut at hits, hits kept, tails filled
GRANULAR  (classic STRETCH)  grains at the tempo: a texture
OLDSCHOOL  (cyclic ramp)    the S950 trick: a tiny loop scrubbed by a synced ramp
SMEAR  (phase vocoder)      any ratio, washy, pitch kept
EXTREME  (Paul-style)       very long ratios, ambient wash
SPLICE  (WSOLA)             time-domain: bass and mono lines, small ratios
```
Groups: **Repitch** (OFF, REPITCH), **Clean** (CLEAN, SPLICE), **Drums** (SLICED), **Character** (GRANULAR, OLDSCHOOL, SMEAR, EXTREME). Names are what the thing is, not Ableton's: CLEAN is not "Complex", SLICED is not "Beats" (its slices and tails are ours).

Rows per mode (the settings list is already built with `...(cond ? [..] : [])`; the same here):

| Mode | Engine | Controls (besides LINES) |
|---|---|---|
| REPITCH | rate only | none; info line gives the pitch effect |
| CLEAN | A (Signalsmith) | QUALITY (FULL / CHEAP = `presetDefault` / `presetCheaper`); later: TRANSPOSE (st, cents), FORMANT (KEEP / SHIFT) |
| SLICED | H | SLICES + SLICE BY (the existing rows), TAIL (CUT / DECAY / LOOP / STRETCH-with-CLEAN), HIT LEVEL later |
| GRANULAR | J | GRAIN (existing 5 values); later GRAIN MS |
| OLDSCHOOL | I | LOOP (grain length: ms 1-200 and divisions of a line), RAMP (FWD / REV / PING / EASE), SMOOTH (0-50%, 0 = authentic), PITCH (st, cents of the grains), JITTER (later) |
| SMEAR | E | WINDOW (ms or samples), PHASE (LOCK on/off), HITS (transient reset on/off) |
| EXTREME | G | WINDOW (50 ms-4 s), SEED (fixed: deterministic) |
| SPLICE | F | WINDOW (ms), SEARCH (ms) |

### 5.2 Old songs sound identical
Keep `I.sync` meaning exactly as today (0 OFF, 1 REPITCH, 2 STRETCH, 3 LOOP) and `I.grain` as today, and add one field, **`I.sx`** (the algorithm inside the family), default absent = 0:
- `sync 2, sx 0` = classic STRETCH, `sync 3, sx 0` = classic LOOP: these run the **existing `stretchData` text unchanged** (kept as the "classic" code path; the UI names them GRANULAR and OLDSCHOOL (classic)); bit-identical output, same `stKey`.
- New modes use `sync 2` or `3` plus `sx > 0` (CLEAN = 2/1, SMEAR = 2/2, EXTREME = 2/3, SPLICE = 2/4, SLICED = 2/5, OLDSCHOOL v2 = 3/1). **Why this way:** a build that does not know `sx` (an older tab, a phone not yet updated) clamps and ignores it and plays the nearest classic mode instead of a wrong or silent sound; the loader already does `clamp(int(o.sync,0),0,3)` (:21276).
- New parameters (`I.og` loop ms, `I.ox` smooth %, `I.orp` ramp, `I.opt` pitch cents...) are written only when `sx > 0`, loaded with clamps like their neighbours, so an old song's JSON does not change and the song hash does not move.
- The picker maps (sync, sx) <-> one list index; nothing else reads the mapping.
- Test: render a song with `sync 1`, `2`, `3` (each GRAIN) before and after the change and compare hashes (the sampler-check / seq-golden approach); an old-song JSON round-trip test.

### 5.3 The REPITCH sentence (SYNC TO LINES and the info line)
REPITCH's whole effect is `rate = loop / target`. Say it: `percent = (rate - 1) * 100`, `semitones = 12 * log2(rate)`, e.g. the prototype's break (2.82 s to fit 3.43 s) is "plays 17.7% slower, 3.4 semitones down"; 15% faster is 2.4 semitones up. Shown in the SYNC TO LINES toast, in the SYNC row's info, and live in the LINES row when the mode is REPITCH. The same line, in the same words, is shown for every other mode as "pitch kept" so the difference is visible next to the picker.

---

## 6. Pitch (shrunk, as the owner's correction asks)

- **First build:** REPITCH is the pitch option: a first-class entry in the picker with the sentence above. Nothing else is added.
- **Later, only where the engine gives it for free:** CLEAN gets TRANSPOSE (semitones, cents) and FORMANT (KEEP / SHIFT) because Signalsmith has `setTransposeSemitones`, `setFormantSemitones`, `setFormantCompensation` (formant needs `setFormantBase`, a rough fundamental estimate, or 0 for its pitch tracker). OLDSCHOOL has its own grain pitch (`r`) in its first build, because that is the technique. No pitch row for SLICED / GRANULAR / SPLICE / SMEAR / EXTREME unless asked.
- **How tracker notes, slides, arps and bends combine with a stretched sample:** as today. The stretch is rendered once at the instrument's setting into the cache (so a CLEAN transpose is part of the key), and a note repitches it by `ev.rate` (so a note a fifth up is a fifth up and 33% shorter, off the grid). Slides and vibrato scrub the stretched buffer like tape. Slices use the note only as the slice selector unless SLC picks the slice (existing rule). **KEEP LENGTH** (re-stretch per note so the loop keeps its length at the note's pitch) is not proposed for the first build: it needs a stretch per distinct pitch an instrument plays (a melody: 5-20 renders, each 0.2-2 s on a phone), a scan of the song for the pitches, and prerender/cancel logic; it becomes cheap only after the Worker, the byte-capped cache and the prerender scan exist (section 8), and only for CLEAN (one pass does stretch + transpose together). Do after PR 6.

---

## 7. OLDSCHOOL in detail

### 7.1 The algorithm (proved in `oldschool.mjs`)
Symbols: `L` source (loop) length in samples, `T` sync length in samples (`lines * line * sr`, exact), `g` grain length in output samples, `r` grain read ratio (1 = original pitch, `2^(st/12)`), `x` smoothing 0..0.5 of `g`, `ramp(u)` shape on [0,1] -> [0,1].
- The output is exactly `T` samples. It is cut into grains at `s0 = 0, g, 2g...`.
- Grain *k* **start position** `p_k = L * ramp(s0 / T)`, sampled and held at the grain boundary (rounded to a sample). If the grain would read past the end it is clamped (`p = min(p, L - g*r - 1)`), so the last grains keep repeating the last `g*r` of the sample (the alternative, wrap, is a flag).
- The grain plays `d[p + i*r]` for `i = 0..g-1` (linear interpolation when `r != 1`).
- **Held start = continuous scrub.** A sampler whose loop start moves continuously reads the new start only when the loop wraps, i.e. every `g`: so "sample-and-hold at the wrap" is the same sound as a continuously scrubbing loop start; no separate model is needed. The sync length is the time the ramp takes from the sample's start to its end, as the owner described.
- **What the controls do to the sound:** `g` is the tone (a hard cut every `g` buzzes at `sr/g`: 12 ms = 83 Hz; measured: boundaries on the grid every 529 samples, the only jumps in the output). The source advances `g * L / T` per grain: at stretch 4x each source region is heard 4 times (the audible repeat), at 1x each once. `x = 0` is the authentic hard cut. `r` repitches the grains without changing the sweep.
- **x > 0 (new, SMOOTH):** grain *k* also reads `x*g` of source before its start and after its end (straight, **not wrapped**) and the neighbours crossfade over `2*x*g` (trapezoid weights summing to 1). Measured, 220 Hz sine, g = 12 ms: x = 5 / 25 / 50% gives max step 1.5x / 1.0x / 1.0x the source's own, zero boundary steps over 4x (the page's classic wrap-style crossfade: 50x, section 1.2).
- **Ramp shapes:** FWD `u`; REV `1-u`; PING `2u` then `2-2u` (the sample scrubbed forward then back in one sync length); EASE `3u^2-2u^3`; EXP `u^2`. Easy to add; the ramp is a function of `u`, so a curve amount later is one more parameter.
- **Same as today's LOOP:** with FWD, `r = 1`, `x` = 1.5 ms and the circular overlap ("classic") the prototype reproduces the page's `stretchData` mode 3 with **0 samples differing at all five GRAIN settings**. So OLDSCHOOL generalises LOOP and LOOP stays as its classic entry (section 5.2).

### 7.2 Grain length as a time and as a division
Store `I.og` in ms (0.5..250, default 12) and an optional division mode: grain = a division of a line (1/1, 1/2, 1/4, 1/8, 1/16 of a line) or of a beat (the existing GRAIN_BEATS), so it can follow the tempo like today or stay fixed in ms like hardware. Both convert to `g` samples at render time; the key carries the resulting integer `g`.

### 7.3 Rendered into the cache or live in the voice?
- **Rendered first (recommended for the first build).** Same cache, same two sequencers, same offline render and stems, slices work (7.5), native port is about 80 lines, goldens are trivial. The cost is O(T): the prototype takes 402 ms for 10 s in and 8 s out in unoptimised JS with linear interpolation and x = 25%, and the classic LOOP is about 220 ms per channel; optimised (no per-sample `at()` call, integer path when `r = 1`) it is tens of ms. Cheap enough to render on the main thread in PR 2 before the Worker exists.
- **Live in the voice (second step).** The fun of the technique is modulating `g`, the ramp rate or the pitch while it plays (tracker commands, LFOs). That needs a new reader beside `Rd` (`core/core-processor.js:214`, which already reads a pad back to front per sample): per-sample state (grain phase, current start `p`, ramp `u`), driven by the existing `Auto` automation objects (evaluated per sample) for `g`, `r`, `x`, ramp amount. The ramp phase either starts at the note (like a sampler triggering) or locks to the song position (option). On native, the same reader goes into `VoicePool` (`native/core/src/voice/voice.cpp`). **Design rule so the two never disagree:** write the reader once as a small state machine `oldschoolStep(state, params) -> sample`, and make the RENDERED mode literally that reader run for `T` samples with constant params. Then constant-parameter live equals rendered to the bit (a golden), and nothing is implemented twice.
- Live needs the source buffer in the core (it has it: `coreBufs`), no stretched buffer, no cache, so it also sidesteps the audio-thread stretch problem for this one mode. It needs `ev.ofsSec/durSec`-like slice handling decided (7.5), and a decision whether a slice restarts the ramp.

### 7.4 Controls he will expect, and where they live
`LOOP` (ms, divisions of a line), `RAMP` (FWD / REV / PING / EASE), `SMOOTH` (0-50%, 0 = authentic hard cut), `PITCH` (grain semitones, cents), `JITTER` (random start offset as a % of `g`, seeded so renders repeat; second step), plus the common `LINES`. It "must be able to sound deliberately crude": `x = 0`, no interpolation when `r = 1` (integer reads), 1-5 ms loops, no anti-aliasing on repitch (a NEAREST flag for the S950 grit; first build uses linear, add NEAREST as a one-row option if he asks).

### 7.5 Slices with a non-linear ramp
With FWD and PING-free modes the timeline is proportional and `applyChop`'s slice mapping holds. With REV, PING or EASE the output time of source slice *k* is `ramp^-1` of its position (REV is an exact mirror; PING visits each slice twice). First build: for non-FWD ramps, slices are cut in **output time** (the rendered buffer divided evenly or by the grid), which is simple and honest ("the slices are of what you hear"); HITS slice mode is disabled there (shown in the info line). The source-aware mapping is a later step for REV only.

### 7.6 What I cannot report
I cannot listen. Everything above is a measurement on a synthetic break (the repo ships no sampled break; `makeBreak` writes a deterministic two-bar kick / snare / ghost / hat loop) and on sines: exact output length (151200 samples = the target, every variant), boundary periodicity (273 hard-cut jumps, every one on a multiple of g = 529; gaps 529 or 1058), no step above 4x the source's own at x >= 5% in straight mode, `r = 2` doubles a 220 Hz sine (437 Hz measured over 0.35 s), and bit-identity with the page's LOOP in classic mode. WAVs for the owner's ears are written to `/tmp/stretch-exp` by the script (source break, classic LOOP 1/32, authentic 12 ms, authentic 30 ms reversed, smoothed 30 ms x 25%, ping-pong 1/16, +5 st at 12 ms): they are the first items for his punch list.

---

## 8. Where it runs, the cache, the audio thread, offline

- **A dedicated Worker** (`stretch/stretch-worker.js`, started on demand, with `stretch.wasm` loaded on first CLEAN): message `{id, key, params, data, data2, sr, L, T}`, answer `{id, data, data2}` with transferred buffers; the page sends **copies** (the pad cache keeps its buffer). Progress: the job runs in chunks of about 16384 output samples and posts `{id, done: 0..1}` between chunks (the chunk loop yields, so a `{cancel: id}` message is seen); cancel = a newer job for the same (pad, mode) supersedes the older one, with terminate-and-respawn only as the fallback. If the Worker cannot start: the same functions run on the page thread (as `rwDown()` does today), in slices with `idle()`.
- **Cache key = every parameter that changes a sample:** pad id and the pad render's revision (`padRev[]`), loop seconds (rounded to a sample), sample rate, mode and `sx`, every mode parameter at its stored resolution, `T` (exact lines x line length, or beats x 60 / bpm), bpm at 0.01. **Not** the reverse flag: reverse is read backwards (the core's `Rd` already does it) or, on the page, reversed once on the way out of the cache, not stored as a second entry. Entries are held in an LRU **by bytes** (proposal: 64 MB on a phone, 256 MB desktop), not by count; today's 96-entry cap is about 370 MB worst case.
- **Prerender, not first-note render.** When a song loads, on PLAY, and after an edit (debounced 250 ms), the page scans the instruments that use SYNC (with the tempo or the TPO values the song uses) and asks the Worker for each unique key not cached; a note that arrives before its buffer is ready plays the last good buffer for that pad if any, else is silent for that note (like `ensurePlay` for a pad with no render), never a main-thread stretch during PLAY. Editing a BEATS / LINES / mode row re-renders in the background and swaps when it lands (`padRev` guards, as `bgRender` does).
- **Tempo change:** new `T`, so a new key: re-render all synced instruments in the background (the same path). Cost for a 10 s loop on a phone: unmeasured. Budget (to be verified in the spike): OLDSCHOOL, GRANULAR, SLICED, SPLICE well under 0.5 s; CLEAN and SMEAR 1-3 s for 10 s stereo on the Fold; EXTREME longer, with a progress bar. A tempo ramp/TPO automation mid-song is the awkward case: first build handles a single song tempo plus the TPO values found by the scan; anything else falls back to the last buffer.
- **Memory:** 10 s stereo 48 kHz float32 = 3.84 MB per stretched entry at 1x; a 2x stretch doubles it; plus the core's copy (`coreBuf` posts a copy to the worklet, so every stretched buffer exists on both threads: 2x). The byte cap is what keeps this bounded.
- **The audio-thread sequencer: what must be fixed first, in order** (these are the blockers; stretched notes cannot work there before 1-3):
  1. `applyChop` is a stub in `core/seq-core.js` (generated; the stub list is in `tools/make-core.mjs`): move the real `loopSecs` / `findHits` / `slicePoints` / `applyChop` into the generated core with the data they need handed in (as native's `set_pad_chop / _hits`): the page sends each pad's `loopSecs` (and HITS points) with `op:'buf'`, because the core's `kit.voices` has no layers. **This alone fixes the open "slices on Audio thread" bug**, and is shippable without any stretch (PR 4).
  2. Give the core stretched buffers: a new message `{op:'sbuf', pad, key, data, data2, sr}` into a map keyed by `stKey`; `mine(ev)` (core-processor.js:563) returns true for an `ev.st` note only when that key is held; the voice start already honours `ofsSec` / `durSec` (:597). Reverse via `Rd`.
  3. The page-side fallback for a missing key plays through `rtBuf`, which must not stretch on the main thread any more (the Worker/prerender above).
  4. `stKey` and `ev.st` must carry the new modes' parameters (so `sx`, `og`, ... are in the key, not only `mode/bpm/beats/grain`).
  Live OLDSCHOOL (7.3) bypasses 2-4 because it needs only the source buffer.
- **Offline render and stems:** `renderGetB` asks the same Worker for each unique (pad, st) before the pass starts (progress in the existing render UI, cancel with the render's own cancel), then `pass()` runs as today. Same key, same buffers as live, so a render equals what was heard; stems share the cache.

---

## 9. The native port: a section a native worker can start from

What the port needs so it renders the same modes (nothing here changes the web app):
1. **One pure function per engine, no streaming API in the host path:** `stretch_render(mode, params, in_L, in_R | null, sr, L, T, out_L, out_R)` in `native/core/src/stretch/` (C++17, no exceptions or RTTI on the audio path, like the rest). CLEAN = the **same `.cc` file** that `dsp/stretch/build.sh` compiles to wasm for the web (Signalsmith's headers at a pinned SHA in `native/core/third_party/PINS.md` and `fetch.sh`, with `prepare.sh` applying patches on a copy: same rules as eurorack and DaisySP). OLDSCHOOL, SLICED, GRANULAR, SPLICE, SMEAR, EXTREME are small portable C++ ported from the JS reference (the JS is the spec, as for `applyChop`).
2. **Where it is called:** on the control thread when a pad (or its sync params, or the tempo / TPO set) changes, and for the offline renderer before the first block (a scan of `SeqEvent::st` for unique keys), **never in the audio callback**. The key string is identical to the web's (`stKey`), so goldens can be shared. Cache = a byte-capped LRU in the renderer's pad table (`RenderPads`), next to `SeqChop`.
3. **Voice side:** `SongRenderer::dispatch` (which today counts `notesStretched` and plays unstretched) looks the key up and plays that buffer with `ofsSec` / `durSec` of the stretched timeline (the slice maths is already ported in `SeqChop`); a missing key keeps the counted fallback. Live OLDSCHOOL reader later in `VoicePool` (7.3).
4. **Goldens:**
   - GRANULAR / OLDSCHOOL classic: bit-exact against the page's `stretchData` text, pulled out of `index.html` by name (`tools/experiments/stretch/lib.mjs: pageStretch()` shows the method; `tools/seq-golden.mjs --chop` does the same for `applyChop`), using native's `rt/fdlibm.cpp` / `jsmath.cpp` for `cos`. The prototype proves the algorithm; its "classic == page LOOP, 0 samples differing" check becomes the golden.
   - New small modes: JS reference vs C++, exact where the maths allows (integer index maths, same interpolation), else max-abs bound 1e-6.
   - CLEAN: wasm build vs native build on a fixed input set (tonal loop, drum loop, noise) at ratios 0.5, 0.75, 1.0, 1.21, 1.5, 2.0 and a transpose: **tolerance golden** (max abs and RMS error bound, e.g. RMS error under -90 dB), not hashes: SIMD and FMA make float output differ between targets; native builds must not use `-ffast-math` differently from the wasm build (document the flags). The native tree already tolerates such differences (the "1.5 dB RMS at 384 kHz" note), so state the bound explicitly.
   - Loop seam: render a loop twice-concatenated and check the join (CLEAN and SMEAR need the loop padded circularly in and trimmed out, or the loop's ends fade).
5. **Order in `docs/port/WORKPLAN.md` terms:** a new unit after "chop": U-stretch-a (the pure functions for the small modes + goldens), U-stretch-b (CLEAN through the same wasm source), U-stretch-c (host cache + prerender scan + dispatch). Until then PICKUP's known gap ("STRETCH and LOOP play unstretched") stays, and any new mode degrades to the nearest classic (5.2).

---

## 10. Build plan in PRs (smallest first; each shippable alone)

Every PR also adds its punch-list items (owner's ears) to HANDOVER and bumps BUILD, changelog and `sw.js` cache as usual; PRs 1-2 can start now, the rest build on the sampler-pitch-sync branch once it has landed.

1. **Picker and the REPITCH sentence** (S, about 150 lines + tests). The SYNC row becomes the grouped picker; `I.sx` (absent = 0) added and carried through load / save / `stKey`; GRANULAR and OLDSCHOOL (classic) are the existing modes renamed; the SYNC TO LINES toast and info say "plays 17.7% slower, 3.4 semitones down". Tests: old-song JSON round trip and render hashes for sync 1/2/3 unchanged; the sentence's numbers. Punch list: the owner reads the sentence on three loops. Notices: none.
2. **OLDSCHOOL v2** (S-M, about 250 lines + the prototype's facts as tests). On the existing main-thread path (it is cheap): LOOP ms and division, RAMP, SMOOTH, PITCH; classic kept. Punch list: the seven WAV variants first (they exist), then "does LOOP 12 ms x 0 sound like the sampler you mean?", ramp shapes, SMOOTH at 0 / 5 / 25%. Notices: algorithms list entry.
3. **Stretch Worker, prerender, byte-capped cache, offline path** (M, about 400 lines). Output bit-identical for classic modes; no main-thread stretch during PLAY; progress and cancel; the slow-work log line proves it. Tests: bit-identity, cancel, fallback when the Worker is down, memory cap.
4. **Chop on the audio thread** (M, delicate: `make-core` regeneration, `core.test`). Real `applyChop` in `seq-core.js` with `loopSecs` / hits handed in via `buf`; fixes the open slices-on-Audio-thread bug on its own. Then `sbuf` + `mine()` for stretched notes. Punch list: Settings sequencer "Audio thread" vs "Page (old)" with SLICES 4 and with SYNC 2/3 sound the same.
5. **CLEAN (Signalsmith)** (L). 5a spike first (one afternoon, throwaway branch): build with wasi-sdk 25, measure wasm size, time for 10 s stereo at 0.5..2x on desktop and (by the owner's phone via a test page) on the Fold; render the owner's loops; go / no-go. 5b the module: `dsp/stretch/`, pinned fetch, notices (MIT x2), mode CLEAN with QUALITY, the "outside 0.75-1.5x" hint, tolerance goldens, loop-seam test. Needs from the owner: a few of his own loops (a drum break, a tonal loop, a full mix excerpt, a vocal) at the ratios he really uses, to listen to A against B (and optionally against Ableton's Complex on the same loop for a reference).
6. **SLICED** (M). Reuses `findHits` / `slicePoints`; TAIL policy (CUT / DECAY / LOOP / STRETCH via CLEAN once 5 exists); no clicks at joins tested.
7. **SMEAR, EXTREME, SPLICE** (S-M each, about 150-250 lines; can be three small PRs). Own code, no dependency, can go before 5 if the owner wants the "more algorithms" sooner. Notices: algorithm citations.
8. **Live OLDSCHOOL voice reader** (L). `oldschoolStep` shared with the render; `Rd`-style reader in `core-processor.js`; commands / LFO targets for `g`, ramp rate, pitch, smooth; the golden "live with constant params == rendered".
9. **Native stretch port** (L, on `native`, a separate worker; section 9).
10. **KEEP LENGTH and CLEAN transpose / formant** (M, after 3 and 5).

Rough total to the first complete set (1-7): about three weeks of worker time at the pace of recent PRs, dominated by 4 and 5.

Licence notices to add: `THIRD_PARTY_NOTICES.md` MIT entry for Signalsmith Stretch and Signalsmith Linear (copyright lines read from their LICENSE files at the pin) with the pinned SHAs, and, under "Algorithms used in our own code (no third-party code)", entries for OLDSCHOOL (cyclic stretch, original), SLICED, SPLICE (Verhelst and Roelands 1993), SMEAR (Flanagan and Golden 1966; Laroche and Dolson 1999; Roebel 2003), EXTREME (after Nasca's published description; no PaulStretch code, as for Extreme stretch). Only if B is added: MPL-2.0 text and a source statement. Only if C is added: a `rubberband/` folder with GPL text and `NOTICE.md`, listed beside LXR.

---

## 11. Open questions, each with my recommended answer

1. **Which engine is the default clean mode?** Signalsmith (A); Bungee (B) second. Ruling needed before PR 5a, which is only a spike, so no commitment until it has been heard.
2. **Spike first?** Yes: PR 5a measures wasm size, time and ratio behaviour on the owner's loops before the dependency enters the app. If A fails on his loops at his ratios, fall back to B or to our own SMEAR + SPLICE.
3. **Is GPL Rubber Band wanted as an optional module?** No for now: it would be a second LXR-style module with App Store and sale limits (its own licence page says it cannot go on the iOS / macOS App Stores). Revisit only if his ears prefer it over A and B, with the sale question (`docs/port/IF-SOLD.md`) answered first.
4. **Does the owner want new modes stored as `I.sx` beside the old `I.sync`?** Yes (old songs identical, older builds degrade to the nearest classic).
5. **Default mode when SYNC TO LINES is pressed.** CLEAN when the ratio is within 0.75-1.5x (once it exists), else REPITCH with its sentence; today (before CLEAN) REPITCH. Ask the owner; he may prefer REPITCH always, since he names it as the "pitch" option.
6. **OLDSCHOOL first: rendered or live?** Rendered (PR 2); live voice reader as PR 8, written so the render IS the reader.
7. **Slices on non-FWD ramps:** slice the output time, disable HITS there. OK?
8. **Tempo changes while playing (TPO commands):** prerender the tempos the song scan finds; anything else keeps the last buffer. Does the owner use TPO in songs with synced loops?
9. **Cache budget:** 64 MB phone / 256 MB desktop by bytes. Confirm these numbers against the Fold's memory behaviour before PR 3 lands.
10. **KEEP LENGTH:** after the Worker and CLEAN exist, only for CLEAN; not before.
11. **What the owner should supply:** 3-5 of his own loops (break, tonal, full mix, vocal) with their BPM and the tempos he fits them to; his picks among the seven OLDSCHOOL test WAVs; whether `GRAIN` in ms or in beats matters to him more.
12. **Where the sampler-pitch-sync branch's field names differ from section 1.7's guess:** the lead tells me when it is pushed; I adjust the key, the LINES row and the sentence's inputs only.
