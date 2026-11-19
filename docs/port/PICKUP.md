# Pick up here (written 17 Nov 2026, 22:00 Mountain, by Fable, the lead, at the owner's request to move to a new chat)

To continue the native port in a new chat, the owner says: **"pull thunder-plus, branch `native`, and continue from docs/port/PICKUP.md"**.
This file is the brief. Background only when needed: `docs/port/TODAY.md` (the full log of 16 and 17 Nov, newest at the end), `docs/port/WORKPLAN.md` (the
plan; sections 1 to 4 and 9), `HANDOVER.md` on `beta` (the web app, the owner's punch list).

## How the owner wants this run (his words, 16 and 17 Nov)
- The lead manages, decides, reviews and merges; **everything that can be is built by low-cost agents** (Sonnet builds a unit in its own `git worktree` under
  `/home/claude/wt/`, Haiku does document chores); an INDEPENDENT Sonnet verifier checks a unit before or right after merge (it has caught a real crash twice).
- Questions: make the best decision and tell him; weigh solutions that port easily. He wants to keep making music on the web app meanwhile: fixes go to
  `beta` by PR, then `main` by PR after each (he said so); a desktop release only when he asks.
- Only what is pushed exists: the session's server restarts every hour or so and stops the workers (they can be resumed with a message to their id; a new chat
  has none of them). Tell every worker to push work in progress every 30 minutes with its `Uxx-NOTES.md` current.
- Machine: 2 cores, about 7 GB free disk. At most two compiling workers; delete build directories of merged work. One git network operation at a time per
  worker; `gh` through REST only (GraphQL is blocked); no CI runs on a push to `native`, only on PRs.

## Where the port stands: 31 of 37 units merged; about half of the whole port by the lead's estimate
- **Stage N1 (playback core):** every worker-buildable unit is merged, pump effect hosting (U18) and Marbles / Tides included. **Left: steps 10 and 11, the
  lead's own: U34 (offline render API and pad renders through the core), U35 (MIDI scheduling), U37 (bridge and host switch: `core.wasm` behind the Settings
  switch).** Before wiring, the lead still has to read line by line: U07 (sequencer), U08 (voices), U09 (mixer), `fxpump.cpp`, `fxhost.cpp`.
- **Stage N3 (engines), merged:** U22 to U29 (foundation, all layer types, Plaits / Elements, Bass / Vox, Acid / Machine / Blip, both sets of rendered-only
  engines), U30 (…waves engine), U33 (pump synths, rendered path), U04 (LXR as a separable GPL module).
- **Not started:** **U31 …chains engine** (next for a Sonnet worker: it reuses U30's `Graph` and `waves` runner; brief it like U30 was, see the U30 entry in
  TODAY.md, and it needs the lead's review of its live part); **U32 SoundFont player** (UNBLOCKED 17 Nov 23:13: the owner chose **stb_vorbis** (public domain / MIT), pinned and fuzzed like
  every other upstream; start it when a compiling slot is free); **U36 Link / recording / inputs** (lead reviews); **Stage N2 (the Android shell)** and **N4 (Windows and Mac shells)**.
- **Fastest route to something the owner can install:** N1 steps 10 and 11, then N2. U31 can run in parallel with a worker.

## State at 00:10 on 18 Nov (the lead, same chat; check-ins are set for after each usage reset)
- **Pump re-pin to e43ee10:** web = PR #224 into `beta` (build 643; CI green; verified SOUND WITH CONDITIONS, report `/home/claude/progress/verify-224.md`;
  the condition, a 0.85 s audio-thread stall when a live FREEZE node is made, is fixed in 7582f8b: warm-up while stopped, render plays while playing;
  an independent check of that fix with real playback is running; merge to `beta` then `main` when it is clean). Native = PR #225 into `native`
  (four `prepare-pump.sh` patches dropped; exact list unchanged at 40 of 80: the fast-math wasm still reassociates `dot4`; merge after #224, mirror the
  web files, one full ctest on the combined head). **FLEX is NOT in pump's web build: 25 types, nothing to hide** (the brief above was wrong).
  For the owner's pump agent: `#pragma clang fp reassociate(off)` in the oversampler's `dot4`; FREEZE spectra built lazily or in chunks.
- **Running (Sonnet):** U31 on `n3/u31`, U32 on `n3/u32` (stb_vorbis). Notes: `docs/port/U31-NOTES.md`, `U32-NOTES.md` on their branches.
- **Wiring:** `docs/port/WIRING-DOSSIER.md` (a Sonnet read of seq / voice / mix / fxhost / fxpump / core.cpp; the lead checked its top findings against
  the source): Seq has no time / bpm setters; Seq holds `const Song&` across a song swap; the U26 live shell drops NoteOff on a full queue
  (`live_shell.h` enqueue); `SongRenderer::dispatch` plays sampler pads only; `core.cpp` is still the N0 stand-in; no MIDI scheduler. Its part 5 is the
  step order: U34a offline song render C API, U34b stems, then dispatch routing + Seq setters + Song lifetime (the lead reviews).
- `docs/port/IF-SOLD.md`: what would change if the app were sold (the owner asked).

## State at 11:30 on 18 Nov (the lead; supersedes the 00:10 pump paragraph above)
- **The owner wants percentage-complete estimates whenever he asks for progress.** At this point: about 50 % of the whole port, 31 of 37 units merged
  (84 %), about 60 % of the way to an Android install (the lead's estimates).
- **The pump re-pin is BACKED OUT of the web app.** Builds 643 (#224) and 644 (#228) reached `beta`, never `main`. pump e43ee10 builds FREEZE's spectra
  at creation (about 0.9 s on the web's audio thread); four verified rounds of web-side warm-up only moved the stall, and 644 lost the first notes when
  PLAY followed a kit load or engine switch within about a second. Build 645 (branch `web/pump-backout`, a Sonnet worker) returns `beta` to 642's pump
  files and removes the warm-up code; verify it is byte-identical to `main` for the wasm files, then merge to `beta` and `main`.
  **PR #225 (native re-pin) is HELD**: web and native re-pin together once pump has the changes in `docs/port/PUMP-REQUESTS.md` (the owner's pump agent).
- **U31 = PR #227, U32 = PR #226**: built; independent verifiers were running (`/home/claude/progress/verify-227.md`, `verify-226.md`; if the session is
  new, rerun a verifier from the PR). U32: Chrome's `decodeAudioData` drops trailing frames of single-page Ogg streams, so short looped SoundFont samples
  stop early in the web app; native decodes correctly by default (the lead's decision, told to the owner) and has a Chrome-parity mode for the gate only.
  The Android host must pass a key list (`prepare_preset(..., keys)`: full-keyboard piano is 191 MB). U31: the lead still owes the line-by-line review of
  its live part and a decision on the 16 library effect rows left out by name.
- **Next:** merge U31 / U32 when verified; then the wiring per `docs/port/WIRING-DOSSIER.md` part 5 (U34a offline render C API first), U36, then N2.

## State at 13:00 on 18 Nov: START HERE in a new chat (supersedes the pump and U31 / U32 lines above)
- **Web:** `main` = `beta` = build 645 (#229, #230): pump files byte-identical to 642, all FREEZE warm-up code removed. The pump re-pin waits on
  `docs/port/PUMP-REQUESTS.md`; PR #225 (native re-pin) stays open and HELD.
- **U31 (PR #227, branch `n3/u31`) and U32 (PR #226, branch `n3/u32`) are each in a second round** after independent verification (both SOUND WITH
  CONDITIONS; full reports in `docs/port/reviews/verify-227.md` and `verify-226.md`). A Sonnet worker was fixing each when this was written; a new chat
  has no worker: start one per branch from the branch's notes and the list below, then merge when the list is done and the full ctest passes in both
  configurations (give `engine_xeng_*` a 3600 s timeout on a loaded machine).
  - **U31 list:** (1) native clamps parameters to their range at load and on `setParam` (`waves/graph.h:65`), the JS chains engine uses raw values
    (`chains-worklet.js:5831`, `:5843`): port the JS, add out-of-range golden cases, check whether waves has the same fault; (2) audio-thread allocation:
    Marbles `arm` -> `make()` (`modfx/mod_mods.cpp:63`) and `space` (CloudSeed `RandomBuffer::Generate`): move to preparation, and put an allocation
    audit over every module type into ctest; (3) no memory budget and `bad_alloc` uncaught (1024 Resample modules abort): budget at preparation;
    (4) live queue: accepted events dropped at `clive.cpp:58`, a dropped Patch event keeps its mailbox slot (`clive.cpp:73`: after 8, every swap is
    refused), `preparePatch` returns true when it cannot deliver: Playing=0 / Bypass release are never dropped, parameter events coalesce;
    (5) pump take-over leaks a handle when four modules fail between services (`cmods2.cpp:234`), and the host must call `service()` at a stated rate;
    (6) the 16 rows beyond -100 dB are ACCEPTED as left out by name, but their recorded dB becomes a bound (3 dB) in `test_chains.cpp`;
    (7) tests for the verifier's six surviving mutations; (8) `mutate-u30.sh` re-targeted at `graph.h`; (9) notes: 17 modules' `show()` not ported,
    the closed build's 5 skips, and malformed cables (native refuses the patch: if the JS plays it, native must load it); (10) ctest timeouts.
    The lead still owes the line-by-line review of the live part (`chains/clive.cpp`, `cengine.cpp`).
  - **U32 list:** (1) stb keeps frames past the last-page granule for samples 234, 235, 704: trim to the granule in `decodeOgg` so the default equals
    libvorbis exactly; (2) clamp SF2 sample sizes to the data present and add a per-preset / per-font memory budget (a 5.6 KB font allocated 1536 MB);
    (3) lower `kMaxWork` in `sf_parse.cpp` (a hostile font spun 8 s); (4) refuse absurd sample rates; (5) committed reference data for the fitted
    resampler and a behavioural test for a loop end beyond the data; (6) parity mode stays off by default; (7) the PINS.md licence quote.
  - **For the owner (told 18 Nov):** in the web app on Chrome, `decodeAudioData` drops trailing frames of one-page Ogg streams, so looped SoundFont
    zones whose loop end lies past the decode lose their loop: 159 samples, 65 of 197 GM presets (square lead stops after 0.06 to 0.08 s). Native
    sustains them by default. A web-side fix (decode SF3 with a wasm Vorbis decoder) is a possible future item: not started, not asked for.
- **Then:** the wiring per `docs/port/WIRING-DOSSIER.md` part 5 (U34a first), U36, N2. Estimates: about 50 % of the port, 31 of 37 units merged.

## State at 12:55 on 18 Nov (the lead, NEW chat; read after the 13:00 section, which was written just before the move)
- New session: `native` cloned at `/home/claude/thunder-plus`, worktrees `/home/claude/wt/u31` and `/home/claude/wt/u32`. A Sonnet worker per branch was
  started at 12:50 from heads ee359e3 (U31) and 89ad779 (U32) with the second-round lists above; each pushes every 30 minutes, fetches before pushing
  and never force-pushes. Neither PR is merged. The lead reads `chains/clive.cpp` and `cengine.cpp` line by line once U31's worker reports.
- Check-ins (this chat): a restart check at 14:05, then after each usage reset: 15:02, 20:07 on 18 Nov; 01:12, 06:17 on 19 Nov (Mountain).

## State at 23:05 on 18 Nov (the lead; newest)
- **U34a is MERGED (#237, native 8e909ad)**, CI green on every platform. Its independent verifier (a0a273a4180a74c30; report so far
  `docs/port/reviews/verify-237.md`) has found defects D1 to D5 and is wrapping up: D1 `max_frames` rounds the output up to a 128-frame quantum; D2
  `drain` does not mark the handle started (set_pad still accepted afterwards); D3 the header's claim about builds without exceptions is false
  (`render_api.cpp` does not compile with `-fno-exceptions`); D4 `set_pad` allocates a temporary copy beyond the stated pad budget; D5 `create` leaks on
  an allocation failure part-way through building the renderer. **Next job when a slot is free:** a Sonnet worker fixes these on a branch from native
  (with the length ESTIMATE for unknown-length renders), then U34b (stems).
- **U31 (#227):** a server restart at about 21:10 stopped both workers for almost two hours (nothing noticed it: hourly checks are now set). Remote
  head e240c43 FAILED CI on six jobs (Linux GCC, sanitizers, clang, macOS, MSVC, wasm); the old worker's later commit 353cf6a was local only. A FRESH
  Sonnet worker (a64ac5611cf5e1607) took over at 23:00 with a tight brief: push first, read the six CI logs, finish D1 / D4 / D5 / D7 / D9 of
  `docs/port/reviews/verify-227-round2.md`, CI green on every job. The old worker (a1971f2533efd1b7b) is retired: do not resume it.
- **Owner, 22:59:** keep working through the night; set check-ins so nothing stalls; keep the documentation current; watch for workers looping or
  down a rabbit hole (each brief now has a 45-minute park rule and a 20-minute push rule); weekly usage was at 76% (he will reset if it runs out);
  "If you come to a stopping point within 1 hour of 8 am, wait for me. If not, keep going." He wants to switch chats at a clean point.
- **Checks set (Mountain, 19 Nov):** 00:05, 01:12, 02:15, 03:15, 04:15, 05:15, 06:17, 07:15 (the 07:15 one applies the 8 am rule).
- Still queued after the above: U32's D1 follow-up (decode work budget), dossier D2 / D3 (`dispatch` routing, `noShape`), Seq setters, U36, N2.

## State at 20:20 on 18 Nov (the lead; newest)
- **U31 (#227, head 9549599) is NOT mergeable yet.** The round-2 independent verifier (still running at 20:20; its log so far is
  `docs/port/reviews/verify-227-round2.md`, the final one replaces it) found: **D1** `setParam(.., "type", NaN)` on `convolve` segfaults on the audio
  thread in chains and waves (the raw-parameter change hands NaN to an index; every defect found is a NaN through setParam, never from JSON);
  **D2** `waves/graph.h:89` `"\x01btrue"` is a greedy hex escape: clang / Apple clang / NDK / wasi-sdk refuse it (5 CI jobs red) and GCC builds a
  wrong key; **D3** `tests/CMakeLists.txt:746` links `Threads::Threads` without `find_package(Threads)` (MSVC configure fails). Confirmed good by the
  verifier's own tests: the bypass fade across cuts (80 of 80 bit-equal), `setParam(track)` cost max 0.37 ms, mutations (33 of 34 killed, 1 equivalent).
  The builder (a1971f2533efd1b7b) was sent D1 to D3 at 20:15 with the lead's ruling: native never crashes on any double and does what the JS does
  for NaN; a NaN sweep over every parameter of every module goes into ctest; CI must be green on every platform before it reports.
- **U34a = PR #237 (head cfea0bf), built, the lead accepted the API shape** (`native/core/include/thunder/render.h`: create / set_pad / set_pad_kit /
  length / read / position / latency / stats_get / drain / free). Merge when CI is green; an independent verifier right after. To add in U34b: a length
  ESTIMATE for unknown-length renders (progress bars), and note that an out-of-memory aborts in a build without exceptions (wasm).
- Still queued: U32's D1 follow-up (work budget for decodes that are discarded), U34b (stems), then D2 / D3 of the dossier, Seq setters, U36, N2.
- Check-ins set: 01:12 and 06:17 on 19 Nov.

## State at 18:10 on 18 Nov (the lead; newest)
- **U32 is MERGED (#226, native 4982eed): 32 of 37 units.** Round-2 independent verification: SOUND WITH CONDITIONS (`docs/port/reviews/verify-226-round2.md`):
  all seven conditions hold; ASan + UBSan full ctest 150 of 151 passed, 1 intended skip; CI green on 9 jobs. **One new defect, D1 (follow-up PR, not yet
  started):** `prepare_preset` does unbounded WORK on samples that decode in full and are then discarded (last granule 0; aliased headers): a 1.7 MB
  hostile font takes 17.7 s on the control thread. Fix: charge every decode to a work budget and remember a failed stream by its offset; also give
  parse memory a budget (a 191 KB font at the `kMaxWork` cap takes 205 MB). Fonts: `/home/claude/v2tmp/fonts/` (lost on a new session: the report
  describes how to make them). Start it with a Sonnet worker when U31's worker frees a compiling slot.
- **U34a started** (Sonnet, worktree `/home/claude/wt/u34a`, branch `n1/u34a`): the offline song render C API as a PULL model (create from song bytes,
  set pad buffers, read blocks, position / length for progress, free to cancel), the lead's decision: no callbacks across the wasm / JNI boundary.
- **U31:** the worker is on the lead's review items A to K (resumed after the 16:35 server restart).

## State at 15:30 on 18 Nov (the lead; newest)
- **Web:** `main` = `beta` = build 647. 646 (#231): the side panel's instrument list above the PLAYING box. 647 (#233): two FlexCore NaN fixes (FLX E8 just
  under a beat; `rd` at buffer index 0) and the FX bar's Flex PATTERN row writing the FLX command under the cursor (the lead fixed a `false ?? x` bug in
  it at review). FLX 76 on the master was measured as working on both sequencers. Open: page-sequencer FLY 0D differs in headless Chrome (cause not
  isolated); `ScrambleCore.rd` has the index pattern fixed in `FlexCore.rd` (the lead's to-do). **Desktop v1.3.13 = build 647**, released at the owner's
  request (run 44405171015, 3 platforms, 11 assets); recorded in HANDOVER (#235, #236).
- **U31:** the lead's line-by-line review of `clive.cpp` / `cengine.cpp` is done; 11 items (A to K) were sent to the worker at 15:05: stats data race,
  single-producer rule, `cur_` fallback race, in-flight count one short during a swap, honest drop counts, Playing flood, queue size, the bypass
  crossfade truncated on a cut block (a click the JS never has), `prepareDerived` cost on the audio thread, `resample_release` on the audio thread, one
  TSan test. The worker records them in U31-NOTES.md under "Lead review, 18 Nov". Merge #227 only after those and both ctest configurations.
- **U32:** the worker was still running its second round (last push 2168e9a, macOS reference fix). Merge #226 when its report is in and checked.
- Workers of this chat: U31 a1971f2533efd1b7b, U32 ad2f95e69f7ff1111 (resume with a message; a new chat starts new ones from the branch notes).
- **Next when a compiling slot is free:** the U34a worker (WIRING-DOSSIER.md part 5).

## Rules the lead set for gates (they hold for every remaining unit)
- Ported JS: bit-exact against the web app's goldens and a Node reference; committed Linux hashes gate every platform. Never loosen a gate, never hand-edit a
  golden, no `-ffast-math`.
- A unit whose reference is a fast-math wasm (pump, Marbles / Tides, Plaits / Elements, pump synths, LXR, library modules inside …waves): the native library is
  built STRICT; each case within -100 dB of the reference's peak, exact cases listed and kept exact; anything else left out BY NAME with its numbers and a
  structural check (finite, RMS within 1 dB, same length); no native hashes where the system libm is involved (pump). Class rules, each written in its test:
  Plaits raw int16 output within 1 step; **LXR within -60 dB (weakest parity in the port)**.
- Live paths: sample-exact events, live output equals the rendered note bit for bit at every host block size; a fixed small latency instead of block-quantised
  timing (31 samples bass, 15 other live cores, 127 …waves); no allocation or locks after preparation.
- Memory safety: every unit fuzzed with several extreme fields at once under ASan + UBSan; a read outside a table in third-party code is patched narrowly in
  `prepare.sh` / `prepare-pump.sh` for every host build (never a sanitizer-only workaround) and reported.

## Conditions recorded for the wiring (N1 step 10 / U34), from the reviews
- U26 live shell: its latency goes into the PDC plan; a NoteOff must never be dropped when the 256-event queue overflows.
- U30: the Expression module must be compiled at patch preparation, not on the audio thread; Marbles' re-make at arm must not allocate there.
- U25: a thread that renders Plaits / Elements must call `pe_prepare()` off the audio thread first (see "Conditions before this is wired" in U25-NOTES.md).
- U33: pump synths are not yet wired into `xeng_render_voice`; LXR host glue and live lanes are not wired.
- Each unit's `Uxx-NOTES.md` has a "Choices for the lead" or "Conditions" section: read them before wiring that unit.

## Waiting on the owner
- **His ears:** Elements (33 cases differ from the first block), LXR (-60 dB class), Plaits' 45 chaotic cases, and the N1 exit listen (three songs with the
  native core switched on in the desktop web copy, once U37 exists).
- **The pump repo: CONFIRMED by the owner at 21:36 on 17 Nov ("pump is committed"). Checked: pump HEAD is e43ee10** (PR #6 `fix/synth-table-reads`, merged
  17 Nov 18:51): WOB sub phase wrap (5468d8c), `BandWaves::read` (d4c4987), GLOSS `DigiWaves::read` (8d851d0), FREEZE spectra made at creation, web build
  (24a24e1), `SynthBase::run` insertion sort (458b88d), the oversampler's halfband FIR summed with four named accumulators in a fixed order (8d95efc).
  **TO DO FIRST IN THE NEW CHAT (a Sonnet worker each side, the lead reviews):** re-pin from deb07d3 to e43ee10. Things to know before starting:
  (1) between the old pin and e43ee10 there is also e7c55dc, **a new pump plugin, FLEX** (Thunder Plus's FLX / FLY as a plugin): a 26th effect type in
  `pump.wasm` and its meta, so the web's lists and the native `kPumpTypes` / `pumptable.inc` change, and **the owner decided (17 Nov 22:17): "FLX / FLY can stay hidden"**: the type is in the wasm and the tables, but the web app's effect picker and lists do not offer it;
  (2) the oversampler change alters TUBE, LIMIT, HEAT, STRIP and anything oversampled by ULPs ON PURPOSE (it removes the fast-math reassociation): the web's
  pump goldens for those move (regenerate only those rows, list them), and natively the TUBE / `x_two_hosts_same` rule and several "within -100 dB" pump
  cases should become bit-exact: re-measure, tighten the exact list, and drop rules that are no longer needed; (3) drop the native patches the fixes
  replace (`prepare-pump.sh`: the three table reads and the insertion sort) and confirm the gates still pass without them; (4) web: rebuild `pump/pump.wasm`,
  `pump/synth/pump-synth.wasm` and both metas from e43ee10 as a new web build on `beta`, prove the unchanged types are bit-identical as build 638 did, and
  re-check the WOB crash case (22050 Hz, note 127, Octave +2, Tune +12) no longer traps; (5) `PINS.md` and `native`'s copies of the web files follow.
- (Settled 17 Nov: the …chains editor keeps accepting any cable; see below.)
- (Settled 17 Nov 23:13: U32's decoder is stb_vorbis.) He also asked for a note of what would have to change if the app were ever sold
  (unlikely): `docs/port/IF-SOLD.md`.

## Web app, state at 22:00 on 17 Nov (no web branch is open; no worker is running)
`main` = `beta` = build 642; desktop v1.3.12 is build 639 (the owner has not asked for another). Today's web builds: 639 (Chains / Waves editors start
again: a raw U+2028 / U+2029 in a regex had stopped both editor scripts since about build 620; the owner's song is the first-open default), 640 (a module
dragged to the edge stays under the pointer; Undo of a long drag is one step), 641 (299 pop-up messages reworded from the audit sheet
https://claude.ai/artifact/WSbgsqqBk5oU6QbpWTKMPy; 53 not applied, listed in HANDOVER; the owner reviews from the sheet later), 642 (one playhead rule in
Pattern view: a bar per track on its own row, the active track included, playing and stopped; the FX bar's ...CHAINS section collapses to its tab; FIT rows
and A- / A+ for the pattern grid: on the owner's screen 32 rows fit at 10 px rows, about 7.7 px text; the floor is 9 px rows). His tests for all of these are in
"Owner's punch list" in HANDOVER.md.

## Decided by the owner on 17 Nov
- The ...chains editor keeps accepting any cable into any input ("leave the cables as is, as long as they do something"): no port-kind rule.
- Effect slots stay at 8 for now; he uses bus tracks for more. **Future feature: 12 slots** (scoped: FXO's first digit is the slot 1 to 8 and 9 means "all",
  so slots 9 to 12 need a new command; FXS reaches 15; older builds drop slots above 8 on load; new songs' mastering slots would move; the native core has 8
  as a fixed size in about 15 places; 16 breaks both digit fields).
- "The dual sound bug can wait" (both instruments sound when a note's instrument number changes during playback).

## Future features he asked for (web app; the lead's, audio path)
- **FIRST: bus into bus.** He wants a bus's output, with its FX, to feed another bus through ...chains ("the reverb I put on the first bus doesn't play when
  things were on the second bus"). Today a bus is "a track with a Chain Out and no Track In of its own" (`chainsTracks`, `chainsFeed`, `chainOutTargetsOf`
  near `index.html` 20935; the bus plan in `core/seq-core.js`), so a track that both receives (Chain Out) and sends on (Track In) is not a bus and its source
  shows MISS. Needs: tracks processed in dependency order, cycles refused with a clear message, the latency plan following the chain, the Mixer's CHN row
  saying what is happening, and the native core's bus plan kept in step.
- 12 effect slots (above).

## The combined `native` head was built and tested after the last two merges
dd879896 (#217 + #214): Release GCC build clean, full ctest 143 of 144 passed with 1 intended skip, the closed build (no LXR, no DSP library, no pump) 3 of 3.
