# U32: the SoundFont / GM player (SF2 / SF3 parse, Vorbis decode, zones and modulators, layered zones, the pad render, the live note)

Branch `n3/u32` (from `native`). Code: `native/core/src/sf/` (+ `native/core/sf/CMakeLists.txt`, `third_party/prepare-stb.sh`). Tests: `native/tests/test_sf.cpp`, `test_sf_fuzz.cpp`, `rt_harness.cpp` (case `run_sf`), `native/tools/sf-ref.mjs` (Node reference), `sf-chrome.mjs` (page decode in Chrome), `sf-mutate.py`.

## The decoder
stb_vorbis from nothings/stb, commit `9600522de53973336fa033a289858a4354cbb04c` (v1.24, MIT or public domain; the licence text is copied from the fetched file into `third_party/PINS.md`). `fetch.sh` pins it (alone with `THUNDER_FETCH_STB_ONLY=1`, so the closed build needs nothing else), `prepare-stb.sh` copies it for host builds. No patch was needed: the fuzz found no fault inside stb_vorbis (below). Only `stb_vorbis_open_memory` and `get_samples_float` are used (`STB_VORBIS_NO_STDIO`, `NO_PUSHDATA_API`, `NO_INTEGER_CONVERSION`); decoding runs on the control thread only, at preset preparation.

## What is ported (the JS is the specification)
`parseSF`, `sfSrc`, `sfVoice`, `sfPick`, `sfPreset`, `sfSample` (SF2 int16 and SF3 Ogg), `sfPreparePreset`, `sfRender` (pad render) and the live note (`sfNote` / `sfRun`, class `SfPool`, same seam as `VoicePool`: `SeqEvent`, `VoiceKit`, take / off / mod / stp / stopAll / render).

## Results (Linux x86-64, GCC, Release)
- Layer (a), same decoded PCM into the JS (`index.html` SoundFont block and `core/seq-core.js` in Node `vm`) and the native code: parse text of both fonts identical (GM font: 197 presets, 9455 zones, 1186 samples); `Math.log10` and `sfSrc` bit-identical; `sfVoice` bit-identical for every zone of the GM font at several keys and velocities; `sfPick` identical for every preset x 128 keys x velocities; pad render 21 of 21 cases bit-identical; live note 8 of 8 cases bit-identical at the web copy's 128-frame blocks. Hashes committed in `native/tests/golden/sf_golden.inc` (29 rows; the test font's on every platform, the GM font's on Linux x86-64 only, because stb's setup uses libm).
- Live = render at host blocks 128, 64, 17, 1, uneven and one block: 40 of 40 runs equal the 128 run bit for bit.
- Layer (b), decoder difference, measured in headless Chrome 153 (`node native/tools/sf-chrome.mjs DIR` then `test_sf chrome DIR 48000`; not part of ctest, it needs Chrome). Context rate 48 kHz:
  - At the same rate (sample 183, 48 kHz): native decode vs `decodeAudioData` is -136.6 dB (float rounding): stb_vorbis and Chrome's decoder agree.
  - Resampled 44.1 / 32 / 22.05 kHz to 48 kHz: the long samples differ by -87 to -89 dB (piano samples 116, 117, 140, 141; 1075: -87.3), shorter or noisier ones by -66 to -77 dB, sample 859 (a short, noisy drum sample) -42 dB. Chrome's resampler is not specified bit for bit; native uses a Kaiser-windowed sinc (beta 9, 32 zero crossings).
  - Render (`sfRender` of the page in Chrome vs native with `ctxRate = 48000`, 3 required presets plus others): piano C4 -88.9 dB, piano C2 -89.1, **bass -81.6** (both within the -80 dB exit test); **drum kit: kick -64.5 dB, snare -73.8: NOT within -80 dB**. Others: strings -79.7, warm pad -80.4, bass 22 kHz -82.6, missing preset -69.0; square lead +11.3 dB and vibes -3.7 dB are far off (see "Chrome quirk"). The unit's exit test is met for the piano and the bass, not for the drum kit note.
  - Chrome quirk, not reproduced: for short Ogg streams (3 pages: samples 183, 948, 906, 859) Chrome returns 110 to 130 frames fewer than the stream's last granule position (2176 of 2297 for sample 183); a 48-frame stream (sample 0) gives 1 frame. stb_vorbis returns exactly the granule count (2297, 12462, ...). The loop points the page computes for those samples lie beyond Chrome's shortened data, which is probably why square lead and vibes (short looped samples) disagree so much. Cause not identified. If the page's exact output matters for short looped samples it is a wiring decision (truncate? no: that would copy a defect).
  - Sample rate in the page: `decodeAudioData` resamples every SF3 sample to the context rate (so `sr` of the decoded buffer is the context rate and `ls`/`le` are rescaled by `ctxRate / hdr.sr`, rounded). Native does this deliberately when `DecodeOpts.ctxRate > 0` (unit check: the loop points of 10 samples equal the page's in Chrome). Default `0` keeps each sample's own rate (what the bit-exact layer uses, since the Node reference is fed the PCM native decoded).
- Fuzz under ASan + UBSan (`-DTHUNDER_SANITIZE=ON`, `test_sf_fuzz ITER SEED`): ctest runs `200 1`. Long run `1500 7`: 6000 + 1500 mutated SF2 files (4164 + 1066 parsed and then fully used: pick, voice, prepare, render, live note), 187 mutated GM font copies, 15000 corrupted Ogg streams from real samples of the font (3532 decoded, 11468 refused), 1500 fonts with several extreme fields at once through the live pool and the render (9000 blocks): no sanitizer report, no out-of-range read. One real finding: a corrupt Ogg can decode to inf / NaN floats; now the whole sample is refused (fallback of 1 silent frame). No patch to stb_vorbis was needed (`prepare-stb.sh` has no patch).
- Real-time: `rt_harness` case `run_sf`: 1817 notes over 778794 frames through `SfPool` note / off / mod / stp / stopAll / render at random block sizes under the allocation guard: nothing allocated (104 notes refused because all 32 voices were busy: counted, not a fault).
- Mutations: 15 seeded faults (`native/tools/sf-mutate.py`), all 15 caught by ctest `sf` / `sf_unit` (sfSrc constant, velocity-modulator skip, mod-env sustain clamp, layer limit, stereo-pair zone, preset fallback bank, render loop minimum, release time constant, render normalisation, SF2 int16 scale, SF3 loop rounding, live quantum 128 to 64, prune at every piece, unstable preset sort, SF3 flag detection). The first run of 5 of them SURVIVED, so `test_sf unit` (hand-built RIFF fonts, no Node) was added for them.

## Memory (a wiring condition)
Decoded float32 PCM, per preset, on demand: `prepare_preset(font, preset, opts, PresetPcm&)` decodes only the samples the preset's zones use (and stereo partners). Measured on `FluidR3Mono_GM.sf3` at each sample's own rate: Grand Piano 191 MB (164 zones), Fingered Bass 2.5 MB, Standard drum kit 12 MB; the whole font decoded would be 416 MB (2.4 s), so it must never be decoded at once. With `ctxRate = 48000` 44.1 kHz samples grow by 9 % (piano about 208 MB). Parse: 2 ms, 1.7 MB of tables; the font bytes (23.7 MB for the GM font) are a view, not copied (the host keeps them, a mapped asset on Android). Storing int16 would halve it but is not bit-exact with the page: not done.
**Wiring condition:** the host prepares a preset's PCM off the audio thread BEFORE the pad plays (the render returns 0 and the live note refuses if the PCM is missing), keeps one `PresetPcm` per active SoundFont pad, and drops it when the pad changes. A phone with the piano on 4 pads of different presets needs the sum of their figures: budget it, or load the piano only.

## Choices for the lead (where this departs from the SoundFont spec or from the JS)
Spec departures (all because the JS does it; kept on purpose):
1. Velocity-to-attenuation default modulator is skipped (`dst 48`, source velocity); `sfSrc` curves use `Math.log10`; generator add for preset level follows the JS `padd` rules (`g[op] = (g[op] || 0) + padd[op]`); a layered preset plays at most 4 zones, a stereo pair counts as one.
2. Only channel 0 of the decoded Ogg is used (`getChannelData(0)`); the SF2 and SF3 players are mono per zone with the zone's own pan.
3. The live automation keeps the JS's `Auto` quirks: `constFrom` looks at the last event only; `prune` is not value-preserving for target and curve events.
4. Loop: only when mode is 1 or 3 and `le > ls + 4`; SF3 loop points are frames from the sample start, rescaled by `round(ls * k)`.
5. Modulators merge by identity (src, dst, amount source, transform); no modulator sources beyond velocity, key, constant (as the JS).

Where native is stricter than the JS (the JS would read undefined, give NaN, or run out of memory):
6. Load limits (`sf.h`): file 2 GiB, 262144 zones, 4 M modulator records, 128 M zone-pair work items, 33 M frames per sample, 32 nested LISTs. Over a limit: `parse` fails, nothing half built. Truncated or inconsistent chunks fail with a status, bag / generator slices are clamped as `slice` clamps.
7. Generators above 63 and modulator destinations above 63 are dropped (the player reads 0..58 only).
8. A loop that starts before the sample (`ls < 0`) is not looped, and a read position that is negative or NaN stops the voice (the JS reads `undefined` and produces NaN).
9. A sample that fails to decode, is too long, or decodes to inf / NaN becomes the JS's own fallback (1 silent frame at 48 kHz).
10. Live time: voices are timed on the host's absolute frame count in 128-frame quanta (`(n & ~127) / sr + (n & 127) / sr`), so the sound is independent of the host block size (the JS's `currentTime + i * dt` at 128-frame blocks, same bits). Automation is pruned only at quantum edges and `constFrom` is asked at the quantum start.

## Conditions for the wiring (U34 / U37)
- Prepare before play (above); keep `Font` (and its bytes), `Preset` and `PresetPcm` valid until the pad's voices have ended.
- Pass `ctxRate` = the engine's rate to `prepare_preset` to match the page's sample-rate handling; do it at preparation, never on the audio thread.
- Events must be handed in before their time (the engine's lead): an event whose time has passed is taken at the current frame.
- One `SfPool` next to the `VoicePool`; a note played by the sampler must also be given to `SfPool::take()` (mono takeover / chokes across both pools), and the other way round. NoteOff, stp and stopAll must never be dropped.
- Counters to surface: `dropped()` (all voices busy), `tOverflow()`, `autoDropped()`.
- Build: the closed build (no LXR, DSP, pump) builds; stb is fetched alone (`THUNDER_FETCH_STB_ONLY=1`).

## Unverified or not done
- The page's drum-kit note is NOT within -80 dB of native (-64.5 dB kick, -73.8 snare), nor the short looped samples (see Chrome quirk). The piano and bass are.
- The Chrome measurements are manual (`sf-chrome.mjs`), not in ctest; they were taken once, with Chrome 153.
- Only the Linux x86-64 hashes of the GM font are committed. Other platforms are checked against the Node reference only where Node's `Math.*` agrees.
- No adapter into the mixer / bridge (out of scope).
