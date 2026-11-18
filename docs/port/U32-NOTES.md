# U32: the SoundFont / GM player (SF2 / SF3 parse, Vorbis decode, zones and modulators, layered zones, the pad render, the live note)

Branch `n3/u32` (from `native`). Code: `native/core/src/sf/` (+ `native/core/sf/CMakeLists.txt`, `third_party/prepare-stb.sh`). Tests: `native/tests/test_sf.cpp` (`compare`, `unit`, `chrome`, `ram`), `test_sf_fuzz.cpp`, `rt_harness.cpp` (case `run_sf`), `native/tools/sf-ref.mjs` (Node reference), `sf-chrome.mjs` (the page's decode and render in Chrome), `sf-mutate.py`.

## The decoder
stb_vorbis from nothings/stb, commit `9600522de53973336fa033a289858a4354cbb04c` (v1.24, MIT or public domain; the licence text is copied from the fetched file into `third_party/PINS.md`). `fetch.sh` pins it (alone with `THUNDER_FETCH_STB_ONLY=1`, so the closed build needs nothing else), `prepare-stb.sh` copies it for host builds. No patch was needed: the fuzz found no fault inside stb_vorbis. Only `stb_vorbis_open_memory`, `get_frame_float` and `get_info` are used (`STB_VORBIS_NO_STDIO`, `NO_PUSHDATA_API`, `NO_INTEGER_CONVERSION`); decoding runs on the control thread only, at preset preparation.

## What is ported (the JS is the specification)
`parseSF`, `sfSrc`, `sfVoice`, `sfPick`, `sfPreset`, `sfSample` (SF2 int16 and SF3 Ogg), `sfPreparePreset`, `sfRender` (pad render) and the live note (`sfNote` / `sfRun`, class `SfPool`, same seam as `VoicePool`: `SeqEvent`, `VoiceKit`, take / off / mod / stp / stopAll / render).

## Layer (a): same decoded PCM into the page's JS and native (Linux x86-64, GCC, Release)
Parse text of both fonts identical (GM font: 197 presets, 9455 zones, 1186 samples); `Math.log10` and `sfSrc` bit-identical; `sfVoice` bit-identical for every zone of the GM font at several keys and velocities; `sfPick` identical for every preset x 128 keys x velocities; pad render 21 of 21 cases bit-identical; live note 8 of 8 cases bit-identical at the web copy's 128-frame blocks. Hashes committed in `native/tests/golden/sf_golden.inc` (29 rows; the test font's on every platform, the GM font's on Linux x86-64 only, because stb's setup uses libm). Live = render: 40 of 40 runs at host blocks 64, 17, 1, uneven and one block equal the 128 run bit for bit.

## The missing frames (cause found, with evidence)
Chrome's `decodeAudioData` returns fewer frames than the Ogg stream holds for short samples. The granule positions are NOT the problem and stb_vorbis is NOT wrong:
- Page granule positions of the short streams (`og.py` style dump): two header pages with granule 0, then ONE audio page that is also the last (EOS) page, carrying the whole audio, granule = total frames (sample 183: 2297; 948: 12462; 906: 20640; 859: 4633; sample 0: 48). The first audio page's granule is never smaller than what its packets decode to, so there is nothing to trim at the START, and the end is trimmed by stb exactly to the granule (its last frame is shorter: 121, 110, 352, 985 frames).
- Three decoders at the sample's own rate (Chrome with a context rate equal to the sample's, so no resampling hides in the count; libvorbis = libsndfile 1.2.2 / python soundfile 0.14, the reference; ffmpeg 6.1.1's own Vorbis decoder, no trimming flags):

| sample (stream rate) | last granule | libvorbis | stb_vorbis | ffmpeg | Chrome 153 |
|---|---|---|---|---|---|
| 183 (48000) | 2297 | 2297 | 2297 | 2304 | 2176 |
| 948 (44100) | 12462 | 12462 | 12462 | 13376 | 12352 |
| 906 (44100) | 20640 | 20640 | 20640 | 20512 | 20512 |
| 859 (44100) | 4633 | 4633 | 4633 | 4505 | 4505 |
| 1075 (44100, 5 pages) | 62568 | 62568 | 62568 | 62568 | 62568 |
| 0 (48000, 48 frames) | 48 | 48 | 48 | 128 | decode error (page: 1 silent frame) |

  stb_vorbis and libvorbis agree on the count AND on the samples (-141 dB, float rounding), so libvorbis is the arbiter and native is right. Chrome (and ffmpeg, differently) is the outlier.
- Rule measured on ALL 1186 samples of the font (Chrome length at the sample's own rate vs stb's frames): when exactly one Ogg page has a granule above 0 (all audio on a single page), Chrome drops `min(size of the last frame, short block size / 2)` frames from the end (a stream whose only frame would be dropped is a decode error). Streams with several audio pages lose nothing. 1182 of 1186 samples follow this exactly; the 4 exceptions are samples 234, 235, 704 (5 and 4 pages, losses of 53, 427, 128) and 253 (lost 128, rule says 123).
- Why the sound differed so much, not just a few frames: loop points are frames from the sample start and the page computes them from the header, so a short looped sample's loop end lies BEYOND Chrome's shortened data (sample 183: loop end 2202, Chrome length 2176). The page's render then runs off the end of the data and the note stops, where native loops. So kick, snare, square lead and vibes (short, looped samples) differ as a different sound.
- Why "loop points match for 10 of 10" held anyway: that unit check compares the loop numbers (`round(ls * k)` from the header, independent of how many frames decoded), which agree for short and long samples alike (4 of the 10 were short ones). It never said the data was as long as the loops. The check stays; the frame counts are now checked too (`test_sf unit`, "frame counts": 6 streams, default = libvorbis, `pageTail` = Chrome).
- Native: the default keeps every frame (libvorbis count, alignment verified: the same samples start at frame 0, offset 0 at -136 dB). `DecodeOpts.pageTail = true` reproduces Chrome's loss for parity with the page (`DecodeOpts::page(ctxRate)` sets it with the rate). It is the wiring's decision which is wanted; a defect copied on purpose is a choice for the lead.

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
11. A sample that fails to decode, is too long, or decodes to inf / NaN becomes the JS's own fallback (1 silent frame at 48 kHz).
12. Live time: voices are timed on the host's absolute frame count in 128-frame quanta (`(n & ~127) / sr + (n & 127) / sr`), so the sound is independent of the host block size (the JS's `currentTime + i * dt` at 128-frame blocks, same bits). Automation is pruned only at quantum edges and `constFrom` is asked at the quantum start.

## Conditions for the wiring (U34 / U37)
- Prepare before play, per preset, with the key list; keep `Font` (and its bytes), `Preset` and `PresetPcm` valid until the pad's voices have ended.
- Decide the decode options once: `DecodeOpts::page(engineRate)` for page parity, or the defaults. Never decode on the audio thread.
- Events must be handed in before their time (the engine's lead): an event whose time has passed is taken at the current frame.
- One `SfPool` next to the `VoicePool`; a note played by the sampler must also be given to `SfPool::take()` (mono takeover / chokes across both pools), and the other way round. NoteOff, stp and stopAll must never be dropped.
- Counters to surface: `dropped()` (all voices busy), `tOverflow()`, `autoDropped()`.
- Build: the closed build (no LXR, DSP, pump) builds; stb is fetched alone (`THUNDER_FETCH_STB_ONLY=1`).

## Mutations
18 seeded faults (`native/tools/sf-mutate.py`; run from the repo root with `SF_BUILD=<build dir>`), each must make `ctest -R '^sf(_unit)?$'` fail: sfSrc constant, velocity-modulator skip, mod-env sustain clamp, layer limit, stereo-pair zone, preset fallback bank, render loop minimum, release time constant, render normalisation, SF2 int16 scale, SF3 loop rounding, live quantum 128 to 64, prune at every piece, unstable preset sort, pageTail cap, pageTail single-page condition, key-list filter, SF3 flag detection. See the final section for the run.

## Final run
(filled in below once the builds finish)

## Unverified or not done
- int16 storage (halves the RAM, changes the bits) is not done; the key list is the reduction.
- Chrome is the only browser measured (153, Linux); the resampler is a fit.
- Only the Linux x86-64 hashes of the GM font are committed.
- No adapter into the mixer / bridge (out of scope).
