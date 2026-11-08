# io/: copyright and licences

This folder is **Thunder Plus's native audio and MIDI I/O helper**: a small native addon (`thunder_io.node`) that the
desktop app loads, if it is there, to open audio interfaces and MIDI ports with low latency. Plan:
[docs/NATIVE-IO-HELPER-PLAN.md](../docs/NATIVE-IO-HELPER-PLAN.md).

## Licences

- **The addon's own code** (`src/`, `binding.gyp`, `build.js`, this file and `README.md`): Copyright (C) 2026 the Thunder
  Plus authors, under the same licence as the rest of Thunder Plus.
- **miniaudio** 0.11.26 (`vendor/miniaudio/miniaudio.h`), by David Reid (Copyright 2025, MIT-0 option): a choice of public domain
  (Unlicense) or MIT-0. Full text: [LICENSES/miniaudio.txt](LICENSES/miniaudio.txt).
- **libremidi** v5.4.3 (`vendor/libremidi/include/`), Copyright (c) Jean-Michaël Celerier and the libremidi contributors:
  BSD 2-Clause, with the parts it derives from RtMidi (MIT) and ModernMidi (BSD 2-Clause), whose notices are kept in the
  same file. Full text: [LICENSES/libremidi.txt](LICENSES/libremidi.txt). Compiled in from phase B (src/midi.cc), header-only,
  unchanged (no local patch).

Both libraries are permissive; their notices must stay with any build that ships the addon.

## Where the sources come from

Both libraries are vendored as files in this folder (no network at build time), at the versions in [VERSIONS](VERSIONS).
`node io/build.js` rebuilds the addon from them.

## Leaving it out

The rest of Thunder Plus does not need this folder. Delete `io/` (and `electron/io/` if it was built locally) and the
desktop app builds and runs as before, using its existing audio path.
