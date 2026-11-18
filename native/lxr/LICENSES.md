# native/lxr: licence facts (U04)

**This folder is GPL-3.0-only code. Nothing outside it is.** No licence text was changed by this unit; this note only records facts.

## What is GPL here, and where it is from

| What | Licence | Where from |
|---|---|---|
| The LXR-02 firmware files the web build reads: `lxr/src/firmware/Core/**` (the sound engine: oscillators, filters, voices, LFO, modulation, FX, MIDI parser, parameter tables), copyright (C) 2012-2026 Erica Synths | GPL-3.0-only with the additional permission and terms in `lxr/src/firmware/NOTICE.md` (GPL section 7: trademarks "Erica Synths", "LXR", "LXR-02"; the firmware's own text) | https://github.com/erica-synths/lxr-desktop-app, commit `a43cd4fcda23ec3d577dc425261c3eb28bd5abff` (23 Oct 2026), as it is in this repository's `lxr/src/firmware/`. **This build reads those files from `lxr/`; none is copied into `native/`.** Patched copies are made in the build directory only (`prepare-lxr.sh`) |
| `lxr/src/lxr_wasm.c`, `lxr/src/stub/*` (the web build's glue and HAL stand-ins), copyright (C) 2026 the Thunder Plus authors | GPL-3.0-only, same terms (`lxr/NOTICE.md`) | this repository; compiled here too (the glue is the page's call sequence) |
| `native/lxr/**` (the module: `src/lxr_module.cpp`, `src/lxr_host_stubs.c`, `src/lxr_tables.inc` (generated from `lxr/lxr-engine.js`'s tables), `include/thunder/lxr_module.h`, `prepare-lxr.sh`, `CMakeLists.txt`) | GPL-3.0-only (it links and patches the firmware: one work) | this repository |
| `native/tools/lxr-dump.mjs`, `lxr-gen-tables.mjs`, `native/tests/test_lxr*.cpp`, `native/tests/golden/lxr_*` | test and generator code that runs the firmware or reads its tables: treat as part of the module (leave out with it) | this repository |
| `lxr/lxr.wasm`, `lxr/lxr-engine.js` (the web app's) | built from the GPL sources; the web build's concern, not part of the native build, but the gate reads them as the reference | this repository |

Everything else of the native build (`native/core`, `native/desktop`, ...) is NOT GPL and does not contain, include or call a line of the above. The core talks to the module only through
`native/core/include/thunder/lxr_seam.h` (a C struct of function pointers, registered at start-up) and `native/core/src/lxr/lxr_seam.cpp` (a registry of one pointer). Those two files, and
`xeng.h` / `xeng_voice.cpp`'s uses of them, contain no firmware code. `native/tools/lxr-boundary.sh` (ctest `lxr_boundary`) checks this: the core's sources name no module or firmware symbol, and
`libthunder_core.a` (and, in an OFF build, every test binary) exports none.

## The module is separable

- `-DTHUNDER_WITH_LXR=OFF`: `native/lxr` is never configured, compiled or linked. The core, the desktop app and every test build and run; a song with an LXR voice (`lxdrum`, `lxsnare`,
  `lxcym`, `lxhat`) is refused cleanly (`kEngUnsupported`), never silence. CI builds and tests this configuration (job "Core (Linux, GCC)": `lxr_seam` in the OFF build).
- `-DTHUNDER_WITH_LXR=ON` (the default for the owner's own builds): the module is built as `thunder_lxr` (a static library that links `thunder_core`, never the reverse); the app calls
  `thunder_lxr_module_register()` once at start-up. Not built with MSVC (the firmware is GNU C): there the module is simply absent, as in a closed build.

## What a closed (non-GPL) distribution must leave out

1. the `native/lxr/` folder, and the `lxr/` folder of the repository (the firmware, glue and the web engines);
2. `native/tools/lxr-*.mjs`, `native/tools/lxr-boundary.sh` (the boundary check may stay: it passes without the module), `native/tests/test_lxr.cpp`, `test_lxr_fuzz.cpp` and
   `native/tests/golden/lxr_*.inc` (the test registrations are guarded by `if(TARGET thunder_lxr)`); `test_lxr_seam.cpp` is not GPL and stays;
3. any binary built with `THUNDER_WITH_LXR=ON` (it contains the firmware): a closed build must be configured OFF;
4. the name "LXR" / "LXR-02" / "Erica Synths" in the product's own naming (the firmware's NOTICE, GPL 7(e)).

The core's engine list still mentions the four engine names (`xeng_voice.cpp`): they are names of songs' voices, not code.
