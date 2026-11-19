# U34a: offline song render C API

> **FOLLOW-UP IN PROGRESS (19 Nov, branch `n1/render-v1`):** code, header, tests and the four mutation checks are done (Release 8 of 8); now the sanitizer build, then the PR and CI. See "Follow-up after verify-238-239" at the end.

> **STATE (19 Nov):** all of D1 to D8, the test gaps and the length estimate are done and tested; PR #238 is waiting for CI (the first run found MSVC-only test assumptions, fixed). Nothing OPEN.

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
* **For the master-chain unit (a question, not a defect of this API):** the independent verifier measured the master chain (mastering + safety limiter) at 384 kHz about 1.5 dB lower in RMS than at 48 kHz on ELECTRO (15 s: 0.1812 at 48 kHz, 0.1783 at 192 kHz, 0.1526 at 384 kHz with the chain on; with effects on and the chain OFF the same render differs by only 0.05 dB: 0.12973 / 0.12913 / 0.12895). So the difference is in the master chain's own behaviour above about 192 kHz, not in the Cloud Seed cap and not in these PRs (`docs/port/reviews/verify-238-239.md`, "Own question"). Nothing here claims the chain is rate-independent; whoever wires the master chain for a 384 kHz render should look at it first. Not investigated in this follow-up.

## Not verified

* The wasm, Android, Windows (MSVC, `/W4 /WX` on the C test), macOS and clang builds were not run here; only Linux GCC (Release, ASan + UBSan, closed).
* Two handles on two threads at once (no TSan run; the tests interleave two handles on one thread). I did not audit the core for function-local statics shared between handles (U25's notes say the Plaits workspace is per thread).
* The `bad_alloc` -> E_MEMORY path was never taken (no test can make `new` fail in these sizes); only the budget checks are tested.
* PDC through the API with real latent slots (pump effects) against the page; only equal to the direct drive, and the plan is empty for the songs used.
* Sample rates other than 48 kHz were created and read in the hostile test and the fuzz, but only 48 kHz output was compared with Chrome.
* Pad rates and extreme songs are only checked for "no crash, no sanitizer report", not for sensible output.


## Verification fixes (the independent verifier's report: `docs/port/reviews/verify-237.md`, branch `n1/u34a-fix`)

Each defect was reproduced first by a test that failed on 7741ba6 (`render_api_fix`, `render_api_oom`, `render_api_noexc`; the new tests are in `native/tests/`).

| | Cause | Fix (file) | Test |
|---|---|---|---|
| D1 | `pump()` tested the cut once a 128-frame quantum and set `total = raw - skip`: rounded up; inside the last quantum the song-end branch won and `hit_limit` stayed 0 | `total = min(avail, max_frames)`, `hit_limit = !over || avail > max_frames` (`render_api.cpp` `pump`) | `render_api_fix` caseMaxFrames: 16 cuts incl. natural-1..-129, plain and with the master latency |
| D2 | `drain` never set `started` | `drain` sets it after its state checks | `render_api_fix` caseDrainStarts |
| D3 | `try` / `catch` do not compile with `-fno-exceptions`; the header said "aborts instead" | `TR_TRY` / `TR_CATCH_*` macros compile the handlers away; header OUT OF MEMORY says exactly what happens (abort) | ctest `render_api_noexc` builds `tests/render_api_noexc.cpp` (includes the file, `-fno-exceptions -fno-rtti -Werror`) |
| D4 | a replacement built the new copy before releasing the old (1.05 GiB at the limit); a 64 MiB hostile song made a 944 MiB handle (measured here; 1195 MB RSS in the verifier's run) | the final state must fit; when old + new do not fit together the old is released first (a failed allocation then leaves the pad removed: header ERRORS (3)); song limit 64 to 16 MiB, vars limit 16 to 4 MiB; measured figures in the header | `render_api_oom` padBudget (peak exactly 512 MiB; before: 768), hostileSizes |
| D5 | a failed `create` leaked what the renderer had made (29 to 44 MB): the core files that build the handle are `-fno-exceptions`, so a throwing allocation skipped their cleanups; also `FxHost` lanes and `FxHost::plan`'s plan were raw owners | `-fexceptions` for the files that make a handle (`core/CMakeLists.txt`) and for the tfx effects (`dsp/CMakeLists.txt`); `FxHost` constructor frees its lanes, `plan()` and `FxBank::load` hold the plan in a `unique_ptr` | `render_api_oom` createFailures: a failure at EVERY allocation index of create for the short songs (180, 678), every 11th of ELECTRO (7734), every effect malloc of ELECTRO; heap (mallinfo2) and live blocks back to start each time |
| D6 | by design the handle is dead after E_MEMORY from read, the header said only E_INTERNAL kills | rule written in the header (ERRORS (2)): dead handle, free only, getters answer; `drain` the same | `render_api_oom` readFailures: 24 injection points in a whole read of ELECTRO with effects |
| D7 | `Fx::operator new` (the web's tfx.h, not edited) does `malloc` + `memset` unchecked | `thunder_fx_malloc` (`dsp/fx_alloc.cpp`, what the effect sources' `malloc` is) throws `std::bad_alloc` (abort without exceptions); `layer_pe.cpp` checks its two raw allocations (`use.bad`); other sites read: `json.cpp` and `core.cpp` use `nothrow` and check; the engine's `new`s throw | `render_api_oom` (test hook `thunder_fx_fail_at`): SIGSEGV before |
| D8 | Cloud Seed's buffers are fixed arrays for 96 kHz (`prepare.sh`); above that a tap is longer than its buffer. The web has no rule (its worklet runs at the context's rate and already reads outside at 96 kHz) | the rate given to `make_cloudseed` is capped at 96 kHz (`fx/fxhost.cpp`, `waves/wmods.cpp`; third-party source untouched); at 44.1 / 48 kHz the audio is unchanged | `render_api_fix` caseHighRates (the sanitizer job sees the index; Release checks finite output); the sanitizer build of the same test |

Wording: above 96 kHz the reverb is therefore shorter than the rate asks for (a documented limit, not a bug of the unit).

### Test gaps
M3 (the pad copy aliasing the host buffer) is killed by `render_api_fix` casePadCopy (the host arrays are overwritten and freed after each set_pad). M8 (drain before the end allowed) by caseDrainStarts (E_STATE before the first read and part way, and the render afterwards equals one that never saw the drains).

### `thunder_render_length_estimate`
Built. A second `Seq` (same song, vars, options and fixed random seed) is stepped to its end without any audio; the result is `seq.time() * sr - latency`, capped at `max_frames`, cached, and the exact length once known. It is an estimate: the voices' tails after the sequencer's end are what only the render can tell (ELECTRO: 4921832 estimated, 4983424 real; the short songs 11.4k frames, the pad tail). Test: `render_api_fix` caseEstimate.

### Can a song reach a pump effect slot? No.
`FxHost::entryFromSlot` maps a song's slot only through `fx_type_of(type)`, which knows the page's 34 wasm effects (0..33) and 93..95; pump kinds (200 and up) are made only by hosts that ask `FxHost::plan` for them (`fxpump.cpp`), and neither `FxBank` nor the render API does. `FxBank::event` (FXC, FXV) only moves values of slots that exist. So `pump_new` / `pump_io` (process-global) are never touched by a render and two handles on two threads share nothing mutable (TSan clean in the verifier's three-thread runs). The header's claim stands, with its reason now stated; a host needs no "one render at a time" rule. (If a future unit lets a song name a pump effect, this changes: the header says so.)

### Measurements for this fix round
- Release GCC 13: full ctest 157 of 157 passed (1 skipped by design: `pumpsynth_refused`); sanitizers (`-DTHUNDER_SANITIZE=ON`, ASan + UBSan + LSan): 157 of 157 (same skip). Both before the last test-only edits (allocator-overhead tolerance, effects guards for MSVC); `ctest -R render` was re-run after them.
- The six CLI WAVs of `render-compare.sh` ELECTRO (plain, `--fx`, `--master`; page and core timing) are byte-identical (`cmp`) before and after.
- Mutations re-applied by hand: M3 (pad copy aliasing the host buffer) fails `render_api_fix` ("the render equals the one made from arrays that stayed"); M8 (drain before the end allowed) fails four checks of the same test.
- D8 before / after: with the cap removed, the sanitizer build of `render_api_fix` stops at `ModulatedAllpass.h:140: index -7 out of bounds for type 'float [4800]'` (192 kHz); with it, 96 / 192 / 384 kHz run clean.
- ELECTRO 48 kHz effects are unchanged (the CLI WAVs above, and the committed goldens in the full ctest).
- MSVC: no tfx (the DSP libraries are not built there), so the read and fx-malloc cases of `render_api_oom` are compiled out there.

## Follow-up after verify-238-239

Report: `docs/port/reviews/verify-238-239.md` (SOUND WITH CONDITIONS: V1 and four test gaps). Branch `n1/render-v1`, cut from `native` at 9de6408.

* **V1, variation replace at the limit (fixed).** `thunder_render_set_variation` built the new copy before freeing the old (peak +512 MiB at the limit), the fault D4 fixed for pads only. Now both follow one rule: the final state must fit; when what the handle holds now plus the new copy fits too, the new copy is built first (a failure leaves the old one); when not, the old copy is released first (a failure leaves it removed). `render_api.cpp` (set_pad, set_variation), header ERRORS (3) and LIMITS say so; the memory sentence ("never holds more than that for pads and variations together") is now true. Test: `render_api_oom varbudget` counts the peak through the replaced `operator new` (peak - base <= 512 MiB + 1 MiB for a same-size replacement, for a replacement after a shrink, and for `set_pad` that drops a variation), and the E_MEMORY outcomes.
* **Page parity: a pad's variations are dropped when its buffer changes.** The page's rule: `core/core-processor.js:337` (`case 'buf'`: `coreBufs.set` / `delete`, then `varDrop(d.pad)`, defined at :239; the same code in `core/seq-core.js:1195` and :1097). `thunder_render_set_pad` now does the same on success (set, replace or remove with `frames == 0`), for every instrument, kind and number of that pad, and the dropped bytes count as freed by the call; a failed call (E_ARG, E_LIMIT, E_STATE) drops nothing. Header: set_pad and set_variation. Test: `render_var` (modes 1 to 5 and another pad's variation) and `render_api_oom varbudget`. This closes "Choices for the lead" item (3) of PICKUP / DISPATCH-NOTES ("`set_pad` does not drop a pad's variations as the page does"). A host sets the pad first and its variations after.
* **Takeover compared with the JS.** The page's `startNote` (index.html:22011): no kit entry returns at :22012; a live pad takes the track over at :22015-22020; the sampler path returns at :22022 (`if (!b) return`) BEFORE the takeover at :22023-22027, so a note whose pad has no buffer leaves the previous note sounding. `sfNote` returns without taking over when the font is missing (:22066) or no zone matches (:22069). The audio-thread core differs: `core-processor.js:658-660` calls `take(ev)` for any note it does not play (and :693 `liveNote` takes over for a live pad), which would cut the previous note for a no-buffer note too. The native side follows the page, which is what an offline render uses and what DISPATCH-NOTES decided: a no-buffer note does NOT take the track over (unchanged, now tested), a live-route / sf-route note and a missing-variation note DO (the sf route always: it has no font input, so it cannot tell the page's two early returns; documented in the header). Nothing was ported: the behaviour already matched the page.
* **Tests** (`render_var`, new; `render_api_oom` +1 case): T1 takeover (live with and without buffer, sf, missing variation: a 5 s note on the track stops, level 0.0; a no-buffer note and an unset-pad note do not stop it; a sampler note does and plays its own pad); T2 failed `set_variation` (NaN, inf, bad rate, null samples, over the frame limit, bad instrument; on an existing and on a new key) leaves the table as it was (render level and `notes_var` / `notes_var_missing`); T3 `THUNDER_RENDER_MAX_VARIATIONS == 4096` hard-coded, 4096 held, the 4097th E_LIMIT, a replacement at 4096 ok, remove one then add; T4 Cloud Seed (type 20) made by an `FxHost` at 192 and 384 kHz is the 96 kHz effect bit for bit (same mapped parameters), and differs from the 48 kHz one, in every build including Release.
* **Header corrections.** `thunder_render_length_estimate` is the START of the last row, so it is below the real length by that row's duration as well as by the voices' tails (ELECTRO: -2.31 %). The memory sentence: see V1.
* **Not from these PRs, a question for the master-chain unit:** see "Conditions for the wiring" (384 kHz master chain 1.5 dB lower in RMS).
* **Mutations re-applied by hand** (each fails `render_var` or `render_api_oom` in Release, the tree restored by `git checkout` after each): M15 live route not taking over (`render_var`: "the long note was taken over", 0.141421 left), also the same for the sf route, a missing variation, and (the other way round) a no-buffer note that takes over; M8 the sample scan moved after `setVar` (`render_var`: output not finite, MUT 6 exists); M11 `THUNDER_RENDER_MAX_VARIATIONS` 4097 (`render_var`, 3 checks); M13 the Cloud Seed cap removed from `fxhost.cpp` make_fx (`render_var`, 192 and 384 kHz not the 96 kHz effect); also V1 (the variation replace not released first: `render_api_oom`, 3 checks: peak, the E_MEMORY outcome) and the pad-drop line removed from set_pad (`render_var`). The same cap is repeated in `waves/wmods.cpp:673`, which this test does not reach (it is the …waves host's own make function).
* **Gates.** Output bit-identical before / after this follow-up on ELECTRO (the real song, the synthetic PadSet pads of the test helpers, 40 s, plain / effects / effects + master chain: FNV hashes `057af78851ad8b6e`, `72c0fe4efe3e0ffb`, `34e61b370e29953a`, the same on `9de6408` and here), standing in for the `render-compare.sh` WAV `cmp` (that script needs the Chrome reference, not available on this machine); `render_api` still compares the API with a direct `SongRenderer` drive bit for bit. No golden touched, no tolerance changed, no `-ffast-math`.
