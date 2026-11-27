# Handoff prompt for the fixing agent (copy-paste from the line below)

You are working in the Thunder Plus repository (`matthettich/thunder-plus`). It is a single-page web audio app/tracker:
`index.html` is the whole app (39.5k lines, one huge inline `<script>`), plus AudioWorklet modules, an Electron desktop
shell, and a test suite under `tests/`.

## Read first, in this order

1. `CLAUDE.md` — the project's own session rules.
2. The top of `HANDOVER.md` ("Where things stand" and the dated entries) — current state and owner decisions.
3. `docs/audits/2026-11-27-code-review.md` — **the review you are implementing.** It has findings P1-1 … P3-9, each with
   an exact `file:line`, evidence, impact, a suggested fix and an acceptance test, plus §8 "Suggested work packages".

## What I want

Work through **§8 of the review, in order, one work package at a time**. Start now with **package 1** (semver comparison in
`electron/updater.js`) and keep going through the list unless I tell you to stop. For each package:

- Read the linked finding(s) before touching code. Do not re-investigate things the review marks "checked and found sound"
  (§7) — those were verified.
- Make the **smallest fix that satisfies the finding**. Do not refactor around it, do not reformat the app, do not extract
  modules, do not "tidy" the one-line-per-function style, and do not fix unlisted issues you notice along the way — note
  them at the end instead.
- Add or extend the test named in the table. If the review names no test, write the smallest one that would have caught the
  bug.
- Run the checks yourself and paste the real output. Never report "tests pass" from memory or from a subagent.
- Commit each package separately, on `beta`.

## Hard rules

- **Branch:** day-to-day work goes on `beta`. Never commit to or push `main`; `main` only changes when the owner asks for a
  merge of `beta`. Never force-push.
- **Generated code is generated.** `core/seq-core.js` and parts of `chains/chains-worklet.js` / `waves/waves-worklet.js`
  are cut out of `index.html` by `tools/make-core.mjs` and `tools/make-fx-cores.mjs`. If your change touches anything those
  tools copy (`makeSeq`, `trackGain`, `curve`, `shapeData`, `sfPick`, `sfVoice`, `pumpPv`, `clkSpb`, `FlexCore`,
  `StepGateCore`, `RetrigCore`), regenerate and commit the output, then confirm both `--check` modes pass.
- **Never edit `beta/` or `stable/`.** They are build artefacts of `tools/make-channel.sh` published as separate URLs.
- **Version bumps:** for every page you change, bump its `?v=` query on the script tags, bump `CACHE` in `sw.js` and bump
  `BUILD`, as the repo normally does. A stale `?v=` or precache entry is itself one of the bugs (P1-3).
- **Behaviour must not change for existing songs.** Audio output, timing and the saved-file format are load-bearing. If a
  fix would alter rendered audio, say so before doing it.
- **Don't lower the bar to make a check pass.** No skipped tests, weakened assertions, `eslint-disable`, or swallowed
  errors. If a check cannot run in this environment, say so and why.
- **Tests:** `npm test` currently does not complete as configured (P2-7) — run the specific test files for the package you
  are on (`node --test tests/<file>.test.js`), and fix the runner in package 9 rather than working around it.
  `tools/recorder-check.mjs` and `tools/browser-check.mjs` must stay green.
- **Ask before:** deleting anything under `beta/`, `stable/`, `dsp/`, `io/`, `link/`; changing the undo depth or any
  user-facing limit (P2-2 is about adding a byte budget, not lowering the count); or touching `.coderabbit.yaml`,
  CI release jobs, or `HANDOVER.md`'s owner sections beyond adding your own entry.

## Definition of done for each package

- The named acceptance test exists and fails on the old code / passes on the new (say which you verified).
- The checks you ran are listed by exact command, with their exit status.
- The diff is minimal and confined to the files the package names (or you explain why another file was unavoidable).
- `HANDOVER.md` has a dated entry: what changed, what you ran, what is still open, and anything now waiting on the owner
  (add devices/checks to "Owner's punch list" if a device test is needed).

## Report back, per package

```
Package N — <name>
Changed:   <files, one line each, why>
Fix:       <what the bug was, in one or two sentences, and what you changed>
Verified:  <exact commands + result, e.g. node --test tests/updater-version.test.js -> 6/6 pass, exit 0>
Not done:  <anything skipped, and why>
Risk:      <what could still break, or "none seen">
```

When you finish a package and the checks are green, continue to the next one without waiting for me. Stop and ask only if a
rule above requires it, or if you find that the review is wrong about a finding (say what you found and what you measured).
