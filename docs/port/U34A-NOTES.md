# U34a: offline song render C API

Unit U34a of the native port (WIRING-DOSSIER 5, item 1). A public C API over `SongRenderer` so a host (wasm bridge, Android JNI, desktop) can render a song without C++ types.
Branch `n1/u34a`, cut from `native` at 9e88d36. Stems (U34b), `dispatch()` routing (D2 / D3), live playback and the wasm bridge are NOT in this unit.

## What is here

| File | What |
|---|---|
| `native/core/include/thunder/render.h` | the public C header (C99-clean) |
| `native/core/src/render/render_api.cpp` | the implementation (the only core file built WITH exceptions: it catches `std::bad_alloc`) |
| `native/core/CMakeLists.txt` | adds the file, `-fexceptions` for it alone (GCC / clang; MSVC has them on) |
| `native/tools/thunder-render/main.cpp` | the CLI, now a client of the C API (it no longer includes `render/render.h`) |
| `native/tests/test_render_api.cpp` | ctest `render_api`: gate 2 (bit identity with the direct drive) and the API's rules |
| `native/tests/test_render_api_hostile.cpp` | ctest `render_api_hostile`: gate 3 (hostile input, extreme fields at once, fixed-seed fuzz) |
| `native/tests/test_render_c.c` | ctest `render_c`: gate 4 (a C translation unit, `-std=c99 -Wall -Wextra -Werror -pedantic`, run once) |
| `native/tests/render_api_common.h` | helpers: songs, made-up pads, the direct drive |

## The API (see the header for every rule)

```c
typedef struct thunder_render thunder_render;
thunder_render* thunder_render_create(const uint8_t* song, size_t song_bytes, const thunder_render_opts* opts, char* err, size_t err_cap);
int     thunder_render_set_pad(thunder_render*, int pad, const float* l, const float* r, int64_t frames, double sample_rate);
int     thunder_render_set_pad_kit(thunder_render*, int pad, int choke, int syn, double rel);
int64_t thunder_render_length(thunder_render*);
int64_t thunder_render_read(thunder_render*, float* l, float* r, int64_t frames);
int64_t thunder_render_position(thunder_render*);
int     thunder_render_latency(thunder_render*);
int     thunder_render_stats_get(thunder_render*, thunder_render_stats* out);
int64_t thunder_render_drain(thunder_render*, int64_t max_frames);
void    thunder_render_free(thunder_render*);
```

* `thunder_render_opts` (size first, then): `sample_rate` (0 = 48000; 8000..384000), `effects`, `master_chain`, `pdc_mode` (0 off / 1 auto / 2 on), `page_timing`, `use_frames` + `frames`, `max_frames`, `vars_json` + `vars_bytes`. All-zero = defaults; `opts == NULL` = defaults. A struct from an older / newer header works (fields beyond `size` default; extra tail ignored). `reserved` fields must be 0.
* PULL model, no callbacks. Progress = `position / length` polled by the host; cancel = stop reading and `free` (any time, NULL allowed).
* `read`: any block size, output independent of it; returns frames written, 0 at (and after) the end, negative on error; fewer than asked only at the end.
* `length`: `frames` from the start when `use_frames = 1`; otherwise -1 until the render reaches its end (when the last voice has ended: the tail depends on the voices, only running tells), then the total. The end is tested after each 128-frame quantum, so the length does not depend on the read sizes.
* Pads are COPIED (host arrays may be freed on return), only before the first read; mono (`r == NULL`) or stereo; `frames == 0` removes a pad. Samples must be finite.
* Errors: return codes `THUNDER_RENDER_E_ARG / STATE / SONG / LIMIT / MEMORY / INTERNAL` (-1 .. -6), `create` returns NULL + `err` text (always NUL-terminated, truncated to `err_cap`). A failed call leaves the handle as it was, except E_INTERNAL (dead handle: only free).
* Not thread-safe per handle (one thread at a time, never a live core's audio thread); two handles share no mutable state.
* Limits: song 64 MiB, vars 16 MiB, pad data 512 MiB in all, one pad 2^28 frames, `frames` / `max_frames` 2^40, pad rate 1..1e6 Hz, unknown-length renders stop at `max_frames` (default 1260 s, `stats.hit_limit`).
* Memory (peak RSS, ELECTRO 0.9 MB, no pads, created and read 1000 frames): 8 kHz 28 MB; 48 kHz 53 MB; 96 kHz 83 MB; 192 kHz 143 MB; 384 kHz 263 MB. With effects + master chain: 59 / 94 / 135 / 218 / 385 MB. Plus 4 bytes per pad sample (copied) up to 512 MiB.

## Results

**Gate 1, `render-compare.sh` ELECTRO (Chrome reference made in this session, `THUNDER_REF=/tmp/u34a-ref`), before (CLI on `SongRenderer`, built from 9e88d36) and after (CLI on the C API):**

| Case | Page timing (the verdict figure) before | after | Core timing before | after |
|---|---|---|---|---|
| plain | -87.4 dB, PASS | -87.4 dB, PASS | -3.7 dB | -3.7 dB |
| `--fx` | -72.3 dB | -72.3 dB | -3.1 dB | -3.1 dB |
| `--master` | -75.1 dB | -75.1 dB | +0.7 dB | +0.7 dB |

The numbers are the same because the WAVs are: `cmp` says all six renders (plain / fx / master, page and core timing) are byte-identical before and after (40 325 380 bytes each). The plain case passes -80 dB as before. The `--fx` and `--master` cases are "not a verdict" cases in `render-compare.sh` and were already above -80 dB before this unit (the script explains why: the Chrome reference's own run-to-run noise); they did not change.

**Gate 2** `render_api`: output through the C API equals the direct `SongRenderer` drive bit for bit (memcmp of both channels) for read sizes 1, 64, 128, 333, 4096 and whole-song, in: a short synthetic song (known and unknown length, plain / master / fx + pdc auto / page timing), ELECTRO 10 s plain (sizes 64..whole; 1 left out for time on the long ones), ELECTRO 5 s with fx + master. Also: length known / unknown, position, latency > 0 with the master chain, `set_pad` after a read refused, free mid-render, two handles read in different orders equal, `max_frames` cut.

**Gate 3** `render_api_hostile`, under `-DTHUNDER_SANITIZE=ON` (ASan + UBSan, no recover): null / empty / junk / NUL-containing / 64 MiB+1 / `size_t` max / 64 MiB of `[` / 100 000 nested objects, every 7th-byte prefix of a song and 59 prefixes of ELECTRO, 20 bad option sets, newer / older option structs, err buffer sizes 0, 1, 5; pads: bad numbers, negative / INT64 extremes, NaN / inf / out-of-range rates, NaN / inf samples, the 512 MiB budget (4 x 128 MiB fits, the fifth and a larger replacement are refused, a replacement gives bytes back), rates 1 and 1e6 and 384000 with 1-frame pads rendered; reads: NULL handle / buffers, L == R, overlap, adjacent, negative, INT64 limits, 0 frames with NULLs; stats with short structs; free before / during / after reads in 8 configurations; EVERY number of the short song (and a third of ELECTRO's) replaced by each of 16 extreme values, with and without effects / master / PDC (64 of 64 songs made a handle and rendered); a fixed-seed (20251118) fuzz of 800 iterations (bit flips, extreme numbers, deletions, truncations of the short song and, every 12th, ELECTRO, random rates 8 kHz..384 kHz, options, pads, block sizes, free mid-render): 292 handles rendered 1.4 M frames, 508 refused, 0 failures, no sanitizer report.

**Gate 4** `render_c`: `-std=c99 -Wall -Wextra -Werror -pedantic`, compiles and runs.

**Gate 5**: the wasm CI job compiles an explicit file list (`core.cpp`, `rt/*`, `song.cpp`) and exports `thunder_create / process / post / poll / put / destroy`: the render files are not in it, so nothing changed there (the render needs the whole core; that is the U37 target). Not run here (no wasi-sdk). The Linux GCC and sanitizer jobs are what ctest below runs; the Android CMake adds `core/` as a subdirectory, so it picks up the new source and header on its own (not run). See "Not verified".

**ctest totals** (`--timeout 3600`, `-j2`): Release GCC 154 tests, 153 passed, 0 failed, 1 skipped (`pumpsynth_refused`, skipped by design where pump is built); sanitizers (ASan + UBSan) 154 tests, 153 passed, 0 failed, 1 skipped (same); the closed build (no LXR, no DSP library, no pump) 110 tests, 105 passed, 0 failed, 5 skipped (`fxzero`, `pump`, `pumpsynth`, `pumpsynth_fuzz`, `fx`: they need the libraries), the three new tests among the passes. Before this unit: 151 (U32's report); this unit adds `render_api`, `render_api_hostile`, `render_c`.

## Choices for the lead

1. **Naming and errors follow the headers where they differ from the brief**: `song_bytes` / `err_cap` (snake_case, as `core.h`'s `sample_rate`, `max_block`). `core.h` returns 1 / 0; this API returns 0 / negative codes because `read` has to carry a count and an error in one value, so the int functions follow it. Easy to flip if you prefer 1 / 0.
2. **Functions beyond your list**, each needed for the CLI to behave as before: `set_pad_kit` (the page's `coreKitV()` entries choke / syn / rel; without them ELECTRO's choke and synth releases differ), `stats_get` (the CLI's diagnostics and its exit code 4 "events lost"), `drain` (exit 3 "a voice never ends" after a fixed-length render), `opts.vars_json` (`SeqVars`: vtrk / NSLOT / TFX from the page's `coreVars()`; the song JSON alone does not carry them).
3. **The CLI has no PDC plan** (your brief lists one): `pdc_mode` exists in the options, default off, and when on the plan is built exactly as `rt_harness` does (`pdc_plan(bus_plan(...), FxBank::pdcSlots(...))`, `setPlan`), after the effects. Tested for equality with the direct drive only (no song in the tests has latent pump slots).
4. **Length**: -1 until the end for unknown-length renders (see above), and the end is evaluated per 128-frame quantum. The old CLI evaluated it per CLI block, so its unknown-length length depended on `--block`; with the default 128 it is the same number. A host that wants a progress bar before the end must give `use_frames` (it knows the song's length from the page's own estimate) or show an indeterminate bar.
5. **Pads: non-finite samples are refused** (E_ARG) and the pad rate must be 1..1e6; the old CLI accepted anything. Choke 0..255, rel 0..60 s in `set_pad_kit`. A zero-length pad removes the pad (the old CLI set `len = 0`; same silence, `notesNoPad` instead).
6. **`render_api.cpp` is built with exceptions** on GCC / clang so a failed allocation comes back as E_MEMORY. The rest of the core stays `-fno-exceptions`, so an allocation failure inside the song renderer's own constructor (not this file) would leak what it had made before unwinding: the budget limits and the rate cap (<= 384 kHz: 385 MB worst measured) are what keep that away; no test can induce it. A wasm build without exception support will abort instead of returning E_MEMORY.
7. **CLI output**: the lines "largest step N of 12288", "of 64 voices capacity", "longest voice / mixer lane" and the mastering / safety on-off line are gone (the C API does not expose them). Nothing parses them (grep of tools and CI). The exit codes 2 / 3 / 4 are kept.
8. **Not done, on purpose, found while reading** (do not fix here): `dispatch()` ignores `kv.live` / `kv.sf`, events `eng` / `chc` / `tempo`, MIDI events, and `SH_VK` plays the base pad; `noShape` is never set (D2 / D3 of the dossier). The CLI path, and so this API, inherits all of it; ELECTRO's reference does not use them (stats: sf 0, vk 0). A song that does will differ from the page.
9. There is no export macro in the public headers (none exist today: the wasm build uses `-Wl,--export=`). I did not invent one; a wasm export of the render API needs the names `thunder_render_*` added to that list when the full-core wasm target exists.

## Conditions for the wiring

* The host renders the pads itself (as the page's `renderOut` does today) and passes each as float32 plus its kit entry; the API does not call any pad renderer.
* Create on a worker / control thread, read on a worker, never on a live audio thread: `create` allocates up to ~50 MB at 48 kHz (385 MB at 384 kHz with effects + master) and zeroes it, `read` cost was not timed with real pads in this unit.
* A wasm host must budget the memory table above plus 4 bytes per pad sample (the copy) and the host's own copy during `set_pad`; pass pads one at a time and free the host copy after each.
* A bridge that wants cancel stops calling `read` and calls `free` (free is safe at any point, including right after create).
* Offline renders are not bit-compared with a Chrome render by this unit's tests: gate 1 is the CLI check above.

## Not verified

* The wasm, Android, Windows (MSVC, `/W4 /WX` on the C test), macOS and clang builds were not run here; only Linux GCC (Release, ASan + UBSan, closed).
* Two handles on two threads at once (no TSan run; the tests interleave two handles on one thread). I did not audit the core for function-local statics shared between handles (U25's notes say the Plaits workspace is per thread).
* The `bad_alloc` -> E_MEMORY path was never taken (no test can make `new` fail in these sizes); only the budget checks are tested.
* PDC through the API with real latent slots (pump effects) against the page; only equal to the direct drive, and the plan is empty for the songs used.
* Sample rates other than 48 kHz were created and read in the hostile test and the fuzz, but only 48 kHz output was compared with Chrome.
* Pad rates and extreme songs are only checked for "no crash, no sanitizer report", not for sensible output.
