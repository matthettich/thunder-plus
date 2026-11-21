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

## DONE (gaps 1-6 closed; 7 and 8 left)
- `mix.cpp bus_plan`: `ownRet`; edges collected per Track In module (CHN on), sorted by (source, bus) stably, a closing cable goes to `BusPlan::loopTo[source]` (bit = bus; the
  web's `loopEdges`) and sets `loop`; `targets`, feed lists, `pure`, `pre` per kept edge / fed module as the web; closures `srcAll` / `busAll` (any depth). New fields are zeroed in
  the constructor. `BusPlan` grew by 3 x 128 bytes (it is copied by value into `Mixer::setPlan`, off the audio thread, as before).
- `mix_audible`: solo follows `busAll` / `srcAll` (solo wins, then mute, then nobody soloed).
- `seq.cpp Seq::buildBus`: now calls `bus_plan` (one plan, not a copy); `busT_` = tracks with feeds, `hearT_` closes over `targets`. Only the body of `buildBus` changed.
- `render.cpp SongRenderer::setStem`: heard set = `busClosure({track})` (audible sources, through any depth). `render_api.cpp thunder_render_stem_tracks`: keepT repeated until stable.
- Not touched: `Mixer`, `pdc.cpp` (no change needed: a 3-deep case in `test_pdc.cpp` proves it), `MidiSched`, `Players`, the song renderer's block loop.
- Real-time safety: all of it runs when the plan is built (off the audio thread: `SongRenderer` constructor / `Mixer::setPlan`, as before). `Mixer::setPlan` / `rebuild` are
  unchanged, so no new swap mechanism was needed. The audio path (`Mixer::render`) is untouched.

## Tests (how to run)
Full ctest of 20 Nov: 191 tests, 190 passed, 1 skipped (pumpsynth_refused, as before), 0 failed. Build as CI: `cmake -S native -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j2 && ctest --test-dir build`. The new / changed ones:
`ctest --test-dir build -R 'busplan|render_bus_chain|^pdc|stems|seq'`.
- `busplan_bib`: `test_busplan native/tests/golden/bus-into-bus-native.json` = the 20 cases of `tests/fixtures/bus-into-bus-native.json` from `beta` (made by the web's own
  functions in a vm by `tools/make-bus-fixtures.mjs`; copied unchanged, no Node needed): chain 1-2-3-4, the owner's patch with eight solo / mute variants, CHN off, send, loop in both
  module orders (loopEdges [[13, 7]]). Compared: feeds, targets, lists, pure, pre, loop, chainsFeed, audible (low 16 bits: the fixture has 16 tracks), loopEdges, srcAll, busAll.
  NOT compared: `labels` (no drawing), `keepT` / `stems` (the stem lists are tested through the render API in `render_bus_chain`, not from the fixture).
- `render_bus_chain` (new): 1 -> 2 -> 3 through the offline renderer with the stems test's pass-through hooks. Reference: ANALYTIC (the source rendered alone x trackGain(A) x
  trackGain(B)), max error 5.5e-9 on a peak of 0.21; no block of delay; transitive solo (source / middle / last), mute wins, a muted middle bus is silent when the buses exist and the
  source plays directly in the web's offline pass; stems: list {3}, `setStem` heard masks (middle bus = {1,2}, last = {1,2,3}, source = {1}), a muted middle bus is not pulled in.
- `test_pdc.cpp`: `chainCase()` (T 175 ms, f[01] 90 ms, c[14] 150 ms, c[08] 100 ms), runs even without Node.
- The existing `busplan` (from `mix-dump --busplan`, which runs the OLD `index.html` of this branch): ONE case, `cross` (track 1 -> out 4, track 4 -> out 1, both CHN on), changes by
  design: the old web said both have a Track In so neither is a bus; the spec's rule keeps 1 -> 4 and drops 4 -> 1 as a loop. `test_busplan` now checks `cross` against the new rule
  and prints "superseded". Every other case (every shipped song under sounds/, the hand-made patches) is unchanged. `mix_golden_current` still passes (it hashes the old JS).

## Reference note (no browser)
A web-rendered golden of a two-bus chain needs a browser (OfflineAudioContext + the chains worklet); none here. The audio test's reference is analytic (above). The plan's
reference IS the web's own code (the committed fixture). Re-recording `mix-dump.mjs` against `beta` would make `--busplan` emit the new fields too: left for when this checkout's
`index.html` is moved to a newer build.

## Differences from the spec / unsure
- The spec said the sequencer needs no change; native had a second copy of the plan in `Seq::buildBus` (see gap 3): replaced by a call to `bus_plan`.
- `loopEdges` is a mask per source (`loopTo`), not a list of pairs (the spec allowed either).
- `cross` case (above).
- Unsure: `Seq` is built per render / per Play; the web's `rebus()` (gap 8) has no native twin; a live host that edits cables while playing must rebuild its `Seq` busT.
- Not tested: a middle bus that also has NOTES (its notes must be dropped: `busT_`): covered by construction (`busT_` = feeds keys) but no audio test.
