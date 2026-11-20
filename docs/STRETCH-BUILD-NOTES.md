# Stretch build notes (steps 1 and 2 of docs/STRETCH-DESIGN.md on branch `beta-stretch-design`)

## Checklist (updated in every commit; this is what is in the middle of)
- [x] PR 1, build 659, branch `beta-stretch-picker`: picker, `I.sx`, REPITCH sentence, no audio change (code, tests and build commit done; waiting only on the PR)
  - [x] fixtures recorded from origin/beta (1dfbf7ad, build 658): `tests/fixtures/stretch-before-659.json`
  - [x] code in `index.html`
  - [x] tests `tests/stretch-picker.test.js`
  - [x] build commit (BUILD, sw.js, changelog, HANDOVER, punch list), push, PR
- [ ] PR 2, build 660, branch `beta-stretch-oldschool` (stacked on PR 1): IN THE MIDDLE OF: code and tests written (index.html, tests/stretch-oldschool.test.js); next: browser smoke, full npm test, then the build commit (BUILD 660, sw.js, changelog, HANDOVER incl. "Time stretch: what is left"), push, open PR

## Choices for the lead
(filled in as they come up)
