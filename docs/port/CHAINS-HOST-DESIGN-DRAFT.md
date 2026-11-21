# CHAINS-HOST-DESIGN-DRAFT: playing the …chains through the native core

DRAFT for the lead, 21 Nov 2026, written on branch `n1/chains-design` (from `origin/native` at `3124bbf`). Nothing was compiled or run;
code was only read. The web reference is `beta` at `cdd94394` (the same commit as `native/tests/web-ref/BETA-COMMIT`).
Every claim carries a file:line; `W:` is the web (`index.html` or `chains/chains-worklet.js`), no prefix is `native/core/src/`.
Marks: **DECISION n** is a point the lead rules on (section 7 lists them with a recommendation). "Not read" is stated in section 8.

## 0. Four findings that change the plan (read these first)

1. **The …chains ENGINE is already native and complete; what is missing is the HOST.** U31 is merged (#227): `ChainsEngine`, `ChainsNode`, `ChainsLive`
   (`chains/chains.h:76`, `:187`, `:220`), all 145 module types of the …chains table (`chains/ctable.inc`, generated from the JS), goldens bit-exact against
   the real JS in Node (827 cases, `docs/port/U31-NOTES.md` "Results"). `PICKUP.md:23` still says "Not started: U31": the code and
   `PICKUP.md:341` ("U31 ... is MERGED (#227)") disagree with it; trust the code. So the module porting cost in section 2 is zero for every type.
2. **Pump effects do NOT reach a song render today, and neither do the shared-module (MODFX) track effects.** `FxBank::load` (`fx/fxbank.cpp:15-20`) calls
   `FxHost::planFromSong` (`fx/fxhost.cpp:174-178`), which keeps a slot only when `fx_type_of(type) >= 0` (`fx/fxhost.cpp:166`); that table holds 45 keys:
   tfx 0..33 and the JS cores 93..103 (`fx/fxtable.inc:50-100`, `fx/fxmap.cpp:20`), no `peq` / `pcomp` / `pverb` / `ptwang` and no MODFX key (`plate`,
   `spring`, `dimension`, `filter`, `glue`, `buscomp`, `gdelay`...). The web keeps them in the same slot list (`W:index.html:20483-20492`, `tfxChain`:
   `TFX[type].pump` and `.mod` branches), so they are silently dropped natively (as the comments in `fx/fxhost.h:22-23` and `include/thunder/render.h:12-14`,
   `live.h:408` (on `n1/u37-p0-live-h`) say). The pump hosting itself exists (slot kinds `kPumpT0 + pt`, `fx/fxhost.h:57`, `fx/fxpump.cpp`) and is exercised
   by tests only (`tests/test_pump.cpp:52`, `tests/rt_harness.cpp:675`); no song reader builds such an entry. Also `mstPump` songs (pump mastering as master
   slots, `W:index.html:20473`, `Song::mstPump` `song/song.cpp:218`) master through the OLD `S.mst` chain natively (`grep mstPump` finds no reader outside
   `song.cpp`). **Settled: pump mastering and pump / MODFX track effects are not in a song render today.**
3. **The songs we can count, in numbers** (read with `node` from `sounds/*.thunder` on `beta`; the census `native/tests/CENSUS.md:25-26,129-135` has the owner's
   `acid.thunder` too): chains modules are only `trackIn`, `chainOut`, `smixer` in all three songs (ELECTRO: 4 Track In + 1 Chain Out on track 4 + 1 Stereo mixer,
   `chainOn` true on 5 tracks; BETA-TESTING-ELECTRO: 7 + 1 + 1, 8 tracks; census total 8 / 2 / 2, 10 tracks with chain). Every patch is "tracks into a
   Stereo mixer into a Chain Out" = a bus. No cable has `fb`, no module has `sck` or text, none uses Sidechain, Resample or BYP. But the **track effects** of
   the same songs are mostly not hosted: ELECTRO has 25 slots of which 12 are hosted (13 not: 2 pump, 11 MODFX: glue, plate, pverb, autopan, gdelay,
   spring, dimension, filter, buscomp); BETA-TESTING has 34 of which 10 are hosted (14 pump, 10 MODFX). **A chains host alone does not make these songs
   sound like the page's.** See DECISION 2 and PR FX1 in section 6.
4. **U31's note "the host compensates 127 samples" (`U31-NOTES.md` "Conditions for the wiring" 4) would make native differ from the web.** The page's latency
   compensation counts pump slots only and never chains (`mix/pdc.h:6-7`; `W:index.html:21343` `L = pumpChainLatency(S.tfx[t])`), and the page's chains node adds no
   delay (a worklet runs in the same quantum). `ChainsLive`'s 127 samples exist only to make events sample-exact (`chains.h:204-207`). The mixer already runs in
   fixed 128-frame quanta (`render/render.h:33`, `render.cpp:93`) that are the engine's own block (`chains.h:36` `kBlock`), so the host can use `ChainsNode`
   directly with no latency (DECISION 1).

## 1. What a chain is in the app (twenty lines)

- **Data.** ONE patch per song: `S.chainPatch` = `{format:'chains.patch', modules, cables, transport, ...}` (`W:index.html:21576` new song, `:21699` load) and
  one flag per track `S.chainOn[t]` (`:21698`; native `Song::chainPatch` / `chainOn`, `song/song.h:80,86`). A patch holds modules `Track In k` / `Chain Out k`
  (`W:chains-worklet.js:401`, `:454`) that address tracks; the saved patch also carries a `transport` (ELECTRO: tempo 120 while the song is 131).
- **Which tracks run through chains:** `chainsFeed(t) = chainOn[t] && the patch has a Chain Out for t` (`W:index.html:21214-21220`; native `chains_feed`
  `mix/mix.cpp:106-109`). A CHN track with no Chain Out plays dry.
- **The audio graph.** One AudioWorklet node `thunder-chains` PER CHN TRACK (`W:index.html:21137` `chainsNode`; `routeTrack` `:23543`), every node loaded with the
  WHOLE patch and a `focus` = its track (runs that track's Chain Out and its ancestors). Inputs: 0 the track's sound, 1 sidechain bus, 2 taps (own sound of
  tracks a Sidechain In listens to, `:22865`), 3 sends (a block late, `:22902`), 4/5 bus feeds (`:22924-22958`); outputs: 0 return, 1 sends (`:21137-21160`).
- **Place in the strip.** `voices -> inp (TRIM) -> [chains -> wet] -> [track FX] -> ins -> g (VOL) -> cg -> pan -> dl -> master sum`
  (`W:index.html:23523-23529` `chnIn`, `:23543-23580`; native diagram `mix/mix.h:4-10`). Offline `wet = 1, dry = 0` at once; live the swap is a 20 ms ramp
  (`:23513` `XF`). Track FX come AFTER chains (`mix.cpp:339-344`).
- **Bus tracks.** `busPlan` (`W:index.html:21032`, native `bus_plan` `mix/mix.cpp:110-170`) says which tracks feed which bus track; a source is heard in the bus'
  node on inputs 4/5 AFTER its fader and pan (or from `pre` when its Track In says "before fader"), 16 pairs per merger, 32 sources at most
  (`:22924-22958`; native `MixFeed`, `mix/mix.h:75`, built at `mix.cpp:341`). A "pure" source leaves the master sum. Bus into bus (build 651) is in `bus_plan`
  (`mix.cpp:125-170`, `loopTo`, `srcAll`, `busAll`) and the mixer orders buses after their sources (`mix.cpp:227-236`).
- **A bus exists only while its track runs through chains:** `mix.cpp:229` (`!chn_[b].fn` skips it); with no chain host no bus exists and every track plays on its own
  (`mix/mix.h:20-27`, `render/render.cpp:17`).
- **`chc` events** (tracker `BYP XY`): `b1..b15` k-rate parameters of the track's node, value -1 own / 0 in / 1 bypassed / 2 flipped for Y rows, made at
  `W:index.html:22535-22538`, delivered at `:23272` (offline: `AudioParam.setValueAtTime`; live with the core: `core-processor.js:636-640`). Native produces them
  (`seq/seq.cpp:655-656`, `SEV_CHC`, p = 1..15) and the renderer counts them as ignored (`render/render.cpp:36`).
- **Tempo** reaches every node as a `transport` message: once at render start (`W:index.html:25867`), and per `tempo` event ROUNDED UP to the next 128-frame
  quantum in an offline render (`:23222-23232`: `Math.ceil(ev.t / q) * q`). Native `SEV_TEMPO` is ignored by the renderer (`render/players.cpp:25`).
- **Offline.** `renderOut -> pass(...)` (`W:index.html:25828-25870`): `keep = busClosure(keep)`; chains made only for `keep` tracks (`:25839`, `:25859-25862`);
  stems = the same pass with one track heard. Native stems: `SongRenderer::setStem` (`render/render.cpp:45-58`) and `heardMask()` (`render.h:138`).
- **Patch edits while playing:** `chnDiff` (`W:index.html:21111-21135`) turns an edit into ops: params become `param` ops (state kept, no reset), added/removed modules and
  cables become `add` / `remove` / `connect` / `disconnect`, an `st2` / `ti2` change reloads the whole patch. `busSync` re-wires and `planChanged` calls
  `seq.rebus()` (`:22918-22923`).

## 2. The module inventory

All 145 types of `chains/ctable.inc` (generated from `W:chains-worklet.js` by `native/tools/chains-gen.mjs`, `--check` fails on drift) have a native
implementation: **145 of 145 already native; porting cost is "reuse" for every row.** Columns: web line = the module's `const X = {` in `chains/chains-worklet.js`;
native = the registry row; in>out = ports; params = count; heap @48k = measured bytes of one module (`chains/cmem.inc`, 12 % margin, `test_chains_mem`);
latency = the def's own `latency:` in the JS (`W:3118`, `:3467`, `:5691`) or the pump plugin's; clock = the JS body reads `ctx.transport` (tempo / startN / bars). No
module reads the SEQUENCER directly; the only sequencer-driven things are `chc` (BYP), tempo and transport (section 4.6). State of every module lives in its
own `create()` object and is reset by a patch load (`U31-NOTES.md` deviation 7). **(used)** = appears in a repo song.
Counts: A 15 chains-only, B 13 pump, C 33 tfx-library effects, D 8 other waves-shared, E 76 MODFX = 145. Parameter names and ranges per type are in `chains/ctable.inc`
(`kCP_<type>`) and, with the pump ones, `chains/cpump.inc`.

DSP summaries are by family (the JS comment above each module is the full description):
A. chains-only: Track In / Chain Out (copy a track's slot of the host buffer in, add into bus k out: `W:401-418`, `:454-473`), VCF, VCA, Mixer (mono, up to 16
inputs) and Stereo mixer (up to 16 stereo pairs, level + balance each, `W:151-160`), Osc, White/pink/brown noise, Repeat (4 s beat repeat), Sidechain In / Comp /
Out (taps and sends), Vocoder (6 modes up to spectral), Morph (FFT, latency 1024), Resample (records bars at the song tempo, hands a take to the host).
B. the 13 pump creative effects as modules (`PUMP_CH` `W:5616`), each runs the plugin in `pump.wasm` / the native pump library; spare instance
swapped in when a block goes bad (`U31-NOTES.md` "Design points").
C. the track-effect set (rings ... tube) wrapped as modules over the tfx library (`waves/wmods.cpp` `EF(...)`); not bit-exact where the library is a strict build: 91 exact,
37 within -100 dB, 16 left out by name (`U31-NOTES.md` "Left out BY NAME").
D. LFO, ADSR, Random, Attenuverter, Scope, Expression, Pitch delay, Pitch reverb. E. MODFX: filters, dynamics (compressor, bus comp, glue, gate), delays, reverbs
(spring, plate, lite), stereo tools, CV / math / logic / clock / sequencer-like modules (`steps`, `euclid`, `clock`, `counter`...), voice-ish (autotune, vowel, rotary,
grain delay), generators (bytebeat, chaos, marbles, tides), spectral, stretch, convolve.

### A. chains-only (`chains/cmods.cpp`, `cmods2.cpp`)
| type | web line | native | in>out | params | heap @48k | latency | clock |
|---|---:|---|---|---:|---:|---|---|
| trackIn **(used)** | 401 | chains/cmods.cpp:338 | 0>2 | 2 | 64 KB | 0 |  |
| vocoder | 563 | chains/cmods.cpp:339 | 2>2 | 6 | 72 KB | 0 |  |
| morph | 5690 | chains/cmods.cpp:339 | 4>2 | 7 | 363 KB | 1024 |  |
| vcf | 77 | chains/cmods.cpp:338 | 3>2 | 3 | 67 KB | 0 |  |
| vca | 102 | chains/cmods.cpp:338 | 3>2 | 1 | 67 KB | 0 |  |
| mixer | 129 | chains/cmods.cpp:338 | 16>1 | 18 | 74 KB | 0 |  |
| smixer **(used)** | 151 | chains/cmods.cpp:338 | 32>2 | 34 | 84 KB | 0 |  |
| scin | 474 | chains/cmods.cpp:339 | 0>3 | 4 | 66 KB | 0 |  |
| sccomp | 499 | chains/cmods.cpp:339 | 3>2 | 7 | 67 KB | 0 |  |
| scout | 529 | chains/cmods.cpp:339 | 2>0 | 2 | 64 KB | 0 |  |
| osc | 312 | chains/cmods.cpp:338 | 2>1 | 4 | 66 KB | 0 |  |
| wnoise | 337 | chains/cmods.cpp:338 | 0>1 | 2 | 64 KB | 0 |  |
| repeat | 359 | chains/cmods.cpp:338 | 3>2 | 6 | 1.7 MB | 0 | tempo |
| resample | 419 | chains/cmods.cpp:339 | 2>2 | 4 | 26.4 MB | 0 | tempo |
| chainOut **(used)** | 454 | chains/cmods.cpp:338 | 2>0 | 2 | 64 KB | 0 |  |

### B. pump modules (`chains/cmods2.cpp` `makePump`; table `chains/cpump.inc`)
| type | web line | native | in>out | params | heap @48k | latency | clock |
|---|---:|---|---|---:|---:|---|---|
| ptube | 5616/pumpModule | chains/cmods2.cpp (pump) | 2>2 | 14 | 1.9 MB | plugin |  |
| psnap | 5616/pumpModule | chains/cmods2.cpp (pump) | 2>2 | 13 | 415 KB | plugin |  |
| pgate | 5616/pumpModule | chains/cmods2.cpp (pump) | 2>2 | 47 | 287 KB | plugin |  |
| pecho | 5616/pumpModule | chains/cmods2.cpp (pump) | 2>2 | 24 | 9.5 MB | plugin |  |
| pspring | 5616/pumpModule | chains/cmods2.cpp (pump) | 2>2 | 13 | 2.7 MB | plugin |  |
| pverb | 5616/pumpModule | chains/cmods2.cpp (pump) | 2>2 | 25 | 7.5 MB | plugin |  |
| pgrain | 5616/pumpModule | chains/cmods2.cpp (pump) | 2>2 | 24 | 16.4 MB | plugin |  |
| pvox | 5616/pumpModule | chains/cmods2.cpp (pump) | 2>2 | 23 | 3.0 MB | plugin |  |
| ptune | 5616/pumpModule | chains/cmods2.cpp (pump) | 2>2 | 32 | 1.9 MB | plugin |  |
| pcrush | 5616/pumpModule | chains/cmods2.cpp (pump) | 2>2 | 16 | 2.5 MB | plugin |  |
| pswirl | 5616/pumpModule | chains/cmods2.cpp (pump) | 2>2 | 13 | 1.7 MB | plugin |  |
| pstutter | 5616/pumpModule | chains/cmods2.cpp (pump) | 2>2 | 24 | 10.5 MB | plugin |  |
| ptwang | 5616/pumpModule | chains/cmods2.cpp (pump) | 2>2 | 19 | 12.2 MB | plugin |  |

### C. tfx-library effects as modules (`waves/wmods.cpp:806-818`)
| type | web line | native | in>out | params | heap @48k | latency | clock |
|---|---:|---|---|---:|---:|---|---|
| beads | 649 | waves/wmods.cpp:810 | 2>2 | 10 | 1.1 MB | 0 |  |
| flanger | 653 | waves/wmods.cpp:811 | 2>2 | 5 | 76 KB | 0 |  |
| tremolo | 656 | waves/wmods.cpp:811 | 2>2 | 3 | 68 KB | 0 |  |
| odrive | 659 | waves/wmods.cpp:811 | 2>2 | 4 | 68 KB | 0 |  |
| folder | 662 | waves/wmods.cpp:811 | 2>2 | 4 | 68 KB | 0 |  |
| autowah | 665 | waves/wmods.cpp:811 | 2>2 | 3 | 68 KB | 0 |  |
| pshift | 668 | waves/wmods.cpp:812 | 2>2 | 4 | 356 KB | 0 |  |
| ladder | 671 | waves/wmods.cpp:812 | 2>2 | 5 | 68 KB | 0 |  |
| xmod | 674 | waves/wmods.cpp:812 | 2>2 | 7 | 153 KB | 0 |  |
| space | 679 | waves/wmods.cpp:812 | 2>2 | 8 | 22.3 MB | 0 |  |
| lofi | 682 | waves/wmods.cpp:812 | 2>2 | 4 | 68 KB | 0 |  |
| drive | 685 | waves/wmods.cpp:813 | 2>2 | 1 | 68 KB | 0 |  |
| air | 688 | waves/wmods.cpp:813 | 2>2 | 2 | 68 KB | 0 |  |
| room | 691 | waves/wmods.cpp:813 | 2>2 | 4 | 1.2 MB | 0 |  |
| comb | 695 | waves/wmods.cpp:813 | 2>2 | 5 | 104 KB | 0 |  |
| transient | 698 | waves/wmods.cpp:813 | 2>2 | 4 | 68 KB | 0 |  |
| tgate | 701 | waves/wmods.cpp:814 | 2>2 | 5 | 68 KB | 0 | tempo |
| ott | 704 | waves/wmods.cpp:814 | 2>2 | 6 | 68 KB | 0 |  |
| fshift | 707 | waves/wmods.cpp:814 | 2>2 | 3 | 68 KB | 0 |  |
| vinyl | 710 | waves/wmods.cpp:814 | 2>2 | 4 | 69 KB | 0 |  |
| dub | 713 | waves/wmods.cpp:814 | 2>2 | 2 | 68 KB | 0 |  |
| cab | 716 | waves/wmods.cpp:815 | 2>2 | 6 | 71 KB | 0 |  |
| tube | 719 | waves/wmods.cpp:815 | 2>2 | 2 | 68 KB | 0 |  |
| eq | 216 | waves/wmods.cpp:809 | 2>2 | 8 | 70 KB | 0 |  |
| comp | 218 | waves/wmods.cpp:809 | 2>2 | 5 | 69 KB | 0 |  |
| saturation | 225 | waves/wmods.cpp:809 | 2>2 | 4 | 69 KB | 0 |  |
| tape | 227 | waves/wmods.cpp:809 | 2>2 | 6 | 79 KB | 0 |  |
| chorus | 229 | waves/wmods.cpp:810 | 2>2 | 5 | 89 KB | 0 |  |
| crush | 231 | waves/wmods.cpp:810 | 2>2 | 4 | 68 KB | 0 |  |
| delay | 223 | waves/wmods.cpp:809 | 2>2 | 6 | 1.6 MB | 0 |  |
| reverb | 220 | waves/wmods.cpp:809 | 2>2 | 5 | 1.2 MB | 0 |  |
| rings | 233 | waves/wmods.cpp:810 | 3>2 | 9 | 263 KB | 0 |  |
| clouds | 237 | waves/wmods.cpp:810 | 2>2 | 12 | 290 KB | 0 |  |

### D. other waves-shared modules (`waves/wmods.cpp:806-808`)
| type | web line | native | in>out | params | heap @48k | latency | clock |
|---|---:|---|---|---:|---:|---|---|
| scope | 1612 | waves/wmods.cpp:807 | 3>0 | 5 | 66 KB | 0 |  |
| expr | 1852 | waves/wmods.cpp:807 | 4>1 | 2 | 70 KB | 0 | tempo |
| pdelay | 2971 | waves/wmods.cpp:808 | 2>2 | 7 | 1.2 MB | 0 |  |
| preverb | 2983 | waves/wmods.cpp:808 | 2>2 | 7 | 1.2 MB | 0 |  |
| lfo | 242 | waves/wmods.cpp:807 | 1>1 | 4 | 64 KB | 0 | tempo |
| adsr | 260 | waves/wmods.cpp:807 | 1>1 | 5 | 64 KB | 0 |  |
| random | 280 | waves/wmods.cpp:807 | 1>1 | 3 | 64 KB | 0 |  |
| atten | 298 | waves/wmods.cpp:807 | 1>1 | 2 | 64 KB | 0 |  |

### E. MODFX modules (`modfx/*.cpp`, `kDefs[]` rows)
| type | web line | native | in>out | params | heap @48k | latency | clock |
|---|---:|---|---|---:|---:|---|---|
| envfol | 726 | modfx/mod_cv.cpp:554 | 1>2 | 5 | 66 KB | 0 |  |
| gateex | 743 | modfx/mod_cv.cpp:555 | 1>2 | 3 | 66 KB | 0 |  |
| pitchfol | 761 | modfx/mod_cv.cpp:578 | 1>2 | 5 | 84 KB | 0 |  |
| pitchtrk | 786 | modfx/mod_cv.cpp:579 | 1>5 | 7 | 74 KB | 0 |  |
| slew | 829 | modfx/mod_cv.cpp:556 | 1>1 | 2 | 64 KB | 0 |  |
| snh | 841 | modfx/mod_cv.cpp:557 | 2>2 | 3 | 67 KB | 0 |  |
| quant | 861 | modfx/mod_cv.cpp:558 | 1>1 | 3 | 64 KB | 0 |  |
| blades | 899 | modfx/mod_filters.cpp:122 | 4>2 | 5 | 68 KB | 0 |  |
| meta | 925 | modfx/mod_stereo.cpp:254 | 3>2 | 3 | 67 KB | 0 |  |
| phaser | 940 | modfx/mod_stereo.cpp:255 | 3>2 | 6 | 67 KB | 0 | tempo |
| width | 971 | modfx/mod_stereo.cpp:256 | 2>2 | 3 | 67 KB | 0 |  |
| pingpong | 990 | modfx/mod_stereo.cpp:257 | 2>2 | 6 | 1.2 MB | 0 | tempo |
| meter | 1045 | modfx/mod_stereo.cpp:258 | 2>2 | 1 | 67 KB | 0 |  |
| limiter | 1066 | modfx/mod_stereo.cpp:259 | 2>2 | 3 | 68 KB | 0 |  |
| utility | 1097 | modfx/mod_stereo.cpp:260 | 2>2 | 5 | 66 KB | 0 |  |
| ringmod | 1122 | modfx/mod_stereo.cpp:261 | 4>2 | 6 | 68 KB | 0 |  |
| spring | 1150 | modfx/mod_reverbs.cpp:206 | 2>2 | 6 | 151 KB | 0 |  |
| plate | 1200 | modfx/mod_reverbs.cpp:207 | 2>2 | 6 | 284 KB | 0 |  |
| lverb | 1274 | modfx/mod_reverbs.cpp:208 | 2>2 | 5 | 288 KB | 0 |  |
| ensemble | 1355 | modfx/mod_stereo.cpp:262 | 2>2 | 5 | 77 KB | 0 |  |
| math | 1386 | modfx/mod_cv.cpp:559 | 2>5 | 2 | 68 KB | 0 |  |
| logic | 1398 | modfx/mod_cv.cpp:560 | 2>4 | 1 | 68 KB | 0 |  |
| divider | 1962 | modfx/mod_cv.cpp:576 | 2>4 | 1 | 68 KB | 0 |  |
| euclid | 1984 | modfx/mod_cv.cpp:577 | 2>2 | 5 | 66 KB | 0 | tempo |
| rect | 1880 | modfx/mod_cv.cpp:570 | 1>4 | 0 | 67 KB | 0 |  |
| scale | 1892 | modfx/mod_cv.cpp:571 | 1>1 | 5 | 64 KB | 0 |  |
| compare | 1905 | modfx/mod_cv.cpp:572 | 2>2 | 2 | 67 KB | 0 |  |
| switch | 1918 | modfx/mod_cv.cpp:573 | 3>1 | 2 | 67 KB | 0 |  |
| xfade | 1937 | modfx/mod_cv.cpp:574 | 3>1 | 2 | 67 KB | 0 |  |
| const | 1952 | modfx/mod_cv.cpp:575 | 0>1 | 2 | 64 KB | 0 |  |
| steps | 1409 | modfx/mod_cv.cpp:561 | 2>2 | 12 | 67 KB | 0 | tempo |
| clock | 1432 | modfx/mod_cv.cpp:562 | 1>3 | 4 | 66 KB | 0 | tempo |
| clkmul | 1457 | modfx/mod_cv.cpp:563 | 2>1 | 2 | 66 KB | 0 |  |
| chance | 1479 | modfx/mod_cv.cpp:564 | 2>2 | 2 | 67 KB | 0 | tempo |
| counter | 1498 | modfx/mod_cv.cpp:565 | 2>2 | 2 | 67 KB | 0 | tempo |
| stepsw | 1516 | modfx/mod_cv.cpp:566 | 6>1 | 2 | 68 KB | 0 | tempo |
| burst | 1545 | modfx/mod_cv.cpp:567 | 1>2 | 4 | 66 KB | 0 |  |
| gatedly | 1566 | modfx/mod_cv.cpp:568 | 1>1 | 2 | 170 KB | 0 |  |
| flipflop | 1585 | modfx/mod_cv.cpp:569 | 3>2 | 0 | 67 KB | 0 | tempo |
| onepole | 1654 | modfx/mod_filters.cpp:123 | 1>2 | 1 | 66 KB | 0 |  |
| allpass | 1668 | modfx/mod_filters.cpp:124 | 1>1 | 4 | 87 KB | 0 |  |
| dcblock | 1692 | modfx/mod_filters.cpp:125 | 1>1 | 1 | 64 KB | 0 |  |
| midside | 1705 | modfx/mod_filters.cpp:126 | 2>2 | 1 | 67 KB | 0 |  |
| dline | 1723 | modfx/mod_delays.cpp:117 | 2>1 | 5 | 642 KB | 0 |  |
| dimension | 2009 | modfx/mod_delays.cpp:118 | 2>2 | 3 | 71 KB | 0 |  |
| bbd | 2035 | modfx/mod_delays.cpp:119 | 3>2 | 8 | 341 KB | 0 | tempo |
| harm | 2086 | modfx/mod_misc.cpp:318 | 2>2 | 10 | 172 KB | 0 |  |
| blipdrum | 2190 | modfx/mod_misc.cpp:319 | 8>2 | 77 | 539 KB | 0 | tempo |
| life | 2251 | modfx/mod_misc.cpp:320 | 2>10 | 15 | 71 KB | 0 | tempo |
| degrade | 2379 | modfx/mod_misc.cpp:322 | 2>2 | 7 | 67 KB | 0 |  |
| reson | 2365 | modfx/mod_misc.cpp:321 | 3>2 | 7 | 120 KB | 0 |  |
| flex | 2926 | modfx/mod_clk.cpp:86 | 3>2 | 3 | 5.8 MB | 0 |  |
| sgate | 2940 | modfx/mod_clk.cpp:87 | 3>2 | 7 | 67 KB | 0 |  |
| retrig | 2956 | modfx/mod_clk.cpp:88 | 3>2 | 6 | 1.3 MB | 0 | tempo |
| spectral | 3117 | modfx/mod_spectral.cpp:335 | 2>2 | 6 | 379 KB | SPX_N |  |
| stretch | 3276 | modfx/mod_spectral.cpp:335 | 2>2 | 5 | 7.9 MB | 0 | tempo |
| convolve | 3466 | modfx/mod_convolve.cpp:582 | 2>2 | 8 | 19.9 MB | 0 |  |
| autotune | 4057 | modfx/mod_voice.cpp:420 | 3>4 | 6 | 185 KB | 0 |  |
| vowel | 4162 | modfx/mod_voice.cpp:421 | 3>2 | 9 | 67 KB | 0 |  |
| rotary | 4220 | modfx/mod_voice.cpp:422 | 3>2 | 6 | 72 KB | 0 |  |
| gdelay | 4300 | modfx/mod_voice.cpp:423 | 2>2 | 13 | 2.3 MB | 0 | tempo |
| reverse | 4364 | modfx/mod_voice.cpp:424 | 2>2 | 8 | 2.4 MB | 0 | tempo |
| envfilt | 4424 | modfx/mod_voice.cpp:425 | 3>3 | 11 | 68 KB | 0 |  |
| bytebeat | 4928 | modfx/mod_gen.cpp:174 | 2>1 | 8 | 68 KB | 0 | tempo |
| chaos | 4961 | modfx/mod_gen.cpp:175 | 2>5 | 5 | 68 KB | 0 |  |
| marbles | 5096 | modfx/mod_mods.cpp:120 | 4>7 | 25 | 88 KB | 0 | tempo |
| tides | 5134 | modfx/mod_mods.cpp:121 | 3>4 | 11 | 76 KB | 0 | tempo |
| sdelay | 5186 | modfx/mod_basic.cpp:326 | 2>2 | 8 | 2.3 MB | 0 |  |
| filter | 5219 | modfx/mod_basic.cpp:327 | 2>2 | 9 | 66 KB | 0 | tempo |
| cut | 5266 | modfx/mod_basic.cpp:328 | 2>2 | 3 | 67 KB | 0 |  |
| compressor | 5301 | modfx/mod_basic.cpp:329 | 3>2 | 8 | 67 KB | 0 |  |
| ngate | 5418 | modfx/mod_basic.cpp:332 | 3>2 | 6 | 67 KB | 0 |  |
| autopan | 5448 | modfx/mod_basic.cpp:333 | 2>2 | 5 | 67 KB | 0 | tempo |
| pump | 5473 | modfx/mod_basic.cpp:334 | 2>2 | 4 | 67 KB | 0 | tempo |
| buscomp | 5351 | modfx/mod_basic.cpp:330 | 3>2 | 8 | 67 KB | 0 |  |
| glue | 5380 | modfx/mod_basic.cpp:331 | 3>2 | 12 | 67 KB | 0 |  |

**Used by the repo's songs: 3 types** (`trackIn`, `chainOut`, `smixer`), all family A, all with 3 golden rows each (`mod/<type>/default|stressed|switches-on`, `over/...`,
`nan/...`) and scenarios `bus/two-chains`, `node/track2-bus`, `focus/*` in `tests/golden/chains_cases.json`. Modules with heavy state worth a look: Resample 26.4 MB, Convolve
66 MiB and Pgrain 57 MiB at 48 kHz (`chains.h:53`, `U31-NOTES.md` memory budget); every CHN track holds its own copy of the whole patch (section 8, risk 1).

## 3. What exists natively and what is missing, exactly

| Piece | State | Where |
|---|---|---|
| Engine, node, live runner, 145 modules, goldens | DONE, merged | `chains/chains.h:76,187,220`; `U31-NOTES.md` |
| The hook `Mixer::setChain(t, fn, user)`, `MixInsertFn(user, track, L, R, frames, t0, feeds, nFeeds)` | DONE; callers: tests only | `mix/mix.h:75-77,111`; `mix/mix.cpp:222,339-343` |
| Bus plan, bus into bus, order, `routed_` / `busOn_`, PRE / POST feeds with PDC feed delays | DONE | `mix/mix.cpp:110-170,227-236,396-406`; `tests/test_busplan.cpp` |
| Bus plan in the renderer | set at construction, "no bus exists until a chains host" | `render/render.cpp:17-19` |
| `chc` events | produced by `Seq`; renderer counts them ignored | `seq/seq.cpp:655`; `render/render.cpp:36`; `render/players.cpp:26` |
| Tempo for chains | NOT routed (`SEV_TEMPO` only feeds the live bass nodes) | `render/players.cpp:25` |
| Stems and the chain hook rule | stem mask in place; a chains host must hook only heard tracks (a comment, no code) | `render/render.h:135-137`; `render.cpp:45-58` |
| PDC | takes `PdcSlot` for pump slots only; chains add none (the web's rule) | `mix/pdc.h:6-7,30`; `fx/fxbank.cpp:60-65` |
| …waves `Graph` / runner (U30) | DONE; U31 reuses it (`waves::PatchImpl`, `waves/graph.h`) | `chains.h:7-8`; `U31-NOTES.md` "What I changed in U30's code" |
| Pump slots in the SONG path | NOT HOOKED (finding 2) | `fx/fxhost.cpp:164-178` |
| Pump modules inside a chain | DONE (13 modules, spares, `service()`) | `chains/cmods2.cpp:188-264` |
| MODFX as TRACK/MASTER effect slots | NOT HOOKED (`fxhost.h:22-23`) | same |
| Taps / sends sources for Sidechain In / Out and for COMP's `sck` | engine side DONE (`setTaps`, `setSendIn`); NO caller anywhere in the host | `chains/cengine.cpp:251-253`; `grep setSources` finds only `fx/fxhost.h` |
| Live host | declaration only, on `n1/u37-p0-live-h`; `LiveBusMsg` publishes the bus plan, "a bus EXISTS only with a chains host" | `live/live_host.h` (that branch), `live.h:61,244,403` |
| Resample take wiring | engine side DONE (`popTake` / `releaseTake`); no host | `chains.h:119-121` |

**Latency.** The engine's own block is 128 (`chains.h:36`). `ChainsNode::process` (`chains/cengine.cpp:337-376`) has no latency; `ChainsLive` adds 127 samples
(`chains.h:214`, `clive.cpp:181`). A chain module's own latency (Morph 1024, Spectral) is never counted by the web's PDC (`mix/pdc.h:6-7`); native follows.
**`heardMask`:** `SongRenderer::heardMask()` = `mix_.audibleMask()` (`render.h:138`); with `setStem` it is the forced mask (`render.cpp:45-58`, `mix.h:106-108`).

## 4. The design

### 4.1 The class and who owns it

New `ChainsHost` in `chains/chost.h` + `chains/chost.cpp` (namespace `thunder`, not `thunder::chains`, next to `FxBank` in spirit). It owns one `chains::ChainsNode` per
CHN track (`chains.h:187`) and the cross-node wiring the page does with Web Audio objects: the bus maps, the send bus, the tap list. Owners:
`SongRenderer` holds `std::unique_ptr<ChainsHost> chains_` like `fx_` (`render.h:191`), `LiveHost` a member like `fxBank_` (`live_host.h`, `create()` comment). The
mixer hook is `Mixer::setChain(t, &ChainsHost::hook, this)` for every track in `chains_feed(...)` and (stems) in `heardMask()` only (`render.h:135-137`).
`hook` (a static, the `MixInsertFn`) does, for track t: build `NodeIn{inL = L, inR = R, feeds = pairs from the MixFeed list, nFeeds, taps, sends}`, call
`ChainsNode::process(in, out{L, R, sends}, frames)` with `out == in` (safe: `p1` copies the input into its own buffers first, `cengine.cpp:353`, and
writes the output last, `:373-375`); the node's own bus `track + 1` becomes the track's sound, exactly `chnIn`'s wet = 1.
A block in which the engine fails gives **silence** for that track (`process` returns false and clears the buses, `cengine.cpp:371`, as the worklet's catch does); a CHN track whose node is not yet
ready passes its input through DRY (the page keeps `dry = 1` until `ready`, `W:23523-23530` `_onReady`); a stat counts both.

### 4.2 Control side: building a plan from `chainPatch`

Style of `FxHost::plan` / `commit` (`fx/fxhost.cpp:143-162`, `:203-218`): everything that allocates happens in `ChainsHost::plan(...)` on the control thread and returns an
immutable `ChainsPlan*`; `commit(plan)` on the audio thread only swaps pointers and hands the old things back.

```
struct ChainsPlan {            // control builds, audio adopts, control frees (collect())
  uint32_t chainMask;          // chains_feed(chainPatch, chainOn): tracks that get a node
  BusPlan bus;                 // bus_plan(chainPatch, chainOn)  (mix.h:70)
  struct Trk { int track; chains::ChainsNode* fresh /*a node made for a track that had none*/; waves::PatchImpl* patch /*prepared, or null = keep*/;
               int8_t busMap[32]; int nBus; int8_t tapMap[32]; int nTap; bool sends; } trk[kSongNT]; int n;
  // returned by commit() for the control side to free: waves::PatchImpl* oldPatch[kSongNT]; ChainsNode* oldNode[kSongNT]
};
```
Build steps (control): (1) `bus_plan(patch, chainOn)` (already called by the renderer, `render.cpp:19`, and by LiveHost `song_publish`); (2) for every track in `chainMask`
that already has a node: `node.engine().preparePatch(doc)` (`chains.h:148`, const, allocates, refuses a hostile patch and leaves the playing one); for a track with no node:
`new (std::nothrow) ChainsNode(sr, t, opts)` then `engine().loadPatch(doc)` (not yet audible, so `loadPatch` is allowed); (3) the memory budget is
checked BEFORE allocation, per patch (`kDefaultMemoryBudget`, `chains.h:53`), see DECISION 7; (4) `busMap` from `bus.feeds[t][0..nFeeds)` (the page's
`busPlan().feeds.get(track).slice(0, 32)`, `W:index.html:21137` processorOptions), `tapMap` from the Sidechain In picks (`chnTapList`, `W:21016`), `sends` = "has a
Sidechain Out or a send Source" (`chnSends`, `W:21018`). The first call of `cdef` / `cinfo` / `modfx::find` allocates statics (`TODAY.md:175`): `plan()` primes them, so the
audio thread never meets a first call.
**Two existing-code limits the host runs into** (both small, in U31's files, for the lead to allow): `ChainsEngine::setBusMap` is `busMap_.clear(); push_back(...)`
(`cengine.cpp:149`): it allocates unless the vector has capacity; the audio side needs a version that writes into a reserved `int[32]` (1 line to reserve in the constructor,
or a `setBusMapFixed`). And `swapPatch` applies the saved patch's `transport` over the engine's (`cengine.cpp:219-229`: ELECTRO's saved tempo 120 would replace the song's 131), so
after every swap the host sets its own transport again in the same block (section 4.6).

### 4.3 Audio side: commit, and the old graph returned for freeing

`commit(plan)` runs at the top of a quantum (offline: before the first block; live: a `LiveCmdType::ChainsCommit`, like `FxCommit`, `live_host.h` `LiveCmdType`). For each `Trk`:
`fresh` node is stored in a slot (pointer store); a prepared `patch` goes through `engine().swapPatch(p)` which returns the old `PatchImpl*` (`chains.h:145`) into
`plan->oldPatch[]`; maps are copied into the node's fixed arrays; then `Mixer::setPlan(plan->bus)` and `Mixer::setChain(t, hook or null, this)` for each track (they
allocate nothing, `mix.h:28-29`; `rebuild()` is a loop over 32 tracks, `mix.cpp:227-236`). Retired nodes and patches ride back in the plan object; the control side frees them in
`collect()` (`chains_free_patch`, `chains.h:171`; the node destructor). **Order inside one commit:** nodes and maps first, `setPlan` last, so a bus never exists before its node.
Offline there is one commit before the first block and nothing is returned until the destructor.

### 4.4 How a bus track gets its inputs, and bus into bus

Nothing new: the mixer does it. Per quantum it walks `order_` (sources before buses, `mix.cpp:227-236,396-398`), runs `track(t)`, and for a bus calls the hook with
`MixFeed fd[]` = the sources' after-pan (or `pre`) pairs, already delayed by the PDC feed delay `fdl` / `fdlP` (`mix.cpp:341,399-405`). The host maps `fd[j]` to `NodeIn.feeds[2j], feeds[2j+1]`
in `busMap` order (`cengine.cpp:357-363`; the order is `plan.feeds[t]` ascending). A track that is both source and bus (bus into bus) works because the middle bus is processed
(hook, fader, pan) before its own bus (`mix.cpp:398-405`, tested by `tests/test_render_bus_chain.cpp`). A hook is set only where `busOn_` wants it: `mix.cpp:229`. A source is heard by
a bus with chains OFF the way the page does: `chainsFeed` false means no node and no bus.

### 4.5 Sources: taps, sends, sidechain (not in the first slice)

Sends: after every node of a quantum ran, the host sums each node's `out.sends[0..7]` into `sendNext_[8][128]`; the next quantum it passes `sendPrev_` to every node whose
patch `chnSends` as `NodeIn.sends` (`cengine.cpp:369-370` `setSendIn`) = the page's `sendBus -> sendDly(128)` (`W:22896-22910`), exactly one block late. Taps: a
Sidechain In reads "the track's own sound, mono, before its effects and fader" (`W:chains-worklet.js:474-480`) = the TRIM output `inp` (`W:22865-22879`). In the mixer
that value exists only inside `track(u)` (`mix.cpp:323-337`, in place), so a bus processed before `u` cannot see it. **One mixer change is needed:** `Mixer::setTapMask(uint32_t)` and
`const float* tapL(u)` = a copy of `L * trimLane` made for the masked tracks at the start of `render()` (the lane value is evaluated once more; trim is a plain gain,
`mix.cpp:334-336`). The same tap list serves `FxHost::setSources` (COMP's External sidechain, `fxhost.h` "SOURCE"), which also has no caller today. DECISION 6.

### 4.6 `chc`, tempo and transport

Events reach the host from `SongRenderer::dispatch` (new `case SEV_CHC` and `case SEV_TEMPO` beside `SEV_FXC`, `render.cpp:28-36`) as `ChainsHost::event(const SeqEvent&, double quantumStart)`.
They are stored in a fixed time-sorted array (256 entries, counted drops like `FxBank::kPend`, `fxbank.h`) and applied at the START of the quantum whose `t0 >= at`, where
`at = ceil(e.t / q) * q`, q = 128 / sr, in the web's own double arithmetic (`W:23224-23227`): that is the web's offline rule for tempo and the k-rate AudioParam rule for
`b1..b15` (`W:chains-worklet.js:6235` `automationRate:'k-rate'`). Applying means `engine().setBypassSwitch(n, v)` (`chains.h:112`, as `ChainsLive::apply` does) and
`engine().setTransportTempo(bpm)` / `setTransport(...)` (`chains.h:95-96`) before `process`; no allocation. DECISION 3 (the alternative is sample-exact cuts, not what the page does).
Transport: tempo = the song's `bpm` at the start (the Seq's own `seq.bpm()`, not the patch's saved 120), `lpb`, `ticks` from the song, `playing = true` from sample 0 offline (`W:25867`);
live, `playing` follows PLAY / STOP and `startN` counts starts (`chains.h:95`; `W:24561-24566` `chainsTempo`). After every `swapPatch` the host re-applies this transport (4.2).
The page's "Thunder Core" path delivers `chc` and tempo through `__tcOv` and a `tcAt` queue (`core-processor.js:157-176,263-270,636-651`); both land at block starts; the golden (section 5) settles whether
Chrome's k-rate sampling takes an event at `t == t0` in the same or the next block.

### 4.7 Block size, latency, PDC, real-time rules

Block: the hook is called with N = 128 from the renderer and LiveHost (`render.h:33`; `live_host.h` `kLiveQuantum`); `ChainsNode::process` also accepts any N (chunks of 128, `chains.h:194`).
Latency: **zero, report nothing** to `pdc_plan` (the web counts none: `mix/pdc.h:6-7`); `LiveHost`'s `latency_frames` is unchanged. DECISION 1 compares the alternatives.
Real-time rules: `hook` and `commit` allocate nothing, lock nothing, free nothing; the per-quantum cost is the engine's (`test_chains_alloc` already proves 0 allocator calls for EVERY module type
through the live runner, `U31-NOTES.md` "Results"; the host adds a copy of 128 floats per feed). Pump modules inside a chain need `ChainsEngine::serviceSpares()` / `ChainsLive::service()` at
least once per 128 frames or they go silent after the second bad block (`chains.h:236-237`); offline `inlineService` is true (the default, `chains.h:140`), live needs a control thread (DECISION 8).
Memory: ~64 KB of node buffers (`cengine.cpp:335`) + the patch per CHN track; seeds: set a fixed `EngineOptions.seed` per node so a render is bit-stable run to run (`waves.h:113`).

### 4.8 A patch edit during play

Two kinds, as in the page (`chnDiff`, `W:21111-21135`). (1) A parameter move: becomes `engine().setParam(id, name, v)` (allocation-free, `chains.h:93` comment) applied at the next quantum start; state kept; no click. (2) A structural
edit (add / remove module, cable, `st2` / `ti2`): the page patches the running graph in place (state of untouched modules kept); native has no in-place edit on the audio thread (those calls allocate), so the host does
`preparePatch` + `swapPatch` at a quantum start: **all module state is reset** (as the JS `loadPatch` does, `U31-NOTES.md` deviation 7), so reverb tails and delay lines of the whole chain restart and a click is likely.
Options in DECISION 4. The host ports `chnDiff` (control side, about 80 lines of C++ over `Json`) to choose between (1) and (2). A change of `chainOn` or of the bus plan re-runs `plan()` (`W:24534` `chainsPush`) and the Seq snapshot is republished by the bridge
(`U37-DESIGN-DRAFT.md` row 2, D-6; `Seq::buildBus` `seq/seq.cpp:318`). The live CHN-switch ramp (20 ms dry / wet, `W:23513-23530`) needs the dry copy of the input; DECISION 10.

### 4.9 The same host in the offline renderer and the live host

- **`SongRenderer`** (offline): `setChains(bool on)` after `setStem` and before `setEffects` / `setMasterChain` (the order rule is in `setStem`'s comment, `render.h:130-134`; `setEffects` is `render.h:141`): builds a plan, commits it, hooks the tracks. New
  `dispatch` cases (4.6). Stems: hook only on `heardMask()` tracks (`render.h:135-137`; `W:25839,25859-25862`). `SongRenderer::quantum` needs no change: the hook runs inside `mix_.render` (`render.cpp:116`).
  Bit-stable: fixed 128 quanta (`render.h:33`), no thread, fixed seeds, inline pump servicing.
- **`render_api` / `thunder-render`**: `thunder_render_opts` gains `chains` (0/1) beside `effects` (`render_api.cpp:139`, `include/thunder/render.h:91`) and `thunder-render --chains`; `createImpl` calls `setChains` between `setStem` and `setEffects` (`render_api.cpp:138-139`);
  `render-compare.sh` gains `--chains` (4 lines). `pdc` code (`render_api.cpp:141-147`) needs no change for chains.
- **`LiveHost`** (branch `n1/u37-p0-live-h`): calls that change: `thunder_live_create` gets flag `THUNDER_LIVE_F_CHAINS` (new bit 64; `live.h:112-117` lists 1..32) and `THUNDER_LIVE_CAP_CHAINS` (`live.h:120-125`);
  `thunder_live_song_publish` builds a `ChainsPlan` when `chainPatch` / `chainOn` changed and hands it over beside the `LiveBusMsg` (`live_host.h` `LiveBusMsg`; `live.h:244`); `thunder_live_play`'s `PlayPlan` (`live_host.h`) gets `ChainsPlan* chains`;
  a new `LiveCmdType::ChainsCommit` and a returned-plan ring like `fxBack_` (freed in `collect()`); `thunder_live_stats` gets the chains counters (refused patches, failed blocks, dropped events, pump swaps / dry);
  the note in `live.h:61` ("NOT IN THIS API: chains") is removed; `live.h:403-404` ("a bus EXISTS only with a chains host") becomes true. **Added:** `thunder_live_chain_edit(L, ops_json, n, at)` carrying the page's own `chnDiff` ops (DECISION 5; without it every slider move
  is a song publish that parses 8.6 to 15.5 times the song, `live.h:235`). **Not changed:** `thunder_live_pdc_set` (chains add no latency), `process`, the MIDI calls. `LiveHost::quantum` step 8 needs no change (the hook runs inside `mix_.render`); step 1 applies `ChainsCommit`; the quantum-start events (4.6) are applied right before step 8.

## 5. Equality: how to prove a chain sounds as the page's

What exists: (a) **node level, bit-exact against the real JS run in Node**: 972 cases in `tests/golden/chains_cases.json` (711 module rows, 203 scenarios incl. `bus/*`, `node/track2-bus`, `focus/*`, `byp/*`, `edit/*`, `send/*`, `src/*`, 54 malformed documents), made by `native/tools/chains-dump.mjs` which
loads the page's own `chains/chains-worklet.js` into a Node VM with a frozen clock (`chains-dump.mjs:1-30`); the committed Linux FNV hashes gate every platform; 16 library effect rows are "within a recorded dB" and left out by name. For the
three types the songs use: 3 default/stressed/switches-on rows each plus over-range and NaN rows. (b) **mixer level, Chrome-checked**: `native/tools/mix-webaudio-check.mjs` checks the strip's arithmetic against real Web Audio; `mix-dump.mjs` is its JS model; the bus plan has goldens `seq-edit-bus.json`, `bus-into-bus-native.json`. (c) **song level**: `render-compare.sh` renders the page's `pass()`
in headless Chrome with chains OFF (`render-compare.sh:1-20`) and gates at -80 dB; `tests/test_render_bus_chain.cpp:1-12` uses a pass-through STAND-IN for the chains node and an analytic reference, and says plainly "no web-rendered golden of the chain is committed".

What a CHAIN golden needs and the tolerance I propose:
1. **Host golden, bit-exact (the main gate).** A Node tool `native/tools/chains-song-dump.mjs` (new) that composes what is already trusted: the real `thunder-chains` worklet class from `chains-worklet.js` (as `chains-dump.mjs` loads it), one instance per CHN track given the song's
   patch, `busMap`, `tapMap` and a `transport` message, fed by the mixer model of `mix-dump.mjs` (strip arithmetic) with the page's wiring rules coded from `W:routeTrack` / `busSync` (feed pairs, pure sources leave the master, PRE feeds, wet = 1), over the 128-frame quanta, with `chc` / tempo events delivered by the page's rule
   (4.6). It writes cases (`ELECTRO`-style bus, `BETA-TESTING`-style, bus into bus, a BYP row, a tempo change, a mono track, mute / solo, a stem) and the FNV of the master output; native renders the same songs through `SongRenderer::setChains` and must hash identically. Tolerance: **exact** for songs whose modules are generated cores or pure float arithmetic
   (every module of the repo's songs); the library effects keep the existing "-100 dB / RMS" rule (`U31-NOTES.md` rule), the pump modules have no native hash (system libm).
2. **Chrome check, -80 dB (the final cross-check).** `song-ref.mjs --chains` (extend: switch chains ON for the reference, off otherwise as today, `render-compare.sh:1-20`) and `render-compare.sh <song> --chains`; reference summary committed like `golden/song-ref/ELECTRO.json`. Chrome's render differs run to run for songs with random modules
   (`render-compare.sh:6`), so this is a -80 dB verdict, not a hash. It settles the open k-rate question of 4.6. I could not run a browser (section 8).
3. **Live equals offline** for the host: host block sizes 1 / 64 / 128 / 480 / 4096 give the same samples (the `live_block_sizes` test of D-10, `U37-DESIGN-DRAFT.md`), plus the quantum-boundary event test.
4. **Mutations**: `native/tools/mutate-u31.sh`'s style (a seeded fault must fail a test): drop the feed pair order, apply `chc` one quantum early, skip the transport re-apply after a swap, hook an un-heard track in a stem.

## 6. Work breakdown (PRs in order, each for one Sonnet builder)

**FIRST: PR C1, "the bus slice": `ChainsHost` offline, no live part.** It makes a song of the owner's kind (tracks into a Stereo mixer into a Chain Out; module types **`trackIn`, `chainOut`, `smixer`**, the only chains types in all three songs counted)
play natively through `SongRenderer` and `thunder-render --chains`, bit-exact against C1g's golden. It is generic (any of the 145 types works because the engine runs the patch), but its tests cover those three plus one BYP row and one tempo change. **Not enough by itself for these songs to equal the page's: their track effects need FX1.**

| PR | What | Lines (est.) | Done when | Parallel with |
|---|---|---|---|---|
| **C0** (lead) | Rulings on section 7; `ChainsEngine::setBusMap` capacity (1 line, `cengine.cpp:149`); allow the Mixer tap API | 30 | the lead's rulings in this file | - |
| **C1** ChainsHost offline | `chains/chost.{h,cpp}` (plan, commit, hook, events, transport), `SongRenderer::setChains` + dispatch cases, `render_api` flag, `thunder-render --chains`, CMake | code ~650, tests ~900 (`test_chains_host.cpp`, `test_render_chains.cpp`) | feeds equal a direct `ChainsNode` run bit for bit; ELECTRO's patch renders; bus into bus, mono track, mute / solo, stem (hook only on heard tracks), CHC at the quantum rule, tempo; `chains_alloc`-style audit of the hook; ASan / UBSan clean | C1g, FX1 |
| **C1g** golden tool | `chains-song-dump.mjs`, committed goldens, `render-compare.sh --chains`, `song-ref.mjs --chains` | ~450 JS / sh | the golden hashes are produced from the page's own worklet; C1's render matches them (the first run may show the k-rate rule, DECISION 3) | C1 |
| **C2** live plan / commit | `ChainsPlan` retire path, `chnDiff` port (`chains_diff`), `LiveCmdType::ChainsCommit`, `F_CHAINS` / `CAP_CHAINS`, stats, `thunder_live_chain_edit`, collect(); TSan test with a patch swap every N quanta | ~500 + tests ~600 | `live_chains_swap`, `live_block_sizes` with chains, allocation guard through `LiveHost::process`, TSan clean, the old graph freed by `collect()` | needs U37 P2/P3 shape; after C1 |
| **C3** sources | `Mixer::setTapMask` / `tapL`, send bus in the host, `FxHost::setSources` feed (COMP `sck`), `setTapMap` | ~350 + tests ~450 | `node/sends-in-out`-style golden through the host; Sidechain In listening to a track | after C1 |
| **C4** live niceties | 20 ms CHN switch ramp, Resample take handling (drop and count, or hand to the pads API), the pump `service()` driver | ~300 + tests ~300 | per decisions 8, 10, 11 | after C2 |
| **FX1** (not chains; same critical path, DECISION 2) | `FxHost::entryFromSlot` for pump slots (`pt`, `pv` from the pump parameter symbols, `bi`, `cr`, `clk`, `sck`) and for MODFX slot kinds (an `Fx` adapter like `ClkFx` over `modfx::Def`), `FxBank::pdcSlots` gets the pump latency, `mstPump` songs master through the pump slots | ~900 + tests ~700 | ELECTRO / BETA-TESTING `--fx` renders within -80 dB of Chrome with ALL their slots | C1 |

**The lead reads line by line:** `chost.cpp` `commit` / retire / mailbox and every place the audio thread touches a pointer (4.3), the event apply (4.6), `SongRenderer::setChains` and the dispatch cases, the one-line `setBusMap` change; in C2 the
`LiveHost` command ring and what `collect()` frees; in C3 the mixer tap copy (it is in the audio path of every song). Everything else is test code and the Node tool.

## 7. OPEN DECISIONS for the lead (most important first)

**DECISION 1. How the host calls the engine: zero-latency `ChainsNode` per 128-frame quantum, or `ChainsLive`.** (a) `ChainsNode` directly, events at quantum starts, own small patch mailbox (about 150 lines of host): zero latency, equals the page, nothing in U31 changes. (b) `ChainsLive`
as is: ready queue / mailbox / `service()`, but 127 samples of latency that the page does not have (0 in `pdc.h:6-7`): chain tracks would be 2.6 ms late against the rest unless the PDC plan is changed away from the page's. (c) add a no-latency quantum mode to `ChainsLive`: reuses its mailbox, but edits the reviewed U31 live code.
**Recommend (a).**

**DECISION 2. Are pump and MODFX track effects (FX1) in this effort?** (a) yes, in parallel with C1, because the owner's songs cannot sound like the page's without them (finding 3: 13 of 25 and 24 of 34 slots are not hosted) and the first Android test would play half the effects; (b) later: chains first, accept that songs "play"
but differ. (c) owner decides after hearing C1's render. **Recommend (a)**, FX1 started at once as its own unit; it does not touch the chains host.

**DECISION 3. Event timing for `chc` and tempo.** (a) quantum-ceil like the page (4.6) for equality; (b) sample-exact cuts like `ChainsLive` (a BYP crossfade and a tempo-sync module differ by up to 127 samples from the page). **Recommend (a)**, with the Chrome golden settling whether `t == t0` lands in the same or the next block.

**DECISION 4. A structural patch edit during play.** (a) whole-patch swap, state reset, accept a click and cut tails; (b) as (a) plus a one-quantum crossfade between the old and new node outputs (the old node is kept for one more quantum: +1 node of CPU for 128 frames); (c) carry state of unchanged modules over (U31's own open choice, `U31-NOTES.md` "Choices for the lead"; large). **Recommend (a) now, (b) in C4 if the owner hears it.**

**DECISION 5. How slider moves reach the host.** (a) through a whole-song publish (the bridge turns any op on `chainPatch` into a publish, `U37-DESIGN-DRAFT.md` D-6): parse cost per tick; (b) the new `thunder_live_chain_edit` taking the page's `chnDiff` ops; (c) both (publish for structure, edit for params). **Recommend (c)**.

**DECISION 6. Sidechain taps and sends in C1 or C3?** None of the three known songs uses them (`sck` absent, no `scout`). **Recommend C3, after C1**, and the mixer tap API serves FxHost's `sck` too.

**DECISION 7. Memory and CPU: one full patch per CHN track (as the page) or one shared graph.** Native memory per track = patch bytes from `cmem.inc` plus 64 KB buffers; the budget is per patch (1 GiB, up to 6 in flight, `U31-NOTES.md` "Choices for the lead"). (a) per track, one TOTAL budget per host (U31's own proposal, 1.5 GiB); (b) per track, per-patch budget as is; (c) shared engine (no: the page and the goldens are per node). **Recommend (a)** and a phone limit lower than 1.5 GiB (owner's device).

**DECISION 8. Who calls pump `service()` live.** (a) the shell's control thread each audio callback (documented rule: at least every 2.67 ms or a pump module in a chain goes silent after its second bad block); (b) a LiveHost-owned service thread; (c) refuse patches with pump modules live. Pump modules are in none of the known songs. **Recommend (a)** documented, (c) only if a shell cannot.

**DECISION 9. Transport at PLAY live.** `startN` / `playing` / lpb / ticks: take the song's values at PLAY and the `tempo` events afterwards (4.6). **Recommend as written; no alternative that equals the page.**

**DECISION 10. The live 20 ms CHN-switch ramp.** (a) implement in C4 (dry copy of the node input, linear 20 ms, `W:23513-23530`); (b) hard switch with a click. **Recommend (a)** in C4; offline needs none.

**DECISION 11. Resample takes** (the pad the page makes, `W:21190-21210`). (a) drop and count in the first versions; (b) hand to the host's pads API. **Recommend (a)** (no known song uses Resample).

### For the owner (plain words; only he can answer)
1. Please run the census on your whole songs folder (`node native/tools/census.mjs <folder>`; the tool only reads the files) or tell us which chain modules you use. We have counted three songs: all three use only Track In, Stereo mixer and Chain Out.
2. Do any of your songs use a Sidechain (Sidechain In / Out / Comp), Resample, or the tracker's BYP command in the chains? (If not, those come later.)
3. Do you edit the chains' cables while a song plays on the phone? If you do, a short click or cut tail when you add or remove a module is expected in the first version; is that fine? (Moving a knob does not do that.)
4. For the first Android test: should the test song already have ALL its track effects (pump, plate, spring, dimension...) working, or is "chains first, effects after" fine?
5. Which three songs do you want to hear first, and how many tracks have CHN on in the biggest one? (Each CHN track runs its own copy of the patch, as the page does; I could not measure the phone's CPU.)
6. Do you use the pump mastering (the five pump slots on the master) in the songs you care about? Songs saved with it use it natively only after FX1.

## 8. Risks, and what I could not find or did not read

**Risks.**
1. **CPU and memory: one copy of the whole patch per CHN track.** `chains.h:7-9` says every module object is made at preparation; the node runs only the focus component but holds all modules. The page does the same, but on a desktop; the Fold's budget is unmeasured. A patch with Convolve (66 MiB) on 16 tracks would be 1 GiB.
2. **The k-rate rule for `chc` / tempo** is an inference from `W:23224-23227` and the descriptor `W:chains-worklet.js:6235`, not a measurement. If Chrome takes `t == t0` in the same block, `ceil` is right; if it is a block off, `at` moves by one quantum.
3. **Pump modules in a chain live** depend on a servicing cadence the shell must keep (DECISION 8); a late `service()` silences a module.
4. **The page's sends / taps rules** were read in the page, not tested natively (C3).
5. **No Chrome reference of a chain exists** (`tests/test_render_bus_chain.cpp:12`); until C1g and the Chrome cross-check run, equality of the glue rests on reading the page.
6. **`render.h:5` still says "No effects, chains ..."** although `setEffects` exists (`render.cpp:67`): stale comment; the same header at `:135` is correct.

**Docs and code disagree** (trusting the code): `include/thunder/render.h:13` lists effect ids "0..33 and 93..95" but `fx/fxtable.inc` also holds 96..103 (JS cores of U16 / U17); `PICKUP.md:23` (U31 "not started", it is merged); `U31-NOTES.md` "Conditions for the wiring" 4 ("report 127 samples to the PDC", the page counts none); `render.h:5`.

**Not found:** a web-rendered song golden with chains on; any native caller of `Mixer::setChain` outside tests; any native caller of `ChainsEngine::setTaps` / `setSendIn` / `FxHost::setSources`; a native reader of `mstPump`; the owner's own songs (only `acid.thunder` through the census numbers, not read by me).
**Not read, or only partly:** `chains/index.html` (the editor) not at all; `W:chains-worklet.js` `ThunderChains` / `ChainsEngine` (`:5784-6326`) read through `cengine.cpp` and `chains.h`, not line by line; `chains/clive.cpp` and `cmods*.cpp` skimmed; `fxpump.cpp` read only where it decides song reachability (the pump hosting itself was not reviewed); `docs/port/LIVE-ROUTE-DESIGN.md`, `reviews/prewire-19nov.md` and `WIRING-DOSSIER.md` not read (only grep'd through `U37-DESIGN-DRAFT.md`, which I read for the chain lines and `live_host.h` / `live.h`, not for ruling D1 in full); `docs/port/U37-DESIGN-DRAFT.md` section 12 only via grep.
`TODAY.md` was read for U30 only. No code was compiled or run except `node` one-liners over the repo's songs and the generated tables. I could not check whether a Chrome / Playwright is available in this sandbox, so no Chrome render was attempted. Line counts in section 6 are estimates from the size of comparable units (`FxHost` 381 lines + `FxBank` 66; `ChainsLive` 223).

## 9. The lead's rulings (21 Nov, 14:00; Fable, lead of 21 Nov)

Checked by the lead before ruling: `native/core/src/chains/` exists with the engine (`cengine.cpp`, `clive.cpp`, `cmods*.cpp`) and eight chains tests; `Mixer::setChain` has no caller outside `mix/`. Sections 4 and 5 to 7 read in full; the module tables (section 2) were not checked row by row.

All eleven decisions are ruled AS RECOMMENDED: D1 (a) `ChainsNode` per 128-frame quantum, zero latency, nothing reported to PDC; D2 (a) FX1 (pump and MODFX track effects, pump mastering in the song path) is built in parallel as its own unit: it is on the critical path with the chains host; D3 (a) quantum-ceil, the Chrome golden settles the `t == t0` edge; D4 (a) now, (b) in C4 if the owner hears the click; D5 (c); D6 C3; D7 (a) one total budget per host, with a phone limit set from a measurement on the Fold; D8 (a); D9 as written; D10 (a) in C4; D11 (a).

C0: the one-line capacity change to `ChainsEngine::setBusMap` and the mixer tap API (`setTapMask` / `tapL`, C3) are allowed; the lead reads both.

Order: C1g (the Node golden tool: no compiling) starts now; C1 and FX1 start as compiling slots free (two at most: U37 P1 round 2 and the MIDI no-recall PR held them at 14:00); C2 follows U37 P2. Stale line found: PICKUP.md's "Where the port stands" still lists U31 as not started; U31 (the …chains engine) is MERGED (#227). Only the host is missing.

For the owner, kept short: questions 3 and 4 are decided by the lead (a click on a structural chain edit during play is accepted in the first version; chains and the missing effects are built in parallel). Still wanted from him: three songs for the first test (the files, so the census can be run on them) and whether he uses sidechain, Resample or pump mastering.
