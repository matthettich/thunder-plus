# U22: the engine foundation (WORK IN PROGRESS, 16 Nov)

State at the first push: the port is written and **bit-exact**: 294 of 294 factory voices (beta's `tests/golden/engines.json` hashes), 160 of 168 direct cases, 1800 of 1800 voiceMod probes (GCC 13, Linux x86-64, Release).
Still to do before the PR: clang build, sanitizer build, allocation guard (`rt_harness`), `parity-u22.sh`, full ctest, this document finished. See "Next step" at the bottom.

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

## Next step
Finish: clang + sanitizer builds, `rt_harness` case, `parity-u22.sh`, full ctest, then the PR into `native` (do not merge).
