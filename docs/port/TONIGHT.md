# Work plan for the night of 15 Nov (Opus leads; Fable reviews on 16 Nov evening)

The owner, 15 Nov 19:40: Fable usage is nearly gone until tomorrow evening; "hand a well developed plan to Opus to execute tonight and
Fable can check everything tomorrow night". **To start: open a chat with Opus selected and say "pull thunder-plus, branch `native`, and
execute docs/port/TONIGHT.md".** This file is the whole brief; `HANDOVER.md` on `beta` ("PICK UP HERE (15 Nov, 18:10") is the background.

## Rules for tonight
- You (Opus) project-manage: Sonnet agents build one unit each in their own `git worktree` under `/home/claude/wt/`, you read every diff
  before it is pushed, Haiku does document chores. Never ask the owner: decide, and write the decision in the review queue below.
- **Never loosen a gate to get green.** A case that is not bit-exact needs its cause found and written beside it; if you cannot find it,
  leave that PR open and red with notes and go on to the next unit. Do not add `-ffast-math` anywhere new.
- Native work: `native/` only (plus `docs/port/`), each unit its own branch `n1/uNN` and a PR into `native`. `native` is never shipped,
  so you MAY merge a PR when every CI job is green (Linux GCC and clang, macOS, Windows, Android, wasm, `test`) so the next unit can build
  on it. Every merged PR gets a line in "Review queue for Fable" at the bottom of this file: PR number, what it does, what you are least
  sure of. Fable reads those diffs tomorrow; nothing from tonight counts as reviewed until then.
- **Not yours tonight (Fable's):** N1 step 10 (song render through the core, MIDI scheduling) and step 11; the "Owed by the lead" list in
  the handover (pad-buffer ownership, `Seq` with its `Song`, thread-safe setters, `thunder_post`); any change to `dsp/`, a worklet, `makeSeq`
  / `core/seq-core.js`; the Beads and Clouds questions waiting for the owner. Write what you find, do not change them.
- Web app (`beta` / `main`): only the owner's list in section A, by a Sonnet agent, display and editing code only.
- Things that bite: `gh` GraphQL is blocked, use REST (`gh api repos/matthettich/thunder-plus/pulls/N`, `.../commits/SHA/check-runs`,
  `-X PUT .../pulls/N/merge -f merge_method=merge`, job logs `.../actions/jobs/ID/logs`). At most 2 git network operations at once in a
  session: one at a time, on HTTP 429 sleep 10 s and retry once. Shallow clones: `git fetch --depth 80 origin BRANCH`. Before committing a
  merge, `grep -rn '^<<<<<<<'`. A PR with a conflict gets no CI run. Passing ctest prints no figures: run a test binary by hand for them.
  The pinned upstream sources land in `native/core/third_party/_deps` at configure time (`-DTHUNDER_DEPS_DIR=` reuses one copy).
- Commit trailers: `Co-Authored-By:` your own model line, as the session tells you.

## A. The owner's web-app list (15 Nov evening). One item = one build = one commit, pushed to `beta` AND merged to `main` after each
("check in and push to main after each is fixed, so if we run out of usage … I can still work on music"; no PRs for these). A Sonnet agent
was working down this list when this file was written: **check `git log origin/beta` for builds 624 and up, read each build's `CHANGES`
entry in `index.html`, and continue with the first item not done.** Per build: `const BUILD`, a `CHANGES` entry, the `sw.js` cache name, a
punch-list group and a short dated entry in `HANDOVER.md`; `npm test` green; push `beta`; then fetch `main` (a bot commits `beta/` to it
after every beta push), merge `beta` into it, `npm test`, push `main` (retry on rejection).
1. Arrange: a selection made upward shrinks a phrase at a time once playback starts. (Done as build 624: `followPlay` guard on `apSel`.)
2. The command bar's make-unique removes the selection: it must keep selection and cursor.
3. Song screen: pressing LOOP scrolls to the bottom of the song: it must not move the view.
4. Changing a note's instrument number in a phrase sounds both instruments. Find whether it is the edit-audition path or the sequencer.
   If the fix is in the sequencer or a worklet: write the diagnosis (file, line, mechanism, proposed fix) here and leave it for Fable.
5. Control bar: the loop-play button goes immediately left of Play.
6. Song and Arrange page ahead of the playhead sooner (about a third of the view from the bottom; the hold-while-working rule stays).
7. Every pattern generator (Acid line, Euclidean fill, Mutate, Drum pattern, …) gets an instrument-number field (default: as today).
8. "Where can we see what tracks an instrument is active on?" If nowhere: a read-only "USED ON: 03 07 12" line on the INST screen
   (cached, not computed per draw). Tell the owner where it is.
9. A MOD section in the bottom FX bar for the current track, collapsible like the other sections: per active LFO a tiny scope or LED
   following its phase (nothing drawn when collapsed, stopped or ANIMATIONS off) and its destination names; clicking may open MOD.
   Display only; follows the skin; shrinks or hides on the folded phone.
10. (Owner, 19:45; sent to the running agent too.) The folded phone's header, folded layout only: BPM and LPB move to the left of the
   countdown timer; REC leaves the header and becomes a File-menu entry (Settings if it reads better; same action, shows on / off); the ROWS
   box sits between LEFT and LPB without its + / − buttons, and there the row count is changed only by tapping the number. Nothing clips at
   344 px wide.

## B. Native port, in this order
State at 19:45: PR #175 (U14, the track-effect host) is **merged** into `native` (539ac47), CI green on every platform. How it went green
is in section C; read it, it changes how effects are built.
1. PR #179 "pump: no MMX": **merged** (20:20, all CI green).
2. U15 Flex / Step gate / Retrigger: PR #180 **merged** (21:00, all CI green; 73 cases bit-exact; the lead read the host changes). Learned: Node on
   macOS arm64 rounds Math.cos / sin / tan / log differently from Node on x86-64 Linux, so `libm` compares against a committed golden made on
   Linux (`libm_golden_current` checks it there); the C++ on macOS matches that golden. `fxclk` still takes its reference from the local Node
   with committed hashes required to match (it passed everywhere): exposure noted in its header. Not done in U15: an end-to-end test through
   `Mixer` + `FxBank` on a real song (the clock hand-over is covered by direct host tests only): for Fable's list.
3. **U18 pump hosting: a Sonnet agent was started at 21:00 on branch `n1/u18` (worktree `/home/claude/wt/u18`, notes `/home/claude/progress/u18.md`); if the
   branch is on the remote take it over, else start it.** (the pump effects as slot kinds of the host, through the `pump_*` C entry points that `pump.wasm` exports; model:
   U14's tfx kinds; reference: the web app's pump worklet path in Node with `pump.wasm`). The NaN that blocked it is fixed (item 1). Two
   things to carry: the pin's MASTER is one commit newer than the app's shipped `pump.wasm` (compare against a wasm built from the PIN, and
   say so); pump is still built with `-ffast-math -fno-finite-math-only`, so expect the same cross-compiler spread U14 had (section C). If
   a pump case misses −80 dB on one compiler only, do NOT change pump's flags tonight: record the figures per compiler for Fable.
4. **U16, U17, U19 to U21: the JS-only effects**, in the order of `native/tests/CENSUS.md` / `docs/port/ENGINE-INVENTORY.md` (unit table
   near line 228). Same pattern each: faithful port, Node reference from the real JS, bit-exact gate, allocation guard. One unit per PR.
5. Stop there. If time is left: the A4 bridge timings and the Android-side review fixes cannot be verified without the Fold: leave them.

## C. What Fable found on 15 Nov evening (for the record; move into HANDOVER.md)
- **U14's red CI was the system maths library, not Autowah state or order.** With `-ffast-math` the effects' samples depended on the
  compiler and the libm: GCC under fast-math calls glibc's vector maths and rewrites calls; glibc 2.43 (the CI image, ubuntu-26.04) and
  Apple's libm round `expf` / `exp2f` / `cosf` differently from glibc 2.39 and the wasm's musl. Proof: GCC 15.3 with glibc 2.39 passes all
  91 cases; preloading a correctly-rounded `sinf` / `cosf` moves `e_pshift_def` to exactly the CI figure. Autowah (a resonator near
  self-oscillation) and Beads amplify one-ulp differences to −50 / −64 dB.
- **Decision carried out (step 3 of the owner-approved plan):** host builds compile `thunder_dsp_tfx` with `-fno-fast-math
  -ffp-contract=off` (`OPT_TFX` in `native/core/dsp/CMakeLists.txt`; the wasm keeps `-ffast-math` until step 11). GCC 13, GCC 15 and clang
  20 then give IDENTICAL samples on Linux x86-64. Against today's `tfx.wasm`: `e_pshift_def` −68.5 dB (own gate −66 in `test_fx.cpp`),
  `e_clouds_alt` one int16 step (−75.9 dB, now allowed on every host build), `e_autowah_def` −82.4 dB, everything else below −90 dB.
  macOS and Linux CI are green with it; their figures are not in the logs (passing tests print nothing). Open for Fable: the owner should
  hear the pitch shifter native against web once there is something to listen to (−68.5 dB is better than the −60 dB "ask him" line);
  the voices and `mods` libraries (`thunder_dsp_w`, `thunder_dsp_mods`) and pump still use `-ffast-math` and may show the same spread on a
  new libm; denormals: strict builds no longer get flush-to-zero from anything, the real-time thread must set it (owed with the lead's list).
- **STUTTER NaN after SPRING:** GCC 13 used an MMX register in `SpringTank::process` with no EMMS; STUTTER's inlined x87 `fprem` then
  overflowed the x87 stack. Fixed with `-mno-mmx` on x86 host builds of pump; ctest now also runs all 25 types in one process.

## Review queue for Fable (Opus: add a line per merged PR and per decision; newest last)
- #175 U14 strict-maths commit 4718bd3 (Fable's own): done.
- `n1/pump-mmx`: Fable's own; merged by Opus when green.
- **Web item 4 (both instruments sound when a note's instrument number is changed): NOT fixed, Fable's.** The Sonnet agent's diagnosis: the
  edit-audition path is clean (`after()` about line 27339 of `index.html` calls `aud(r)` with the NEW instrument only). While the song plays,
  the sequencer has already booked the coming note with the OLD instrument: the core's look-ahead (`coreLead`, 0.25 s, about line 24443) plus
  `coreSong()` (about 24448) sending the edit as a `patch` only after a 100 ms debounce; `makeSeq` on the page has the same look-ahead. So the
  next note on that row within about 0.25 to 0.35 s plays the old instrument right after the audition of the new one. Second, cosmetic: stepping
  the number quickly stacks auditions (`aud()` with no key held uses hold = false, so `previewOff` cannot cut a ringing drum pad). Proposed:
  apply IC / note edits to the core at once (no debounce) and drop or re-book the queued, unplayed events of that cell when the patch lands;
  maybe choke the previous edit-audition. Needs the core (`core/seq-core.js`, `node tools/make-core.mjs`): Fable.
- Web items done by 20:25: 1 (624), 2 (625), 3 (626), 5 (627). The agent continues with 6 to 10.
- #180 U15 (Sonnet agent; the lead read fxhost / fxbank changes, not clkcores.cpp line by line against the JS, and not fdlibm.cpp): merged.
- **Web list state at 21:50: `beta` = `main` = build 631.** Done: 1 (624), 2 (625), 3 (626), 5 (627), 6 (628), 7 (629), 9 (630), 10 (631); 8 needed no build
  (INST screen's IN USE row and the Synth page's "Used on" line already list the tracks). 4 is Fable's (above). Fable read the diffs of 624 to 631: accepted.
  Not done: `HANDOVER.md`'s PICK UP HERE block on `beta` still describes 18:10 (Haiku: bring it up to date from this file), `docs/TEST-CHECKLIST.md` not re-exported.
  **One follow-up for a Sonnet agent (build 632):** item 10 put the folded tempo line as ROWS, BPM / LPB, time left. The owner's words were "bpm and lpb to the
  left of the countdown timer" and "the rows box … between left and lpb", where LEFT is the time-left box: so the order he asked for is **BPM / LPB, ROWS, LEFT**.
  It is the `order` values in the build-631 media query (`body.mob #tRowsBar`, `body.mob #tClk`); check at 344 px in two skins with a screenshot.
  Item 3 is only partly confirmed (the jump was reproduced from an empty cursor row only): its punch-list item asks the owner what he pressed.
- **U18 pump hosting at 22:20: branch `n1/u18` pushed (Sonnet agent, then one commit by Fable), NO PR yet, NOT to be merged tonight: it carries decisions for Fable.**
  Read `docs/port/U18-NOTES.md` in the branch. Pump effects are slot kinds 200 + type of `FxHost`; 80 cases against a `pump.wasm` built from the pin.
  Fable's decision, done in the branch: host builds compile pump without `-ffast-math` (as the track effects); VOX then passes on GCC 13 and clang 20.
  **For Opus tonight on this branch (a Sonnet agent):** (1) regenerate the reference with the PIN-built wasm (the copy Fable tested against had the shipped wasm, where
  MASTER differs by design: 77 parameters / latency 4454 against 75 / 358) and print the full table for GCC 13, clang 20 and GCC 15 with the strict build; ctest the whole
  suite on each; (2) `x_two_hosts_same` is −68.4 dB on both compilers (14 of 163840 samples, a CRUSH quantiser edge): find out exactly why a second host changes those 14
  samples at all (two hosts must not influence each other: is it shared state, or the same spread showing in this case only?) and write the proof; do not gate it away;
  (3) run `parity-u18.sh` end to end and open the PR so CI runs (the wasm job's new step was never run); leave the PR open.
  **For Fable (not Opus):** the audio thread can allocate when a resting pump slot wakes or a plugin's output runs away (the JS does `pump_free` + `pump_new` there; kept);
  the gate that keeps the audio thread from waiting on `pump_new` passes the sound unprocessed for a block now and then (a click when an effect is added mid-song);
  `pump_io` is process-global, so every host must run on one audio thread (not enforced); no ThreadSanitizer run; whether the web app's `pump.wasm` is rebuilt from the pin;
  the heap overwrite the agent found and clamped (a value list longer than the plugin's parameter count wrote past `pump_values`): check whether the web app can do the same.
- Units U16, U17, U19 to U21 do not depend on U18: branch them from `native`.
- **Opus (lead) from 21:05:** build 632 (Sonnet; folded header order BPM / LPB, ROWS, LEFT via the `order` values; screenshots at 344 px in IDM pink/green and Minimal black,
  `npm test` green) pushed to `beta` and merged to `main` (ee095ad). `HANDOVER.md` PICK UP HERE brought to 21:30 and section C moved into it (Haiku, lead restored two dropped notes; `beta` 814109e, not merged to `main`: docs only).
- #181 U16 Grains / Scramble / Glitch / VSamp (slot kinds 100 to 103, Sonnet agent; 242 cases bit-exact on GCC 13 and clang 18; `js_tanh` from V8 added to fdlibm and the libm golden): merged, all CI green.
  **Lead's decision in it:** the agent's test failed if the C++ differed from the LOCAL Node run, which would have gone red on macOS (these cores call sin / cos / log). The gate is now the committed
  Linux hash on every platform (bit-exact; `fxidm_golden_current` proves it on Linux), and the local-run comparison must hold only on x86-64 Linux. Same exposure in U15's `fxclk` (left as is: it passed). Least sure of:
  the lead read the host / map / test changes, not `idmcores.cpp` line by line against the JS, nor the new `js_tanh`; the fixture is heavy (about 800 MB, 26 to 34 s in CI; 145 s here).
- #182 U18 opened (NOT merged, by design). The Sonnet agent's follow-ups: reference from a PIN-built `pump.wasm` (wasi-sdk 25); GCC 13 = clang 18 on all 80 cases (40 bit-exact, 39 within −80 dB, worst passing
  −115.6 dB); `x_two_hosts_same` is NOT a two-host effect: the same 14 samples and −68.4 dB with one host, hosts bit-identical to each other in both JS and C++; cause is TUBE's arithmetic spread against the wasm
  (−128.6 dB, 1 ulp from sample 2) flipping CRUSH's `round(x*q)/q` steps. Which TUBE operation differs is open. `pump` ctest against the SHIPPED wasm fails on MASTER by design, so the PR's CI is expected red there.
- #183 U17 Pitch reverb / Pitch delay / Resonator / Degrade (slot kinds 96 to 99; pad `applyFx` cores `pad_degrade`, `pad_reson`, `pad_pitchfx` tested directly, the pad `V.fx` loop waits for U22): 233 cases bit-exact on GCC 13 and clang 18, merged all CI green.
  Least sure of: the pad cases rest on the agent's cut of `applyFx` out of `index.html`; pad functions return a `std::vector` (allocate: fine offline, not for a real-time pad path); Pitch reverb / delay / Resonator nap like a wasm effect (one 31.5 s case).
- #184 U19 MODFX modules A: 54 modules in `native/core/src/modfx/` behind the agent's own minimal `Module` / `IO` / `Ctx` / `Seeds` / `Def` interface (U31 may wrap or replace it); `js_log2` from V8. 162 of 162 `chains-modules.json`
  engine hashes and 904 direct cases bit-exact. Left: `expr` (MathExpr + per-instance text) and `scope` (`show()` data), both U31 decisions; `show()` not ported anywhere; `waves-modules.json` waits for U30 (the agent checks the shared block is
  identical in the two worklets). **Lead's fix before merge:** the sanitizer build did not link (`SeedCounter` vtable / typeinfo emitted only in the `-fno-rtti` core) and, once linked, UBSan's vptr check on core-made objects reported "invalid vptr"
  and freed inside the guarded callback: `SeedCounter::next` made inline, `-fno-sanitize=vptr` for `test_modfx` and `rt_harness` only (the core already has no vptr check under `-fno-rtti`; comment in `native/tests/CMakeLists.txt`). Fable: confirm or prefer moving the virtual calls into the core.
  Degrade and Reson exist twice now (U17 track-FX cores and U19 modules): not unified.
- #185 U20 Spectral / Extreme stretch / Convolution reverb + FFT cache; `js_atan2`, `js_hypot3` from V8: 9 engine hashes (171 of 171 overall) and 99 direct cases incl. song IRs, bit-exact; merged all CI green. Least sure of: the song-IR hand-over
  (`Ctx::irs`, raw pointers kept while an IR is built, ~100 blocks: U31 must keep old buffers alive); FFT scratch made per instance instead of the JS's shared per-size one (fully overwritten each start); FFT tables are allocated in constructors, so U31 must make modules off the audio thread.
  The `modfx-ref` fixture is about 189 MB with U20 (written inputs); if it keeps growing, generate the inputs in C++ instead of writing `.in.f32`.
