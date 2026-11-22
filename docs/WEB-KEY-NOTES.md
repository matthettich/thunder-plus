# WEB-KEY notes (branch web/key-and-transpose, PR into beta) - work in progress

Resume notes for a fresh builder. Line numbers are for beta at 3c32435 (approximate after edits).

## Item 2 (command bar TRANSP + / -)  [code DONE, tests NOT yet]
- Handler: index.html ~36212 `$('tXposeDn'/'tXposeUp')` -> `apRun(transposeSel, [+-1, true])`; same `transposeSel` / `xposeScope` (~29062) as Alt+, Alt+. keys.
- Left out today: the Sect screen (a row selection did nothing: "Transpose works on a phrase, or on blocks on the Song"), and bus tracks were not skipped (Song blocks on a bus track, the Pattern view's other tracks).
- Done: Sect branch in xposeScope (selected chain rows, each phrase once, "also used elsewhere" count), busTrack skips in all three branches; existing test rigs got `busTrack`/`busSay` stubs.
- TODO: tests/transpose-selection-all.test.js.

## Item 1 (generators follow the song key)  [NOT started]
Plan: `key` toggle (default true) in arpSet / chordSet / polySet / markSet / mutSet; helper keySnap using exact intervals (Project SCALES, else ACID_SCALES);
no toggle: Ratchet, Probability fill, Euclidean fill, Drum pattern (rhythm / copies of existing notes), Acid line (has its own KEY: From the song / Set by hand).

## Still to do
HANDOVER.md work-log + punch list, build number bump, sw.js cache if CLAUDE.md says so, full `npm test` on final head, PR.
