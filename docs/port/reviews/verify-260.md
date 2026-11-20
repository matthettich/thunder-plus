# verify-260: PR #260 `n1/players` (Players: dispatch and the pools behind one class)

Verifier: independent (did not build it). PR head `cd15eaa5`, base `origin/native` `6fd16ea9`. Status: complete. Verdict: MERGE.

## 1. It is a move and nothing else: CONFIRMED (no behavioural difference; structural differences listed)

Read `git diff origin/native...HEAD` against the old `render.cpp`. Every statement of the old `dispatch` / `capPoly` / `takeAll` is in `Players` in the same order, with the same conditions, arguments and counters. Differences, all of them:

1. Routing is now two-stage. `SongRenderer::dispatch` handles TVL/TVA/TPN/MVOL/MOUT, BEAT, FXC/FXP, then `default: break` and calls `players_->dispatch(e, *seq_, S_)`; `eventsIgnored++` when it returns false. Old order was STP, TVL.., BEAT, OFF, MOD, NOTE, FXC/FXP, default. The two sets of event types are disjoint, so the order cannot matter. `st_.events++` is still first. OFF / MOD with `track < 0` still do nothing and are not counted as ignored (`return true`); NOTE with `track < 0` returns before `notes++` as before.
2. The 12 `notes*` counters moved from `RenderStats st_` to `PlayerStats` (same `int` type as `RenderStats`, same increment sites). `SongRenderer::stats()` now copies them into the returned `RenderStats`; `st_.notes*` in `SongRenderer` stay 0 and nothing reads `st_` directly (grep). Same for `peakAll_` (moved to `PlayerStats::peakAll`, sampled after every event by `samplePeak()`, the old `std::max(peakAll_, voices())` line) and `serial_` (moved, starts at 0 as before). The final `peakVoicesAll = max(p.peakAll, pool_.peakVoices())` is the old expression.
3. `Players` is heap-allocated (`std::unique_ptr<Players>`), so `pool_` / `sfpool_` are no longer inline members of `SongRenderer`: one more allocation at construction (3648 bytes), one more pointer hop in `voices()`, `pool()`, `sfPool()`, `stats()`, and these four are now out-of-line (were inline). Not per-sample.
4. Construction order is the same: `seq_`, then `players_` (inside it `pool_(sampleRate, maxVoices)`, `sfpool_(sampleRate)`, same arguments and sizes), then `mix_`. Destruction is the reverse. A failed pool allocation throws out of the `Players` constructor exactly where it threw out of the member init (players.cpp is in the `-fexceptions` list in CMake, as render.cpp is).
5. `note()` declares `const RenderPads& pads_ = *this->pads_;` which shadows the member of the same name inside that function (readability; no `-Wshadow` in the build; `takeAll()` uses the member). Same object within a call.
6. `Players::dispatch` takes `const Seq&` (old code called through `seq_->`); `arp()` and `bpm()` are `const`. `S_` is passed as the `const Song&` argument. `setTime(t0, frame0)` and `render(...)` keep the old order (`pool_.setTime`, `sfpool_.setFrame((uint64_t)frame0)`, then `mix_.setTime(t0)` by the caller; render: `pool_` then `sfpool_` adds into the same inputs); `qFrame_` is `int64_t`, cast to `uint64_t` at the same place.
7. `render.cpp` no longer includes `rt/num.h` (moved to players.cpp); `-Wreorder` warnings for `scratch_` / `q_` in the `SongRenderer` constructor are old (base has the same 3).
8. Sizes: `sizeof(SongRenderer)` 36048 -> 32464 bytes; `sizeof(Players)` = 3648 (VoicePool 1280 + SfPool 2296 + pads pointer + PlayerStats 52 + serial); `RenderStats` unchanged (160).

No change found in: a refused / failed pool call (`pool_.note` returning false still counts nothing; `capPoly` still only when `VoicePool::playable`), `VoiceBuf b` copy (still a copy of the small descriptor), `notesSf` counted before the "no preset" check, the no-zone early return before the takeover.

## 2. Bit identity by my own means: CONFIRMED (950 renders identical)

Harness `/tmp/bid/bid.cpp` (C API only; not committed): one binary linked against the PR's `libthunder_core.a`, one against a `libthunder_core.a` whose `render.cpp.o` / `render_api.cpp.o` were rebuilt from `origin/native` sources with the identical flags and `players.cpp.o` removed (the link succeeding shows base `render.cpp` has no `Players`). Each render: SHA-style FNV-64 of every output float (L then R) and of the whole `thunder_render_stats` struct bytes, plus a printed counter summary. `cmp` of the two outputs: byte-identical.

| Group | Renders | Result |
| --- | --- | --- |
| ELECTRO.thunder (64 synthetic pads), configs plain / fx / fx+master / fx+master+PDC on / fx+master+PDC auto / page timing / master only, host blocks 1, 128, 480, 4096 | 28 | identical |
| ELECTRO with Live-route pads, SoundFont pads (sf_test.sf2 and FluidR3Mono_GM.sf3, several programs and keys), chop on pads, MUT/GEN variations, missing pads; 3 variants x 4 blocks | 12 | identical |
| Random songs 000..159 (1 to 8 tracks, chords on poly columns, MIDI-only / sliced / synced / plain instruments, random fx commands, Live and SF routes with and without a prepared preset, missing pads, chop, random effects / master / PDC / page timing), blocks 128, 480, 4096 (and 1 for every 4th) | 520 | identical |
| Random songs 1000..1119, tuned so MUT/GEN variations actually match | 390 | identical |

Counters seen over the random runs (so the paths ran): notes live 140 runs, SoundFont route 294 (played 239), variation played 22, variation missing 106, unshaped 128, shaped-reverse 4, MIDI-only 171, no pad 113, stretched 22. 0 create failures, 0 setup failures.

Release `ctest` of the render group on the PR head: `render_api`, `render_api_hostile`, `render_api_fix`, `render_dispatch`, `render_chop`, `render_var`, `render_sf`, `render_c`, `hostile`, `rt_core`, `rt_guard_selftest`: 11 of 11 pass.

## 3. Ruling P1: CONFIRMED

`Players` members: `const RenderPads* pads_` (pointer, re-pointable by `setPads`), `VoicePool pool_`, `sf::SfPool sfpool_`, `PlayerStats st_`, `uint64_t serial_`. No `Song&`, no `Song*`, no `RenderPads&`, no `Seq*` is kept across calls.

Every read of song / instrument / pad data in `players.cpp` and its source on each call:

| Read | Where from |
| --- | --- |
| `song.insts[ii].mo`, `.slc`, `.sync` (note) | the `const Song& song` argument of `dispatch` / `note` |
| `seq.arp(e)`, `seq.bpm()` (mod, note) | the `const Seq& seq` argument |
| `pads_.route[pad]`, `.sf[pad]`, `.buf[pad]`, `.kit[pad]`, `.findVar(...)` (note) | `*this->pads_`, dereferenced at the top of each `note()` call into a local reference |
| `pads_->kit[pad]` (takeAll) | the member pointer, read at call time |

Use-after-free test (`/tmp/bid/p1.cpp`, ASan + UBSan build of the PR head): construct `Players` with pads A (SoundFont route on pad 1); dispatch 54 events with Song A (sampler chords + SF notes) and render; destroy Seq A and Song A; load Song B (different instruments, sliced), build pads B on the heap, `setPads(&B)`, destroy pads A, dispatch and render; destroy Song B, pads C (no SF), `setPads(&C)`, destroy pads B, dispatch with Song C, render 200 more quanta with voices from earlier calls still sounding, destroy. Result: clean, no ASan / UBSan report (notes 36, played 24, sf 12, sf played 12). Negative control (same program, skipping the `setPads` before freeing pads A): ASan reports heap-use-after-free in `Players::note`, so the test does catch it.

Contract that remains (pre-existing, documented in `players.h` and `sf_live.h`): voices borrow the pad sample memory / SoundFont PCM / variation data (the `VoiceBuf` and `PresetPcm` pointers), not the `RenderPads` struct, so the sample memory and PCM must outlive the voices that play them; `setPads` is safe only for a new `RenderPads` that points at the same live sample memory. A variation buffer (`RenderVar::data`) is in `RenderPads::var`, so freeing pads A while a MUT/GEN voice from it still sounds would be a use-after-free: worth one line in the future live-host plan.

## 4. Real-time rules unchanged: CONFIRMED

- `players.cpp` / `players.h`: no `new`, `std::vector`, `std::function`, mutex, `printf`, file call; no `virtual` anywhere in `Players`. `dispatch` / `note` / `capPoly` / `takeAll` are the old code. `Players::render` / `setTime` forward to the same `VoicePool::render` / `SfPool::render` calls once per 128-frame quantum; the per-sample loops are inside the pools and untouched. The only new run-time cost is one direct (non-virtual, cross-file, so not inlined) call per event and per quantum plus one pointer hop.
- `rt_harness` (ctest `rt_core`, `rt_guard_selftest`): pass in Release and under ASan + UBSan. The whole-song case (ELECTRO, 1792 steps, 1823 notes played, 20 mixer events, 4.9 M frames, peak 21 voices) reports "no allocation" under the allocation guard on the PR head; the SoundFont pool case in that harness passes too.
- `sizeof(Players)` = 3648; `sizeof(SongRenderer)` 36048 (base) -> 32464 (PR); `sizeof(VoicePool)` 1280, `sizeof(sf::SfPool)` 2296, `sizeof(PlayerStats)` 52, `RenderStats` 160 both.

## 5. Lifetime and order: CONFIRMED

- Members of `SongRenderer` in declaration order: `seq_`, `players_` (was `pool_`, `sfpool_`), `mix_`, then `evs_`, `step_`, `scratch_`, `q_`. The pools were between `seq_` and `mix_` before and the `Players` that now holds them sits in the same slot, so construction and destruction order against the mixer is as before (inside `Players`: `pool_` then `sfpool_`, destroyed `sfpool_` then `pool_`, as the old members). Nothing in `~SongRenderer` depends on the pools outliving the mixer or the reverse (it is `= default`, defined in `render.cpp`, where `Players` is complete).
- `seq.arp(e)` is read inside the same `Players::dispatch` / `note` call that gets the event, passed to `pool_.mod / note` and `sfpool_.mod / note`; the event loop in `quantum()` still dispatches all `k` events of one `step()` before the next `step()` (`for i<k: dispatch(evs_[i]); players_->samplePeak();`), so the borrowed `arpPool_` pointer is used before the next `step()` exactly as before. `seq.bpm()` is read at call time as before.

## 6. Tests and CI

- Sanitizer build (`-DTHUNDER_SANITIZE=ON`, ASan + UBSan, UB fatal) of the PR head: `render_api`, `render_api_hostile`, `render_api_fix`, `render_dispatch`, `render_chop`, `render_var`, `render_sf`, `render_c`, `hostile`, `rt_core`, `rt_guard_selftest`: 11 of 11 pass. The P1 test above is clean under it too.
- Release: the same 11 pass (section 2). Full Release ctest on the PR head (`ctest -j2`, 181 tests, 4538 s): 180 passed, 0 failed, 1 skipped (`pumpsynth_refused`, skipped by its own CMake condition, nothing to do with this PR); that includes `render_api_oom` and `render_sf_oom` (allocation-failure injection through the new heap-allocated `Players`).
- CI on `cd15eaa5` (`gh api .../check-runs`): Core (Android arm64-v8a, NDK) success; Core (wasm, wasi-sdk) success; Core (macOS universal) success; Core (Windows x64, MSVC) success; Core (Linux, GCC) success; Core (Linux, clang) success; Android shell v0 success; `test` success; CodeRabbit and "Attach the APK" skipped; Core (Linux, sanitizers) cancelled at the 60-minute limit (14:32 to 15:31 UTC; the known problem fixed by PR #247, not this PR's: I ran the same sanitizer tests above and they pass).

## 7. Mutations by hand

I did not edit the PR worktree: each mutant was a modified copy of `players.cpp` compiled with the build's flags into a copy of `libthunder_core.a`, the 10 test programs (`test_render_dispatch`, `test_render_chop`, `test_render_var`, `test_render_c`, `test_render_sf`, `test_render_api`, `test_render_api_fix`, `test_render_api_hostile`, `test_hostile`, `rt_harness`) relinked and run, and the bit-identity harness run against it on 120 random songs (60 from seed 1000, 60 from seed 300) and compared with base. Scripts: `/tmp/mut/` (not committed).

| Mutant | Caught by | Bit-identity harness vs base |
| --- | --- | --- |
| SF route: `pool_.take` moved before `capPoly` | none | identical (not observable) |
| sampler route: `sfpool_.take` moved before `capPoly` | none | identical |
| `takeAll`: `sfpool_.take` before `pool_.take` | none | identical |
| SF route: `capPoly(e)` skipped | `render_sf`: line 376 "poly: four sampler notes then an SF note: the oldest sampler note rings on", 379 | 9 renders differ |
| sampler route: `capPoly` skipped | `render_sf` line 371 "poly: four SF notes then a sampler note: the oldest SF note rings on" | 9 differ |
| SF route: `pool_.take` replaced by `sfpool_.take` (wrong pool) | `render_sf` line 316 "sampler then sf" | 77 differ |
| SF route: the note goes to `pool_.note` instead of `sfpool_.note` | `render_sf` (41 checks, first line 282) | 133 differ |
| `capPoly`: serial stamped differently (`pool_` gets `s + 1`) | `render_sf` line 379 "poly: mixed ages" | 3 differ |
| `OFF` no longer forwarded to `sfpool_` | `render_sf` (line 282, 284) | 42 differ |

Findings about the test suite (not blockers for a pure move):
1. Three order swaps survive every test and also the 120-song fuzz and are probably equivalent (the two pools are independent; I did not prove it). If the swaps matter, nothing would catch them: `pool_.take` / `capPoly` order, `sfpool_.take` / `capPoly` order, `takeAll` order.
2. Every other mutant is caught by `render_sf` only. `render_dispatch`, `render_api*`, `render_var`, `render_chop`, `hostile` and `rt_core` do not exercise the poly cap and takeover across the two pools; if `render_sf` is skipped or slow-gated in some CI job, this code is unguarded.

## Verdict: MERGE

It is a move. No difference in order, condition, argument, counter or type was found; the structural differences (heap-allocated `Players`, counters in `PlayerStats` copied in `stats()`, four accessors now out of line) have no effect on any output. 950 renders are bit- and stats-identical to `origin/native`, ruling P1 holds and the ASan test (with a negative control) is clean, real-time rules are unchanged, full ctest and the render group under ASan + UBSan pass, and every CI job except the known-cancelled sanitizer job is green.

Nits, none blocking: (a) `Players::note` shadows the `pads_` member with a local `const RenderPads&` of the same name; rename it when `setPads` is first used by a live host. (b) The voices borrow the variation buffers inside `RenderPads::var`; a live host that re-points pads while a MUT/GEN voice sounds must keep the old pads' `var` alive (same rule as the sample memory; worth one line in `players.h`). (c) The test-suite gaps in section 7.
