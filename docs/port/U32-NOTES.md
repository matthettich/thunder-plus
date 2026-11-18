# U32: the SoundFont / GM player (SF2 / SF3 parse, Vorbis decode, zones and modulators, layered zones, the pad render, the live note)

Branch `n3/u32` (from `native`). Work in progress; this file is kept current.

## State (first push)
- stb_vorbis pinned: nothings/stb `9600522de53973336fa033a289858a4354cbb04c` (v1.24, MIT or public domain), fetched by `third_party/fetch.sh` (alone with `THUNDER_FETCH_STB_ONLY=1`), recorded in PINS.md with the licence text read from the file; `prepare-stb.sh` makes the copy host builds compile (no patches yet).
- Ported so far (`native/core/src/sf/`): parse (`sf_parse.cpp`), sfSrc / sfVoice / sfPick / sfPreset (`sf_voice.cpp`), sample decode and per-preset preparation (`sf_decode.cpp`), the pad render sfRender (`sf_render.cpp`), Math.log10 (`sf_math.cpp`).
- First measurement on `sounds/FluidR3Mono_GM.sf3`: parse finds 197 presets, 9455 zones, 1186 samples (same as the JS); parse 2 ms, 1.7 MB of tables. Decoding EVERY sample as float32 takes 416 MB and 2.4 s (no sample fails); one preset: Grand Piano 191 MB (164 zones), Fingered Bass 2.5 MB, Standard drum kit 12 MB.
- Not done yet: the Node reference and tests, the live note, Chrome comparison, fuzz, mutations.
