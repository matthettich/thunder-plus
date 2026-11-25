# Pick up here (written 17 Nov 2026, 22:00 Mountain, by Fable, the lead, at the owner's request to move to a new chat)

To continue the native port in a new chat, the owner says: **"pull thunder-plus, branch `native`, and continue from docs/port/PICKUP.md"**.
This file is the brief. Background only when needed: `docs/port/TODAY.md` (the full log of 16 and 17 Nov, newest at the end), `docs/port/WORKPLAN.md` (the
plan; sections 1 to 4 and 9), `HANDOVER.md` on `beta` (the web app, the owner's punch list).

> **22 Nov, web-app work to mirror (read first):** the lead is adding **patch knobs** on `beta` (plan `docs/plans/PATCH-KNOBS-PLAN.md`
> on `beta`): tracker commands **KN1–KN8** appended to `FXC` after `CHD`, AudioParams **k1–k8** on `thunder-chains` (later the …waves live
> node), a new shared module **`knob`** that reads the track's knob values, later MOD targets K1–K8. The sequencer (U07), chains (U31) and
> waves (U30) ports will need these; check that plan's progress notes on `beta`. Nothing in `native/` was changed by it.

## How the owner wants this run (his words, 16 and 17 Nov)
- The lead manages, decides, reviews and merges; **everything that can be is built by low-cost agents** (Sonnet builds a unit in its own `git worktree` under
  `/home/claude/wt/`, Haiku does document chores); an INDEPENDENT Sonnet verifier checks a unit before or right after merge (it has caught a real crash twice).
- Questions: make the best decision and tell him; weigh solutions that port easily. He wants to keep making music on the web app meanwhile: fixes go to
  `beta` by PR, then `main` by PR after each (he said so); a desktop release only when he asks.
- Only what is pushed exists: the session's server restarts every hour or so and stops the workers (they can be resumed with a message to their id; a new chat
  has none of them). Tell every worker to push work in progress every 30 minutes with its `Uxx-NOTES.md` current.
- Machine: 2 cores, about 7 GB free disk. At most two compiling workers; delete build directories of merged work. One git network operation at a time per
  worker; `gh` through REST only (GraphQL is blocked); no CI runs on a push to `native`, only on PRs.

## Where the port stands: 35 of 37 units merged, U36's core complete (21 Nov); FX1, U37 P3, P4 and P5 merged 24 Nov; about 77% of the whole port by the lead's estimate (24 Nov, 17:05) (the lists below are older: see START HERE)
- **Stage N1 (playback core):** every worker-buildable unit is merged, pump effect hosting (U18) and Marbles / Tides included. **Left: steps 10 and 11, the
  lead's own: U34 (offline render API and pad renders through the core), U35 (MIDI scheduling), U37 (bridge and host switch: `core.wasm` behind the Settings
  switch).** Before wiring, the lead still has to read line by line: U07 (sequencer), U08 (voices), U09 (mixer), `fxpump.cpp`, `fxhost.cpp`.
- **Stage N3 (engines), merged:** U22 to U29 (foundation, all layer types, Plaits / Elements, Bass / Vox, Acid / Machine / Blip, both sets of rendered-only
  engines), U30 (…waves engine), U33 (pump synths, rendered path), U04 (LXR as a separable GPL module).
- **(Stale: U31 is MERGED, #227.)** Was: **U31 …chains engine** (next for a Sonnet worker: it reuses U30's `Graph` and `waves` runner; brief it like U30 was, see the U30 entry in
  TODAY.md, and it needs the lead's review of its live part); **U32 SoundFont player** (UNBLOCKED 17 Nov 23:13: the owner chose **stb_vorbis** (public domain / MIT), pinned and fuzzed like
  every other upstream; start it when a compiling slot is free); **U36 Link / recording / inputs** (lead reviews); **Stage N2 (the Android shell)** and **N4 (Windows and Mac shells)**.
- **Fastest route to something the owner can install:** N1 steps 10 and 11, then N2. U31 can run in parallel with a worker.

## START HERE (25 Nov, 00:50): the state for a new chat. Everything below this section is history, newest first.
**Say to a new chat:** "Pull thunder-plus, branch native, and continue from docs/port/PICKUP.md. Start at the newest START HERE."

**The owner's rules:** as the 21 Nov 18:18 and 24 Nov 13:05 sections; plus: use Fable (the top model) for audio-thread reads, Opus otherwise, and say when to switch; work goes on overnight; a summary at his morning check-in.

**MERGED overnight:** C2a #310 (squash 980a5e4: `chains_diff`, `bus_plan_equal`), **C2b #311 (squash fad450f: the chains host LIVE through `LiveHost`, `thunder_live_chain_edit`, F_CHAINS / CAP_CHAINS)**. Design and amendments: `docs/port/C2-DESIGN.md`. About **81%** of the whole port.

**MERGED 25 Nov 04:10: PFX #314** (every pad effect the page offers in the core's pad render; 165 page-made goldens; verifier + re-check MERGE, `review/verify-pfx`). Follow-ups: Beads media 0 differs by about -58 dB (gate -50 dB, 8 dB margin: fast-math in the page's wasm); `rv_beads_media1` compares silence with silence; the Windows core build has no effects library, so library pad effects read as `Kind::Other` there (songs fall back).
**Also built overnight (drafts, not merged):** N2 H3 #317 (songs / samples folders, open from a file manager, save with a changed-underneath check), H4 #318 (release APK beta / stable, signing from secrets the owner must create, "check for new build"; it also changes `.github/workflows/native.yml`), H5 #319 (fold / keyboard / back; `native/tests/ANDROID-CHECKLIST.md`), P3b #320 (private random state per effects plan; the FX-param snapshot like L1; open: FX params while a play is stalled). The verify-n2 findings (`review/verify-n2`) are fixed on #312..#316 except the LOW F7b, F7c, F12, F15, F17.
**CI at 05:10:** every core job green on #312, #313, #315, #316, #317, #320. **#318 and #319 have NO Native CI run at all** (only "Tests" ran): #318 changes `.github/workflows/native.yml` and the run was never triggered; find out why first thing (re-push, or the workflow change needs the owner to approve it), the YAML parses. About **83%** of the whole port.
**Owner decisions waiting:** the four signing secrets; Back at the page's top (background, keep playing?); swipe-away while playing; the INTERNET permission for the new-build check; PC1 (index.html client loader) go-ahead; N2-PAGE-CLIENT's five questions.

**OPEN, in order of merge (all drafts into `native`, stacked: each contains the ones before it):**
- **PFX #314** `n1/pfx-pad-effects`: every pad effect the page offers, in the core's pad render (`EngFx::Kind::Lib`; 134 page-made goldens). Verifier: MERGE AFTER FIXES (`review/verify-pfx`: Beads freeze+size crash, -fexceptions not in effect, missing cases, absent segment keys); the builder is fixing them. Independent of the N2 stack.
- **N2 H1a #312** `n1/n2-live-shell`: the Android app on the live engine (JNI over `thunder_live_*`, audio callback, control / poller threads, the core's own pad renders, "Play bundled song natively" test path). Plan: `docs/port/N2-PLAN.md`; notes: `docs/port/N2-NOTES.md`.
- **N2 H1b #313** `n1/n2-h1b`: the shell side of CORE-BRIDGE (incl. section 7), `SongFit` (fallback reasons; the page's `engFx` rule), events to the page.
- **N2 H1c #315** `n1/n2-h1c`: audio focus, ducking, headphones, stream rebuild, MediaSession with the real song name, the A5 10-minute gate button.
- **N2 H2** `n1/n2-h2` (in progress): MIDI through AMidi for the live engine.
- **NEEDS THE TOP MODEL (Fable) before merge:** H1a's audio callback / threads (`live_engine.cpp` render / controlLoop / pollLoop; `live_jni.cpp` dataCallback / start / shutdown), H1b's event hand-off and `engineReset`, H1c's stream rebuild; `docs/port/CORE-BRIDGE.md` section 7 (draft additions G1..G14 + H1c's) to freeze; each of these PRs also gets an independent Sonnet verifier.
- **The page side:** `docs/port/N2-PAGE-CLIENT.md` (design: one root `thunder-core-client.js` behind a switch, ~22 guarded sites in index.html, PRs PC0..PC7, ~56 builder-hours). **PC1 was NOT started: it changes the owner's daily web app (`index.html` on `beta`) and waits for his OK.** Its five questions for him are in its section 8.
- After #311: merge `origin/native` into `n1/n2-live-shell`, then `n1/n2-h1b`, `n1/n2-h1c`, `n1/n2-h2` (merges, never rebase), so F_CHAINS turns on in the app.

**Estimate to the Android beta (WORKPLAN N2 exit):** about 1.5 to 2 weeks including the owner's device tests (shell ~76 builder-hours by N2-PLAN, page client ~56, lead review ~14 hours).

## START HERE of 24 Nov, 17:35 (now history: C2 built and merged)

**The owner's rules:** as the 13:05 section below. Lead: Fable (new chat, 17:07).

**C2 (the chains host LIVE) is DESIGNED and being built.** The design and the lead's rulings R1..R15: `docs/port/C2-DESIGN.md` (read it before anything else). Two Sonnet builders, each in its own worktree under `/home/claude/wt/`:
- **C2a** `n1/c2-cdiff`: `chains/cdiff.{h,cpp}` (the page's `chnDiff`, classified Same / Params / Structural; `bus_plan_equal`), a Node golden tool; notes `docs/port/C2A-NOTES.md` on that branch.
- **C2b** `n1/c2-live`: `ChainsHost` live additions (`keepPatch`, `play` / `stop`, `param`, the `live` flag) and the `LiveHost` wiring (F_CHAINS, the plan in `PlayPlan` / `ChainsCommit`, `chBack_`, the edit ring, `thunder_live_chain_edit`, stats); notes `docs/port/C2-NOTES.md` on that branch. It merges `n1/c2-cdiff` when that is pushed.
A new chat has neither builder: read each branch's notes and re-brief a builder from them and from C2-DESIGN.md. Then: an independent Sonnet verifier per PR (`review/verify-c2a`, `review/verify-c2b`), the lead reads the lines listed at the end of C2-DESIGN.md, squash-merge when all CI is green.

**After C2, in order:** the Android shell on the live host (N2; the call order is in U37-P5-NOTES.md, plus F_CHAINS); P3b; C3, C4.

## START HERE of 24 Nov, 13:05 (now history: P4 / P5 merged, C2 next)

**The owner's rules:** the 21 Nov 18:18 section, plus (23 Nov, 22:31): delegate whenever possible, switch chats as often as is useful, no update while he sleeps. A two-hour "moving or stalled" line while he is awake.

**U37 P3 is MERGED: #301, squash `49c956b` (24 Nov, 12:54).** Every job green on `966f165` except macOS, which never completed there (four runs "not acquired by Runner", one cut off at 60 minutes with 95 tests passed, none failed). The owner approved ("Merge it then", 10:51) merging on the macOS pass of #302's head `be7f5f8`, which held the same code.

**U37 P4 MERGED (#302, `2816922`) and U37 P5 MERGED (#308, 24 Nov 17:05 Mountain; CI all green on `5b2e090`).** P5 = the C API over `LiveHost` (`live_api.cpp`), notes `docs/port/U37-P5-NOTES.md`; verifier `review/verify-p5`; the engine offset is `thunder_live_engine_offset` (one atomic; never derive it from stats). Not implemented (E_UNSUPPORTED, caps off): `font_add`, `font_remove`, `pad_sf`, `pad_live`. #303 was closed (its history held a mistakenly committed `build/`).

**Next, in order (supersedes the list below):** C2, the chains host LIVE (plan / commit command, a returned-plan ring before `settle()`, `beginBlock(engine frame)` from `LiveHost::quantum()` at the marked hook, `thunder_live_chain_edit`; design in `docs/port/CHAINS-HOST-DESIGN-DRAFT.md`): design-heavy audio-thread work, the owner was told it is the point to switch the lead back to Fable; then the Android shell on the live host (N2; the call order is in U37-P5-NOTES.md); P3b; C3, C4. Estimate: about 77% of the whole port.

The P4 notes below are history.

**(History) In review, NOT merged: U37 P4 = PR #302, branch `n1/u37-p4`, head `58feb0c`** (MIDI in `LiveHost`, the DIRECT path; the posted fallback was not needed). Notes: `docs/port/U37-P4-NOTES.md` on that branch.
- `58feb0c` is ONE commit on `native`: the tree of `be7f5f8` (the old eight-commit head, on which ALL NINE CI jobs were green, macOS and sanitizers included) with native's PICKUP.md. Checked with `git diff --stat be7f5f8 58feb0c`: PICKUP.md only.
- Byte-equal to the posted path: 18 ledger songs + start-midway + loop; stop while playing (18 cases); non-zero FX latency against `SongRenderer`. `thunder_midi_set_lead` cap 1.2 s. The macOS job's limit is 90 minutes in this PR.
- The lead read the whole core diff (both rounds) and accepts it. Independent verifier: `review/verify-p4` ("MERGE" on `8f903c9`); round 2 (`de1c4fd`) answers its findings 1 (wrong MIDI after a refused publish: the scheduler's song post is now the last step that can fail), 3, 4, 6, 7, 8, 9.
- **To do: when every job is green on `58feb0c`, squash-merge #302 (REST, pin the sha).** If only the runners fail again (macOS not acquired; sanitizers "lost communication" at about 49 minutes: seen three times today, cause not found, it passed on `966f165` and `be7f5f8`): re-run the job. The code is the tree that already passed.

**Things that bit today**
- **Force-pushes are refused by this session's permission rules: put a moved branch on a NEW branch name and open a new PR instead. Never `git rebase` in this shallow clone** (not even `--onto`): every file conflicts as "both added", and `-X ours` silently DROPS the branch's changes (a wrong head `59b87c0` was on `n1/u37-p4` for a minute). To move a branch onto a squash-merged base: `git checkout -B <branch> origin/native && git read-tree -u --reset <old head> && git checkout origin/native -- docs/port/PICKUP.md`, commit, then check `git diff --stat <old head> HEAD`.
- A PR stacked on an unmerged one can be opened against `native` at once: its CI runs (CI needs a PR into `native`), which is how P4 was tested while P3 waited for a macOS runner.
- A test's "non-empty" guard must allow for the sanitizer build's reduced matrix (`live_midi` asked for more than 50 records, the sanitizer run has 46).

**Rulings for P5 (the lead, 24 Nov)**
- MIDI records keep the ENGINE frame (verify-p4 finding 2). P5 corrects `live.h`'s text ("host timeline's frame") to say so: the shell maps engine frames to its clock with the CLOCK records, as for every other record. `midiDrain` does not add the offset (a record booked before a re-anchor would be mapped with the wrong one).
- A MIDI command posted after `play` in the same quantum is ordered differently from the posted path (verify-p4 finding 5, the documented difference): accepted.

**Next, in order:** merge #302; U37 P5 (the C API `thunder_live_*` over `LiveHost`, the shells; `thunder_live_alloc` blocks must be `new float[]`; must set `THUNDER_LIVE_CAP_MASTER_SWAP`; the rulings above) on `n1/u37-p5`; P3b (a private rand state per effects plan: `FxHost::plan` on the control thread and `FxHost::process` on the audio thread share `rand_` / `stmRng_` / `daisySeed_`); C2; the Android shell on the live host. Open after P3, unchanged: ring pushes that are `assert` only (`fxBack_`, `giveBack`); pump slots have no FXV / `fx_param`; a pump slot waking from rest allocates on the audio thread. Estimate: about 74% of the whole port (P3 merged, P4 built, verified and green).

**Web side:** the owner asked (24 Nov) about the per-track loop ("independent playheads": a track that runs out loops on its own) and decided to HOLD OFF. It stays "possible, not decided" in HANDOVER on `beta`; nothing to build.

## START HERE of 24 Nov, 07:35 (now history: P3's review and CI)

**The owner's rules:** the 21 Nov 18:18 section, plus (23 Nov, 22:31): delegate whenever possible, switch chats as often as is useful, no update while he sleeps.

**In review, NOT merged: U37 P3 = PR #301, branch `n1/u37-p3`, head `966f165`** (effects, master chain, PDC and meters in `LiveHost`, with R1 and R2). Notes: `docs/port/U37-P3-NOTES.md` on that branch.
- Built by a Sonnet builder: LiveHost bit-equal to SongRenderer on ELECTRO with all slots (4 variants, several call sizes and frame0); `rt_harness` third pass "no allocation in process".
- **The lead has read the whole audio path up to `5f54ee2` and accepted it**; the commits after it (`24078f1`, `966f165`) are test-only.
- Independent verifier (Sonnet; report in this session at /home/claude/verify-301.md, NOT yet pushed to `review/verify-301`: if that branch is missing the report is lost, its findings are here): "merge after finding 1". Finding 1 (wrong audio, confirmed by a run): a waiting FxCommit with a future `at` was committed AFTER a play's own plans. FIXED in `5f54ee2` (`commitWaitingFx`: effects plans commit in PLANNING order; tests fail without it). Real-time audit, lifetimes, R1, R2, the P0 contract: clean.
- **What blocks the merge is CI only**, and not clearly our code: (1) "Core (Linux, sanitizers)" died twice at about 49 minutes with "The hosted runner lost communication" (heads `5f54ee2`, `24078f1`; no log). Cause NOT found. `966f165` makes `test_live_fx` lighter (sanitizer build: 153 s to 54 s, 623 MB; reduced matrix under `THUNDER_SANITIZE_BUILD`) but the builder could not show that test was the cause. If it dies again: re-run the job once; a third time, a Sonnet builder bisects (sanitizer ctest with -V, a per-test timeout, the log uploaded as an artifact even on failure). (2) "Core (macOS universal)": GitHub's macOS arm64 runners are short ("job was not acquired by Runner", "capacity constraints") and slow on some runs (the BUILD took 40 minutes on `native` `d934c0f`, cancelled at the 60-minute limit; 20 minutes for the whole job on `5f54ee2`). Re-run the job (`gh api -X POST repos/matthettich/thunder-plus/actions/jobs/<id>/rerun`, once the run has finished); if it keeps timing out, raise the macOS job's limit to 90 in `.github/workflows/native.yml`.
- **When every job is green on the head:** squash-merge #301 (REST, pin the sha), write the next START HERE, start P4.
- **Open after P3 (file in the next START HERE):** P3b = a private rand state per effects plan (`FxHost::plan` on the control thread and `FxHost::process` on the audio thread share `rand_` / `stmRng_` / `daisySeed_`: non-repeatable noise in Beads / Clouds / Cloud Seed / PitchShifter, no memory unsafety; old code, reachable since P3); ring pushes that are `assert` only (`fxBack_`, `giveBack`); pump slots have no FXV / `fx_param`; a pump slot waking from rest allocates on the audio thread (the accepted FX1 rule, hit more often in live use); P5 must set `THUNDER_LIVE_CAP_MASTER_SWAP`.

**Next, in order:** merge #301; U37 P4 (MIDI in the host: `docs/port/U37-P0-NOTES.md` section 5 ruling 1; byte-equal to the posted path for the 18 ledger songs, else the posted fallback; raise `thunder_midi_set_lead`'s cap to 1.2 s) on `n1/u37-p4`; P5 (the C API, the shells); P3b; C2; the Android shell on the live host. Estimate: about 72% of the whole port (FX1 merged, P3 built and checked).

## START HERE of 24 Nov, 02:35 (now history: FX1's merge)

**The owner's rules:** those of the 21 Nov 18:18 section below, unchanged. From 23 Nov, 22:31: delegate whenever possible, switch chats as often as is useful, and no update while he sleeps (one update at 07:30; the overnight check-ins work silently). His weekly usage reset on Mon 23 Nov 18:59 Mountain.

**FX1 is MERGED: #286, squash `d934c0f` (24 Nov, 02:30).** CI all green on `e546a21` (the merged head `a018c52` differs by one notes line). Independent verifier (Sonnet): "merge after test hardening", no crash or wrong-audio defect found; its report is on `review/verify-286` (if that branch is missing, the push failed: the findings are summarised here). After the 21:50 section was written:
- The lead read the rest of the audio path (`fxhost` / `fxsong` / `fxpump` / `fxbank`) and accepted it. Known and accepted: a final block shorter than 128 frames runs a full 128-frame module block (Chrome only renders 128s).
- Hardening (`286c48f`, `83765a8`): `render_api_oom` tolerates a pump-side leak only when it EQUALS the pump_new blocks still live, ours must be 0, tolerated cases capped (4: Linux sees 1, macOS 2); the no-pump ELECTRO check (only where the effects library is built); static_asserts and run-time guards tying `modfxtable.inc` / `pumpparams.inc` to `kModMax` / `kModMaxParams`; `fx_make_mod` refuses a module without outputs.
- Pump's C++ is built `-fexceptions` on every host build (owner: "Build with exceptions"). macOS arm64 aborted in `render_api_oom` (uncaught bad_alloc) until `native/CMakeLists.txt` got `-funwind-tables` on APPLE (files built `-fno-exceptions` have no unwind tables there). The sanitizer job: LeakSanitizer does not track what pump_new allocates in that test; `test_fxsong` without the vptr check.
- **TWO SESSIONS drove #286 overnight** (this chat and the previous chat's builder, which kept pushing: every push cancels a 70-minute CI run). The owner was told to close the old chat. Before starting a unit, check via REST that its branch does not already exist.
- **Open after FX1** (`docs/port/FX1-NOTES.md`, "Open after FX1"): FX1b (BETA's master pump chain -69.0 dB against Chrome, cause not found; bmod -69.7; lverb / room -69.9); the tape-then-cab rounding on ELECTRO track 7 (older than FX1); Convolve IRs, the BETA golden file, sidechain, NOTES FROM, pump FXV; an owner for the raw arrays in pump's `verb` `Engine::prepare` (the one remaining out-of-memory-only leak; in the pump repo, not here: the owner asked "do we need to fix pump at all" and was told no, not for the port). Verifier's remaining notes, not done: `test_fxsong` compares the host with a re-implementation of `modRun` (a shared transcription error would pass); the test aid `thunder_pump_create_depth` lives in `fxpump.cpp`; dead `page_ ? -1 : 0` at `beat.cpp` about line 67.

**Web side:** #299 is merged into `beta` (found merged on 23 Nov, 22:37). Otherwise as in the 21:50 section below.

**Next, in order:** U37 P3 (effects, master chain, PDC, meters in `LiveHost`, with R1 and R2 of the 21 Nov 18:18 section): a Sonnet builder on `n1/u37-p3`, notes `docs/port/U37-P3-NOTES.md` ON THAT BRANCH (if the branch exists, the notes say where it stands; if not, it was never started). Then P4, P5; C2; the Android shell on the live host. Kept open as before: the U31 engine's OOM leak; FX1b; the web items in HANDOVER on `beta`.

## START HERE of 23 Nov, 21:50 (now history)
**Say to a new chat:** "Pull thunder-plus, branch native, and continue from docs/port/PICKUP.md. Start at START HERE (23 Nov, 21:50)."

**The owner's rules:** those of the 21 Nov 18:18 section below, unchanged (delegate to Sonnet builders, the lead reviews every audio-path line, an independent Sonnet verifier per PR, squash-merge into `native` when ALL CI is green; a two-hour "moving or stalled" line; notify him rarely; push often). His weekly usage reset on Mon 23 Nov 18:59 Mountain. This session's permission rules refuse a merge into `beta` (and edits some builders try) without his words in chat: ask in one line.

**FX1 (PR #286, branch `n1/fx1-song-fx`, worktree `/home/claude/wt/fx1` in the OLD chat's machine: a new chat makes its own): close to merge, NOT merged.** A builder was still working when this was written and its report may never be read: **the truth is `docs/port/FX1-NOTES.md` ON THAT BRANCH plus the PR's CI.**
- **Done and read by the lead:** `origin/native` merged in (`ca88237`: CMake and `thunder-render` conflicts, both sides kept). **The master Flex / Retrig mismatch is FIXED:** Chrome's offline render uses the page player's beat clock (a ConstantSource `offset` ramped in float32); native modelled only the core lane, and `clkSpb` reads the clock's slope, so one ulp moved Flex's tempo estimate. `BeatClock` page mode (`native/core/src/mix/beat.cpp` `prender`, on only with `page_timing`), `test_beat_page` bit-equal on 9216 Chrome values. The lead read `beat.cpp` / `beat.h` / `mix.cpp` `clockBlock` / `render_api.cpp:138` and accepts them. NOT yet read by the lead: `fxhost` / `fxsong` / `fxpump` changes of the first FX1 builder (functions listed in FX1-NOTES): read them before merging.
- **Measured (builder's figures, not re-measured by the lead), all slots against Chrome:** ELECTRO -69.4 dB (was -16.9), BETA-TESTING-ELECTRO -72.7 dB (was -5.9); master Flex alone -87.4; most single effect types -80 or better.
- **Lead's ruling, told to the owner:** FX1 merges at these figures, the -80 dB done-test is NOT met and the remainder is filed: (i) ELECTRO = the old tfx-vs-wasm tape rounding amplified by cab on track 7 (-73.6 dB from 14.2976 s), older than FX1; (ii) **FX1b, a new follow-up unit:** BETA's master pump chain alone (peq, pcalm, pcomp, ptone, pmaster) is -69.0 dB from 39.57 s while Chrome repeats itself at -121.9 dB: a real native-pump-vs-Chrome difference, cause not found; also bmod -69.7, lverb / room -69.9; (iii) not started: Convolve IRs, the BETA golden file, sidechain, NOTES FROM, pump FXV.
- **`render_api_oom`:** failed because (1) MODFX modules were built `-fno-exceptions` (fixed: their sources are in the `-fexceptions` list) and (2) 49 leaks inside the pinned pump plugin on bad_alloc (`pumpfx_new_*` -> `Engine::Engine` / `Engine::prepare`; `native/core/pump/CMakeLists.txt:86` builds pump `-fno-exceptions`). **The owner said "Build with exceptions" (23 Nov, 21:42):** pump's C++ objects get `-fexceptions` in host builds; the builder was told to do it, keep the test STRICT if the leaks go to zero, otherwise strict for ours and tolerant (counted, bounded) only for what remains in pump's own code, without patching pump's sources. Check FX1-NOTES for how that ended; if the builder's edit was refused by the tool, the lead makes it (the owner has approved it).
- **To finish:** CI green on the final head (last seen: GCC / clang red on oom, MSVC `fxsong` fixed in `1dc017f` but CI not seen, sanitizers lost a runner), the lead's read of the remaining audio-path functions, an independent Sonnet verifier, squash-merge, then this file.

**UPDATE 24 Nov, 02:15: FX1 is READY TO MERGE, waiting only for CI on its docs-only head `a018c52`** (every job was green on `e546a21`, the commit before it: GCC, clang, sanitizers, macOS, MSVC, wasm, Android, `test`). When green: squash-merge #286. Done since the text above: pump built `-fexceptions` on every non-wasm build (owner's word), `-funwind-tables` on Apple; `render_api_oom` on ELECTRO, 207 injections: 0 leaks of ours, 1 pump-side on Linux, 2 on macOS (raw arrays in pump's verb `Engine::prepare`), tolerated cap 4. **The lead read** `FxHost::modRun`, the `fxhost.cpp` / `fxpump.cpp` changes and the page clock: nothing on the block path allocates. **Independent verifier (Sonnet, read only, built nothing): APPROVE WITH FOLLOW-UPS.** The follow-ups, as unit **FX1c** (small, a Sonnet builder; none blocks the merge):
1. `test_render_api_oom`: "pump-side" is a scope flag around `pump_new` (`thunder_pump_create_depth`, `fxpump.cpp:61`), so a whole pump instance WE fail to free would be tolerated once (only the cap of 4 stops it), and `__lsan_disable` hides the same blocks from LeakSanitizer. Tighten: tolerate only blocks allocated in the FAILING `pump_new` call (not earlier, completed ones), keep ours at exactly 0.
2. **`PumpSlot::renew` (`fxpump.cpp:166`, called in the block loop at `:257` / `:292`) runs `pump_new` (allocates) and now `giveState()` on the AUDIO path; with `-fexceptions` a bad_alloc there unwinds into the render loop.** The allocation is older than FX1 (it mirrors the page's worklet) and is harmless in the OFFLINE renderer, but it must be moved to the control side (a spare made in advance, as `ModFx::spare`) BEFORE pump effects run in the live host (U37 P4): put it in P4's brief.
3. Refusal counters (`kRefSidechain`, `kRefNotesFrom`, `kRefUnknown`, `kRefNoEngine`, `fxvRefused_`) are read only by `test_fxsong`: add them to `thunder_render_stats` and `thunder-render`'s output; FXV on a slot that is not hosted (`fxbank.cpp:55`) returns false without a count though the header says it is counted.
4. Tests that can pass too easily: `test_fxsong` prints `nHostMod` / `nHostPump` but does not assert them (the notes' 82 and 50); `test_beat_page` asserts `checked > 0`, not 9216; the MODFX reference re-implements `modRun`; no test with `ctx.playing = true`; nothing checks the master pump latency against the delay actually applied.
5. `beat.cpp`: an unreachable `page_` branch in `render()`.

**Web side:** `beta` is at build 672 or later (another chat of his is building patch knobs KN1 to KN8 and the FLZ Flex bank there: see the 22 Nov note at the top of this file; **the Flex CORE changed on the web after the native port's copy**, so compare native only against references made from the `native` branch's own `index.html`). **PR #299** (HANDOVER.md only, into `beta`: the app-switch alignment bug, the lost-header entry's status, two things waiting on him) is open and waits for his "merge #299 into beta". #288 was closed (stale).

**Next, in order, after FX1:** FX1b (above) can wait; the critical path is item 2 onward of the 21 Nov 18:18 list below (U37 P3 with R1 and R2, P4, P5; C2; the Android shell on the live host). Kept open as before: the U31 engine's OOM leak; the web items in HANDOVER on `beta`.

## START HERE of 21 Nov, 21:45 (now history)
**Say to a new chat:** "Pull thunder-plus, branch native, and continue from docs/port/PICKUP.md. Start at START HERE (21 Nov, 21:45)."

**The owner's rules** are those of the 18:18 section below (unchanged), plus (21 Nov, 18:38): weekly usage at 90 %; push work to git regularly; notify him rarely (needs, and the two-hour "moving or stalled" line).

**MERGED since 18:18:** #283 (U37 P2 `LiveHost` core, `a00609a`), #281 (chains C1, `ChainsHost` offline, `897a4dc`), web #284 into `beta` (build 661: generators follow the song's key, TRANSP. + / - cover a whole selection; the owner said "Merge"). `beta` is NOT yet merged into `main` (he was given the GitHub steps to do it himself).

**Web PRs, all MERGED into `beta` on his word (22:00):** #285 (HANDOVER: the possible per-track jump command, the lost-header bug entry), #287 (build 662: the header lost on the phone PWA after a notification; a SAFETY NET, `resetStrayScroll` / `remeasureAfterReturn`, cause not reproduced; waits for his test on the Fold), #288 (HANDOVER: "PICK UP HERE (21 Nov, 22:05)", the web app's brief; if still open, it only needs CI and his word). `beta` = build 662; `main` is behind (he has the steps to merge it himself).

**USAGE: the weekly cap was nearly reached at 22:00; it resets Monday 23 Nov, 18:59 Mountain. A check-in is scheduled for 19:05 that day to resume FX1. The FX1 builder was told at 21:57 to push and stop.**

**STOPPED for the usage cap, resume first: FX1** (PR #286, WIP; builder was in `/home/claude/wt/fx1`, branch `n1/fx1-song-fx`, last push `1f35ca3` at 21:24: "notes with measured figures, ELECTRO all-slot reference summary"). State and how to resume: `docs/port/FX1-NOTES.md` ON THAT BRANCH. It was branched before #281 merged: expect small conflicts in `src/render/render.cpp` and the two CMake lists. When it reports: the lead reads every audio-path function it lists, an independent Sonnet verifier checks the PR, squash-merge when ALL CI is green.

**FX1's final report (22:05; PR #286, head `5701ba9`, WIP, NOT mergeable yet): the -80 dB done-test is NOT met.** Played now: pump effects and MODFX as track effects, pump mastering on the master, MODFX FXV, master pump latency in PDC; refused and counted: sidechain, NOTES FROM, pump FXV. Tests in `native/tests/test_fxsong.cpp` (reader bit-exact against the page; MODFX and pump slots through `FxHost` equal the direct module, 82 and 50 cases; ELECTRO 25 of 25 slots hosted). Measured against Chrome with all slots (reference made with `song-ref.mjs --fx-all`; `thunder-render` needs `--pdc`): **ELECTRO -16.9 dB**, of which the MASTER Flex / Retrig alone is -17.5 dB and PREDATES FX1 (earlier `--fx` runs dropped those slots on both sides); without them -69.4 dB (tfx-only baseline -73 dB). **BETA-TESTING-ELECTRO -5.9 dB**: Flex plus pump glitches unexplained; the master pump chain alone -69 dB; bbd -51 dB. Most single effect types are -80 to -91 dB. Local ctest 225 of 226: `render_api_oom` fails on ELECTRO (likely a bad_alloc inside the pinned pump plugin's constructors leaking; an exception-safety fix in `PumpSlot::make` / `make_pump` is pushed but NOT re-run). Not rebased on #281. Unfinished: the oom test, the Flex / PDC / bbd / autopan mismatches, Convolve IRs, the BETA golden (that song lives only on `beta`). **To resume: one Sonnet builder from `docs/port/FX1-NOTES.md` on the branch; FIRST the master Flex / Retrig mismatch (it is the whole of ELECTRO's gap and is older than FX1: the lead should read the Flex core's master path himself), then BETA's Flex + pump glitches.** None of its audio-path code has been read by the lead yet.

**Next, in order:** as the 18:18 list below from item 2 (U37 P3 with R1 and R2, P4, P5; C2; the Android shell on the live host).

**New kept-open items:** (a) the …chains ENGINE (U31, `cengine.cpp`) leaves 1 to 3 allocations behind per prepare it refuses after its own bad_alloc (GCC only seen; `docs/port/C1-NOTES.md`, "Finding for U31"): `chains_host`'s sweep checks the HOST strictly and allows only that balance. (b) Web item 3 of the 18:18 section (display and audio out of alignment after an app switch on the phone) is NOT started; the lost-header bug is its sibling (both: state not re-measured on return to the app). (c) The owner asked what a per-track loop would cost (a setting, or a track jump command): answered "medium, three sequencers"; recorded as a possible future feature in #285, nothing to build.

## START HERE of 21 Nov, 18:18 (now history)
**Say to a new chat:** "Pull thunder-plus, branch native, and continue from docs/port/PICKUP.md. Start at START HERE (21 Nov, 18:18)."

**The owner's rules, as of today.** Unchanged: the lead manages, rules, reads every audio-path line and merges; Sonnet builders in `/home/claude/wt/<name>`, an independent Sonnet verifier per code PR (report on `review/verify-NNN`), squash-merge when ALL CI is green. NEW today: (1) he wants ONLY needs and, every two hours, one short "moving or stalled" line (schedule it); a percentage only when he asks. (2) His weekly usage was above 80 % at 15:40: short turns, no more than two builders, builders push every 10 minutes with exact notes, no new unit started casually. (3) The session's server restarted three times today (13:57, 15:40, 18:03) and stops every agent: only what is pushed survives; a new chat has none of today's agents and re-briefs from the branch notes.

**The owner's decisions today (his words: "Android app. Old engine unless it's a risk? All of them use chains").** The new engine is tried FIRST IN THE ANDROID APP; the web switch (wasm, worklet, page glue) waits. A song the new engine cannot play is played by the old engine with a short message. ALL his songs use chains, so the chains host and the missing track effects are on the critical path with the live host. He will send three song files (run `node native/tools/census.mjs` on them) and has the U36 device test on his punch list (`native/tests/OWNER-TEST-U36.md`; APK: artifact `thunder-plus-native-n0-android-apk` of Actions run 44935839127, kept until 21 Dec). The lead's estimate of the whole port, told to him at 12:42: about 60 %.

**MERGED today:** #272 (live route step 1), #275 (U36 T9 Android MIDI out), #276 (U37 P1 `PadsHost`: pads-and-chop snapshot, exact free rule on a per-voice epoch board), #277 (punch list, `beta`), #278 (MIDI no-recall: `sink.clear` returns the time up to which nothing was recalled; the core releases handed-over note-ons), #279 + #282 (chains C1g: `native/tools/chains-song-dump.mjs`, 23 golden cases; Chrome MEASURED: `chc` acts at quantum ceil(ceil(t * sr) / 128), tempo at ceil(t / q)), #280 (U37 P0: `include/thunder/live.h` + `src/live/live_host.h`, the frozen contract).

### FIRST THING TO DO: two PRs are waiting only for CI
- **#283 U37 P2, `LiveHost` core** (`n1/u37-p2-livehost`, head `b882079`). Lead read the audio path twice; verifier: **MERGE** on b882079 (`review/verify-283`, "Re-check of b882079"). CI was running at 18:18 (sanitizers take 45 to 70 minutes). **When every job is green: squash-merge.** If Windows fails again in `live_threads` read the log (the last fix made `thread_prepares` count prepares actually done; a build without the Plaits / Elements library has none). What it is: the real-time host; the ENGINE counts its own gapless frames and the host's timeline is mapped by one offset that a re-anchor changes (a stream restart neither silences nor bursts); bit-equal to `SongRenderer` for any `frame0`; all 30 sequencer goldens exact through the step dump. TWO follow-ups from the verifier, to give to the P3 builder: (R1) `songPublish` chooses the PLAYING path from `playOut_ > 0 || state_ == PLAYING`, and a `collect()` between the audio side's `giveBack` and its `state_` store makes a publish take the IDLE path (the running song misses that edit): keep a control-side "a play is out" flag cleared by the STOPPED / ENDED record, or store `state_` before `giveBack`; (R2) `test_once` / `test_sim` in `thunder_live_play_opts` must be E_ARG unless the host was created with `F_DUMP_STEPS`.
- **#281 chains C1, `ChainsHost` offline** (`n1/chains-c1`, head `343012d` at 18:18). 23 of 23 golden cases exact through `Mixer` + `ChainsHost` and through `SongRenderer`; `thunder-render --chains` on ELECTRO clean and block-size independent. Verifier: MERGE AFTER G1 (`review/verify-281`), and G1 is in (425006f). After that only CI trouble: the 4096-entry event array was moved from inline to the heap (53d48ce) because `chains_host` overflowed MSVC's 1 MB stack; that broke the bad_alloc sweep on Linux GCC ("every allocation given back", `test_chains_host.cpp:340`); the builder pushed `343012d` for it but had NOT reported when this was written. **Read `git diff 425006f..<head> -- native/core/src/chains` (small: the event array's allocation, a host whose allocation failed must refuse plans and events cleanly), then squash-merge when every job is green.** If the sweep still fails, a fresh builder fixes it from `docs/port/C1-NOTES.md`.

- **Late report from the C1 builder (21:25, relayed by the previous chat, which no longer acts on the PRs):** #281's head is now `1b02e5c` (it includes a merge of `native`). The Linux GCC failure was NOT the event array: the bad_alloc sweep's end balance was +21 live allocations on CI's GCC 15 / glibc 2.42 only, from plans the ENGINE itself refused after its own bad_alloc inside `ChainsEngine::preparePatch` (each leaves 1 to 3 allocations): **a leak in U31's engine code (`cengine.cpp`; look for raw `new` on the `waves::readPatchDoc` / `PatchImpl::sync` paths that a mid-way bad_alloc skips), not in `ChainsHost`.** The builder made the sweep strict for the host's own `plan()` failures and ALLOWED the end balance to exceed zero by what refused-but-kept plans leave: before merging, read that loosened check in `test_chains_host.cpp` and decide whether to accept it with the engine leak filed as a follow-up unit (the previous lead's view: accept, it is an out-of-memory-only leak in already-merged code, but file it), or fix the engine leak first. Also in that head: the event array is `new (std::nothrow)`, `valid()` reports a failed allocation, an invalid host refuses plans and drops events (counted), `setChains` returns `kChainsNoMemory`. At 21:25 every job but the sanitizers was green on that run.

### Next, in order (nothing below is started)
1. **FX1** (critical path): pump and MODFX (shared-module) track effects and pump mastering in the SONG path. Today `FxHost::entryFromSlot` keeps tfx kinds only: ELECTRO plays 12 of its 25 effect slots natively. Brief from `docs/port/CHAINS-HOST-DESIGN-DRAFT.md` (finding 2, section 3, section 6 row FX1, section 9). Done-test: ELECTRO and BETA-TESTING-ELECTRO `--fx` renders within -80 dB of Chrome with ALL slots.
2. **U37 P3** (effects, master chain, PDC, meters in `LiveHost`) with R1 and R2 above; **P4** (MIDI in the host: `docs/port/U37-P0-NOTES.md` section 5, ruling 1: the audio side calls the scheduler directly right after `midi_begin`; done-test: byte-equal to the posted path for the 18 ledger songs, else the posted fallback; raise `thunder_midi_set_lead`'s cap to 1.2 s); **P5** (the C API `thunder_live_*` over `LiveHost`; `thunder_live_alloc` blocks must be `new float[]`).
3. **C2**: the chains host LIVE (plan / commit command, a returned-plan ring before `settle()`, `beginBlock(engine frame)` from `LiveHost::quantum()` at the marked hook, `thunder_live_chain_edit`); then C3 (sidechain taps and sends), C4.
4. **The Android shell on the live host** (N2): the app plays a song through `thunder_live_*`; then the owner's device test of real playback.
5. Kept open, small: the MIDI note-only table (U36-NOTES open points, verify-278 F1: a CC burst can evict a post-Stop note-on at negative latency + Gear); U36 F4 / F5 after the owner's device test; a swappable live-engine pool (`pad_live` while playing); the stale line in "Where the port stands" above (U31, the …chains ENGINE, is MERGED: #227).
6. Waiting: the web host (U37 P6 to P10), U32 SoundFont player, N4 desktop shells.

### Three WEB-APP requests from the owner (21 Nov, 18:18 to 18:25): on `beta`, by PR, each punch-listed; not port work
1. **Every generator follows the song's key by default.** What exists (beta `index.html` at cdd9439): the song's key is SCALE / SCALE ROOT in Project (`S.scale`, `S.sroot`, `:21719`, `:27924`); `songKey()` (`:30646`) returns it and, when SCALE is off, works the key out from the song's notes; `genScope` hands every generator `G.key` / `G.root`; the Acid line generator already uses it (`:30129`, `:30249`); Arp uses only the root for its default note (`arpGen` `:30330`: fixed interval shapes); Markov (`:30421`) learns from the song's notes; Chord (`:30367`), Poly (`:30396`), Ratchet (`:30283`) were NOT read. THE OWNER'S TWO ANSWERS (he said "I like both decisions" to the lead's two questions; the lead reads that as yes to each proposal): (a) when SCALE is off, generators use the DETECTED key; (b) each generator gets a "follow song key" toggle, ON by default, so he can still go out of key on purpose. Confirm that reading with him in one line before building if in doubt.
2. **Transposing a selection with the + / - buttons in the command bar transposes EVERYTHING in the selection.** His words; the current behaviour was not looked at: find the + / - handler for a selection first, say in one line what it leaves out today (other note columns? every row but the cursor's? instrument or command columns that hold notes?), then make it cover the whole selection, with a test.
3. **After switching to another app and back on the phone, the sequencer display and the audio are out of alignment, and STOP then PLAY does not fix it** (the owner, 18:24 and 18:25: "I switch to Claude and back, the sync is off. Stopping and start doesn't seem to fix it"; Galaxy Z Fold 6, Chrome). Asked whether a resync exists: PHONE RESYNC (`index.html:24831-24844`, 10 Nov) holds the SONG at real-time speed when Chrome's audio clock races; it does not re-align the display. PLAYHEAD OFFSET (Settings → Screen, `:26752`, `:27890`, -100 to +300 ms) is a fixed manual offset. NOT TRACED: why the display stays wrong. Because a new PLAY does not fix it, the stale state outlives the sequencer: suspect first the audio-to-wall clock map the display reads (`mapUpd` / `amap` / `a2p`, about `:17909-17925`: smoothed 2 % a call, a jump over 50 ms taken at once) and the measured output latency (the 7 Nov change, build 470) after the page was hidden and the output re-opened; `visibilitychange` has 21 uses to read. To do: reproduce (or reason from the playback report he can paste), find which value is stale, re-measure / reset it when the page becomes visible again and at PLAY, and add a manual "resync display" action only if the automatic reset cannot be made reliable. The Android app removes the cause (no Chrome audio clock).
One Sonnet builder can take 1 and 2; item 3 needs an investigation first (a builder reads and reports, the lead rules). The lead reviews every diff (the app's own file).

### Where the designs and rulings are
`docs/port/U37-DESIGN-DRAFT.md` (section 12: rulings; the owner's answers at the end), `docs/port/U37-P0-NOTES.md` (the contract's rulings), `docs/port/U37-P1-NOTES.md`, `docs/port/U37-P2-NOTES.md` (on the P2 branch until #283 merges: departures and unkilled mutations), `docs/port/CHAINS-HOST-DESIGN-DRAFT.md` (section 9: rulings), `docs/port/C1G-NOTES.md` (the golden tool; "Chrome k-rate check"), `docs/port/C1-NOTES.md` (on the C1 branch until #281 merges), `docs/port/U36-NOTES.md`, and the verifier reports on branches `review/verify-275`, `-276`, `-278`, `-281`, `-283`.

### Things that bit today
No file under `native/core` outside the allow list may contain "lxr" in any case, comments included (CI test `lxr_boundary`). MSVC: no `<unistd.h>`, a 1 MB default stack (no large arrays inline in objects tests put on the stack). A UBSan vptr false positive when RTTI test code releases a `shared_ptr` made in the `-fno-rtti` core: `-fno-sanitize=vptr` on that TEST target only. A branch made from a rewritten shallow history cannot be merged or rebased (40 conflicts): take the files with `git checkout <ref> -- <paths>` or `rebase --onto`. A builder's "tests pass" before its last edit is not a pass: CI on the final head decides. Merging into `beta` from this session may be denied by the session's permission rules until the owner says so in chat.

## START HERE of 21 Nov, 09:15 (now history)
**Say to a new chat:** "Pull thunder-plus, branch native, and continue from docs/port/PICKUP.md. Start at START HERE (21 Nov, 09:15)."

**Who runs what.** The chat that wrote this (lead of 20-21 Nov) hands EVERYTHING over except what follows. The older chat (lead of 19-20 Nov) may still hold `n1/live-eng` (PR #272: ENGINE TARGETS + the idle skip): if #272 is still open and its branch has not moved for a day, ask the owner whether that chat is dead before taking it over. No worker survives a chat: re-brief from the branch and its notes.

**21 Nov, 09:57: THE OLD CHAT (lead of 19-20 Nov) HAS STOPPED. PR #272 (`n1/live-eng`: ENGINE TARGETS + the idle skip) is NOT merged and is now yours.** It needs ONE thing: the verifier's verdict is **MERGE** on the code commit `12015e10` (three rounds; `docs/port/reviews/verify-272.md` on `review/verify-272`, section "Re-check of 12015e10"); the head is `c65277e3` (two lines of notes on top, nothing else) and its CI was still running at 09:55 (clang, GCC, sanitizers, macOS, Windows pending; wasm, Android NDK, Android shell, `test` green). **When every CI job on `c65277e3` is green, merge it** (`gh api -X PUT repos/matthettich/thunder-plus/pulls/272/merge -f merge_method=merge`; re-run "Android shell v0" on an HTTP 429; if it conflicts with `native`, merge `origin/native` into the branch and wait for CI again). No further review is needed unless code changes. That completes step 1 of the live-engine route; `n1/live-route` and `n1/live-eng` are then finished, and what is left of the live route is steps 2 (pump synths, the other live kinds) and 3 (…waves) of `docs/port/LIVE-ROUTE-DESIGN.md`. The old chat's agents are gone with it (start fresh ones). What #272 fixed after review: the idle skip dropped the last ~26 samples of a voice's tail (now waits for `lastLive + latency`; skip on = skip off bit for bit on 600 scripts + 1300 resume cases); the page's `rate` default is 1, not 0; the shell read the rate and engine lanes at `s / sr`, one ulp before the sequencer's `sample_time(s)` at samples like 500 and 1000, so a note's first control block read the lane before its own event (this also means some bass / mach live-pad songs were WRONG at the merge-base and differ from it now: a correction). Follow-ups, none blocking: (1) after the last change the full Release ctest (199), the sanitizers and the full 52-mutant run were not repeated locally (CI runs the suite and the sanitizers; mutants: `node tools/live-mutants.mjs`, 22 of 52 run, all killed); (2) the live-pad song differences vs the merge-base (9 of 40 songs) are explained by the `sample_time` cause but there is no song-level oracle proving every difference comes from it; (3) Chrome's own rule for an AudioParam event landing exactly on a frame was not checked (the code follows the sequencer's `sample_time`, as the goldens do); (4) `native/tools/hold-vs-chrome.mjs` (proves the dump tool's hand "hold" patch equal to Chrome) runs by hand: put a recorded Chrome fixture in CI (proposal in verify-272.md, F4); (5) the verifier's surviving mutants V06-V08 (`rateT0` writes redundant with each other: defence in depth, no test needed).

**21 Nov, 11:20 (lead of 21 Nov, a new chat that took over at 10:00): #272 is MERGED** (10:49, merge commit `db765471`, all CI green on `c65277e3`): live route step 1 is complete. **#275** had a red sanitizer job: a UBSan vptr FALSE POSITIVE (the test's RTTI code releasing a `shared_ptr` control block made in the `-fno-rtti` core; fixed with `-fno-sanitize=vptr` on the test target, as `test_seq_takeover` has). The verifier's report (`docs/port/reviews/verify-275.md` on `review/verify-275`: first verdict MERGE AFTER FIXES, 11 findings; "Re-check of b7b464f": R1 to R3) led to two more rounds by the builder: held messages are flushed at engine stop, `sink.clear` drops held messages of a port without timestamps, the partial write continues from byte n, the fallback map adds the output latency, the status text is valid JNI UTF-8, a held message goes before a later immediate one, partial attach is retried. RULED: no `AMidiInputPort_sendFlush` on a stamped port (a flush can discard a note-off the core will not replay). Round 3 (the R1 compile error in `midi_jni.cpp`, the R2 ruling) was with the builder at 11:20: when it is pushed, the verifier re-checks the delta, then squash-merge on MERGE + green CI, then the owner's device test. **U37: a design draft exists with the lead's rulings: `docs/port/U37-DESIGN-DRAFT.md`, section 12.** Its two findings change the plan: there is NO live host in C++ yet (`LiveHost` + `include/thunder/live.h` are new, and every shell needs them, not only the web switch), and no chains host is wired into any song path. P1 (pads snapshot + deferred free, branch `n1/u37-p1-pads`) was started; P0 (`live.h`) is the lead's. Three questions are with the owner (O1, O2, O4 of that file).

**21 Nov, 12:45: #275 is MERGED** (squash `2d81c7d2`, all CI green on `9b7b14e`; verifier verdict MERGE on 6f011fe, the last commit was notes only). **U36 T9 is ready for the owner's device test**: `native/tests/OWNER-TEST-U36.md`, the APK is the artifact `thunder-plus-native-n0-android-apk` of https://github.com/matthettich/thunder-plus/actions/runs/44935839127 (kept until 21 Dec). KNOWN BUG, found by tracing after the verifier's note: on a no-recall sink (Android) a note-on already handed to the OS (up to 20 ms ahead) when Stop comes plays and gets NO note-off (`MidiSched::releaseAll`, midisched.cpp:564-590, undoes it assuming the CLEAR recalled it); needs output latency + Gear delay under 20 ms (the Fold measured 26 ms, so it is rare there); test step 3b of the owner's sheet looks for it. THE FIX is a core change on branch `n1/midiout-norecall` (a Sonnet builder): `sink.clear` reports per device whether it recalled; not recalled = the core replays nothing and queues a note-off for every handed-over note-on after the marker; the shell's replay-swallow list (`sent_`, round 3) goes if the contract makes it unnecessary. The lead reads every core line of it. Open points kept in `docs/port/U36-NOTES.md`: F4 (4 refused writes = cable gone until the next `set_devices`), F5 (the fallback map before the first latency probe), SysEx over 3 bytes. **U37 P1 is PR #276** (`n1/u37-p1-pads`: `PadsHost`, per-slot epoch board, `Seq::setChop`; the lead read the audio-thread code and the free rule: OK; a verifier's report goes to `review/verify-276`; CI was running). **U37 P0** (`include/thunder/live.h` + the `LiveHost` declaration) is being DRAFTED by a Sonnet agent on `n1/u37-p0-live-h` for the lead to edit and freeze.

**21 Nov, 12:45: THE OWNER ANSWERED (his words: "Android app. Old engine unless it's a risk? All of them use chains").** (1) The new engine is tried FIRST IN THE ANDROID APP (N2), not the web switch: after the live host (U37 P0 to P5) the next wiring is the Android shell; the web host (P6 to P10: wasm, worklet, page glue, browser A/B) WAITS. (2) A song the new engine cannot play yet is played by the old engine with a short message saying why (the lead judged it no risk: the switch only takes effect at a PLAY, nothing restarts by itself after a failure). (3) ALL HIS SONGS USE CHAINS: so the chains host (with U31, the …chains engine) is on the CRITICAL PATH, level with the live host: without it none of his songs plays natively. A design draft was started (`docs/port/CHAINS-HOST-DESIGN-DRAFT.md` on branch `n1/chains-design`, a Sonnet agent; the lead rules on it). This replaces ruling D1's "(a) then (c)" order in `docs/port/U37-DESIGN-DRAFT.md` section 12: chains are built NOW, in parallel with P2. The lead's estimate of the whole port, told to the owner at 12:42: about 60 % (the morning's 68 % did not count the missing live host and chains host).

**21 Nov, 12:55: the CHAINS HOST design is ruled** (`docs/port/CHAINS-HOST-DESIGN-DRAFT.md`, section 9). What it found: the …chains ENGINE is already native (U31 is MERGED, #227: the "not started" line further up this file is stale); only the HOST is missing (`ChainsHost`: one `ChainsNode` per CHN track on `Mixer::setChain`, plan / commit like `FxHost`). AND the songs' pump and MODFX track effects and pump mastering do NOT reach a native song render (`FxHost::entryFromSlot` keeps tfx kinds only): ELECTRO has 12 of 25 effect slots hosted. So two units are on the critical path beside the live host: **C1** (ChainsHost offline, `thunder-render --chains`, with **C1g** the Node golden tool) and **FX1** (pump + MODFX slots and pump mastering in the song path). Then C2 (chains live, after U37 P2), C3 (sidechain taps and sends), C4. Also at 12:55: PR #276 (U37 P1) is in round 2 (the verifier found no unsafe interleaving; the lead changed the free rule to an exact per-voice board; test gaps F1 to F4, F6 being closed); PR #278 (the MIDI no-recall fix: `sink.clear` returns the time up to which nothing was recalled; the core releases handed-over note-ons) was read by the lead and sent back for the already-due case; U37 P0 (`live.h`, `live_host.h`) is frozen with the lead's rulings on `n1/u37-p0-live-h` (PR after #276); PR #277 adds the U36 device test to the punch list on `beta`.

**21 Nov, 15:22: state of the day.** MERGED today: #272 (live route step 1), #275 (U36 T9 Android MIDI out), #276 (U37 P1: `PadsHost`, an EXACT free rule on a per-voice epoch board; verifier MERGE on 0480a1e), #277 (punch list on `beta`: the U36 device test), #279 (chains C1g: the song-level golden tool, 22 cases; Chrome was MEASURED: `chc` acts in quantum ceil(ceil(t * sr) / 128), tempo in ceil(t / q)). OPEN: **#278** MIDI no-recall (round 3 with its builder after `review/verify-278`: contract text, the release recorded in the ring, tests; one follow-up kept in U36-NOTES: a note-only table so a CC burst cannot evict a note-on at negative latency + Gear); **#280** U37 P0 (`live.h`, `live_host.h`: the frozen contract; CI re-running after a comment tripped `lxr_boundary`: no file under `native/core` outside the allow list may contain "lxr"); **#281** chains C1 (`ChainsHost` offline: 22 of 22 golden cases exact; round 2 with its builder after `review/verify-281`: plan ownership, a 4096-event queue, the total memory budget, the page's keep rule `audible & (noteTracks | busClosure)`, events by FRAME with `beginBlock(frame)`, statuses and counters through the render API); **#282** one more golden case (`l/chain-out-no-notes`, 23). IN WORK: **U37 P2** `LiveHost` core, a Sonnet builder on `n1/u37-p2-livehost` (large; scope and tests in its brief are repeated in `docs/port/U37-P2-NOTES.md` on that branch). NOT STARTED, next compiling slot: **FX1** (pump + MODFX track effects and pump mastering in the song path: `docs/port/CHAINS-HOST-DESIGN-DRAFT.md` section 6). After P2: P3 (effects and master live), P4 (MIDI in the host), P5 (the C API), C2 (chains live), then the Android shell wiring (the owner chose Android first). The server restarted at 13:57 and stopped three builders: all were resumed from their transcripts (agents can be resumed by id after a restart; a new chat has none of them and re-briefs from the branch notes). The owner now wants only needs and a two-hourly "moving or stalled" line; he will send three song files (run `native/tools/census.mjs` on them) and has the U36 device test on his punch list.

**21 Nov, 17:22: merged since the 15:22 note:** #278 (MIDI no-recall: `sink.clear` returns the time up to which nothing was recalled; the core releases handed-over note-ons; verifier MERGE after three rounds; follow-up F1 in U36-NOTES), #280 (U37 P0: `live.h` + `live_host.h`, the contract), #282 (golden case 23). **OPEN: #281** chains C1 (verifier: merge after G1, which is in; ALL Linux / macOS CI green; BLOCKED only by `chains_host` SEGFAULT on Windows MSVC: the builder is finding the cause; do not merge until that job is green or the cause is proven test-only). **#283** U37 P2 `LiveHost` core (lead read the audio path; `review/verify-283`: MERGE AFTER FIXES; round 2 with the builder: the lead ruled that the ENGINE keeps its own gapless frame count and the host's timeline is mapped by one offset that a re-anchor changes, which fixes the backward re-anchor silence (an Android stream restart), the forward burst and the frame0 rounding in one design; also F9 play / adopt order, the ENDED timer from the song's end, `poll` order, non-fatal errors off the critical ring, per-key `mix_set` ranges, Stop / Play clear the waiting timed commands). NOT STARTED (held back while the owner's weekly usage is above 80 %): FX1, P3, P4, P5, C2. A second server restart at 15:40 stopped the builders; uncommitted work was committed as WIP by the lead and the builders resumed; builders now push every 10 minutes.

**How to run it** (the owner's rules, unchanged; see "How the owner wants this run" at the top and the 05:15 section below): Sonnet builders in `/home/claude/wt/<name>`, an independent Sonnet verifier per code PR (report on `review/verify-NNN`), the lead reads every core diff and rules, CodeRabbit reviews every PR into `native` automatically (one round by the builder, the lead decides the rest), merge by squash when ALL CI is green (sanitizers: 45 to 70 minutes). At most two compiling workers. Never `git fetch --unshallow` in the shared clone. The owner wants few messages: one update when asked or when something needs him, with a percentage.

### State of `native` at 09:15: 35 of 37 units merged; U36's core is complete; about 68 % of the port
Merged since 20 Nov 21:00: #265 live route step 1 (the older chat), #266 bus into bus, #267 follow-ups, #268 U36 PR A (port table, pump, clock map), #269 CodeRabbit config, #271 the web's 19-20 Nov changes (lines, envelope lines, reset track incl. live voices, `sx` carried, goldens from the pinned web copy in `native/tests/web-ref`), #273 bus-into-bus tests, **#274 U36 PR B** (`SongRenderer::setMidi` + `setSeqStart`, `Players::setDropLate`, the pump HOLDS records until the first clock map and drops note-ons / CCs more than 0.15 s old at that first map, the gone-TOKEN fix). What each did and every ruling: the 05:15 section below, `docs/port/U36-SPEC.md` section 8, `docs/port/U36-NOTES.md`, the reviews in `docs/port/reviews/verify-26x/27x.md`.

### Open
- **#275 `n2/u36-android-midi`: U36 T9, the Android MIDI OUT sink.** Built by a Sonnet worker, NOT yet read by the lead and NOT verified. Kotlin `MidiModel.kt` (no Android imports, JUnit-tested in CI), `MidiPorts.kt`, `NativeMidi.kt`; C++ `midi_out.{h,cpp}` (token table, sender thread, clock map; AMidi behind `PortIo`; Linux ctest `android_midi_sink`), `midi_jni.cpp`; a debug "MIDI out test" button; `native/tests/OWNER-TEST-U36.md`. Android CI jobs green on 44ea1c36; the slow jobs and CodeRabbit's second review were still running. TO DO: the lead reads `midi_out.cpp` (sender thread, shutdown order, the clock-map pair from AAudio) and anything called from the AAudio callback; a verifier; merge; then the owner's device test (Fold + a USB MIDI device). The builder's three questions, ruled: (1) `CAP_TS` for the whole sink with only USB ports stamped, other ports held by the shell and sent plain: ACCEPTED; (2) a write AMidi refuses 4 times 0.2 ms apart is a send error and the cable is gone until the next `set_devices`: accepted for now, re-check after the device test (a briefly full buffer should not mute a device for 3 s); (3) the one-last-block-then-Stop at engine stop is debug-only: U37 owns the real one.
- #272 (the older chat's), #225 `n1/repin-pump` (old, untouched).

### Next, in order
1. #275 as above.
2. **U37's design: the lead's own work, the next big item.** Inputs: `docs/port/reviews/prewire-19nov.md` (one design for F2 + F3: an immutable pads-and-chop snapshot with deferred free; pump never on two threads; the live route's idle skip), `docs/port/CORE-BRIDGE.md`, `docs/port/WIRING-DOSSIER.md`, and the duties found on 20-21 Nov: `setMidi` + `setSeqStart(0.08)` with `thunder_midi_start`; post `thunder_midi_stop` when STOP is asked or 0.3 s after `songOver()`, then one more quantum before destroying the renderer; `clock_map` once per audio callback AFTER the block, with the shell's own (frame, host time) pair; `setDropLate(true)`; `Players::stopTrack` for reset track; republish the Seq snapshot on every chain-patch edit (the web's `rebus`); `eng_env_lines_fit` with the song's bpm and lpb before a render and again on a settled tempo change (verify-271 F4); verify-274 F4's list. Then the full wasm target.
3. U36 T10 (CoreMIDI / WinMM sinks) belongs to N4 with the desktop shells. MIDI in, clock in, Measure: phase 2.
4. Not ported: the stretch itself (`sx` / OLDSCHOOL play like LOOP natively; needs in `docs/port/WEB-FOLLOW-20NOV-NOTES.md`); live route steps 2 and 3; U04's LXR seam.
5. Small: move the root web snapshot and the older dump tools (mix-dump, song-golden, u15-song-dump) to `native/tests/web-ref`; the golden-harness hardening and the `[5]`-in-an-`...L`-field point CodeRabbit raised on #271; `midi_glue`'s equivalent mutant (verify-274 F3).

### Decisions made for the owner on 20-21 Nov (told; he can reverse any)
With the live host's `setDropLate` on, notes more than 0.15 s late after a stall or seek are SKIPPED on every route (the web's core plays them in a burst): pads and gear agree. The pump holds MIDI until the first clock map, then drops stale note-ons / CCs. All ten U36 spec questions as recommended. Devices are identified by a shell-supplied token. Clock-map smoothing is per 25 ms of host time. Reset track stops live voices. Native keeps the web's stems double-count (incl. the nested-send form) until the web fixes it.

### Waiting on the owner
The device test of #275 once merged (`native/tests/OWNER-TEST-U36.md`). From before: loops for the CLEAN stretch listening test; Rubber Band yes / no; whether to convert the five skipped web tests; a side-by-side beta app; anything he finds in beta.2. For the web's queue: the stems double-count also shows for a nested send.

## START HERE of 21 Nov, 05:15 (now history)
**Say to a new chat:** "Pull thunder-plus, branch native, and continue from docs/port/PICKUP.md. Start at START HERE (21 Nov, 05:15)."

**Two chats still run.** The OLD chat (lead of 19-20 Nov) merged #265 (live route step 1) at 00:52 and keeps ONLY `n1/live-eng` (PR #272: ENGINE TARGETS + the idle skip). Do not touch `n1/live-eng` / #272. The NEW chat (lead of 20-21 Nov, this section's author) runs everything else.

**How the night of 20-21 Nov was run (keep doing this):** Sonnet builders in `/home/claude/wt/<name>`, an INDEPENDENT Sonnet verifier per code PR (report on `review/verify-NNN`, its tests brought into the PR), the lead reads every core diff and rules, merge by squash when ALL CI is green (the sanitizers job takes 45 to 70 minutes). **CodeRabbit now reviews PRs into `native` and `beta` automatically** (`.coderabbit.yaml`, #269 / #270; the owner asked for it as a standing reviewer): each builder waits up to 15 minutes, does ONE round (fix what is right, reply on each thread), the lead decides the rest. The server restarted about every 3 hours tonight; stopped workers were resumed automatically. At most two compiling workers; never `git fetch --unshallow` in the shared clone.

**07:30 addendum (21 Nov):** #273 and #271 are MERGED (all CI green). The `midiport_threads` flake is explained: the test called `clock_map` after the first `process`, so the first notes were dropped as "no map yet" (counted in `dropped_nomap`, which the failure line did not print); the lead ruled that the pump HOLDS records until the first map (and, at the first map, drops note-ons / CCs more than 0.15 s old; note-offs, Start / Stop / SPP, CLEAR always go). **U36 PR B is PR #274** (`n1/u36b`): verifier MERGE AFTER FIXES (`docs/port/reviews/verify-274.md` on `review/verify-274`); the builder is applying the lead's rulings: a Seq start offset on `SongRenderer` so the renderer equals `runBlocks` with the web's 0.08 s lead for all 18 ledger songs; the late-note comments corrected (the web's core PLAYS late sampler / SoundFont / live notes at once, native with `setDropLate` skips them on purpose: the owner was told and can reverse it); `clock_map` documented as called AFTER `midi_end`; tests for the SoundFont and live previous-voice cases. When #274 is green: lead re-reads the offset code, merge, then U36 T9 (the Android sink).

### Merged into `native` on 20-21 Nov by this chat
- **#266 bus into bus** (the web's #254): `bus_plan` with `ownRet`, edges in (source, bus) order, `loopTo`, `srcAll` / `busAll`; transitive solo; stems through any depth; `Seq::buildBus` now calls `bus_plan` (it had a second copy of the old rule). Verifier: MERGE.
- **#267 follow-ups:** an unknown-length render no longer ends before the song's end (it waits for the audio clock to reach the sequencer's end); the MIDI clock after a live tempo change before a TPO row (`SeqStep::bpm0` = the tempo the step was timed with; `bpmPrev_` is gone); the stem list repeats until stable; test gaps of #246 / #264; `test_players`.
- **#268 U36 PR A:** `midi_spp_for`; `midi_begin` / `midi_sched` / `midi_end` (`src/midi/midi_host.h`; `thunder_midi_process` rebuilt on them, unchanged for C hosts); `include/thunder/midiport.h` + `src/midi/midiport.cpp` (device list with stable TOKENS, four port letters, sticky cable slots, time-ordered queue with per-cable CLEAR and the F4 drop order, pump with `CAP_TS` / spin, clock map with the web's `mapUpd` smoothing made time-based). Spec and the lead's rulings: `docs/port/U36-SPEC.md` (section 8); builder's notes: `docs/port/U36-NOTES.md`; verifier: `docs/port/reviews/verify-268.md`.
- #269 `.coderabbit.yaml` (and #270 on `beta`).

### Open at 05:15
- **#273 `n1/bib-tests`** (tests only: the #266 verifier's tests + CodeRabbit's four test points + a real-patch stem list + a pin of the nested-send stems double-count, which is the WEB's behaviour at build 660). Everything green except one sanitizer run that failed on `midiport_threads` (not this PR's code: see the flake below); the job was re-run. Merge when green.
- **#271 `n1/web-follow-20nov`** (SYNC TO LINES `instBeats`, OLDSCHOOL fields carried on `ev.st`, `env_lines.cpp` = the web's `envLinesFit` (no caller yet), reset track `stopTrack` on the sampler, SoundFont AND live pools, `takeOver` no longer copies `busT_` = the web's `api.rebus()`, goldens re-recorded from a pinned web copy in `native/tests/web-ref`). Verifier: MERGE AFTER FIXES; F1 (live voices not stopped), F2, F3, F6 are applied (head 10eeaf6f, full ctest 208 green locally). Merge when CI is green. Three minor CodeRabbit points left for a follow-up (an array `[5]` in an `...L` field reads 0, the web gives 5; two notes points).
- **U36 PR B, `n1/u36b`** (a Sonnet builder is on it): `SongRenderer::setMidi` (the three calls in `quantum()`), `Players::setDropLate`, tests 9-11 of the spec, the rest of T7, the gone-TOKEN fix in `midiport.cpp` (CodeRabbit's last point on #268). The lead reads `quantum()` and `Players::dispatch` line by line; then a verifier.
- **A flake to settle (the PR B builder has it): `midiport_threads` under CI's sanitizers lost 4 of 2996 notes in the same-list run with `dropped_gone` 0** (log in the #273 sanitizer job of 21 Nov 11:06 UTC). Either a real uncounted loss in `midiport.cpp` (the round-2 changes of #268: `cabPurge`, the epoch sweep, the map re-read) or the test ending early. Until it is explained, treat the pump as NOT proven.

### Next, in order
1. Merge #273, #271; review + verify + merge U36 PR B; settle the flake.
2. U36 T9: the Android sink (`MidiPorts.kt`, AMidi in `jni.cpp`, the pump thread, the clock map from AAudio timestamps): spec section 6. Compile-only in CI; the device test is the owner's (USB MIDI interface on the Fold).
3. The full wasm target, then **U37's design (the lead's own)**: `docs/port/reviews/prewire-19nov.md` still binds (one design for F2 + F3: an immutable pads-and-chop snapshot with deferred free; pump never on two threads). New duties for U37 found tonight: republish the Seq snapshot on every chain-patch edit (the web's `rebus`), call `eng_env_lines_fit` with the song's bpm and lpb before a render and re-render on a settled tempo change (`docs/port/WEB-FOLLOW-20NOV-NOTES.md`, verify-271 F4), turn on `Players::setDropLate`, call `Players::stopTrack` for reset track, supply `clock_map` its own (frame, host time) pair per audio callback.
4. Still not ported: the stretch itself (`sx` / OLDSCHOOL are parsed and carried only: an `sx` song plays like LOOP natively; what a port needs is in WEB-FOLLOW-20NOV-NOTES.md); live route steps 2 and 3; U04's LXR seam.
5. Small follow-ups: the root web snapshot and the older dump tools (mix-dump, song-golden, u15-song-dump) still read the old web copy (move them to the pinned `web-ref`); the golden-harness hardening CodeRabbit asked for on #271.

### Decisions made for the owner on 20-21 Nov (he can reverse any)
All ten open questions of the U36 spec as recommended (one core pump with optional OS timestamps; web-style device ids; a vanished device keeps its letter and rejoins; WinMM on Windows; `midi.cfg` read as arrays of four, BRIDGE_V stays 1). The pump identifies devices by a stable token the shell supplies, not a list index. The clock map's 2 % smoothing is per 25 ms of host time (the web's rate), not per call. Reset track stops live-engine voices too. Native keeps the web's stems double-count (and its new nested-send form) until the web fixes it. `.coderabbit.yaml` was merged into `native` and `beta` without waiting for CI (config only).

### For the web app (told to the owner, not fixed)
The stems double-count now also shows for a nested send (track to its own Chain Out and to a bus that feeds a bus): CodeRabbit on #266; add to "the stems double-count" job. Everything else as in the 20 Nov section below.

**Port progress: 35 of 37 units merged, U36 about half done (PR A in, PR B building, the sinks not started); about 67 % of the whole port by the lead's estimate.**

## START HERE of 20 Nov, 21:00 (now history)
**Say to a new chat:** "Pull thunder-plus, branch native, and continue from docs/port/PICKUP.md. Start at START HERE (20 Nov, 21:00)."

**TWO CHATS ARE RUNNING. Read this first.** The OLD chat (the lead of 19-20 Nov) stays open ONLY to finish the live-engine route: PR #265 (`n1/live-route`: bass / vox / acid / mach as live engines; verifier's verdict MERGE AFTER FIXES, report `docs/port/reviews/verify-265.md` on `review/verify-265`; its builder is applying F1-F6) and its follow-on `n1/live-eng` (ENGINE TARGETS + the idle skip). **The new chat must NOT touch `n1/live-route`, `n1/live-eng`, `review/verify-265`, or merge / close #265.** When #265 and the eng PR are merged the old chat writes one line here ("live route step 1 merged, date") and stops. If that line is missing and those branches have not moved for a day, ask the owner whether the old chat is dead before taking them over (the design and every ruling are in `docs/port/LIVE-ROUTE-DESIGN.md`, the builder's status at the top of `docs/port/LIVE-ROUTE-NOTES.md`, both on `n1/live-route`).

**21 Nov, 00:55: PR #265 (the live-engine route, step 1: bass / vox / acid / mach) is MERGED into `native`** after the verifier's re-check (MERGE; `docs/port/reviews/verify-265.md` on `review/verify-265`) and green CI on every job. `n1/live-route` is finished. **`n1/live-eng` (ENGINE TARGETS + the idle skip) is STILL with the old chat**: do not touch it until a line here says it is merged or handed over. Follow-ups from #265's verifier: `native/tools/live-route-dump.mjs` emulates the page's "hold" by hand (not the app's own code: the class of flaw that produced the false B11 in U35: replace it with the app's code or prove it equivalent; the old chat's eng PR is to do it); the two `*_oom` tests and `live_u26` were not run under the sanitizers locally (CI's sanitizers job runs the whole suite); a vox pad on 33 tracks costs about 235 KB a track and renders silence at about 3x real time until the idle skip lands (required before U37). The "Android shell v0" CI job can fail on a Maven Central HTTP 429: re-run it, it is not the code.

**Usage limits and timers (the owner's instruction, 20 Nov):** a 5-hour usage limit stops the lead AND every worker; twice on 19-20 Nov a check-in that fired DURING a limit stayed queued until the owner next wrote (7 hours lost each time), and a chained check-in (each schedules the next) breaks at the first one that cannot run. So: when a worker fails with "You've hit your 5-hour limit · resets HH:MM", or whenever work is left running unattended, schedule ONE check-in for a few minutes AFTER the stated reset time (not a chain during the limit), and tell the owner the reset time. Workers stopped by a limit or by a server restart resume with SendMessage to their agent id; ids die with the chat. Server restarts happen about hourly: only what is pushed exists. The machine has 2 cores: thirteen workers at once gave load 25 and everything crawled; keep to about four, at most two compiling. Workers must never `pkill` (two did, killing others' test runs).

### Where things stand
**Native port: 35 of 37 units merged, about 65% of the whole port** (the lead's estimate; units left: U36 and U37; plus the live route's steps 2 and 3, U04's LXR seam, the stretch port, and the shells' work behind them).
- Merged on 20 Nov into `native`: #247 follow-ups (the sanitizers CI job now has 120 minutes, takes about 42), #246 stems (U34b), #260 `Players` (dispatch and the pools behind one class; holds neither Song nor pads by reference), #264 U35 MIDI scheduling (`MidiSched`, `thunder_midi_*`; the web's MIDI bugs B1-B9 and B12 are FIXED natively, each difference from the web ledgers an exact rule in `native/tests/golden/midi-ledger-expect.json`; B11 was a harness artefact, not a web bug).
- Queued native follow-ups (one small PR, Sonnet): #246's verifier F1 (the U34a header says the length estimate is always below the real length: false; an unknown-length render can also end up to 0.3 s early), F2 / F3 (no audio test of a stem under solo; no cross-track choke test in a stem), F4 (the stem list's bus order vs the JS Map order: matters for bus into bus); #264's verifier N1 (a live tempo change just before a TPO row leaves the clock about 15 ms off) and its four test gaps (two TPO rows, `block_sec` forwarding, `block_sec` > 1 rejected, Start's priority in the ring); `Players`' take / capPoly order is guarded only by `render_sf` (the live route's tests are to pin it).
- The lead's pre-wiring read is `docs/port/reviews/prewire-19nov.md`: before U37 (the live host) F2 + F3 need ONE design (an immutable pads-and-chop snapshot with deferred free), pump must not run on two threads at once, and the live route needs its idle skip (a vox pad on 33 tracks renders silence at about 3x real time).
- The native side has NOT yet followed these web changes of 19-20 Nov (each has a spec written for a native worker): bus into bus (`docs/BUS-INTO-BUS-NATIVE.md` on `beta`: `bus_plan`, transitive solo, `setStem`), SYNC TO LINES (`insts[i].lines`: `docs/SAMPLER-NOTES.md`), envelope times in lines (the host converts to ms before the render: `docs/ENV-LINES-NOTES.md`), the stretch modes (`I.sx`, OLDSCHOOL: `docs/STRETCH-BUILD-NOTES.md`; the stretch port itself does not exist), reset track's `stp` message. Re-record the sequencer goldens against the current `beta` when doing these.
- Next units by the workplan: U36 (the shells' MIDI: port table, sending `MidiSched`'s records; clock in is phase 2) and the core glue that calls `MidiSched` each block; then the full wasm target and U37's design (the lead's own).

**Web app: `beta` is at build 660** (head 1ae8d41, CI green). Merged 19-20 Nov, all by PR, each punch-listed in HANDOVER.md: #248 Expression examples, #252 beta icon, #249 drag on the Settings sliders, #253 FOLLOW order, #250 FX preset keys (mod + Left / Right; the value nudge moved to mod + Shift), #256 master double-click, #255 in-use outline, #251 reset track, #254 bus into bus (+ solo follows the chain, CHN labels MS / LP, and the fix: the mix is re-applied when the bus plan changes), #257 sampler pitch + SYNC TO LINES, #258 playback-report diagnostics, #259 envelope times in bars + lines, #262 the FlexCore golden, #261 the stretch mode picker (659), #263 OLDSCHOOL stretch (660). **Desktop betas published: v1.3.14-beta.1 (build 658) and v1.3.14-beta.2 (build 660). NOT merged to `main`: the owner has not asked.** A desktop beta is: `desktop.yml` from `beta` through the API with `inputs[version]=1.3.14-beta.N`, `inputs[prerelease]=true` (it replaces the stable app: same appId; stable installs ignore pre-releases).
- Almost none of this has been run by a person. Never run in a real page at all: the FX preset keys, the envelope rows, a long swell on both sequencers. The owner's tests are "Owner's punch list" in HANDOVER.md ("give me my test checklist" makes the doc).
- The stretch plan (`docs/STRETCH-DESIGN.md` on `beta-stretch-design`; "Time stretch: what is left" in HANDOVER.md): steps 1 and 2 of 10 are merged. Next: step 3 (stretching in a Worker + a byte-capped cache), step 4 (chop and stretch on the audio-thread sequencer: today `applyChop` is a stub in `core/seq-core.js`, so slices and synced samples play unchopped there: the owner's open "slices" bug), then CLEAN (Signalsmith Stretch, MIT: a listening spike on the owner's loops FIRST), SLICED, the character modes, live OLDSCHOOL, the native port.
- Queued small web jobs: convert the five other history-based tests that silently skip in CI (`core-memory`, `block-size`, `eng-targets`, `eng-targets-pump`, `idle-shortcut`) to committed references (waiting for the owner's yes); the stems double-count (a send's direct sound is in its own stem and in the bus's: on `beta` before these PRs; write-up at the end of `docs/BUS-INTO-BUS-NATIVE.md`) and "a pure source whose only bus is muted still gets a stem".

### Decisions made for the owner on 19-20 Nov (he was told each; he can reverse any)
Enter on an empty master cell in an empty row fills the row (as double-click). Replacing the sample in an EXISTING Sample layer keeps that layer's tuning (a toast says how to fix the pitch); only a new layer gets the corrected tune. Envelope and SYNC lengths in lines follow the song's LPB. A reset never changes a track's bus role (a bus or a feeder keeps CHN on). A loop of …chains cables is accepted and the closing cable is left out (label LP). A middle bus is heard only through the next bus. OLDSCHOOL is rendered first; live in the voice later. The tempo settle (300 ms) applies to OLDSCHOOL only. Natively: live engines run ahead by their latency (no PDC entry); a note-off always wins on a full queue; the MIDI clock is realigned after a TPO command; late notes for gear-only instruments are sent, not dropped. New web FEATURES are merged into `beta` only with his yes ("merge everything" covered the 19 Nov batch only).

### Waiting on the owner
Two or three of his loops (a break, something tonal, a full mix) for the CLEAN stretch listening test. Rubber Band yes / no (GPL: only as a separable module; the lead recommends no). Whether to convert the five skipped tests. Whether he wants a side-by-side beta app (own name, appId and settings folder). Anything he finds in beta.2.

### Suspected web bugs told to the owner, NOT confirmed by ear and NOT fixed in the web app
"Audio thread" sequencer: slices / sync do not chop (the stub above); CUT / RES ignored on nine live engines (COIL, FOLD, TRACT, STRIKE, BOING, FREEZE, LIFE, X0X, …waves); a WOB command on an empty row ignored; a slide cut by the next note stops gliding. MIDI out (all fixed natively only): vol 00 leaves the previous mono note ringing; clock jitter and a permanent 42-50 ms offset after a tempo command; Start not Gear-delayed; two tracks on one port + channel cut each other; stale CC / channel tables; a tick burst after a stall; late pad notes dropped but their MIDI sent; a stray note-off on release; on a browser port without `clear()`, a tick and a note-off go out after Stop; no RPN bend range / bank select is ever sent. The quiet-mix report of 19 Nov: the likely cause (gains not re-applied after a bus-plan change with a solo set) is fixed in build 651+; the owner's test is on the punch list.

## START HERE of 19 Nov, 17:00 (now history)
**Say to a new chat:** "Pull thunder-plus, branch native, and continue from docs/port/PICKUP.md. Start at START HERE (19 Nov, 17:00)."

**UPDATE 20 Nov, 08:55. Only what is pushed exists.**
WEB: all twelve PRs of 19 Nov are MERGED into `beta` (build 658, head 1dfbf7ad, CI green): #248-#259 (Expression examples, beta icon, drag sliders, FOLLOW order, FX preset keys, master double-click, in-use outline, reset track, bus into bus + the applyMix-after-plan-change fix, sampler pitch + SYNC TO LINES, playback-report diagnostics, envelope times in bars + lines). **Desktop beta 1.3.14-beta.1 was started ONCE** (run 44766538470 from `beta`, prerelease): do not start it again; check the release page. Building now on `beta`: stretch steps 1 and 2 (`beta-stretch-picker` 659, `beta-stretch-oldschool` 660; design `docs/STRETCH-DESIGN.md` on `beta-stretch-design`), and `beta-flex-golden` (the FlexCore test's reference is stale since 647 and skipped in CI). Not merged to `main`: the owner has not asked.
NATIVE: #247 follow-ups MERGED (the sanitizers CI job now has 120 minutes; it takes about 42). #246 stems: verdict MERGE (`review/verify-246`), branch updated onto native, merge when CI is green; its three follow-ups (F1 the length-estimate comment is wrong, F2 / F3 two missing audio tests) go into the next follow-ups PR. #260 `n1/players` (PR A): verifier report on `review/verify-260`; merge on a MERGE verdict + green CI. Live routes PR B on `n1/live-route` in stages B1-B6 (B1 done: reset / seek; status at the top of `docs/port/LIVE-ROUTE-NOTES.md`). U35 `n1/u35-spec`: MidiSched building. After bus into bus on the web, the native mixer needs `docs/BUS-INTO-BUS-NATIVE.md` (on `beta`) applied; after SYNC TO LINES and envelope lines, the native host must read `insts[i].lines` and the envelope unit fields (`docs/SAMPLER-NOTES.md`, `docs/ENV-LINES-NOTES.md` on `beta`).

**UPDATE 19 Nov, 22:45 (the owner went to bed; "merge everything", then "push a new desktop beta"). Only what is pushed exists.**
NATIVE (branch `native`):
- #246 `n1/u34b` stems: open, CI green except sanitizers (was running); verifier report expected on `review/verify-246`. Merge when both are clean.
- #247 `n1/followups-19nov`: open (beats 0 -> 4 fixed); verifier report expected on `review/verify-247`. Merge when clean.
- Live routes: design + rulings L1-L10, C1-C11, P1-P6 in `docs/port/LIVE-ROUTE-DESIGN.md` on `n1/live-route`; worker building the decision-log golden, PR A `n1/players`, PR B on `n1/live-route`.
- U35 MIDI: `n1/u35-spec` (notes, `tools/midi-ledger.mjs`, 28 ledgers, rulings: the web's MIDI bugs B1-B9 are FIXED natively, each with a listed expected difference); worker building `MidiSched`.
- The lead's pre-wiring read: `docs/port/reviews/prewire-19nov.md` (F1-F3 must be fixed before U37).
WEB (branch `beta`; each PR holds a build number; every merge makes the next need a rebase of BUILD / sw.js / changelog / HANDOVER; merge one at a time when CI is green on the rebased head):
- MERGED: #248 Expression examples (648).
- Ready after rebase: #252 beta icon (`beta-icon-swap`), #249 drag sliders (649), #253 FOLLOW order (656).
- #250 FX preset keys (653): the owner has not answered whether moving the value nudge to mod+Shift+arrows is right; the lead's call if he stays silent: merge as built (his own example needs it) and tell him.
- #251 reset track (650): verifier report expected on `review/verify-251`.
- Building, no PR yet at 22:45: `beta-bus-into-bus` (651; rulings in `docs/BUS-INTO-BUS-DESIGN.md`; CHN labels two letters: MS, LP), `beta-solo-fx-quiet` (652, bug hunt), `beta-inuse-outline` (654), `beta-sampler-pitch-sync` (655; bars + lines), `beta-master-dblclick` (657; a NEW master block), `beta-env-lines` (658; 4 bars must work, 8 if cheap; bars + lines).
- Design only: `beta-stretch-design` (`docs/STRETCH-DESIGN.md`): build steps 1 and 2 after 655 merges. Waiting on the owner: his test loops, Rubber Band yes / no, OLDSCHOOL rendered first (the lead's default: yes).
- DESKTOP BETA when the web PRs are in: run `desktop.yml` from `beta` through the API with `inputs[version]=1.3.14-beta.1`, `inputs[prerelease]=true` (the beta icon is picked by `THUNDER_BETA=1`, set by the workflow for a prerelease). Beta and stable share one appId: the beta replaces the stable app.

**UPDATE 19 Nov, 20:25 (a later chat; if that chat is gone, its workers are gone too: only what is pushed exists):**
- **PR #246 `n1/u34b` (U34b stems) is OPEN, not merged.** The lead read the core diff (sound). An independent verifier was running; its report, if it
  got that far, is `docs/port/reviews/verify-246.md` on `review/verify-246`. No report there: send a new verifier, then merge when it and CI are clean.
- **`n1/followups-19nov` (queue item 1) was being built** (notes: `docs/port/FOLLOWUPS-19NOV-NOTES.md` on that branch). No PR yet at 20:25.
- **The live routes (queue item 2) are designed and ruled:** `docs/port/LIVE-ROUTE-DESIGN.md` and `LIVE-ROUTE-NOTES.md` on `n1/live-route`. A worker was
  building step 1: the decision-log golden, PR A `n1/players` (the `Players` move, no behaviour change), PR B on `n1/live-route` (LivePool: bass, vox,
  acid, mach). Continue from the checklist at the top of LIVE-ROUTE-NOTES.md with a new Sonnet worker briefed from the design's last section.
- Told to the owner as suspected "Audio thread" web bugs (from the code, not by ear): CUT / RES ignored on nine live engines, a WOB command on an
  empty row ignored, a slide cut by the next note stops gliding. The native port follows the page's player for all three.

**Nothing is running and no PR of ours is open** (#225 `n1/repin-pump` stays open, waiting on pump). Weekly usage was at 90% at 12:45.

**Merged into `native` on 19 Nov (each verified by an independent Sonnet verifier; reports on the `review/verify-*` branches):**
- **#243** the sequencer on a live clock: `Seq::setTime / setBpm / shift`, `SeqRunner` (catch-up skip, 3 steps a block, EXACT STOP).
- **#242** the SoundFont route into the offline renderer: `thunder_render_add_font / set_pad_sf`, the poly cap shared by both pools.
- **#244** the Song's lifetime under a running sequencer: `SeqSession`, `SongSnap`, `Seq::takeOver`, `seq_adopt`, `SeqRunner::rebind`.
- **#245** chop: sliced / synced instruments chopped as the page's `applyChop` (`seq/chop.h` `SeqChop`, `thunder_render_set_pad_chop / _hits`).
- Design and rulings: `docs/port/SEQ-LIVE-DESIGN.md`. Notes: `SEQ-LIVE-NOTES.md`, `SEQ-TAKEOVER-NOTES.md`, `SEQ-TAKEOVER-BUILD-NOTES.md`,
  `RENDER-SF-NOTES.md`, `CHOP-NOTES.md` (all in `docs/port/`).

**Estimates (the lead's): 33 of 37 units (89%); about 60% of the whole port; about 72% of the way to an Android install.**

**The queue, in order (nothing started):**
1. **A small follow-ups PR (Sonnet):** a reversed-chop render test (verify-chop F1: `voice.cpp` ~:178 `max(0, dur - ofsSec - durSec)` is untested);
   the PERFORM-part path clamps slc / beats as the main path (verify-chop F4); U31's round-3 follow-ups (F1 to F7, `reviews/verify-227-round3.md`).
2. **The live routes:** the live-engine route and the SoundFont LIVE route into the mixer (the renderer has the SoundFont route; `notes_live`
   notes are still silent and counted). Conditions: PICKUP's "Conditions recorded for the wiring", R11 (the U26 shell must never drop a NoteOff).
3. **U34b stems** (Sonnet).
4. **U36** (Link, recording, inputs), **the full wasm target and U37** (the core behind a Settings switch in the web app), **U35** MIDI
   scheduling, then **N2**, the Android shell.
- **Conditions for U37 (the host), from today:** the host sets the start time on a PLAY's first Seq before publishing it and calls
  `runner.start` after `seq_adopt` on a new session; past `sessionFull()` (8 MB) it stops growing the registries and warns (no restart at a
  pattern edge); `SeqChop` is read-only while a Seq runs, so a pad change during PLAY publishes a new table with the snapshot; the host
  calls `set_pad_chop` (and `set_pad_hits` for SMODE instruments) after every `set_pad`, with `loopSecs` of the pad AS RENDERED.
- **Known and audible until the stretch port exists:** sync modes STRETCH and LOOP play unstretched (counted in `notes_stretched`), and their
  offsets and slice lengths are those of the stretched timeline, so slices land in the wrong place.
- Still open from before: can a saved song's JSON exceed the render API's 16 MiB limit; the 1.5 dB RMS difference at 384 kHz (master chain).

**For the owner:**
- Reproduce the suspected web bug: Settings, sequencer "Audio thread" vs "Page (old)", an instrument with SLICES 4, four notes a semitone
  apart: on Audio thread every note seems to start at the top of the sample. The chop builder adds: the worklet already has the pad data
  (`core/seq-core.js` :912), only `applyChop` is a stub there, so the web fix is to stop stubbing it (a `beta` job when he confirms).

**Lessons of 19 Nov:** every brief says "add files by path, never `git add -A`" (two branches had build directories committed;
`native/build*/` is in `.gitignore` now) and "every pushed commit compiles; a checklist in the notes updated in every commit"; check a PR's
file count before merging; the sanitizers CI job takes about 50 minutes; start the verifier while CI runs; the server restarted at 12:19
and 12:36 and stopped the workers (a stopped agent of the same chat is resumed by its id).

## START HERE of 19 Nov, 12:50 (now history)
**Say to a new chat:** "Pull thunder-plus, branch native, and continue from docs/port/PICKUP.md. Start at START HERE (19 Nov, 12:50)."
(Written because weekly usage reached 90%: the owner may stop here. Nothing new is started until he says so.)

- **Merged today:** #243, the sequencer on a live clock (setters, `SeqRunner`); verified after the merge (`review/verify-243`), no defect.
- **#242, the SoundFont route into the renderer: MERGED at 12:50** (every CI job green on the clean commit 730d787; verified, the five findings
  fixed: leak, budget hole, the poly cap shared by both pools, test gaps, stats). `native/build*/` is now in `.gitignore`.
- **#244, Part B, the Song's lifetime: MERGED at 15:07** (`SeqSession`, `SongSnap`, `Seq::takeOver`, `seq_adopt`, `SeqRunner::rebind`). Verified
  (`review/verify-244`), its findings fixed (the dead sequencer when a PLAY and an edit are adopted in one call; test gaps; the plain `adopt()`
  is not callable on `SnapSlot<SongSnap>`), every CI job green, the lead read `adopt(handover)`, `seq_adopt` and `takeOver`.
  - **The lead's ruling on the session's size (F5b): no restart at a pattern edge.** Every PLAY is a new session already. Past 8 MB
    (`sessionFull()`) the host stops growing the registries: new places get no counter and are counted in `dropped`, and the host warns.
    The wiring step (U37) implements and tests that.
  - **Conditions for the wiring:** the host sets the start time on a PLAY's first Seq before publishing it, and calls `runner.start` after
    `seq_adopt` on a new session (`rebind` keeps hold / lr / ls / ended).
- Estimates at 15:07: 33 of 37 units (89%); about 58% of the whole port; about 70% of the way to an Android install (the lead's estimates:
  of the wiring toward live playback, the live clock, the Song lifetime and the SoundFont render route are done; chop, the live-engine and
  SoundFont live routes, U36, the wasm target + U37 and U35 remain before N2).
- **Next, not started:** the chop unit (ruled: `docs/port/SEQ-LIVE-DESIGN.md`, last-but-one section; Sonnet). Then the old queue from item 4.
- **For the owner:** reproduce the suspected web bug (Settings, sequencer "Audio thread" vs "Page (old)", an instrument with SLICES 4: on Audio
  thread every note seems to start at the top of the sample). Steps were given in the chat of 19 Nov.
- **Lessons of today:** every brief must say "add files by path, never `git add -A`; never commit a build directory" (`native/build*/` goes
  into `.gitignore` with #242); check a PR's file count before merging; the server restarted at 12:19 and 12:36 and stopped the workers
  (a stopped agent of the same chat can be resumed by its id).

## START HERE of 19 Nov, 07:30 (now history): the state for a new chat. The dated sections below are the history, newest first after this one.

**Merged into `native` (33 of 37 units; about 55% of the port, about 65% of the way to an Android install):**
- U31, the ...chains engine (#227), after three verification rounds; U32, the SoundFont player (#226).
- Wiring step 1, the offline song render C API (U34a: #237, its verification fixes #238): `native/core/include/thunder/render.h`.
- Wiring step 3, `dispatch` routing and `noShape` (#239).
- Web: `main` = `beta` = build 647; desktop v1.3.13 = build 647.

**Open pull requests into `native`: none of ours (08:20 on 19 Nov).** Both follow-ups merged with every CI job green: **#240** (render API: variation
replace within the budget, four test gaps, `set_pad` drops a pad's variations, header corrections; CI ran on the tree with U31 merged in) and **#241**
(U32: a work budget for decodes, a stream cache, the granule read first, parse memory bounded). They overlap nowhere (render vs sf), but the head of
`native` with both has had no CI run of its own: the next PR's run covers it.
- (#225 `n1/repin-pump` stays open and unmerged: it waits for pump-side changes, see the 11:30 section of 18 Nov.)
- **Lesson of the night:** a green PR is green against the `native` it was tested with. When `native` has moved, merge `native` into the branch and let
  CI run again before merging (U31's first combined run failed on a test whose premise another PR had changed).

**Started 08:40 on 19 Nov (a new chat; this chat's workers are Sonnet, each in `/home/claude/wt/`):**
- Item 1, Part A (clock setters + `SeqRunner`): **MERGED, #243** (10:50; CI green, the lead read the audio-thread diff). Its verifier runs after the
  merge: report `docs/port/reviews/verify-243.md` on branch `review/verify-243`. Part B (Song lifetime, design in `docs/port/SEQ-LIVE-DESIGN.md`):
  the read-only first step (make-time table, run-state inventory, counter sizes) is `docs/port/SEQ-TAKEOVER-NOTES.md` on branch
  `n1/seq-takeover-notes`; the lead rules on it, then a builder starts.
- Item 2: **PR #242** (`n1/render-sf`), CI green, NOT merged: its verifier's report is `docs/port/reviews/verify-242.md` on `review/verify-242`.
  Before merging: merge `native` into the branch and let CI run again. The lead still rules on its "Choices for the lead" (RENDER-SF-NOTES.md):
  the poly cap across the two pools, PCM shared per preset before Android.
- Item 3 is RULED: the page's sequencer is the specification, sliced / synced notes are chopped (same design file, last section). New unit "chop"
  after Part A. To tell the owner: with Settings on the audio core the web app seems to play sliced / synced instruments unchopped.

**The queue, in order (as of 07:30; see the lines above for what has started):**
1. **Seq setters and the Song lifetime** (WIRING-DOSSIER D1: `time_`, `base_`, `bpm_` setters, RESUME shift, the catch-up skip, EXACT STOP hold; R1).
   The lead's design; gate `seq-golden`. This is the next step on the path to live playback, and it is the top model's work.
2. **The SoundFont route into the renderer** (`thunder_render_set_pad_sf`, an `SfPool` rendered into the mixer), then the live-engine route. Sonnet,
   the lead reviews. Until then such notes are silent and counted (`notes_sf`, `notes_live`).
3. **The lead must rule on chop:** the C++ sequencer follows `core/seq-core.js`, where `applyChop` is empty, while the page's own sequencer chops
   sliced / synced notes (`docs/port/DISPATCH-NOTES.md`, choice 1). Which is the specification?
4. **U34b stems** (Sonnet). 5. **U31 follow-ups PR** (round 3's F1 to F7; round 2's notes-only D6, D8, D10; `docs/port/reviews/verify-227-round3.md`).
6. **U36** (Link, recording, inputs), **the full wasm target and U37** (the core behind a Settings switch in the web app), **U35** MIDI scheduling,
   then **N2**, the Android shell.
- To check before the host relies on it: can a real saved song's JSON exceed the render API's 16 MiB song limit (embedded samples)?
- Noted for the master-chain unit: 1.5 dB lower RMS at 384 kHz than at 48 kHz (verify-238-239).

**How to run it (what worked, 18 to 19 Nov):**
- One Sonnet worker per job, in its own `git worktree` under `/home/claude/wt/`, at most two compiling at once (2 cores). Every brief: push every 20
  minutes, a top line in the notes saying what it is in the middle of, park any problem after 45 minutes, CI green on every job before reporting,
  never merge. A fresh worker with a tight brief beat resuming one with a very long transcript.
- **An independent Sonnet verifier for every unit, before or right after merge.** It found real defects every single time (crashes on NaN, a thread
  race, build breaks on five platforms, leaks, an out-of-bounds read in the reverb). The lead reads audio-thread and API-shape diffs himself.
- **The server restarts about every hour and stops every worker.** An hourly check (a scheduled check-in in the chat) that resumes them is required:
  without one, two workers once sat dead for two hours. A new chat cannot resume this chat's workers: it starts new ones from the branch and notes.
- CI does not run on pushes to `native`, only on PRs; `gh` works through REST only. Gate rules: the section "Rules the lead set for gates" below.
- The owner (22:59 on 18 Nov): keep working without him, keep the documentation current, watch for loops and rabbit holes, be economical with usage
  (weekly usage was at 76%). He switches chats at a clean point; close the old chat afterwards (a chat left open kept pushing to the same branches).

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

## State at 13:00 on 18 Nov (the START HERE of 18 Nov, now history; supersedes the pump and U31 / U32 lines above)
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

## State at 06:25 on 19 Nov (the lead)
- **U31, the ...chains engine, is MERGED (#227): 33 of 37 units.** Three verification rounds (reports in `docs/port/reviews/verify-227*.md`); the last
  CI run was on the branch with `native` merged in (308d2f1), nine jobs green. The first combined run had failed on one test, `render_api_oom`, whose
  premise (a read allocates) stopped holding because U31 moved the effects' allocations to preparation: the lead changed the test to pass when a read
  allocates nothing. Follow-ups F1 to F7 of round 3 and the notes-only items of round 2 (D6, D8, D10) are still to do in a small PR.
- **Verification of #238 + #239: SOUND WITH CONDITIONS** (`docs/port/reviews/verify-238-239.md` arrives with the follow-up PR): D1 to D8 confirmed
  fixed; one defect V1 (replacing a variation at the 512 MiB limit holds two copies), four test gaps, two header nits. Worker a0c0a0461610425b2 on
  `n1/render-v1` (worktree `/home/claude/wt/renderv1`) is fixing them, plus `set_pad` dropping a pad's variations as the page does, and checking the
  track-takeover rule against the JS. Noted for the master-chain unit: 1.5 dB lower RMS at 384 kHz than at 48 kHz.
- **U32 follow-up:** worker afe6a6893a1478d8e on `n3/u32-d1`; last push c1aadff at 04:20; it was in its full ctest at 06:18, no PR yet.
- **Owner's rule for this morning:** from 07:15 no new unit is started; running work finishes and is pushed; a fresh START HERE goes at the top.

## State at 04:50 on 19 Nov (the lead)
- **Dispatch routing is MERGED (#239, native 9efae76)**, CI green on nine jobs; the lead read the `render.cpp` diff line by line. MUT / GEN notes play
  a variation buffer the HOST supplies (`thunder_render_set_variation`; missing: silent + `notes_var_missing`, never the base pad); live-engine and
  SoundFont pads are silent with a stat through `thunder_render_set_pad_route` (the live shell and `SfPool` are not wired into the renderer yet);
  `noShape` set per event for sliced / synced instruments (D3); other events counted in `events_ignored`. ELECTRO unchanged (six WAVs byte-identical).
  **Choices left for the lead (docs/port/DISPATCH-NOTES.md):** (1) the C++ sequencer follows `seq-core.js`, where `applyChop` is empty, while the
  page's own sequencer chops sliced / synced notes: a real difference from the page, not fixed: decide which is the specification; (2) SoundFont needs
  a font input (`set_pad_sf`) and an `SfPool` rendered into the mixer: the next routing step; (3) `set_pad` does not drop a pad's variations as the
  page does; (4) `findVar` is a linear search (fine offline; index it before live use).
- **Verifier a42bba9f5eb481d31** (worktree `/home/claude/wt/vrender`, report `/home/claude/progress/verify-238-239.md`) is checking #238 and #239
  together (neither had an independent check).
- **U31 (#227, head 444cad9 = the branch with native merged in):** CI on the combined tree was still running at 04:50 (two Android jobs, wasm and
  `test` green; the Linux, macOS and Windows jobs in progress). **Merge it the moment every job is green** (round 3 was SOUND, nothing blocking).
- **U32 follow-up:** worker afe6a6893a1478d8e on `n3/u32-d1` (decode work budget, stream cache, up-front granule check, parse memory bound).
- Queue: Seq setters + Song lifetime (dossier D1, the lead's), SoundFont route into the renderer, U34b stems, the U31 follow-ups PR (F1 to F7), U36,
  full wasm / U37, N2.

## State at 03:50 on 19 Nov (the lead)
- **U31 (#227): round-3 independent verification = SOUND WITH CONDITIONS, nothing blocking** (`docs/port/reviews/verify-227-round3.md`): D1, D2, D3, D4,
  D5, D7, D9 verified fixed on 2515ab0; full ASan + UBSan ctest 165 of 168 passed, 0 failed, 3 intended skips; CI green on nine jobs; 561 chains and 383
  waves NaN cases against Node with no difference; goldens gained only `nan/*` rows. The lead read the pump spare hand-over (`cmods2.cpp`) and accepts it.
  The lead merged the current `native` into the branch (clean, no conflicts) and pushed it at 03:50 so CI runs on the COMBINED tree (U31 + the U34a
  fixes, which changed `-fexceptions` on the DSP libraries and `wmods.cpp`). **Merge #227 as soon as that CI run is all green: then 33 of 37.**
  Follow-ups for a small PR afterwards (F1 to F7 of the report): `mutate-u31.sh:32` no longer applies after the D9 edit; the TSan test catches the
  reverted race in only 1 of 10 runs (longer run or random service gaps); the sweep skips the I/O modules; nothing checks that `mods.wasm` stays the
  web's bytes; plus round 2's notes-only items D6 (quadratic document load), D8 (memory budget is per patch, six can be in flight), D10.
- **Dispatch routing (D2 / D3):** worker a98d8ec030262b0d9 on `n1/dispatch`, resumed after the 02:40 server restart; it had pushed routes, tests and
  notes (47a9c18) and was in its full ctest. Then PR, CI, the lead's line-by-line review.
- Queue: Seq setters + Song lifetime (dossier D1, the lead's), U34b stems, the U31 follow-ups PR, U32's D1 follow-up, U36, full wasm / U37, N2.

## State at 02:05 on 19 Nov (the lead)
- **U34a's verification fixes are MERGED (#238, native 33d136d)**, CI green on all nine jobs. The verifier's final verdict was SOUND WITH CONDITIONS
  (`docs/port/reviews/verify-237.md`, D1 to D8); all eight fixed with a test each, plus `thunder_render_length_estimate` (song end without voice
  tails: ELECTRO 4,921,832 against a real 4,983,424). Things that changed outside the API: the tfx `Fx::operator new` and two `layer_pe.cpp`
  allocations now throw / are checked; `-fexceptions` on the core files that build a handle and on the tfx effect libraries (host builds only; goldens
  and the six ELECTRO WAVs unchanged); CloudSeed's rate is capped at 96 kHz in `fx/fxhost.cpp` and `waves/wmods.cpp` (its buffers are sized for 96
  kHz: above that the reverb runs with 96 kHz timing); song limit 64 -> 16 MiB, vars 16 -> 4 MiB. **To check before the host (U37) relies on it: can a
  real saved song's JSON exceed 16 MiB (embedded samples)?** No song can reach a pump effect slot through the render API, so two handles share nothing.
  Not yet done: an independent look at #238 itself (the lead read its report only).
- **Wiring step 3 started:** Sonnet worker a98d8ec030262b0d9, worktree `/home/claude/wt/dispatch`, branch `n1/dispatch`: dossier D2 (`dispatch` routing:
  MUT / GEN variations, live engines, SoundFont, null kit entries) and D3 (`noShape`). Rule given: never the wrong sound; silence plus a counted stat
  where a piece is missing. Notes: `docs/port/DISPATCH-NOTES.md`. The lead reviews its diff line by line before merge.
- **U31 (#227):** worker a64ac5611cf5e1607 found the cause of each of the six CI failures (a fifth NaN crash site in the vowel filter; a NaN-to-int
  cast that differs between arm64 and x86-64; Unix-only calls in two tests on MSVC; a wasm link error that predates round 2), merged native in
  (2515ab0) and is waiting for CI. After its report: an independent verifier again (round 3, short), then merge.
- Queue after these: Seq setters + Song lifetime (dossier D1, the lead's), U34b stems, U32's D1 follow-up, U36, the full wasm target / U37, N2.

## State at 23:05 on 18 Nov (the lead)
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

## State at 20:20 on 18 Nov (the lead)
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

## State at 18:10 on 18 Nov (the lead)
- **U32 is MERGED (#226, native 4982eed): 32 of 37 units.** Round-2 independent verification: SOUND WITH CONDITIONS (`docs/port/reviews/verify-226-round2.md`):
  all seven conditions hold; ASan + UBSan full ctest 150 of 151 passed, 1 intended skip; CI green on 9 jobs. **One new defect, D1 (follow-up PR, not yet
  started):** `prepare_preset` does unbounded WORK on samples that decode in full and are then discarded (last granule 0; aliased headers): a 1.7 MB
  hostile font takes 17.7 s on the control thread. Fix: charge every decode to a work budget and remember a failed stream by its offset; also give
  parse memory a budget (a 191 KB font at the `kMaxWork` cap takes 205 MB). Fonts: `/home/claude/v2tmp/fonts/` (lost on a new session: the report
  describes how to make them). Start it with a Sonnet worker when U31's worker frees a compiling slot.
- **U34a started** (Sonnet, worktree `/home/claude/wt/u34a`, branch `n1/u34a`): the offline song render C API as a PULL model (create from song bytes,
  set pad buffers, read blocks, position / length for progress, free to cancel), the lead's decision: no callbacks across the wasm / JNI boundary.
- **U31:** the worker is on the lead's review items A to K (resumed after the 16:35 server restart).

## State at 15:30 on 18 Nov (the lead)
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
