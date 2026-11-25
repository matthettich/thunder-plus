# U37 P5: the C API `thunder_live_*` over `LiveHost`

Branch `n1/u37-p5` on `58feb0c` (P4 = PR #302). Files: `native/core/src/live/live_api.cpp` (new, about 230 lines), `native/core/include/thunder/live.h` (text only), `native/core/CMakeLists.txt`, `native/tests/{test_live_c.c, test_live_capi.cpp, test_live_capi_dead.cpp, rt_harness.cpp, CMakeLists.txt}`. Nothing outside `native/` and `docs/port/`.

## Done
- All 45 functions of `live.h`, each a thin `extern "C"` wrapper (`ctl()` helper: NULL handle = E_ARG, dead handle = E_STATE, `try`/`catch` through macros that compile away without exceptions, `std::bad_alloc` = E_MEMORY, anything else = E_INTERNAL and the handle is dead). `process` goes straight to `LiveHost::process` after one relaxed load.
- ABI rule of `size` (opts, play opts): copy `min(size, ours)`, the rest default; a caller newer than the build must have zeroed the tail (else NULL / E_ARG); size < 4 is refused.
- `thunder_live_alloc` = `new (std::nothrow) float[n]` (n in 1 .. 2^30), `thunder_live_free` = `delete[]`. `caps` = MASTER_SWAP | TAKE | EFFECTS (when `THUNDER_HAVE_TFX`).
- The lead's rulings: (a) `live.h` MIDI text now says the records carry the ENGINE frame; recipe for the shell: `OFFSET = stats.frames - 128 * stats.quanta` (checked in `live_capi`: gapless and across a re-anchor); `midiDrain` adds nothing. (b) The ordering of a MIDI command posted after `play` in the same quantum is documented in `live.h` as accepted.

## NOT implemented (as `live.h` says for a build without the cap; caps bits are 0)
SoundFonts (`font_add`, `font_remove`, `pad_sf`) and live-engine pads (`pad_live`): `LiveHost` answers E_UNSUPPORTED to every argument (P2 scope); `pad_sf_clear` / `pad_live_clear` answer 0 for a pad in range. CAP_SF, CAP_LIVE_SWAP, CAP_ENGINES are not set. This follows ruling D8/D9 (those songs fall back / stay on the sampler route).

## Departures from the header (all written into `live.h`)
1. `thunder_live_alloc` alignment: "16-byte aligned" became "as operator new[] is" (16 on glibc, bionic, macOS, MSVC x64, wasi-libc). A guaranteed 16 would need `freeBuf` to change (P0 work list); nothing in the core needs more than `alignof(float)`.
2. NULL handle: `process` writes nothing (buffer size unknown); the header said outputs are always written. A dead handle zeroes them (size known).
3. `fx_plan` and `master_config`: n above 4 MiB is E_LIMIT (new; before the parse), `master_config` also E_ARG for NULL with n > 0 and `safety` not 0 / 1. `thunder_live_pad_take`: `stereo` not 0 / 1, `loop`: `has` not 0 / 1 are E_ARG.
4. `THUNDER_LIVE_WHERE_FATAL`: nothing posts it (the audio side cannot post for a handle it does not run; a control call that dies answers E_INTERNAL). Documented as reserved.
5. Dead handle: poll, stats_get, midi, host_stats, alloc/free still work (getters); `midi_drain` is E_STATE.

## Questions for the lead (I chose the contract-keeping option and went on)
- Add a field/function giving the engine offset directly? The recipe from `stats` works but is a snapshot of two relaxed counters (can tear by one quantum exactly at a re-anchor).
- wasm: the CI wasm job (`native.yml`) compiles only core.cpp, rt/, song/; the live host (`live_host.cpp`, `live_api.cpp`, hence the whole core) is NOT in any wasm build yet: P6. I only checked `g++` and `clang++ -fno-exceptions -fno-rtti -fsyntax-only` of `live_api.cpp` (clean); no wasi-sdk here. MSVC not available here either: the code uses nothing but the standard library; `test_live_c.c` is C99 with no VLA / unistd (checked with clang `-std=c99 -pedantic -Werror`).

## What the Android shell (N2) will call, in order
create (`F_EFFECTS|F_MASTER|F_METERS|F_MIDI`, sample_rate = stream rate, max_block = burst size) -> `thunder_live_midi` -> `thunder_midiout_create` + devices / ports (CONTROL thread) -> `host_stats(out_latency, 0)` -> first audio callback: `process` (prepares the thread; frame0 = its running count) -> `song_publish` (whole text + vars) -> `pad_set` / `pad_take` (blocks from `alloc`) + `pad_kit` / `pad_route` / `pad_chop` / `pad_hits`, `pads_publish` -> `meters_want` -> `play` -> per callback: `process` then `thunder_midiout_clock_map` (engine frame = host frame - OFFSET); sender thread: `thunder_midiout_pump` (not `midi_drain`); UI thread: `poll`, `stats_get`, `collect` every few ms, edits (`song_publish`, `bpm`, `loop`, `mix_set`, `fx_plan`+`fx_commit`, `fx_param`, `pdc_set`, `master_config`, `stop_track`, `hold`, `perf`, `lead`, `midi_lead`) with E_BUSY retried after the next callback -> shutdown: `stop`, one more `process`, pump, `thunder_midiout_free`, `destroy`.

## For the lead to read
`live_api.cpp` is all control side except `thunder_live_process`, `thunder_live_poll`, `thunder_live_midi_drain`, `thunder_live_stats_get`, `thunder_live_host_stats` (a relaxed load, then `LiveHost`): no allocation, no lock. `LiveHost` itself is unchanged by P5.

## Tests (GCC Release, 2 cores)
See the final report for the run on the final head. New: `live_c` (746 checks, 3 s), `live_capi` (bit-equal C API vs `LiveHost` vs `SongRenderer` on ELECTRO with effects, master, PDC, meters: call sizes 128 / 333 / 480 / 1 / 4096, frame0 0 / 37 / 2^31, pad_set and pad_take; threaded run), `live_capi_dead` (17), `rt_core` pass "C API". Sanitizer builds: `THUNDER_SANITIZE_BUILD` reduced matrix (5 s runs, fewer cases, 700 control ops); guards are `> 0`, not matrix-sized.
