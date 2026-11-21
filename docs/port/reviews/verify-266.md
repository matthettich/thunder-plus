# Verification of PR #266 (n1/bus-into-bus -> native): bus into bus in the native core

Verifier: independent (did not write the code). Head reviewed: c5964ea. Web truth: `origin/beta` `index.html`, `core/seq-core.js` (read, not checked out).
Web line numbers are beta's (they have moved since the spec's 20773-20826: busPlan is now `index.html` 21032-21090, `audible` 21975-21980, `busStemPlan` 25816, `busClosure` 25823, `renderOut` 25895).

## Verdict: MERGE

No divergence from the web was found in any of the five areas. Nothing blocks the merge. Two follow-ups and four notes below. Two new test files are committed with this report (both pass).

## Results

- Release build, `-j1`: clean. `ctest -j1`: **193 tests, 100 % passed, 0 failed, 1 skipped** (`pumpsynth_refused`, skipped on this machine, unrelated). 191 are the PR's; 2 are mine (`busplan_verify266`, `render_bus_notes`).
- The PR's own new tests: `busplan_bib` (20 web-made cases), `pdc` (three-deep case), `render_bus_chain`: pass.
- CI at the time of writing (c5964ea): Android shell success, Core Linux clang success, wasm (wasi-sdk) success, Windows MSVC success, Android NDK success, macOS universal success, `test` success; Linux GCC and Linux sanitizers and CodeRabbit still in progress (not yet concluded). CodeRabbit comments on the PR: none (REST `pulls/266/comments` is empty).
- The committed golden `native/tests/golden/bus-into-bus-native.json` is byte-identical to what `tools/make-bus-fixtures.mjs` at beta HEAD produces now (I regenerated it from a `git archive` of beta in /tmp; `cmp` equal), so it is not stale against beta.
- I also ran the native `tools/mix-dump.mjs --busplan` against beta's `index.html` + `core/seq-core.js` (copied to a temp tree) and fed the result to the PR's `test_busplan`: all 16 hand cases (`own_in`, `bus_of_buses`, `send_keeps_out`, `twice`, `many`, `aud_*` ...) agree with the NEW web, 0 failed (`cross` is skipped by the hardcode, see F2; the new web's answer for it is checked in F3).

## Findings

### F1 (NOTE) `bus_plan` matches the web's `busPlan()`, point by point
Native `native/core/src/mix/mix.cpp` 124-175 vs web `index.html` 21032-21090.
- `ownRet`: both collect, for every Track In with a valid track (CHN on or off), whether its reachable Chain Outs include its own track (native 128-137, web 21060-21061). Candidate filter `b == t || ownRet.has(b) || !chainsFeed(b)` is the same (native 147, web 21068).
- Candidates are Track Ins with `chainOn[t]` only, in module order (first position of an id, last definition wins, as the web's `Map`), each with its outs ascending. Sorted by (t, b), stable (native 152 `stable_sort`, web 21069 `cand.slice().sort`). A cable is left out when `reaches(b, t)` (native 154, web 21070). Which cable a loop drops: always the higher-numbered source's, in both.
- `feeds` / `targets` filled afterwards in candidate order (module order), `feeds` sorted at the end, duplicates kept (native 156-157, web 21073-21076).
- `pure`: per FED Track In, `!outs.has(t)`; `pre`: per FED Track In with a non-zero `pre` param. `pre` still requires `fed` (native 158-160 `if (!q.fed) continue`, web 21077-21079 `if (!q.fed) continue`). Confirmed.
- Closures `srcAll` / `busAll`: transitive over `feedMask` / `targets` (native 163-170, web 21083). The plan is acyclic so both end.
- Hand-derived cases (my own, from the web code, committed in `native/tests/test_busplan_verify266.cpp`, about 29 check sites, some in loops): three-node loop in both module orders and a rotated one (cand sorted (0,1),(1,2),(2,0): the last is dropped; rotated (0,2),(1,0),(2,1): 3->2 is dropped), two-node loop in both orders, a track that is source and bus with its own Track In reaching its own Chain Out (not pure, only the 3 bus is a bus), the owner's middle bus (own Track In not reaching own Chain Out), CHN off on the middle track (no edges: `chainsFeed(2)` false so (0,1) is not a candidate and ti2 is skipped), CHN off on the last bus, tracks 21/26/31 plus a closing cable (>= 16; the web's NT is 32, `index.html` 19111 and `seq-core.js` 75), 17 sources each with two Track Ins into bus 31 (feedLen 34, nFeeds 32 = kMixFeedMax, first 32 of the sorted list wired, as `busSync`'s `slice(0, 32)`), `pre` only on a fed Track In. Each case has its derivation in a comment above it.
- Reproduce: `build/tests/test_busplan_verify266` prints `ok (0 failures)`.

### F2 (FOLLOW-UP) the `cross` expectation is hardcoded and skips the other comparisons
`native/tests/test_busplan.cpp` 36-38 (PR). The new expectation is RIGHT: I worked it out by hand from beta's `busPlan`: patch Track In 1 -> Chain Out 4, Track In 4 -> Chain Out 1, CHN on 1 and 4: ownRet = {}, cand (0,3), (3,0); (0,3) added; (3,0): `reaches(0, 3)` yes -> `loopEdges [[3,0]]`. So feeds[3] = {0}, targets[0] = {3}, pure = {0}, loop = true; and the web's own output for `cross` (native `mix-dump.mjs --busplan` run on beta, which uses beta's `busPlan` and `audible`) is `feeds[3]=1, targets[0]=8, pure=1, loop=true, chainsFeed=9, audible=0xffffffff`. The hardcode (`feedMask[3]==1, targets[0]==8, targets[3]==0, feedMask[0]==0, pure==1, loop, loopTo[3]==1`) agrees.
The weakness: the branch ends in `continue`, so `chainsFeed` and `audible` are not compared for `cross`, and the case only exists because the checkout's own `index.html` (native branch) still has the pre-651 web. When the web copy under `native` is refreshed from beta the hardcode should go and the Node reference should be used for `cross` too. Not a merge blocker.

### F3 (NOTE) `loopTo` is a mask, the web's `loopEdges` is a list
`native/core/src/mix/mix.h` 62 vs web `P.loopEdges.push([e.t, e.b])`. Two Track In modules on the same track whose cable to the same bus is dropped give two entries in the web and one bit in native. `test_busplan.cpp` compares the count (`LE.size() != nl`), so such a patch would be reported as a difference by the test if it were in the fixtures. No fixture has it; nothing in native reads the count. Say so in a comment or leave.

### F4 (NOTE) `mix_audible` matches `audible(t)`
Native `mix.cpp` 173-181 vs web 21975-21980: solo, then mute, then nobody soloed = heard, else heard when any track in `busAll[t]` or `srcAll[t]` is soloed. Same order, same transitive closure in both directions. Mute does not stop the closure (a muted middle bus does not hide what is up- or downstream of it from solo), both sides. Cases in `test_busplan_verify266.cpp`, derived from the web code: chain 1-2-3-4 with solo 4 / solo 1 / solo 2 (all four heard), solo 2 + mute 3 (1, 2, 4 heard; 3 off), solo 4 + mute 2 (1, 3, 4), solo 2 + mute 2 (solo wins, all four), an unrelated track 5 silent while something is soloed, and the same transitive solo on tracks 21/26/31. The PR's fixture test masks `audible` to 16 bits (`0xffff`); mine covers tracks >= 16.

### F5 (NOTE) `Seq::buildBus` now using `bus_plan`: whole-function comparison and build
- Web `makeSeq` (`core/seq-core.js` 271-274): `busT = {t : isBusT(t)}` with `isBusT = busPlan().feeds.has(t)` (196); `hearT` (non-song or `o.only`) is the transitive closure of the starting tracks over `busPlan().targets`. So the web DOES follow chains in `hearT` (through a middle bus). Native `seq.cpp` 311-322 after the PR: `busT_[b]` = `feedLen[b] != 0`; `hearT_` closure over `targets`, with the same `hasOnly` / non-Song start set (`o_.only` bits, or `o_.track`). Same. The master column (index 32) has no targets; `targets[x]` for it is an empty map entry, harmless.
- The change also removes the seq copy's own `chainOn[b] && chainsTracks.count(b)` test; `chains_feed()` inside `bus_plan` is the same rule (`mix.cpp` 109).
- Build: `thunder_core` is ONE static library holding both `seq/seq.cpp` and `mix/mix.cpp` (`native/core/CMakeLists.txt` line 2 and the `-fexceptions` list), `mix/mix.h` already included `seq/seq.h`, so `seq` -> `mix` adds no new link edge. Tests link `thunder_core` whole (`native/tests/CMakeLists.txt`, no target compiles `seq.cpp` alone; grep of every CMake/gradle/script file found `seq/seq.cpp` only in `core/CMakeLists.txt`). `native/tools/build-wasm.sh` builds only `thunder_wasm_tfx / _w / _mods / _pump / _pump_synth` (dsp and pump), not `thunder_core`; CI "Core (wasm, wasi-sdk)" is green. Windows MSVC, Android NDK, macOS universal and Linux clang green.
- A middle bus's notes: `has` in `thunder_render_stem_tracks` comes from the sequencer's events, in which a bus's notes are dropped (web `seq-core.js` 648, 689 `nbus`). See F7.

### F6 (NOTE) stems: `setStem` and `thunder_render_stem_tracks` match the web
- `SongRenderer::setStem` (`render.cpp` 43-53): m = {track}, then repeatedly OR the audible sources (`feedMask & aud`) of every member until nothing is added. Web `busClosure` (`index.html` 25823-25828): breadth-first over `feeds`, a source added only when `audible(t)`; a muted middle bus is not added, so what only feeds it is not either. Same set (`aud` is the song's mute/solo mask, as `audible`).
- `thunder_render_stem_tracks` (`render_api.cpp` 296-312): `keep = aud & has`, grown by every audible bus with a kept source until nothing is added, then `list = keep` minus pure tracks with a target in `keep`. Web `busStemPlan` (25816-25821) and `renderOut` (25911-25914): same fixed point and same `inBus`. In the owner's patch the only stem is bus 14.
- Known web behaviours NOT changed by the PR and flagged by the spec (the stems double count of a send's direct sound; a pure source whose only bus is muted still gets a stem; offline, `pass()` routes only heard tracks so the source of a muted bus plays directly): the PR pins the last two in `test_render_bus_chain.cpp` and leaves them. Agreed. FOLLOW-UP for the lead's ruling, not this PR.

### F7 (NOTE) middle bus with notes: test written, passes
`native/tests/test_render_bus_notes.cpp` (new, `render_bus_notes` in ctest). Chain 1 -> 2 -> 3 as in `test_render_bus_chain.cpp`; notes on track 1 only, then also on track 2 (the middle bus), on track 3, and on all three. Results: the mix is sample-for-sample the same in all four (max difference <= 1e-9), so the notes of every bus are dropped. Control: with the chain OFF, notes on track 2 do change the mix (so the equalities are not vacuous). The stem list is {3} with notes on all three and on 1 + 2; the last bus's stem equals the mix and the middle bus's stem equals the source times its fader, notes on the middle bus notwithstanding.

### F8 (NOTE) real-time safety: nothing in the diff runs on the audio thread
`bus_plan` callers: `Seq::buildBus` (`seq.cpp` 314, from the `Seq` constructor, run by `SeqSession::make`, `session.cpp` 62-68, a control-thread snapshot build, and by the offline renderer), `SongRenderer` constructor (`render.cpp` 16), `render_api.cpp` 140 (pdc plan, create time). `mix.h` 28-30 already states `bus_plan()` "reads JSON and allocates: control thread". `buildBus` already built `std::map` / `std::set` before the PR. `SongRenderer::setStem` and `thunder_render_stem_tracks` are offline API calls; their new loops are fixed-size bit loops (<= 32 x 32) with no allocation. The closures in `bus_plan` are bit loops too.

### F9 (FOLLOW-UP) web beta has `api.rebus` (20 Nov), after the spec
`core/seq-core.js` 279 and 1202-1209: on a patch or song change the web re-reads which tracks are buses while the song plays. The PR (and the 19 Nov spec) do not mention it. Native recomputes `busT_` only when a `Seq` is built, i.e. per snapshot (`SeqSession::make`). If the live hosts publish a new snapshot whenever the chain patch changes, behaviour matches; I did not verify the host side (outside the diff). Please confirm in the live-route work or add a test there ("a track that becomes a bus drops its notes from the next row").

## Not found / checked and fine
- No difference in `pre` (still needs `fed`), `pure`, `feeds` order, duplicates, closures, loop-dropping order.
- No difference in tracks >= 16 (masks are `uint32_t`, NT is 32 on both sides; `1u << 31` is fine).
- `kMixFeedMax` truncation behaves as the web's `slice(0, 32)` (pre-existing, now tested).
- The PDC three-deep case (`test_pdc.cpp`, tracks 01 10 ms, 02 100 ms, 08 50 ms, 14 25 ms: T = 175 ms, f[01] = 90 ms, c[14] = 150 ms, c[08] = 100 ms) matches the web test the spec cites.

## New files in this review
- `native/tests/test_busplan_verify266.cpp` (ctest `busplan_verify266`)
- `native/tests/test_render_bus_notes.cpp` (ctest `render_bus_notes`)
- `native/tests/CMakeLists.txt`: two target registrations appended at the end.

## Verdict
**MERGE.** (CI: GCC and sanitizer jobs were still running when checked; every finished job is green. Re-check them before pressing the button.)
