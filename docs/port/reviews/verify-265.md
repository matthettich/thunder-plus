# Verification of PR #265 (n1/live-route: the live-engine route, `LivePool`)

Independent verifier, 20 Nov 2026. Code read and measured at `abfddd81` (the PR head when I started); the PR has since gained two commits (`ff719102`, `fbd4c5e7`: a portable popcount in `set_pad_live`, nothing else: `git diff abfddd81 origin/n1/live-route` is one line of `render_api.cpp`). Baseline `origin/native` = `26780bee`. Everything below was run by me, from my own drivers (kept outside the repo); nothing of the builder's notes was taken on trust.

**STATUS: items 1 and 2 done; 3 to 7 in progress (this file is updated as I go).**

## Verdict (so far): MERGE AFTER FIXES

* F1 must fix: the six new stats fields are NOT appended: they sit before `stem` / `heard_mask`, so an old caller reads `stem` and `heard_mask` wrong (measured, below).
* F2 should fix: a note-off can still be dropped (stuck note): `LiveShell::enqueue` drops a NoteOff when its 256 queue is full; ruling C2 said it never would.
* F3 should fix: D7 (a slide cut by the next note glides on) is NOT implemented: the pool holds the FROM pitch (the core's behaviour, the opposite of the ruling); nothing tests it.
* F4 should fix: NaN reaches the cores through `bendMul`, `mul`, `vibSpd` (and `gain` 1e300 gives inf): the pool checks only rate / gain / pan / slide.
* F5 should fix: the L10 gates are thinner than reported: 27 of the 54 skipped JS sequences ARE expressible by the sequencer (tempo, wob, bends, rate by `mul`), and 9 of my 24 own mutants survive (sameCol, the forced-off rule, tempo, D6 wob, the song-end release time, the 4096 quiet threshold).
* F6 note: L2 as ruled (bind at the first note, re-bind the oldest, `liveNoNode` when all busy) is not what was built: one node per masked track, made up front; no idle skip (a vox pad on all 33 tracks renders silence at ~3x real time, loaded machine).
* F7 note: CI on `abfddd81`: Windows MSVC failed (`__builtin_popcountll`: fixed by the two later commits); the Linux jobs were cancelled by the next push.

## 1. Nothing changes for songs without live pads: CONFIRMED (and one ABI failure, F1)

Driver `ident.cpp` (random songs: 1 to 8 tracks, 1 to 4 instruments, 32-row phrases, notes at 35 %, three random commands (any of the 81) at 22 % a slot, random vol, 8 pads of random length / rate / mono-stereo, choke groups, `syn` / `rel`, 30 % of pads routed LIVE but never declared with `set_pad_live`, slice / chop pads, 44.1 or 48 kHz, effects / PDC off auto on, master chain, page timing, a stem in 1 of 6, a SoundFont pad (FluidR3Mono_GM.sf3) in 1 of 12, `use_frames` or unknown length) plus ELECTRO.thunder in four option sets (effects, master chain, PDC auto / on, a stem) with 64 synthetic pads, 8 of 13 notes on live-routed pads.

* 300 random songs + 4 ELECTRO option sets, each at host blocks 1 / 128 / 480 / 4096 (ELECTRO at 128 / 480 / 4096): **1212 renders** (14 947 notes, 2 611 of them notes of an undeclared live-routed pad, in 356 renders; 20 renders played a SoundFont; 167 stems).
* Baseline `origin/native` library with its own header (A) against the PR library with the PR header (B): **1212 of 1212 identical**: SHA of the output samples, the length, and every named field of the stats (35 fields, hashed by name), and the six new fields are all 0 in all 1212. `notes_live` counts the undeclared live pad's notes exactly as before and they stay silent.
* **F1 (must fix).** The same driver compiled against the OLD header and linked to the PR library (C: what a host built against `native` before this PR is): the output samples are identical in 1212 of 1212, but the **stats differ in 1212 of 1212**: `heard_mask` reads 0 instead of the real mask (1045 renders) and `stem` reads the wrong number in the 167 stem renders. Cause: `render.h` / `thunder/render.h` put `notes_live_played .. live_failed` after `seq_chop_no_hits` and before the U34b fields (`stem`, `heard_mask`); the header comment says "appended". They must go after `heard_mask`. The opts struct is untouched (nothing to append); an old caller's `size`-gated stats copy works only if the new fields are last.
* The six counters and `set_pad_live` / `clear_pad_live` are new entry points: an old host never calls them.

## 2. The rulings

Code: `live_pool.cpp` (LP), `players.cpp` (PL), `render.cpp` (R), `live_shell.h` (LS). Tests: `test_live_pool` (T-pool), `_tracklist` (T-list), `_decisions` (T-dec), `_jsparity` (T-jsp).

| Ruling | Code | Test | Verdict |
|---|---|---|---|
| L1 one pool, adds into mixer inputs | LP runNode :286-319, PL :14 | T-pool seam | CONFIRMED |
| L2 nodes made in set_pad_live, none at a note | LP addPad :34-63 | rt_harness, test_live_reset | CONFIRMED |
| L2 bind = reset+seek, re-bind oldest ended, all busy: silent + liveNoNode + takeover | not built: one node per masked track made up front (:54-59); `reset()` never called by the pool, only `seek` at the first block (:291); liveNoNode only for an off-mask track / missing vox node (:98,:113); takeover after noNode is there (PL :44) | T-dec no_node | PARTIAL (F6) |
| L3 U26 timing, any host block | LS process | T-pool testApi blocks 1/128/480/4096 | CONFIRMED |
| L4 run ahead by engine latency, same sample as sampler | LP :291-295 | T-pool testLate, testSeam | CONFIRMED, measured below |
| L5 note-off always wins, timeline | LP book :79-90 | none (mutant X08 survives) | PARTIAL |
| L5/C2 LiveShell::enqueue never drops a NoteOff | LS enqueue :127 drops anything at 256 | none | WRONG (F2) |
| L6 one voice list per track | PL :19-23,:38-44,:79-90 | T-list | CONFIRMED |
| L7 Players, setPads (P1) | players.h :28-33 | identity run | CONFIRMED |
| L8 stats appended | render_api.cpp :356, R :115 | T-pool testApi | WRONG placement (F1); shell drops / rejects never counted |
| L10 (a) seam | | T-pool testSeam | CONFIRMED |
| L10 (b) JS hashes | | T-jsp | PARTIAL: 68 of 122 (F5) |
| L10 (c) mutants | | | PARTIAL: 9 of 24 mine survive (F5) |
| L10 (d) rt_harness | | rt_harness | see item 4 |
| L10 (f) decision log | | T-dec | CONFIRMED (14 cases, 23 nodes) |
| C1 reset()/seek() no allocation | bass_core / vox_core / xeng_live / LS | test_live_reset | CONFIRMED (pool uses seek only) |
| C2 timeline + cancelFrom on every NoteOn and kill | LP :133, :265-271 | T-dec | CONFIRMED |
| C3/D4 note-on at a booked off's sample takes it back | LP :75-78 | T-dec bass_same_sample; X07 killed | CONFIRMED |
| C4 ended = quiet 4096 or 4 s; held voices released at song end | LP :262-263, :297-303; R :100 | T-pool testSongOver; X14, X20 survive | CONFIRMED (render ends: bass 0.64 s, vox 0.74, acid 0.49, mach 0.94 for a 0.5 s song with a held note), thresholds unpinned |
| C5 vox = 5 nodes a track | LP :58 | measured 235 KB a track | CONFIRMED |
| C6 SEV_TEMPO forwarded | PL :23, LP :206 | none (X10-X12 survive) | PARTIAL |
| C7/D7 slide cut by next note glides on | LP :136 `rate.hold` (Auto::hold keeps the FROM value) | none | WRONG (F3): rate at 0.59 s = 1.0, page gives 1.45 |
| D6 mod with wob moves bass wobble | LP :188 | none (X17 survives) | PARTIAL |
| P1 | | | CONFIRMED |
| P2 mono branch for bass/acid/mach even on poly; polyVox takes nothing | PL :43, LP :100-102 | T-list; mutants killed | CONFIRMED |
| P3 live notes run no cap loop but can be shifted out | PL :82-87 | T-list; X13, M_cap_order killed | CONFIRMED |
| P4 tempo with each note and from SEV_TEMPO in time order | LP :138, :206 | none | PARTIAL |
| P5 rate lane is an Auto lane, arp never dropped | LP :135, :158-167 | T-dec (3-point arp) + mine | CONFIRMED: 97-point arp x3 back to back, 0 of 291 points wrong, 0 dropped |
| P6 (a) arp copied at the event | LP pitchMods uses it at once | | CONFIRMED |
| P6 (b) wob in mod | see D6 | | PARTIAL |
| P6 (c) SEV_ENG | not in this PR (documented) | | NOT IN SCOPE |
| P6 (d) one seconds-to-sample rule | sampletime.h | T-pool; X15, X16 killed | CONFIRMED |

Same-sample start (L4 / P6d): impulse sampler pad on track 1, live pad on track 0, same ev.t, 4 times each at 44.1 / 48 / 96 kHz. Sampler starts at T always. Live: vox and mach at T+0; bass and acid at T+1, which equals each core's own first non-zero sample (`BassCore` / `AcidCore` k0 = 1, vox / mach k0 = 0, measured directly): the core's first sample lands exactly on T. CONFIRMED.

L5 answer: the builder's "latest booked event is dropped when nothing is at or after the off" is a weaker addition, but the pool timeline does discard everything at or after the off (:84). The real hole is one level down (F2): 255 wob events + the off in one 128 window plus one pending event gives 257 for the shell's 256 queue; the shell drops the NoteOff and the bass note is stuck (output 0.296 held vs 0 after release, 14 of 62 alignments). Pool counters show nothing (dropped 0, forced 0). Direct on the shell: 256 events + off: stuck. Not reachable from a song (a step gives a node a handful of events) but it breaks the ruling and the U37 host.
