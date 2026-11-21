# U37 P2 notes: LiveHost core (work in progress)

Branch `n1/u37-p2-livehost` (base `native` 56ffdc3 + the five P0 contract files from `n1/u37-p0-live-h` a048b8a).

## State (update at every push)
- DONE: `native/core/src/live/live_host.cpp` (all of scope A; compiles clean), `live/dump_plain.h`, PadsHost additions (B), `SongSnap::rev` / `ord`, `SnapSlot::pending()` / `collect()` count.
- NEXT: tests (live_equals_render, live_step_dump, live_transport, live_events, live_pads, live_threads), rt_harness extension, pads epoch checks, sanitizer runs, mutations.
- Not in P2 (as scoped): effects, master chain, PDC, MIDI, meters (stub), SoundFont / live-engine pads (E_UNSUPPORTED), the C API.
