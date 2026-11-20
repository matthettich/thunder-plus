# verify-260: PR #260 `n1/players` (Players: dispatch and the pools behind one class)

Verifier: independent (did not build it). PR head `cd15eaa5`, base `origin/native` `6fd16ea9`. Status: IN PROGRESS (items 1 to 3 done; 4 to 7 below as they finish).

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

## 4 to 7

(in progress)
