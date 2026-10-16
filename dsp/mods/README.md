# dsp/mods: the modular modules' WebAssembly

The WebAssembly behind two …chains/…waves modules:

- **Random generator** (`marbles`), based on Mutable Instruments Marbles (MIT): `mod_marbles.cc`
- **Function generator** (`tides`), based on Mutable Instruments Tides 2 (MIT): `mod_tides.cc`

`mods.cc` is the module table and the exports (`mods_new`, `mods_free`, `mods_params`, `mods_in`, `mods_out`,
`mods_process`), in the style of `../tfx/tfx.cc`. One copy runs per audio thread or worker, and every module made there
is an object in it (up to 64), so the memory stays small: about 9 KB for each Random generator and 1 KB for each Function
generator, on top of the 192 KB the module starts with.

## Rebuilding

```
git clone --depth 1 https://github.com/pichenettes/eurorack && git -C eurorack submodule update --init stmlib
E=eurorack WASI_SDK=/path/to/wasi-sdk-25.0 sh dsp/mods/build.sh
```

That writes `mods.wasm` (about 185 KB) and `mods.b64` next to the script. Paste `mods.b64` into `MODS_WASM_B64` in the
SHARED MODULES block of `chains/chains-worklet.js`, then run `python3 tools/sync-shared.py` to copy it into …waves.
The toolchain is wasi-sdk 25 (https://github.com/WebAssembly/wasi-sdk/releases), the same as `../tfx/build.sh`.
