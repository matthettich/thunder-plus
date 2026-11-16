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
