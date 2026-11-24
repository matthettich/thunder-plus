# Verification of U37 P5 (C API `thunder_live_*`), head `10b0a47` on `58feb0c`

Independent verifier. Reviewed `git diff 58feb0c 10b0a47 -- . ':!build'` (9 files). GCC Release build, ASan+UBSan build, TSan build of `live_capi`.

## Verdict: MERGE AFTER finding 1 (CI would go red on the sanitizer job) and findings 2 to 3 are accepted or fixed

No wrong audio, no crash, no real-time violation found in `live_api.cpp`. LiveHost is untouched by P5. 02d397b changed only `build/` (7027 files) plus `docs/port/U37-P5-NOTES.md` and `native/tests/rt_harness.cpp`, both intended; 10b0a47 untracks build/. Nothing else outside build/ was changed by 02d397b.

## Findings

1. **test weakness / CI red (must fix)**: `live_capi_dead` fails under ASan (LeakSanitizer, on by default on Linux): `LiveHost::play` leaks 2 x 1932 bytes (the `PlayPlan` pushed on the command ring and the `LiveBusMsg` never published) when the test seam (`live_host.cpp:413`, between the ring push and the snapshot publish) throws. Run: ASan+UBSan build of `native/` (THUNDER_SANITIZE=ON), `ctest -R live_capi_dead` fails, "3864 byte(s) leaked in 2 allocation(s)", both from `thunder_live_play` -> `LiveHost::play`. live_c, live_capi, rt_core pass under ASan/UBSan. The leak is an artefact of the seam: real code cannot throw between push and publish (asserts only). Fix in the test (call `thunder_live_destroy` after draining? no: the plan sits in the ring; set `ASAN_OPTIONS=detect_leaks=0` for that test, or put the seam where the throw leaves nothing behind, or have `~LiveHost` free un-popped Play commands). A side effect of the same seam: after the bad_alloc case the handle is "alive" and process returns 0, but the audio side holds a Play whose snapshot was never published and so stalls every later adoption; harmless because no real code path can throw there, but the E_MEMORY "nothing changed" claim is not true for that seam, so the test checks only `bpm` and `process`, not a later play.
2. **thread safety / contract (accept with a note, or fix before N2)**: the engine-offset recipe `stats.frames - 128 * stats.quanta` tears by +-128 in a plain gapless run, not only at a re-anchor. `statsGet` loads `P_FRAMES` and `P_QUANTA` as two independent relaxed atomics (`live_host.cpp:605`); `publish()` stores them one after the other (`:1070`). Failing run: `/tmp/tear.cpp` (one thread calls process 128 frames at a time from frame0 1000, another loops `stats_get`): 1870 to 2835 torn reads in 76 to 93 million (off = 872 or 1128 instead of 1000), three runs. A shell that reads once per callback hits it far more rarely, but a torn read gives a 2.7 ms MIDI mapping error, and if the offset is cached, a lasting one. live.h already says the stats are "not one instant", so the recipe contradicts it. Cheap fixes: read twice and accept when `quanta` is equal and the two offsets agree (document that in live.h), or publish `off_` as its own atomic / a direct getter (needs a LiveHost change: a P6 or N2 item). The builder's remark that it tears "exactly at a re-anchor" is incomplete. Also: `publish()` runs once per quantum, so a re-anchor in a call that completes no quantum (a 1-frame call) leaves `stats.frames` on the old offset until the next quantum; the recipe is then stale for that one call.
3. **test weakness (note)**: the bit-equal test (`live_capi`) compares audio only for song_publish, pad_set / pad_take, pad_kit, pads_publish, meters_want, play and process. Wrappers for the other calls are checked for return codes only (live_c). Mutations of `live_api.cpp` that survived BOTH tests: `master_config` with `safety` inverted (`safety != 1`), `fx_commit` with `at` replaced by -1. See mutation table. Mutations that live_capi missed but live_c caught (swapped args in bpm `at`, hold `at`, fx_param `at`, fx_plan bpm, pdc_set, pad_route, var_set, pad_hits, pad_chop, meters_want, host_stats, midi_lead, play row/track swap, play pdc_mode dropped): acceptable for a thin wrapper, but nothing proves the audio effect of any of them.
4. **note (header text)**: live.h now says alloc alignment is 16 on "bionic"; 32-bit bionic (armeabi-v7a) malloc / operator new is 8-aligned. Nothing in the core needs more than `alignof(float)`, so harmless; soften the text.
5. **note (ABI)**: `abiCopyIn` trusts a caller `size` and reads that many bytes past our struct to check for non-zero bytes: a garbage `size` reads out of bounds (by contract). A shorter old struct gets zeros, so `play_opts.t0` would be 0, not NOW (-1); irrelevant while the struct has only one version, but "every zero is its default" is not true for `t0`.
6. **note (dead handle)**: a dead handle never runs process again, so MIDI notes already sounding on an external port are not released by the core; the shell must `thunder_midi_panic` / free the midiout itself. Worth one sentence in live.h. `midi_drain` is E_STATE on a dead handle (not in the header's getter list, so consistent), which loses MIDI records booked before the failure; the builder noted this.

## 1. Contract fidelity

All 45 functions of live.h exist in `live_api.cpp` (header and implementation symbol sets compared: identical). A scratch program (not committed) called every function with a NULL handle, bad sizes and bad arguments: NULL -> E_ARG for every int function, `midi` NULL, destroy / host_stats / free no-ops, process with NULL writes nothing; process overlap, frames > max_block, NULL output, negative frame0, frame0 above 2^62, frames < 0 -> E_ARG; `pad_take` with stereo 2, NULL block, pad out of range (256 pads) -> E_ARG; `play` with `size` 2 -> E_ARG, a larger `size` with a zero tail -> accepted, with a non-zero byte past our struct -> E_ARG; `create` with `size` 3 -> NULL and an err text; `alloc(0)` and `alloc(2^31)` -> NULL; `font_add` -> E_UNSUPPORTED; `pad_sf_clear` 0 in range, E_ARG out of range; `master_config` safety 2 -> E_ARG; `midi_drain` without F_MIDI -> E_STATE; `dump_step` without the flag -> E_STATE; `loop` has 2 -> E_ARG. All as live.h says.

Departures (the builder's five, all written into live.h): (1) alloc alignment: acceptable, see 4. (2) process with NULL handle writes nothing: acceptable (size unknown). (3) E_LIMIT above 4 MiB for fx_plan / master_config, extra E_ARG on `stereo`, `has`, `safety`: acceptable, additive, in the header. (4) `WHERE_FATAL` reserved: acceptable. (5) getters on a dead handle, `midi_drain` E_STATE: acceptable. No departure found that the builder did not list.

## 2. Dead handle

`dead` is `std::atomic<int>`, written (relaxed) only on the control thread in `ctl`, read (relaxed) by process / control calls / midi_drain / dump_step. Nothing else is published through it, so relaxed is enough; no data race (TSan clean). Window between the throwing call and `dead.store`: the audio thread may run one more process on the host; what a LiveHost control call leaves behind is whole commands on the rings and whole snapshots, so audio sees no half-applied state, except through the test seam of finding 1, which sits where real code cannot throw. LiveHost's own `catch (...)` turns almost everything into E_MEMORY, so E_INTERNAL is reachable only from an exception that escapes a LiveHost method. Destroy: `delete L->h; delete L;` relies only on the header's rule (audio stopped, no call in flight, midiout freed first): no use-after-free that the rule does not already forbid. `poll` / `midi_drain` / `stats_get` dereference `L->h` with no liveness check beyond that rule.

## 3. Audio / consumer path

`thunder_live_process`: NULL check, one relaxed load, `LiveHost::process`. `poll`, `midi_drain`, `stats_get`, `host_stats`: a NULL check and the LiveHost call; no allocation, no lock, no try. rt_harness pass 4 (`run_live_c_api`) really goes through the C API: 2995 process calls, 7475 quanta, two re-anchors, 258 KB of records, 1925 MIDI records, with `thunder_live_process`, `poll`, `midi_drain`, `stats_get`, `host_stats` each inside `rt_guard` (aborts on allocation); it passed (run alone, and as `rt_core` under ASan/UBSan). The guard checks allocation only, not locks or exceptions; those were checked by reading.

## 4. Memory

`alloc` = `new (std::nothrow) float[n]` (n 1 .. 2^30), `free` = `delete[]`, `PadsHost::freeBuf` = `delete[]`: pairs. `pad_take` ownership: every error return (wrapper E_ARG on `stereo`, LiveHost E_ARG / E_LIMIT / budget, `setPadTake` kArg / kNoMem) leaves the block with the caller; after `setPadTake` succeeds nothing can throw (`dropStagedVars`, `refreshCtl` are plain stores), so E_MEMORY after a take cannot happen: no double free. On a dead handle the block stays the caller's and `free` works. The 4 MiB limits are applied before the parse (fx_plan, master_config). The API cannot check that `block` holds `frames * (stereo ? 2 : 1)` floats: a shell that passes stereo = 1 with a mono-sized block reads out of bounds; the header says what to allocate, so contract only. No out-of-memory sweep was added by P5 (render_api_oom-style); not run.

## 5. Bit-equality test and mutations

`live_capi` compares the API run with (a) `SongRenderer` built directly in the test (not the code under test) and (b) `LiveHost` driven directly, 14 s of ELECTRO with effects, master, PDC auto and meters, call sizes 128 / 333 / 480 / 4096 / 1, frame0 0 / 37 / 2^31, pad_set and pad_take; non-vacuous (peak > 0.01, `memcmp` on every sample, counters compared). Mutations of `live_api.cpp`, each rebuilt, `live_c` and `live_capi` run, original restored (diff against the original: identical):

| mutation | live_c | live_capi |
|---|---|---|
| play: pdc_mode forced 0 | caught (2) | missed |
| process: frame0 ignored (0) | caught (6) | caught (13) |
| play: row and track swapped | caught (1) | missed |
| mix_set: value and ramp swapped | caught (4) | caught (1) |
| loop: y0 / y1 swapped | caught (3) | caught (1) |
| bpm: `at` ignored | caught (1) | missed |
| pad_take: stereo inverted | caught (1) | caught (11) |
| fx_param: `at` ignored | caught (1) | missed |
| stop_track: track + 1 | caught (1) | caught (1) |
| pad_kit: choke / syn swapped | caught (2) | caught (10) |
| midi_lead: seconds halved | caught (2) | missed |
| master_config: safety inverted | MISSED | MISSED |
| host_stats: args swapped | caught (1) | missed |
| perf: what / index swapped | caught (3) | caught (1) |
| pad_hits: n_slices / count swapped | caught (2) | missed |
| fx_plan: bpm replaced by 120 | caught (3) | missed |
| pdc_set: always 0 | caught (1) | missed |
| pad_route: always 0 | caught (1) | missed |
| var_set: num / pad swapped | caught (2) | missed |
| hold: `at` ignored | caught (1) | missed |
| lead: seconds x 2 | caught (2) | caught (1) |
| fx_commit: `at` ignored | MISSED | MISSED |
| pad_chop: always 0 | caught (1) | missed |
| meters_want: any nonzero -> 30 | caught (1) | missed |

24 mutations, 22 caught by at least one test, 2 missed by both (see finding 3).

## 6. Engine-offset recipe

Arithmetically right: `stats.frames` = `qFrame_ + off_` after `quanta_++` (live_host.cpp:684, 1070), and the engine renders 128 frames per quantum from 0, so `frames - 128 * quanta = off_`. live_capi checks it gapless (777) and across a forward gap of 5000 (777 + 5000). It tears: see finding 2 (measured). A direct call or one atomic would remove the tear; the read-twice rule is enough for a shell in the meantime.

## 7. Portability

`live_api.cpp` is in the exceptions list of core/CMakeLists.txt (not MSVC, which has exceptions on); the try / catch macros compile away without exceptions. The core files built `-fno-exceptions` are called from process only; nothing throws on the audio side, and a throw from a `-fno-exceptions` TU on the control side (std::vector inline) would propagate on Linux (.eh_frame always) and on Apple arm64 (`-funwind-tables` is added). `test_live_c.c` builds as `-std=c99 -Wall -Wextra -Werror -pedantic` (done in this build), no `<unistd.h>`, stack arrays at most 1 KB, `//` comments are C99. No "lxr" in any added line, none in live_api.cpp or live.h. MSVC and the NDK were NOT available: not compiled; `/W4 /WX` on the C test is unverified.

## 8. Unimplemented calls vs caps bits

`font_add`, `font_remove`, `pad_sf`, `pad_live`: E_UNSUPPORTED (after the dead / NULL checks); `pad_sf_clear`, `pad_live_clear`: 0 for a pad in 0..255, E_ARG outside. Caps: MASTER_SWAP, TAKE, EFFECTS (only with THUNDER_HAVE_TFX); SF, LIVE_SWAP and ENGINES not set. Consistent with live.h's text for every one of them. One thing to keep in mind: a shell that tests caps before calling `pad_take` is fine; `pad_live` / `pad_sf` are not callable at all.

## Runs

- GCC Release (`-DTHUNDER_WITH_LXR=OFF`): live_c (746 checks), live_capi (32 checks), live_capi_dead, rt_core: pass. `live_capi` ran 4000 control ops, 7846 quanta, 2516 MIDI records.
- ASan+UBSan (THUNDER_SANITIZE=ON): live_c, live_capi, rt_core pass; **live_capi_dead FAILS (leak, finding 1)**. `live_midi` / `live_threads` built, not run (not P5).
- TSan (`-fsanitize=thread`, THUNDER_SANITIZE_BUILD reduced matrix, build of test_live_capi only): 0 warnings; threaded section 700 control ops, 1804 quanta, 775 records, 772 MIDI records; 32 checks, 0 failed.

## Not checked

MSVC and NDK builds; wasm build (not in any wasm job: P6); the full ctest of the repo (only the P5 tests and rt_core); an out-of-memory sweep through the API; macOS; `-fno-exceptions` run time; the TSan run of live_c and rt_harness; whether live_capi_dead leak also fails the real CI job (assumed: LeakSanitizer is on by default on Linux x86-64).
