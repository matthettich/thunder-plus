# Bus into bus, native (branch `n1/bus-into-bus`; spec: `docs/BUS-INTO-BUS-NATIVE.md` on `beta`, web PR #254, builds 650-651)

## GAP LIST (written before coding, against `origin/native` 5abe174, 20 Nov)

What existed: `native/tests/test_busplan.cpp` (bus_plan / chains_feed / mix_audible against the JS of the OLD web app, cases from `tools/mix-dump.mjs --busplan`: hand-made
patches and the shipped songs; compares feeds, targets, lists, pure, pre, loop, chainsFeed, audible); `test_render_stems_bus.cpp` (ONE bus fed by one source: list, bus stem
holds its source, stems add up to the mix, mute); `test_pdc.cpp` (pdc follows the longest path already).

What the spec still required, and what was found:
1. `bus_plan` (mix.cpp): used `ownIn` (a Track In of the track exists) instead of `ownRet` (its OWN Track In reaches its OWN Chain Out); edges in module order, so the
   closing cable of a loop depended on module order; no `loopEdges`, no `srcAll` / `busAll`. GAP.
2. `mix_audible`: solo followed only direct `targets` / `feedMask`. GAP.
3. `Seq::buildBus` (seq.cpp): NOT in the spec, found here: the sequencer carries its OWN COPY of busPlan (with the same `ownIn` rule) for `busT_` (notes of a bus are dropped)
   and `hearT_` (LFOs). The spec says "no change beyond bus_plan"; that is wrong for native. GAP (a middle bus with notes would have kept playing them).
4. `thunder_render_stem_tracks` (render_api.cpp): the keepT closure ran ONE pass over the buses in ascending track order; busStemPlan repeats until nothing is added, so a chain
   whose buses are not in ascending order (source 3 -> bus 2 -> bus 1) lost its last bus. GAP.
5. `SongRenderer::setStem` (render.cpp): took the direct audible sources only; busClosure goes through any depth, adding a track only when audible. GAP.
6. `Mixer::rebuild` / `pdc_plan`: no change needed (confirmed by the tests below). Test gap only (3-deep pdc case).
7. Labels (`ON BS MS LP --`): native has no mixer drawing; not implemented. The data a UI needs is in `BusPlan::loopTo` (the cables left out).
8. Newer than the spec, NOT done: web build 652+ `api.rebus()` (20 Nov, "BUS EDITS WHILE PLAYING": the sequencer re-reads which tracks are buses when a cable edit changes
   the plan while playing). Native's `Seq` keeps its `busT_` from construction (as PLAY did); the live host (U37) will need a rebus call. Left for the lead.

Out of scope, as ordered (native's current behaviour recorded):
- Stems double-count of a send's direct sound: native's stem render is the same as the web's (a bus stem takes its sources with it, `setStem`'s closure includes a SEND source
  (not pure) whose direct path stays on; the send also keeps its own stem). Not fixed, not mirror-fixed. Pinned by nothing new.
- "A pure source whose only bus is muted still gets a stem": native does the same (list = keep minus inBus; the bus is not audible so it is not added, so the source is not
  inBus). Pinned in `test_render_bus_chain` ("current behaviour: ...").
