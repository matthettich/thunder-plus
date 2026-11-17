# U22: the engine foundation

Port of the voice core of index.html's ENGINE block to `native/core/src/engine/`. **Bit-exact**: 294 of 294 factory voices against beta's `tests/golden/engines.json` hashes (SHA-256 of the float32 bytes of `renderVoice(v, 48000, null, hold)`), 168 of 183 direct cases (random voices through the real `renderVoice`, plus written-out edge cases) and 1800 of 1800 `voiceMod` probes against the real JS run in Node. GCC 13 and clang 18 (Release), GCC ASan + UBSan; full ctest 89 of 89 on GCC. Gate command: `native/tools/parity.sh u22`.

## What is where
- `native/core/src/engine/engine.{h,cpp}`: tables, `EngRng` (rng), `eng_blep`, `eng_sinT` + SIN, `EngSeedCounter` (freshLayer's seed), the oscillator scaffolding (`Osc`, makeOsc's closures), `renderLayer`, `eng_voice_mod` (voiceMod), `voiceFilter`, `voiceAmp`, `eng_render_voice` (renderLayersOnly + renderVoice: drive, level, the pad effect chain, the trailing-silence trim, the 2 ms fade), `EngVoice` / `EngLayer` / `EngFx` (mkVoice / freshLayer parameters with their defaults).
- `native/core/src/engine/engine_json.{h,cpp}`: fills an `EngVoice` from the JSON of a song voice (only what differs from the defaults needs to be there).
- `native/tools/engine-dump.mjs`: the reference (the real ENGINE block in Node's vm), the fixture generator, the golden writer / checker.
- `native/tests/test_engine.cpp`, `native/tests/golden/engine_voices.json` (the 294 voices), `engine_golden.inc` (beta's hashes, copied), `engine_cases_golden.inc` (hashes of the direct cases).

## Layer types in this unit
sine, tri, saw, square, pulse and the noise family: white, pink, brown, blue, metal (808), lfsr (Digital), dust (Crackle), grit (S&H). A layer of any other type makes `eng_render_voice` return `kEngUnsupported` (never silence). `chaos` is not here (needs ChaosCore); `click` is not here (U23's family; it is `sinT`-only, add a case to `Osc::init` / `Osc::s`).

## Decisions
- **Gate source**: beta's `tests/golden/engines.json` (2142 voices). The ENGINE block of `native`'s index.html is byte-identical to beta's (checked), so the reference runs this repo's block. The voice *list* (the presets outside the ENGINE block) exists only on beta: `--fixture <beta checkout>` makes `engine_voices.json` from it by hand and checks, voice by voice, that the slim description reproduces beta's hash.
- **Pad effects**: the golden's Node has no `TX` (tfx wasm), no `PL` (macro wasm) and no `WavesDSP`, so `fxReady` is false for every effect except the four JS ones; `applyFx` returns the sound unchanged for them. The C++ does the same (`EngFx::Kind::Other` is a no-op). 18 of the 294 voices have such an effect (crush, room, drive) and match the golden because of that; they prove the dry path only. Giving those effects a home is U14 / U19 / U31's.
- **Buffers**: the caller hands in `out` of at least `eng_render_bound(V, opts)` floats; nothing here owns a pad buffer. A voice without effects renders without allocating (the layers work in `out`, the oscillators live on the stack). The four JS pad effects go through U17's `pad_*` cores, which return `std::vector`: a voice WITH those effects allocates (offline render on the control thread, as the page's Worker does).
- **hold**: `renderLayersOnly`'s `V = Object.assign({}, V, {gate: hold*1000, len: hold})` is a copy of the `EngVoice` on the stack.
- **bpm**: `songBpm()` is an option of the render (`EngRenderOpts::bpm`, 120 like the JS fallback when `S` is absent); only the tempo-synced LFOs and the mod sequence read it.
- **Layer seeds**: the voices carry their layers' seeds explicitly (they come from a counter every built voice bumps). `EngSeedCounter` reproduces the counter (the product exceeds 2^53: the JS rounds it in doubles; so does the C++); proved on six start states.
- The interface is **provisional** (as U19's module block was): U23 / U24 / U25 add layer types by extending `Osc`; the lead decides pad-buffer ownership and where a voice description comes from.

## Not sure
- `Float32Array` stores of denormals: C++ keeps them (no FTZ in this path); the JS does too. No voice here reaches one.
- A JS exception inside the pad chain (a Pitch reverb with no `size`) makes `renderVoice` throw; here `kEngFxThrew`. Not reachable from the factory voices.
- More than 8 layers or 8 effects on one voice is refused by the loader (mkVoice makes 8; nothing in the factory has more).

## Tests (ctest): `engine_ref` (Node makes the reference: ~55 s), `engine` (C++ against beta's hashes, the local Node's hashes, the tables, the defaults, the seed counter, sinT / rng / blep / hashRnd samples, the refusals), `engine_golden_current` (Linux: the committed hashes are what the JS gives now), `rt_core` (`rt_engine`: voices without pad effects under the allocation guard). Mutation checks done: a changed blep constant, a changed triangle-LFO centre and a 1e-7 level change are each caught (the direct cases and probes catch what the factory voices do not reach).
To find a first differing sample after a failure: `node native/tools/engine-dump.mjs /tmp/ref --samples` then `test_engine /tmp/ref native/tests/golden/engine_voices.json <voice-substring> --first` (prints sample index, both values).
The 294 factory voices are 560 layers: white 186, sine 135, metal 69, square 45, saw 40, tri 32, pink 28, lfsr 9, pulse 6, dust 5, grit 3, brown 1, blue 1; 19 voices use Degrade (U17's `pad_degrade`), 6 use an LFO, 52 a drive, 19 the voice filter / amp. Pitch delay / reverb and Resonator are covered by the direct cases only (no factory voice uses them).

## Left for others (nothing broken here)
- U23 / U24 / U25: the other layer types: add a case in `Osc::init` / `Osc::s` (engine.cpp) and the name in `eng_layer_type`; widen `OK_TYPES` in `engine-dump.mjs` and re-run `--fixture <beta checkout>` (by hand; it needs beta's tools and goldens) and `--golden`.
- `renderVoice`'s other branches (Bass, Vox, Blip, the XENG engines, stereo samples `.r`, `renderLayersOnly`'s `stages` peaks) are not here.
- The effects other than the four JS ones (see Decisions) and the voice description reader (U06 may want `engine_json.cpp`'s keys).

## Added after the first CI run (macOS)
- Gate on every platform is now committed Linux hashes for everything: beta's hashes (voices), `engine_cases_golden.inc` (direct cases) and `engine_probes_golden.inc` (SHA-256 of the real voiceMod outputs of the 1800 probes). The comparison with the LOCAL Node run (samples and probes) must hold only on x86-64 Linux; elsewhere it is reported (macOS arm64 Node rounds Math.sin / cos differently, which moved 7 probes and 1 case). `engine_golden_current` proves all three files on Linux.
- Resonance clamp: 15 more direct cases (`edge_lpres*`, `edge_hpres*`, `edge_gres*`: res 0.9 / 0.975 / 0.98 / 0.985 / 1 on the layer low pass, high pass and the voice low pass). Changing the layer clamp 0.98 to 0.97 now fails 5 of 183 cases (it failed 1 of 168 before, and 0 of the 294 voices).
- `eng_init()` builds the sine table now (call once off the audio thread); the lazy path in `eng_sinT` / `eng_sin_table` stays.
