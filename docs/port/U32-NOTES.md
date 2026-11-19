**IN THE MIDDLE OF (branch n3/u32-d1): follow-up D1, "decode work budget" (code and tests written; measuring before/after, mutations, full ctest, then the PR). See "Follow-up: decode work budget" at the end of the file.**

# U32: the SoundFont / GM player (SF2 / SF3 parse, Vorbis decode, zones and modulators, layered zones, the pad render, the live note)

Branch `n3/u32` (from `native`). Code: `native/core/src/sf/` (+ `native/core/sf/CMakeLists.txt`, `third_party/prepare-stb.sh`). Tests: `native/tests/test_sf.cpp` (`compare`, `unit`, `chrome`, `ram`), `test_sf_fuzz.cpp`, `rt_harness.cpp` (case `run_sf`), `native/tools/sf-ref.mjs` (Node reference), `sf-chrome.mjs` (the page's decode and render in Chrome), `sf-mutate.py`.

## Second round (after the verification of PR #226, `docs/port/reviews/verify-226.md`)
Each item: status, then the evidence (file:line in this branch). All tests named run in `test_sf unit` (ctest `sf_unit`) unless said otherwise.
1. **Granule trim: DONE.** `decodeOgg` reads the last page's granule from the Ogg page headers and trims the decode to it (`native/core/src/sf/sf_decode.cpp:20-28` scan, `:66` trim; shortening only). Proof for EVERY sample, not three: `native/tests/test_sf.cpp` H4 / G2 (about line 409-430) decodes all 1186 SF3 samples of the GM font with the default options and compares each frame count with its last page's granule parsed independently from the Ogg bytes: 1186 of 1186 equal, 0 shorter, 0 bad; samples 234, 235, 704 (37067 / 33493 / 60416) and 183, 859, 948, 8, 9, 10 at the libvorbis numbers (9 of 9). Patched granules 0, 1, 100, 2296..2298, 5000, 2^40 behave as libvorbis (8 of 8). The libvorbis comparison itself (libsndfile 1.2.2) is in "The missing frames" above.
2. **Sample sizes clamped to the data, memory budgets: DONE.** SF2: `sf_decode.cpp:116-118` (frames = min(claimed, frames the smpl chunk holds)); SF3: frames bounded by the stream's bytes (`sf_decode.cpp:54`, 4096 frames per byte) and by the granule. Budgets per preset (`DecodeOpts::presetBytes`, `sf.h:87`) and per font (`fontBytes` and the host counter `fontUsed`), applied while decoding (`sf_decode.cpp:131-138`). Tests H1 (a 200-byte chunk whose header claims 2^25 frames costs 400 bytes, not 1536 MB; the last frame is read, none past it), H2 (status `kPrepOverPresetBudget` / `kPrepOverFontBudget`, nothing kept, render 0, counter), H3 (SF3 refused before and after resampling).
3. **`kMaxWork` lowered: DONE.** `sf.h:27`: 200000 (was far higher; the GM font needs 10027). Test H5: 30000 x 30000 zones refused (`kParseTooBig`) in about 11 ms (was about 8 s), 400 x 400 = 160000 pairs still parses. Mutation "parse work cap lifted" is caught.
4. **Absurd sample rates refused: DONE.** Three places: the Ogg stream's rate 4000..192000 Hz (`sf_decode.cpp:52`), the sample header's rate 400..192000 Hz (`sf.h` `kMinHdrRate`, check at the top of `decode_sample`), the context rate `DecodeOpts::ctxRate` 0 or 4000..192000, NaN / inf / negative refused (same place). A refused sample is the JS's own fallback (1 silent frame at 48 kHz). Tests H4 (stream rates 0, 1, 7, 3999, 192001, 2^31-1, 2^32-1 refused, 4000, 44100, 192000 accepted, each at context 0 and 48000, each under 5 s) and H6 (header rates 0, 1, 399, 192001, 2^32-1; context rates -1, 1, 3999, 192001, 1e300, inf, NaN: 19 of 19).
5. **Reference data for the fitted Chrome resampler, loop end beyond the data: DONE.** `native/tests/golden/sf_resampler_chrome.json` (Chrome 153's own output for fixed noise inputs, made by `native/tools/sf-resampler-ref.mjs`), checked by test R (`test_sf.cpp` about line 453): length equal to Chrome's and within -85 dB (measured -107 dB or better). Loop end beyond the data: golden cases `syn_loop_beyond` and `syn_loop_beyond_live` (`native/tests/golden/sf_cases.json:241`, `:888`, bit-exact against the JS) AND a behavioural test H8 that does not depend on a hash: with a loop inside the data the second half of the note has energy (3.76e3), with the loop end past the data (150 > 100 frames) the second half is exactly 0 (the note ends with its data, the page's guard). Mutation "live loop guard" is caught.
6. **Parity mode off by default: DONE.** `sf.h:85` (`pageTail = false`), `ctxRate = 0`. Test H7 asserts `DecodeOpts()` has `pageTail == false` and `ctxRate == 0`, and `DecodeOpts::page(48000)` turns both on. Mutation "parity mode ON by default" is caught.
7. **PINS.md licence quote exact: DONE.** `native/core/third_party/PINS.md` now holds the licence block of `stb_vorbis.c` at the pin (lines 5664 to 5702 of the file, sha256 `b52cf518...60a6`) as a fenced block copied by script, no `[...]`; checked byte for byte against the fetched file (the block is a substring of it, and equal to lines 5664..5702).

Also found by the CI of this PR (two more real failures, not on the list; the CI of 33ed71d had Windows and macOS red, the rest green):
- **macOS arm64, test `sf`:** `sf_log10 differs from Math.log10 in 165 of 40265 inputs` and `sfVoice (gm)`. Cause: the comparison used the LOCAL Node as the reference, and Node on macOS arm64 fuses multiply-adds inside `Math.log10` (the repository already knows this: `native/tests/CMakeLists.txt:173-176`, the libm tests use a committed golden for that reason). Reproduced on Linux by compiling `sf_math.cpp` with `-ffp-contract=fast -mfma`: 161 of 40265 differ, with `-ffp-contract=off` (what the core uses) 0. Native is right (strict, = Node on x86-64). Fix without loosening anything: `sf_golden.inc` (regenerated by `sf-ref.mjs --golden`, the render rows are unchanged) now also commits the FNV hash of Node 22 / Linux x86-64's `log10b` (30000 inputs from an integer LCG, no libm in the inputs), `src` and `voice_gm`; `test_sf compare` requires the native results to equal those hashes on EVERY platform (`mathGolden`, `test_sf.cpp` near line 41), and keeps the comparison with the local Node, fatal only where Node is the reference (Linux x86-64). Not verifiable here: the macOS run itself (see the CI of the next push).
- **Windows MSVC:** `sf_decode.cpp` used `M_PI` (undeclared in MSVC): now a local constant `kPi`.
- **Windows configure** (first failure of this PR): fixed as follows.
  `third_party/fetch.sh` passed sparse-checkout patterns starting with `/` (`/stb_vorbis.c`) to git under Git for Windows' sh, where MSYS rewrites them into `C:/Program Files/Git/...`: nothing was checked out and the configure failed ("cp: cannot stat ... stb_vorbis.c"). Fixed with `MSYS_NO_PATHCONV=1` / `MSYS2_ARG_CONV_EXCL="*"` in `fetch.sh` (a no-op elsewhere). Not verifiable from this machine (Linux): see the CI of the push after this one.

## The decoder
stb_vorbis from nothings/stb, commit `9600522de53973336fa033a289858a4354cbb04c` (v1.24, MIT or public domain; the licence text is copied from the fetched file into `third_party/PINS.md`). `fetch.sh` pins it (alone with `THUNDER_FETCH_STB_ONLY=1`, so the closed build needs nothing else), `prepare-stb.sh` copies it for host builds. No patch was needed: the fuzz found no fault inside stb_vorbis. Only `stb_vorbis_open_memory`, `get_frame_float` and `get_info` are used (`STB_VORBIS_NO_STDIO`, `NO_PUSHDATA_API`, `NO_INTEGER_CONVERSION`); decoding runs on the control thread only, at preset preparation.

## What is ported (the JS is the specification)
`parseSF`, `sfSrc`, `sfVoice`, `sfPick`, `sfPreset`, `sfSample` (SF2 int16 and SF3 Ogg), `sfPreparePreset`, `sfRender` (pad render) and the live note (`sfNote` / `sfRun`, class `SfPool`, same seam as `VoicePool`: `SeqEvent`, `VoiceKit`, take / off / mod / stp / stopAll / render).

## Layer (a): same decoded PCM into the page's JS and native (Linux x86-64, GCC, Release)
Parse text of both fonts identical (GM font: 197 presets, 9455 zones, 1186 samples); `Math.log10` and `sfSrc` bit-identical; `sfVoice` bit-identical for every zone of the GM font at several keys and velocities; `sfPick` identical for every preset x 128 keys x velocities; pad render 21 of 21 cases bit-identical; live note 8 of 8 cases bit-identical at the web copy's 128-frame blocks. Hashes committed in `native/tests/golden/sf_golden.inc` (29 rows; the test font's on every platform, the GM font's on Linux x86-64 only, because stb's setup uses libm). Live = render: 40 of 40 runs at host blocks 64, 17, 1, uneven and one block equal the 128 run bit for bit.

## The missing frames (cause found, with evidence)
Chrome's `decodeAudioData` returns fewer frames than the Ogg stream holds for short samples. The granule positions are not the cause. What each decoder does (all 1186 samples of the font, each decoded by Chrome at a context rate equal to its own, so no resampling hides in the count):
- **libvorbis is the arbiter** (libsndfile 1.2.2 / python soundfile 0.14): it returns exactly the LAST PAGE's granule position as the frame count (the Vorbis I rule; a smaller granule cuts more, a larger one changes nothing).
- **stb_vorbis only trims the LAST BLOCK** to the granule. It equals libvorbis (count and samples, -141 dB) except where the granule cuts more than the last block: samples 234, 235, 704 (multi-page streams) came out of stb at 37120 / 33920 / 60544 frames where the granule, libvorbis and Chrome say 37067 / 33493 / 60416. An earlier version of these notes called those three "Chrome exceptions"; they were stb's, and the earlier native default kept 53 / 427 / 128 extra tail frames for them.
- **Native now equals libvorbis exactly** (`decodeOgg`, our wrapper, no patch inside stb): it reads the last page's granule from the Ogg page headers and trims the decoded length to it, only ever shortening (a granule above the decoded length, or none, leaves the length as decoded; a granule of 0 gives an empty stream, which is refused as a decode failure, as libvorbis returns 0 frames). `test_sf unit` checks it on the whole font: all 1186 samples have exactly their last page's granule as frame count (0 end before it), and samples 234, 235, 704, 183, 859, 948 and the long 8, 9, 10 are at the libvorbis numbers.
- **Chrome 153 (and ffmpeg, differently) loses frames when the audio of a stream is on ONE Ogg page** (the last, which is also the first audio page; the granule is never smaller than what the packets decode to, so there is no start trim): it drops min(last frame, short block size / 2). 1165 of the 1186 samples follow this exactly; 17 more are the same rule applied to tiny streams (48 to 126 frames: the whole stream goes, Chrome reports a decode error and the page falls back to 1 silent frame); the only real exception is sample 253 (single page, Chrome drops 128, the rule says 123).

| sample (stream rate) | last granule | libvorbis | stb_vorbis, raw | native default | ffmpeg 6.1.1 | Chrome 153 |
|---|---|---|---|---|---|---|
| 183 (48000) | 2297 | 2297 | 2297 | 2297 | 2304 | 2176 |
| 948 (44100) | 12462 | 12462 | 12462 | 12462 | 13376 | 12352 |
| 906 (44100) | 20640 | 20640 | 20640 | 20640 | 20512 | 20512 |
| 859 (44100) | 4633 | 4633 | 4633 | 4633 | 4505 | 4505 |
| 1075 (44100, 5 pages) | 62568 | 62568 | 62568 | 62568 | 62568 | 62568 |
| 234 (5 pages) | 37067 | 37067 | 37120 | 37067 | | 37067 |
| 235 | 33493 | 33493 | 33920 | 33493 | | 33493 |
| 704 | 60416 | 60416 | 60544 | 60416 | | 60416 |
| 0 (48000, 48 frames) | 48 | 48 | 48 | 48 | 128 | decode error (page: 1 silent frame) |

- Why the sound differs so much in the page, not just a few frames: loop points are frames from the sample start and the page computes them from the header, so a short looped sample's loop end lies BEYOND Chrome's shortened data (sample 183: loop end 2202, Chrome length 2176). The page's own guard (`a.le <= a.d.length`, `index.html` `sfNote` and `core/seq-core.js:1316`) then drops the loop and the note stops after a short burst (square lead: 0.06 to 0.08 s, measured in the real page in Chrome 153); native by default sustains it. **Impact in the font (independent check):** 159 samples and 65 of the 197 GM presets have a looped zone whose loop end lies beyond Chrome's decode (electric pianos, vibraphone, guitars, brass, leads, sitar, koto, kalimba, Synth Bass 1 to 4): in the web app on Chrome those notes lose their loop. This is a defect of the web app today (the page has no way to see it).
- Why "loop points match for 10 of 10" held anyway: that unit check compares the loop numbers (`round(ls * k)` from the header, independent of how many frames decoded). It never said the data was as long as the loops. The frame counts are now checked separately.
- Parity mode (kept, off by default): `DecodeOpts::pageTail` / `DecodeOpts::page(rate)` reproduces Chrome 153's decode (the lost frames, plus the resampler) for the layer (b) gate and for a host that wants the page's sound, loop defect included. It is a defect copied on purpose, hence a choice for the lead.

## Sample rate: what the page does, what native does
- Page: `decodeAudioData` resamples EVERY SF3 sample to the AudioContext rate at decode, so `sr` of the decoded buffer is the context rate and `ls` / `le` are `round(header ls * ctxRate / header sr)`; `sfRender` / `sfRun` then play at `a.sr / sr` (1 when the rates match).
- Native default (`ctxRate = 0`): each sample keeps its own rate, no resampling at load; the player reads it at `a.sr / sr` with linear interpolation (so the resampling happens at playback, lower quality than the page's windowed sinc, and half-sample different).
- `ctxRate > 0`: resampled at load the way Chrome does. The resampler was identified by fitting Chrome's output (Blink's SincResampler: 32-tap Blackman window alpha 0.16, 32 kernel sub-sample offsets with linear interpolation, cutoff 0.9 of the lower Nyquist, no normalisation, output `floor(n * ratio)`): native matches Chrome's resampled samples to -107 to -109 dB (long 44.1 kHz samples, context 48 kHz) and to -135 dB where the same filter sees the same input (context 44.1 kHz, the 22.05 / 32 kHz samples), at context 44.1 and 48 kHz.
- Cost of `ctxRate`: RAM: the 44.1 kHz samples grow 9 % at 48 kHz (piano 191 MB to 208 MB, a 22.05 kHz sample x2.18), CPU: about 64 multiply-adds per output frame, on this (loaded, 2-core) machine 0.1 s extra for the bass preset, 0.2 s for the drum kit and about 13 s extra for the whole piano keyboard, roughly 1.5 s per octave (so decode only the keys in use, below), once at preparation, never on the audio thread. For parity with the page the wiring needs `DecodeOpts::page(engineRate)`.

## Layer (b): native vs the page in headless Chrome 153 (HeadlessChrome/153.0.0.0, Linux x86-64, captured 2026-11-18)
One command (from the repo root, with a built `test_sf`; Chrome on the PATH, no npm package): `node native/tools/sf-chrome.mjs OUTDIR --test BUILD/tests/test_sf` (rates default `44100,48000`; it captures `decodeAudioData` of 13 samples and `sfRender` of 11 GM cases of `native/tests/golden/sf_cases.json` into `OUTDIR/c<rate>/` and prints this table). Difference = 10 log10(sum (native - page)^2 / sum page^2), whole render. "pageTail" = `DecodeOpts::page(rate)`.

| case (render rate) | before this change (Kaiser sinc, ctx 48k) | ctx 44.1k, default | ctx 44.1k, page | ctx 48k, default | ctx 48k, page |
|---|---|---|---|---|---|
| piano C4 (48k) | -88.9 | -135.7 | -135.7 | -109.5 | -109.5 |
| piano C2 (44.1k) | -89.1 | -135.9 | -135.9 | -109.2 | -109.2 |
| bass (48k) | -81.6 | -132.4 | -132.4 | -104.2 | -104.2 |
| bass +12 (22.05k) | -82.6 | -131.7 | -131.7 | -104.4 | -104.4 |
| drum kit, kick (48k) | -64.5 | **-64.5** | -137.2 | **-64.5** | -116.6 |
| drum kit, snare (48k) | -73.8 | **-76.0** | -136.7 | **-76.0** | -112.1 |
| strings | -79.7 | -135.4 | -135.4 | -134.0 | -134.0 |
| square lead | +11.3 | **+11.3** | -106.7 | **+11.3** | -137.5 |
| warm pad | -80.4 | -135.4 | -135.4 | -135.3 | -135.3 |
| vibes | -3.7 | **-3.6** | -136.0 | **-3.7** | -109.5 |
| missing preset (bank 99) | -69.0 | -135.1 | -135.1 | -107.2 | -107.2 |

- The exit test (within -80 dB of the page render for a piano, a bass and a drum-kit note): piano and bass pass in every column; the drum kit passes ONLY with `pageTail` (kick -137.2 / -116.6, snare -136.7 / -112.1) and fails by default (-64.5 / -76.0) for the single reason above: Chrome's lost last frame (with the samples' own rate as context, no resampler involved: -137.2 dB once the frames are matched). Nothing in this table is above -80 dB once `pageTail` is on; the cases that fail by default fail only because native keeps the frames libvorbis keeps. The lead decides whether the default stays libvorbis-correct (current) or page-identical.
- Decode level (13 samples, both rates, printed by `test_sf chrome`): equal rate -135 to -136 dB; resampled -107 to -109 dB (the long 44.1 kHz samples), -100 to -116 dB for short ones; the 48 to 44.1 kHz down-conversion of sample 183 is -104.9 dB with `pageTail` (-55.7 dB without: the lost frames sit inside the filter's reach).
- Not measured: other Chrome versions, other OSes (Safari / Firefox / Android WebView decode with their own libraries and resamplers).

## Fuzz under ASan + UBSan (`-DTHUNDER_SANITIZE=ON`, `test_sf_fuzz ITER SEED`; ctest runs `200 1`)
Parser: the test font and the GM font truncated, with bits flipped, 32-bit fields set to extremes (chunk sizes, counts, offsets), blocks swapped; whatever parses is used (pick, voice, key-listed preparation, render, live note). stb_vorbis: real Ogg streams of the font corrupted (flips, zeroed spans, truncation, swapped blocks, the setup headers attacked on purpose) through `decode_sample`, with and without `pageTail` and resampling. Voice: fonts built directly with several extreme generators, modulators and sample fields at once, extreme rate / gain / pan / bend / vibrato / cut / release through the live pool and the render. Results are in the final section. A corrupt stream can decode to inf / NaN: the whole sample is refused (fallback of 1 silent frame).

## Memory (a wiring condition)
- Storage: float32 at the sample's own rate (`Pcm::d`, `std::vector<float>`), only the samples the preset's zones reference (plus a stereo partner); not float64, not resampled unless `ctxRate` is set. The JS holds Float32 AudioBuffers too, so float32 keeps layer (a) bit-exact. int16 storage would halve it but changes the bits (and the page's), and is NOT done.
- What actually cuts it: `prepare_preset(font, preset, opts, out, keys)` with a 128-flag key list decodes only the zones that cover a listed key (any velocity), with the same bits. Measured on `FluidR3Mono_GM.sf3` (`test_sf ram`, own rates):

| preset | all keys (before = after) | 3 octaves C3..B5 | 1 octave C4..B4 | all keys, resampled to 48 kHz |
|---|---|---|---|---|
| Grand Piano (145 samples) | 191.4 MB | 83.6 MB (69 samples) | 27.7 MB (25) | 208.4 MB |
| Fingered Bass (9) | 2.5 MB | 0.2 MB (1) | 0.2 MB (1) | 5.5 MB |
| Standard drum kit (61) | 12.3 MB | 8.4 MB (35) | 1.1 MB (12) | 13.4 MB |
| Strings (17) | 5.4 MB | 3.2 MB (11) | 1.2 MB (4) | 8.1 MB |

  Whole font decoded at once: 416.1 MB (never do this). The font bytes (23.7 MB) are a view, not copied (a mapped asset on Android); parse is 2 ms and 1.7 MB of tables.
- **Budgets (hostile or oversized fonts):** `prepare_preset` returns a `PrepareStatus` and refuses a preset whose decoded PCM would exceed `DecodeOpts::presetBytes` (default 256 MB: the real font's largest preset, the whole-keyboard piano, is 191 MB, 208 MB at 48 kHz) or, with the host's counter `DecodeOpts::fontUsed` (the host adds on success through `prepare_preset` and subtracts a `PresetPcm`'s `bytes` when it drops it), `DecodeOpts::fontBytes` (default 512 MB): status `kPrepOverPresetBudget` / `kPrepOverFontBudget`, nothing is kept, the pad plays nothing. The budget is applied while decoding (a sample is refused as soon as it would not fit, before and after resampling), so a hostile header cannot allocate first. Every sample is also clamped to the data present: SF2 to the frames the smpl chunk really holds (a 5.6 KB font with 12 headers claiming 2^25 frames each once made `prepare_preset` allocate 1536 MB; it now costs the bytes that are there), SF3 to what the stream's own bytes can hold (4096 frames per byte) and its granule. Tested in `test_sf unit` (hostile sections).
- **Wiring condition:** "one preset prepared at a time, before play, off the audio thread": the host prepares a preset's PCM (with the key list of the notes its song and its pad can play) BEFORE the pad plays, keeps one `PresetPcm` per active SoundFont pad (the sum of their sizes is the budget: the full piano is 191 MB, too much for the Android target, 28 MB per octave is not), and drops it when the pad changes. A key whose zones were not prepared plays nothing (the live note is refused and counted, the render returns 0).

## Choices for the lead (where this departs from the SoundFont spec or from the JS)
Spec departures (all because the JS does it; kept on purpose):
1. Velocity-to-attenuation default modulator is skipped (`dst 48`, source velocity); `sfSrc` curves use `Math.log10`; generator add for preset level follows the JS `padd` rules; a layered preset plays at most 4 zones, a stereo pair counts as one.
2. Only channel 0 of the decoded Ogg is used (`getChannelData(0)`); the player is mono per zone with the zone's own pan.
3. The live automation keeps the JS's `Auto` quirks: `constFrom` looks at the last event only; `prune` is not value-preserving for target and curve events.
4. Loop: only when mode is 1 or 3 and `le > ls + 4`; SF3 loop points are frames from the sample start, rescaled by `round(ls * k)`.
5. Modulators merge by identity (src, dst, amount source, transform); modulator sources beyond velocity, key and constant are not supported (as the JS).

Choices about decoding (for the lead to decide):
6. The default keeps every frame (libvorbis); the page (Chrome) loses the last frame of one-page streams: `DecodeOpts::pageTail`. The default rate is each sample's own; the page resamples to the context rate: `DecodeOpts::ctxRate`. `DecodeOpts::page(rate)` is the page-identical combination.
7. The resampler is a fit of Chrome's (Blink's), not a copy of its source (the source was not reachable from this session); it matches the page's output to -107 dB or better on the tested samples. A different browser resamples differently.

Where native is stricter than the JS (the JS would read undefined, give NaN, or run out of memory):
8. Load limits (`sf.h`): file 2 GiB, 262144 zones, 4 M modulator records, 128 M zone-pair work items, 33 M frames per sample, 32 nested LISTs. Over a limit: `parse` fails, nothing half built. Truncated or inconsistent chunks fail with a status; bag / generator slices are clamped as `slice` clamps.
9. Generators above 63 and modulator destinations above 63 are dropped (the player reads 0..58 only).
10. A loop that starts before the sample (`ls < 0`) is not looped, and a read position that is negative or NaN stops the voice (the JS reads `undefined` and produces NaN).
11. A sample that fails to decode, is too long or over budget, has a stream sample rate outside 4000 to 192000 Hz, or decodes to inf / NaN becomes the JS's own fallback (1 silent frame at 48 kHz). An SF2 sample whose header claims more frames than the smpl chunk holds is clamped to the frames present (the JS would pad zeros).
11b. Parse limits tightened to 20x the real font (FluidR3 GM: 10027 zone pairs, 9455 zones, 26265 modulator records): `kMaxWork` 200000 pairs (a 30000 x 30000-zone, 720 KB font used to keep `parse` busy for about 8 s CPU before refusing; it is refused in 11 ms now; 400 x 400 = 160000 pairs still parses), `kMaxModPool` 2^19.
12. Live time: voices are timed on the host's absolute frame count in 128-frame quanta (`(n & ~127) / sr + (n & 127) / sr`), so the sound is independent of the host block size (the JS's `currentTime + i * dt` at 128-frame blocks, same bits). Automation is pruned only at quantum edges and `constFrom` is asked at the quantum start.

## Conditions for the wiring (U34 / U37)
- Prepare before play, per preset, with the key list; keep `Font` (and its bytes), `Preset` and `PresetPcm` valid until the pad's voices have ended.
- Decide the decode options once: `DecodeOpts::page(engineRate)` for page parity, or the defaults. Never decode on the audio thread.
- Events must be handed in before their time (the engine's lead): an event whose time has passed is taken at the current frame.
- One `SfPool` next to the `VoicePool`; a note played by the sampler must also be given to `SfPool::take()` (mono takeover / chokes across both pools), and the other way round. NoteOff, stp and stopAll must never be dropped.
- Counters to surface: `dropped()` (all voices busy), `tOverflow()`, `autoDropped()`.
- Build: the closed build (no LXR, DSP, pump) builds; stb is fetched alone (`THUNDER_FETCH_STB_ONLY=1`).

## Mutations
35 seeded faults (the last six, from round 2 (the sixth is a last-bit change of `sf_log10`, caught by the committed math golden): header-rate floor, context-rate refusal, parity default, work cap, SF2 clamp) (`native/tools/sf-mutate.py`; from the repo root, `SF_BUILD=<build dir> python3 native/tools/sf-mutate.py [numbers]`), each must make `ctest -R '^sf(_unit)?$'` fail. Mine: sfSrc constant, velocity-modulator skip, mod-env sustain clamp, layer limit, stereo-pair zone, preset fallback bank, render loop minimum, release time constant, render normalisation, SF2 int16 scale, SF3 loop rounding, live quantum 128 to 64, prune at every piece, unstable preset sort, pageTail cap, pageTail single-page condition, key-list filter, SF3 flag detection, granule 0, granule trim one frame too many, stream-rate floor. The independent verifier's 8, now covered: live loop guard (`le <= n`: a new loop-end-beyond-data case in the render and live cases of `sf_cases.json`, compared with the real JS), live release constant, resampler cutoff 0.9 to 0.89 (reference data from Chrome, below), pitch-correction sign, live loop wrap off by one, mono pan right channel, SF2 frames-present off by one, stp kill constant. One mutant ("granule trim: `granule + 1 < size`") is equivalent (stb already cuts the last block to the granule, so the two never differ) and was replaced. Before this round five of the verifier's mutants survived or were caught only by the fuzz crashing.
- Resampler reference: `native/tests/golden/sf_resampler_chrome.json` = Chrome 153's `decodeAudioData` output for a 1500-sample LCG-noise WAV at 44.1 to 48 kHz, 48 to 44.1, 32 to 48 and 22.05 to 44.1 kHz (captured 2026-11-18 by `node native/tools/sf-resampler-ref.mjs`); `test_sf unit` requires native within -85 dB (it measures -90.5 to -93.0 dB, whole signal, edges included), so a change of the filter fails in ctest without Chrome.
- Loop end beyond the data: `syn_loop_beyond` (render) and `syn_loop_beyond_live` (live) use a new sample of the test font whose loop end (700) lies past its 500 frames; both are bit-exact against the real JS (the JS drops the loop, `core/seq-core.js:1316`). In the pad render the guard is implicit (the read runs off the data before the loop end), in the live path it is the explicit `le <= n` of `sf_live.cpp`.

## Final run
Date 2026-11-18, GCC 2-core machine, `nice`, `-j2`, head commit of this round (code identical to the last push before this notes edit). Measured by the second-round worker (me):
- Default build (LXR, DSP, pump ON), Release GCC, full `ctest -j2 --timeout 3600`: **151 tests, 0 failed, 1 skipped** (`pumpsynth_refused`, closed-build only).
- Sanitizer build (`-DTHUNDER_SANITIZE=ON`, ASan + UBSan), full `ctest -j2 --timeout 3600` (includes `engine_xeng_*` and `sf_fuzz 200 1`): **151 tests, 0 failed, 1 skipped** (`pumpsynth_refused`). This run was on the code before the macOS reference fix and the `kPi` edit, which touch only `test_sf.cpp`, `sf-ref.mjs`, `sf_golden.inc` and one constant in `sf_decode.cpp`; the Release run and the closed run below are on the final code.
- Closed build (`THUNDER_WITH_LXR=OFF THUNDER_WITH_DSP=OFF THUNDER_WITH_PUMP=OFF`, `THUNDER_DEPS_DIR` an empty folder, so stb alone was fetched by `THUNDER_FETCH_STB_ONLY=1`): **107 tests, 0 failed, 5 skipped** (fxzero, pump, pumpsynth, pumpsynth_fuzz, fx: they need pump).
- Mutations (`SF_BUILD=<dir> python3 native/tools/sf-mutate.py`): 35 seeded faults (the last six: header-rate floor, context-rate refusal, parity default, work cap, SF2 clamp, sf_log10 last bit), **35 of 35 CAUGHT** (34 in one run on the final tests, the 35th run alone).
- Not re-run by me in this round, as written by the earlier session (kept for the record, unverified by me):
  - Fuzz, ASan + UBSan, `test_sf_fuzz 1200 21` (this code; earlier `1500 11`, `1500 7` on the previous): 4800 + 1200 mutated SF2 files (3295 + 829 parsed and fully used), 150 mutated GM font copies (115 used), 12000 corrupted Ogg streams (2732 decoded, 9268 refused; with and without `pageTail`, resampling and small budgets), 1200 extreme-field fonts (298 notes, 7200 blocks): no sanitizer report, no stb_vorbis patch. `ctest -R '^sf'` under ASan + UBSan: 6 of 6 (the fuzz run separately).
  - Layer (b) re-run after these changes with `node native/tools/sf-chrome.mjs OUT --test .../test_sf`: the table above, unchanged (default: kick -64.5, snare -76.0, square lead +11.3, vibes -3.7; with page parity nothing above -104 dB).
- `rt_harness` `run_sf` (no allocation on the audio thread) is part of the 151 / 107 above and passed.

## Unverified or not done
- int16 storage (halves the RAM, changes the bits) is not done; the key list is the reduction.
- Chrome is the only browser measured (153, Linux); the resampler is a fit.
- Only the Linux x86-64 hashes of the GM font are committed.
- No adapter into the mixer / bridge (out of scope).

## Follow-up: decode work budget
Cause of D1 (`docs/port/reviews/verify-226-round2.md`): `prepare_preset` charged only the bytes it KEPT. `decodeOgg` decoded a stream in full and only afterwards applied the last page's granule, and a granule of 0 trims the stream to nothing (`kDecodeBad`), so the decode was thrown away; `add()` repeated that for every sample header, and headers may alias the same stream any number of times. Nothing bounded the work.

Fix (all in `native/core/src/sf/`):
1. **Granule read before decoding** (`sf_decode.cpp`, `decodeOgg`, the `oggScan` call and `if (sc.lastGranule == 0) return kDecodeBad;` at its top): the last page header's granule 0 is refused with no decode and no charge. Only 0 is refused up front: a granule ABOVE the decoded length only shortens nothing (test H4b: 2^40 decodes to 2304 frames, as libvorbis), so refusing "absurd" ones would change results.
2. **Cache by byte range** (`StreamCache`, `decodeSample`): key = (offset, length) within the font's smpl chunk; success (index of the first sample holding the PCM, copied for the next alias, bounded by the byte budgets) and failure are remembered per `prepare_preset` call, so aliased headers decode once. A refusal for the budget is not remembered (it ends the preset). A copy is not charged work (memcpy, already bounded by the byte budget).
3. **Work budget** (`sf.h` `DecodeOpts::presetWork` 2^28, `fontWork` 2^29, host counter `fontWorkUsed`; `kWorkPerOpen` 32768; `PresetPcm::work`; `PrepareStatus kPrepOverWorkBudget`): every decode that runs, kept or discarded, is charged kWorkPerOpen + stream bytes + decoded frames (+ resampled frames; SF2: frames copied), taken from the smaller of the preset's and the font's room while it runs; when it runs out the preset is refused with nothing kept. After a refusal `PresetPcm::work` is what the call spent (informational; the host's counter is untouched). The font counter has the lifecycle of `fontUsed`: a kept preset adds its `work`, the host subtracts it on drop. Calibration (measured, 2 cores): a long stream decodes at 80 to 90 million units per second, an open costs 0.35 ms (setup header) = about 29000 units, hence kWorkPerOpen; the limits are about 3 s and 6 s of decode work. The most any GM preset needs is 119,206,389 units (whole keyboard at 48 kHz): limit 2.25x that.
4. **Parse memory** (`sf_parse.cpp`, before a zone is added; `sf.h` `kParseTableFactor` 32, `kParseTableFloor` 8 MB): zones + generators + modulators (element sizes times counts) may hold at most 8 MB + 32 x the file's size, else `kParseTooBig`. What allocated: every zone pair copies its merged generators (up to 64 x 8 bytes) and modulators into `Font::zones/gens/mods`; 200000 pairs x 50 generators = about 94 MB of tables, 205 MB with vector growth. GM's tables are 0.02x its file; the test font is under the floor.

Tests (`test_sf unit`, new blocks H9 to H12; the fonts are generated in the test from the GM font's own streams, `aliasSf3` / `genSf2` / `zeroLastGranule`; stb_vorbis does not check the page CRC, so the CRC is not fixed):
| | before | after |
|---|---|---|
| 30000 headers on one 4003-byte stream, granule 0 (1.74 MB font) | 12.2 s | 0.001 s, work 0 |
| 2000 headers on one 62410-byte stream (1.29 MB decoded), granule 0 | 9.2 s | 0.000 s, work 0 |
| 128 distinct granule-0 streams, 8 MB | 0.69 s | 0.000 s |
| 12000 aliases of one good stream | (decoded 12000 times) | 0.024 s, work = one decode |
| 175 KB font, 200000 pairs x 50 generators: counted peak of operator new | 208.5 MB, parsed | 26.5 MB, `kParseTooBig`, 18 ms |
| the same shape, 20000 pairs (81 KB) | parses, 13.3 MB | parses, 13.3 MB |
| GM font, all 197 presets x (own rate, 48 kHz): status, samples, bytes, FNV of every sample | `test_sf gmall` | identical in all 394 lines; time 39.2 s before, 36.7 s after (noise) |
(Times: this machine, Release, shared 2 cores; the verifier's 17.7 s / 13.5 s were on a loaded box. Peak allocation is counted with a replaced `operator new` in `test_sf.cpp`, not RSS.)
Mutations 36 to 39 in `native/tools/sf-mutate.py` (work not charged on a discarded decode, cache keyed by offset only, granule check skipped, parse table bound lifted): all CAUGHT.
