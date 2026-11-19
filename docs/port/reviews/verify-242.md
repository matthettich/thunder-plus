# Verification of PR #242 (`n1/render-sf`: the SoundFont route into the offline song renderer)

Verifier: independent (did not build it). Head checked: `4907c26`. Built with `-DTHUNDER_SANITIZE=ON` (ASan + UBSan, Debug). No code on the branch was changed; scratch tests lived outside the PR (`scratch/` of the verify worktree, not committed).

## Verdict: SOUND WITH CONDITIONS

No crash, no sanitizer report, no hang, no budget drift, no out-of-bounds in any hostile run (below). Two real defects (a leak on the OOM path, a memory-budget hole for fonts with very many sample headers), one confirmed behaviour difference from the JS that the builder already disclosed (poly cap per pool), and three untested rules (the tests pass with the code broken). None of them corrupts memory. F1 and F2 should be fixed before merge (both small); F3 can go as a known limit if the lead accepts it; F4 should be closed with three tests.

## Findings

**F1. leak (OOM path), fix before merge.** `native/core/src/sf/sf_decode.cpp:70` (`decodeOgg`): `d.insert(...)` can throw `bad_alloc`; the stb_vorbis handle `v` opened at :58 is not closed on that path (no RAII), so it leaks (about 170 KB of decoder setup per event). Before this PR the file was `-fno-exceptions` (a failed allocation aborted); the PR moved it to the with-exceptions list, so the failure now surfaces as `E_MEMORY` correctly but leaks. Repro: operator-new fail-at-k over `thunder_render_set_pad_sf(pad, GM sf3 font, prog 19, keys {60})`, as `render_api_oom` does; LeakSanitizer reports 11 leaks, all `stb_vorbis_open_memory <- decodeOgg <- decodeSample <- prepare_preset <- thunder_render_set_pad_sf`. Fix: close `v` in a small guard object. Everything else on the OOM path is clean (below).

**F2. budget hole, fix before merge (or count it).** `sf_decode.cpp:159` (`prepare_preset`): every `set_pad_sf` allocates `PresetPcm::byIndex`, one `Pcm` (about 64 bytes) per sample header of the font, for each pad, and none of it is counted in `sfBytes` / `padBytes` (render_api.cpp:394-402 count only `pcm->bytes`, the float data). A font with many sample headers costs that per pad, 256 pads times. Repro: a 13.8 MB SF2 with 300,000 shdr records (valid: one preset, one zone; generator `scratch/mkfont.py`), 40 pads on it: `add_font` +57 MB RSS (counted), then 40 `set_pad_sf` calls all return 0 and RSS grows by 790 MB (counted: about 0). 256 pads would be about 5 GB against a 512 MiB budget; a 100 MB font with 2 million headers about 32 GB. Hostile-input only (FluidR3 has a few thousand samples), and a failure is `E_MEMORY` or the OS killer, not memory corruption. Fix: add `font.samples.size() * sizeof(Pcm)` to the pad's counted bytes (and refuse over the budget).

**F3. wrong sound vs the JS, poly tracks that mix pools (builder disclosed it; confirmed and quantified).** JS: one list `T(track)` shared by `startNote` and `sfNote` (`core/core-processor.js:442-443` and `:582-583`), `while (T.length >= POLY) kill(T.shift())`. Here each pool caps its own (`sf_live.cpp` note(), `voice.cpp:152`), and `render.cpp` dispatch calls `pool_.take` / `sfpool_.take` which do not count the other pool. Repro (`scratch/poly.cpp`): poly track (track 4 of 8), organ SF pad plays 4 notes on rows 0-3, a sampler pad note on row 4. JS: the sampler note is the fifth, the oldest organ note is released. Here the first organ note rings on: the render differs from one without that first note by a peak of 0.385 (about full level, a held organ note, forever) from 0.9 s to the end. Control (five SF notes, one pool): identical, as the JS. Mono tracks (the default) are exact. A poly track can ring up to 8 voices instead of 4. Not memory-unsafe. Blocks the merge only if the lead wants byte-for-byte JS on mixed poly tracks; otherwise keep it in the notes as a known difference (it is already there, item 5).

**F4. test gaps: the builder's tests pass with the route broken (mutation run, all against the ASan build).**
- Mutated `render.cpp`, `render_sf` result:
  - `sfpool_.off(e)` removed: FAILS (4 checks). Good test.
  - `sfpool_.take(e, ...)` removed from the sampler path (a sampler note no longer takes over SF voices): FAILS (1 check). Good test.
  - `if (!sfpool_.covers(e, sp)) return;` replaced by "always continue" (a note with no zone for its key takes the track over): PASSES, 856 checks. Nothing tests sfNote's "no zone: no takeover" rule (the notes file and render.h both state it; the test file's section (5) comment says "no zone" but only tests no-sf-prepared and the sampler route).
  - unprepared SF pad no longer takes over (`takeAll` removed on `!sp.font`): PASSES. The rule that an SF-route pad with nothing prepared still cuts the track's previous note (the JS: `mine()` false then `take(ev)`) is untested; behaviour matches the JS today.
  - `sfpool_.mod(e, arp)` removed from the SEV_MOD case: PASSES. No check produces a `SEV_MOD` event on a sounding SF voice (`nMod` is never asserted; the bend is an attribute of the note, not a mod).
- No test for poly (F3), or for the SF route with a choke group across pools.
- Their truncation test steps 61 bytes; I ran every length of both the golden font and a second font (15,048 and 746 lengths): all rejected cleanly (only the full file is accepted), no report.

**F5. note.** Every `SongRenderer` now builds an `SfPool` (32 voices, `sizeof(Voice)` 53,608 bytes: 1.7 MB zeroed) even for songs with no SoundFont pad, and `autoDropped()` walks it. Acceptable on the control thread; just not free.

**F6. note (observability).** `SongRenderer::stats()` (render.cpp:126) does not include `sfpool_.dropped()` (all 32 voices busy), `tOverflow()` or `autoDropped()`, and `peakVoices` is the sampler pool only. A note lost to a full SF pool shows only as `notes_sf` greater than `notes_sf_played`, indistinguishable from "no zone / no preset".

**F7. note.** `add_font` parses before the budget check on the tables, so peak memory during a parse can exceed the 512 MiB by the parser's own table limit (zones/gens/mods bounded at a few hundred MB by `kMax*`; transient, freed on refusal). The `shrink_to_fit` inside parse swallows `bad_alloc` by library design: a failure injected there returns a valid font id (seen at the last four allocations of `add_font`), which is fine.

## What was checked and is clean

Under ASan + UBSan, scratch tests (not in the PR):
- **Truncation, every length** of the 15 KB golden font and a 746-byte one, each accepted prefix also set on a pad with banks 0/1, programs 0-2: no report, no hang (10 s watchdog), ids not consumed by failures.
- **Garbage with a valid RIFF/sfbk header**, 3000 random bodies (half with chunk ids swapped in): all rejected or used safely.
- **Mutated fonts rendered end to end**, 800 (byte flips, 0xff / 0x7f, 32-bit overwrites, random trans -128..128, level +-200, atk/rel up to 100, len 3600): no report, no non-finite output.
- **Random interleavings** of add_font / remove_font / set_pad_sf / clear_pad_sf / set_pad_route (20,000 operations, pads 0-3, ids 0-15, key lists of 0-5 keys including 0 and 127) against a model of which fonts and pads exist: every return code as the model says (remove of a used font is `E_ARG`, id reuse after remove is correct), then a render: clean.
- **Budget drift**: 10,000 add/remove cycles with set/clear between (small font), then 60 replaces of the GM font's preset on 7 pads with different key lists, clear, remove: the largest `set_pad` that fits (134,217,728 frames, the full 512 MiB) is identical before and after both. No leak of budget, no wrap.
- **Key lists**: nkeys 0 with non-null, > 4096, a key of 128, duplicates (builder's tests) and 4096 duplicates: all as the header says.
- **Lifetime**: a pad's preset replaced by another font's, the first font removed, render; clear then render; remove while used: `E_ARG`; set after the first read: `E_STATE` (the builder covers the last). No use-after-free found: `fonts[id]` is heap-stable (`unique_ptr<RenderFont>`), `pads.sf[pad]` is reset in `dropPadSf` before the PCM is freed, and the commit section has no allocation.
- **OOM injection** (`operator new` fails the k-th allocation; copy of the `render_api_oom` technique): `add_font` (golden font) at every index, `set_pad_sf` replace at every index (the pad then renders bit-identical to before: atomic), `set_pad_sf` on the GM SF3 at 40 strides, `add_font` of the GM SF3 at 25 strides: every failure is `E_MEMORY`, live bytes through `operator new` return to baseline, ids and budget unchanged; only the stb_vorbis leak (F1, which uses `malloc`, invisible to the counter, caught by LeakSanitizer).
- **Exceptions**: `add_font` and `set_pad_sf` wrap their bodies in `TR_TRY` with `bad_alloc` to `E_MEMORY` and anything else to `dead` + `E_INTERNAL`; `remove_font` and `clear_pad_sf` allocate nothing and have no try. The audio path (`SfPool::note/render`, `covers`) allocates nothing (the builder's allocation counter over 60 reads; `-fexceptions` on `sf_live.cpp` only adds unwind tables). I found no way for an exception to cross the C API or the read path.
- **Takeover and forwarding vs the JS** (`core-processor.js` `sfNote` :434, `mine` :559, `take` :568): take / off / stp / mod are forwarded to both pools; mono takeover crosses the pools both ways (sampler then SF and SF then sampler leave the other cut, tested by the builder and by my mutation); a note on an SF pad with a prepared preset and no zone for its key returns before taking over (as `sfNote`); a note on a `ROUTE_SF` pad with nothing prepared takes over and is silent (as the JS: `mine()` false, then `take(ev)`); the kill time for an SF voice cut by a mono note is `min(relTc, 0.05)` in both (the JS SF voice has `syn: true`).

The existing `render_api_oom` also passes on this head under ASan + UBSan.

## Reproducing

Scratch sources (not committed to the PR): `hostile.cpp` (truncation, garbage, mutate, interleave, drift, big-table), `oom.cpp`, `poly.cpp`, `mkfont.py` (SF2 writer), `mut.sh` (the five mutations). Build the same flags as `test_render_sf` plus `-DTHUNDER_SANITIZE=ON`.
