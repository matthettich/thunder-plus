# Work plan for 16 Nov, daytime (until 19:00 Mountain, when Fable's usage resets)

The owner, 16 Nov 10:20: "look at what happened with Thunder Plus last night and make a plan for Opus to execute today until 7 pm";
Fable has about 9 % of its usage left, so Fable starts this plan with low-cost agents and **whoever is managing when it runs out hands
over to Opus.** To take over: open a chat with Opus selected and say **"pull thunder-plus, branch `native`, and execute
docs/port/TODAY.md"**. This file is the whole brief. Background only if needed: `docs/port/TONIGHT.md` (last night, its rules and its
review queue), `docs/port/WORKPLAN.md` (sections 2, 3, 6), `HANDOVER.md` on `beta` ("PICK UP HERE (15 Nov, 21:30)").

## Where things stand at 10:30 (checked, not assumed)
- Web app: `beta` = `main` = build 632, desktop v1.3.10 released. Last night's list is done except item 4 (both instruments sound
  when a note's instrument number changes: sequencer core, Fable's) and item 3 (LOOP jump: waiting for the owner's steps).
- `native` = 79053de, CI green on every platform. Merged last night: #180 U15, #181 U16, #183 U17, #184 U19, #185 U20, #186 U21.
  **None of them has been reviewed by Fable yet.**
- Open: #182 U18 pump hosting (open on purpose; its CI is red on MASTER by design). Old PRs #11 and #7 are not ours: leave them.
- `docs/TEST-CHECKLIST.md` on `beta` is a snapshot from build 619: builds 620 to 632 are not in it.

## How to take over (the manager reads this first, every time)
1. Read "Progress log" at the bottom of this file: it says what is running, what is pushed and what is next.
2. A new chat is a new machine: nothing from the previous session's disk exists. **Only what is pushed exists.** For each unit marked
   "in progress", `git ls-remote origin 'refs/heads/n3/*' 'refs/heads/rev/*'`, fetch that branch, read `docs/port/Uxx-NOTES.md` in it
   (the worker's own notes: what passes, what does not, the next step) and start a fresh Sonnet worker from there.
3. Then continue with the first item below that is not ticked in the Progress log.

## Rules (last night's rules in TONIGHT.md still hold; these are the ones that matter most, plus today's)
- You manage. **Sonnet** workers build one unit each in their own `git worktree` under `/home/claude/wt/`; **Haiku** does document
  chores. You read every diff before it is merged. Never ask the owner: decide, and write the decision in the review queue.
- **Never loosen a gate to get green.** Never edit a golden to make a test pass. No `-ffast-math` anywhere new. A case that is not
  bit-exact gets its cause found and written down; if it cannot be found, the PR stays open and red with notes, and you move on.
- **This machine has 2 cores and 7 GB.** At most **two** workers that compile at the same time; read-only workers (section B) do
  not count. Builds: `-j2`, and reuse one copy of the pinned sources with `-DTHUNDER_DEPS_DIR=/home/claude/deps`.
- Git: at most 2 network operations at once in the whole session, so tell every worker to do them one at a time and, on HTTP 429,
  sleep 10 s and retry once. `gh` GraphQL is blocked: use REST (`gh api repos/matthettich/thunder-plus/pulls/N`,
  `.../commits/SHA/check-runs`, `-X PUT .../pulls/N/merge -f merge_method=merge`). Push with `git -c push.negotiate=true push`.
- **Every worker pushes its branch at least once an hour** (work in progress is fine, with `Uxx-NOTES.md` up to date), so a
  take-over loses at most an hour.
- Native work is `native/` and `docs/port/` only; one branch and one PR into `native` per unit. You MAY merge a PR when every CI job
  is green. Every merged PR and every decision gets a line in "Review queue for Fable" below: PR, what it does, what you are least
  sure of.
- **Not yours today (Fable's):** N1 steps 10 and 11 (U34, U35, U37); the "Owed by the lead" list in the handover; U26, U27, U30,
  U31, U36; any change to `dsp/`, a worklet, `makeSeq`, `core/seq-core.js`; every decision listed for Fable on #182; the Beads and
  Clouds questions; U32 (needs a licence decision); U33 (needs U18). **The web app gets no code changes today** (the owner is
  making music on `main`): documents only.
- **Stop starting new units at 17:45.** By 18:30: everything pushed, PRs open, the Progress log and the review queue complete,
  scheduled check-ins deleted. Set one check-in every 2 hours to catch a stalled worker.

## A. Chores first (Haiku, about 30 minutes, in parallel with B and C)
1. `beta`, documents only, pushed straight to `beta` (do not merge to `main`: nothing changes for the app): bring `HANDOVER.md`'s
   PICK UP HERE block to this morning's state from "Where things stand" above and from TONIGHT.md's review queue.
2. `docs/TEST-CHECKLIST.md` on `beta`: add the owner's checks for builds 620 to 632, taken from each build's dated entry and punch-list
   group in `HANDOVER.md`, most important first, unticked, and say in its header that the Claude Doc was not updated (a worker
   cannot edit it). Do not invent checks: one line per thing the owner can try.
3. `native`: bring the status table in `docs/port/WORKPLAN.md` section 7 up to date (step 8: U14, U15 merged, U18 open as #182;
   step 9 done), and copy the "For Fable" items from TONIGHT.md's review queue into WORKPLAN section 9 as a list, one line each.

## B. Make Fable's review tonight cheap (Sonnet, read-only; findings go in `docs/port/REVIEW-16NOV.md` on branch `rev/16nov`)
Last night's lead said which files nobody read line by line. One worker per item; each writes a section with **only findings**:
file and line on both sides, what differs, why it matters. "Nothing found" is a valid result; say what was read. No code changes.
1. **C++ against the JavaScript it was ported from, line by line**, for: `clkcores.cpp` (#180), `idmcores.cpp` (#181), the pad
   functions cut out of `applyFx` (#183), Pitch correct and the Bytebeat compiler (#186). Report: any difference in order of
   operations, state not initialised or not reset the same way, loop bounds, integer against float arithmetic.
2. **The maths helpers copied from V8** (`js_tanh`, `js_log2`, `js_atan2`, `js_hypot3`, and `fdlibm.cpp` from #180) against V8's
   own source (`src/base/ieee754.cc` at the V8 version of the Node in CI): constants and branches, one by one.
3. **Real-time safety, by reading**, of every `process` path added in #180 to #186: anything that allocates, locks, throws, grows a
   container, or loops without a bound. Already known (do not re-report, just confirm the list is complete): pump slot wake-up
   allocates; Marbles / Tides re-create allocates; U17 pad functions return `std::vector`; FFT tables allocate in constructors.
4. **Can the web app hit the pump value-list overwrite** the U18 worker clamped (a value list longer than the plugin's parameter
   count)? Trace every caller that sends a pump value list in `index.html` and the pump worklet on `beta`. Report only.
5. **Is the web app's shipped `pump.wasm` built from the pinned pump commit?** Compare hashes and read the build workflow. Report only.

## C. New port units (Sonnet; these need no Fable review by the work plan and nothing of Fable's blocks them)
Branches `n3/uNN`, PRs into `native`. Unit definitions: `docs/port/ENGINE-INVENTORY.md`, the table near line 228. The gate for every
one is `tests/golden/engines.json` (U01's harness on `beta`): the C++ render of each factory voice the unit covers must give the
same hash. Follow the pattern of #181 and #184: a faithful port, a Node reference made from the real JS, a committed Linux golden as
the gate on every platform (Node on macOS rounds `Math.sin` and friends differently; see TONIGHT.md, U15 and U16), an allocation
guard, the `js_*` helpers in `native/core/src/rt/jsmath.cpp` for every `Math.` call. A new helper is copied from V8, never written.
1. **U22, the engine foundation, first and alone** (everything else here builds on it): tables, `rng`, `blep`, `renderLayer`,
   oscillator scaffolding, `voiceFilter`, `voiceAmp`, `voiceMod`, `renderVoice`, `mkVoice` parameters, the pad effect chain (using
   U17's `pad_*` cores). Renders go into a buffer the caller hands in: **do not decide who owns pad buffers** (that is on Fable's
   list); the test owns them. The worker's interface is provisional, as U19's was: say so in the PR.
2. **U23 and U24, after U22 is merged, one worker each** (layer types: standard, wavetable, PD, chip, pwm, sample / grains, vint;
   then the FM and additive families, membrane, bell, bar, karplus, vosim, formant).
3. **U28, then U29** if there is time (the rendered-only engines; U29 needs the `BBExpr` compiler from #186: reuse it, do not copy).
4. **Not U25 today** (Plaits / Elements run in the fast-math C++ library and will hit the same wall as Marbles / Tides: see D2).
5. If U22 is not bit-exact by 15:00, stop adding to it: push, write what differs and where in `U22-NOTES.md`, leave the PR open,
   and spend the rest of the day on B and D. Do not start U23 on an unmerged U22.

## D. Investigations (Sonnet; write the proof, change no gate; fit them around C's compile slots)
1. **#182, which operation in TUBE differs by one ulp from the wasm** (sample 2 onward, −128.6 dB, which then flips 14 CRUSH
   steps). Bisect by printing intermediate values from both builds. Result goes in `docs/port/U18-NOTES.md` on `n1/u18`.
2. **Marbles / Tides (#186, switched off):** find the first sample that differs and the operation that makes it; say whether the
   golden's fast-math wasm is the only cause. Result in `docs/port/U21-NOTES.md`. Leave them switched off.
3. **A ThreadSanitizer run** of the whole native test suite on Linux: report in REVIEW-16NOV.md; do not fix.
4. **A whole-song test of U15** (Flex / Step gate / Retrigger through `Mixer` + `FxBank` on ELECTRO, against a Node render): a new
   test is not a loosened gate; PR it, and merge it only if it is bit-exact or within the mixer test's existing −80 dB rule.
5. **Shrink the `modfx-ref` fixture** (257 MB of written inputs): generate the inputs in C++ from the same seeds, prove the hashes
   are unchanged, PR.

## For Fable at 19:00 (so the evening starts without a search)
Review queue below and REVIEW-16NOV.md first; then #182's decisions; web item 4 (the sequencer fix); the "Owed by the lead" list;
N1 step 10.

## Review queue for Fable (add a line per merged PR and per decision; newest last)
- (nothing yet today; last night's queue is in TONIGHT.md and is still unreviewed)

## Progress log (the manager updates and pushes this after every change of state; newest last)
- 10:30 Fable: plan written. Nothing started.
- 10:45 Fable (managing from the owner's chat): five workers started on this session's machine. Haiku: section A (all three). Sonnet:
  C1 U22 on `n3/u22` (PR to open when bit-exact, not to be merged by the worker); B1 + B3 and B2 + B4 + B5 (read-only; they write to
  `/home/claude/progress/review-*.md`, **not pushed yet**: the manager copies them into `docs/port/REVIEW-16NOV.md` on `rev/16nov`);
  D1 then D2 (notes to `n1/u18` and `rev/d2-mods`). Not started: C2 onward, D3, D4, D5. Next for the manager: when a worker reports,
  read its diff or findings, log it here, and start the next unstarted item (keep two compiling workers busy).
- 10:55 Section A done (Haiku): `beta` 9361306 (HANDOVER PICK UP HERE to this morning; TEST-CHECKLIST builds 624 to 632, 620 to 623 were
  already there), `native` b6180a0 (WORKPLAN sections 7 and 9). The checklist stays newest build first, as the file was; the Claude Doc
  is not updated. Still running: U22, B1 + B3, B2 + B4 + B5, D1 / D2.
- 11:00 B2, B4, B5 done (Sonnet, read-only): pushed as `docs/port/REVIEW-16NOV.md` on `rev/16nov`. B2: the V8 helpers and `fdlibm.cpp` match
  V8 12.4.254 token for token; **one miss elsewhere: `js_exp` lacks V8's `if (x == 1.0) return E;`** (`js_exp(1.0)` is one ulp off Node).
  B4: the web app cannot send too long a pump value list today (lists come from `pump/pump-meta.js`, counts equal the shipped wasm's), but
  the worklet write at `index.html:20306` is unclamped and nothing checks meta against wasm. B5: the shipped `pump.wasm` is NOT from the
  pin (meta stamp 30b389d8, MASTER 75; the pin gives 72cc7e55, MASTER 77); nothing in this repo builds it (manual copy).
  **New item D6 (Sonnet, small, when a compile slot is free):** add the `x == 1.0` branch to `js_exp` exactly as V8 has it, with a
  `test_libm` case; every golden must stay unchanged; PR into `native`, merge when green, line in the review queue.
- 11:15 B1, B3 done (Sonnet, read-only; appended to REVIEW-16NOV.md on `rev/16nov`). Section B is complete. The ports are faithful on
  operation order, state and `js_*` routing in clkcores, idmcores, the U17 pad functions, Pitch correct and BBExpr. **Top finding, for
  Fable (it is in the web app too, a worklet): a Bytebeat formula can loop forever**: a constant 50 or 51 is run as a jump through the
  op 77 / 78 quirk, e.g. `(t && 51) + a`; reproduced in Node on the real JS. The 16 built-in formulas are safe; typed text can freeze
  the audio thread. Proposed: cap executed steps per call, the same in JS and C++. Not to be fixed today (worklet; and the cap changes
  nothing for formulas that end). Also: no module checks `ctx.block <= 128`; `modfx::find()` / `regAll()` allocate on first call (U31
  must call them off the audio thread). Read by grep only, not in full: mod_basic, mod_stereo, mod_filters, fxbank, fxmap and parts of
  five other modfx files (list in the report); the pump slot wake-up was not examined.
- 11:40 D1 and D2 done (Sonnet; notes only, no fix applied). **D1, #182 (`n1/u18` f4ad8cb, U18-NOTES.md):** the first differing bit is the
  float dot product `acc += evenTaps[j]*h[j]` in `HalfbandStage::up()` (pump pin, `common/dsp/Oversampler.hpp`), at the 5th input sample:
  the wasm (`-ffast-math -msimd128`) sums in four lanes, the strict native build in sequence (0xb755edb3 against 0xb755edb2). Evaluation
  order: not libm, not FMA, not x87. Measured: `-fno-associative-math` on the wasm build makes TUBE bit-exact with native over 163,840
  samples; that the 14 CRUSH flips then vanish is inferred, not run. **D2, #186 (`rev/d2-mods` 98d6555, U21-NOTES.md):** the cause is
  `-ffast-math` in both builds; built strict, native and wasm are bit-identical on eight cases (the worker's own, not the golden's).
  Equal builds need `-fno-associative-math` on the wasm and `-fno-associative-math -fno-finite-math-only` on GCC, i.e. a golden from a
  rebuilt `mods.wasm`: **Fable's decision, with the same question for `pump.wasm`** (one decision covers TUBE, Marbles, Tides and U25).
  Started: one Sonnet worker for D6 (`n1/jsexp`), then D5 (`n1/modfx-fixture`), then D3 (TSan report to `/home/claude/progress/tsan.md`).
  Still running: U22. Not started: D4, C2 onward.
- 11:45 **If you are taking over in a new chat while this is the last entry:** two Sonnet workers are still running on the previous
  session's machine and will push and open their PRs on their own: U22 (`n3/u22`) and D6 / D5 / D3 (`n1/jsexp`, `n1/modfx-fixture`).
  **Do not start a second worker on those branches** unless a branch has had no push for 90 minutes (`git ls-remote` + the notes file).
  Your jobs meanwhile: read the diff of each PR as it opens, merge when CI is green (not #182), log it, and start D4. C2 (U23, U24)
  only after U22 is merged. The TSan report (D3) is written to the old machine's disk: if it is not in REVIEW-16NOV.md by 17:00, run D3 again.
- 11:25 (clock corrected: the entries above are about 20 minutes fast) **U22 is PR #189, open, CI running** (Sonnet): 294 of 294 factory
  voices of the covered families give beta's `engines.json` hashes; 168 direct cases and 1800 `voiceMod` probes bit-exact; GCC, clang and
  the sanitizer build pass locally. Worker's decisions: pad effects other than Degrade / Resonator / Pitch delay / Pitch reverb pass the
  sound through (as the golden's Node does, which has no wasm loaded: 18 of the 294 voices prove only the dry path); layer types outside
  the unit return `kEngUnsupported`; click left to U23; voices with JS pad effects allocate (U17's cores return `std::vector`).
  **A second Sonnet worker is verifying it independently** (is the gate real, three mutation tests, scope): report to
  `/home/claude/progress/verify-u22.md`. Manager's next step: when CI is all green AND the verifier passes it, merge #189, add its line
  to the review queue, and start U23 and U24 (one worker each; the fixture worker must have finished or only one of them at a time).
