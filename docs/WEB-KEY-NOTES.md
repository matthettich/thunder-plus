# WEB-KEY notes (branch web/key-and-transpose, PR into beta, build 661)

Everything is done; what is left is review. Line numbers are approximate (beta 3c32435 + this PR).

## Item 1: generators follow the song's key
- `songKey()` (~30660): the Project branch now also returns `iv` (the SCALE's exact notes; ACID_SCALES squeezes Lydian etc. to Major / Minor). `genScope` (~30242) hands every generator `G.kiv`.
- Helpers `keyIv`, `keyOn`, `keySnap`, `genKeyField` sit before RATCHET (~30290). `keySnap` = nearest note of the key, ties down (as `scaleSnap`).
- Option `key:true` in `arpSet`, `chordSet`, `polySet`, `markSet`, `mutSet` (in-memory settings objects, which is how every generator keeps its options); panel row "Song key".
- No option (and why): Ratchet (RET / DEL on existing notes), Probability fill (copies the note before it), Euclidean fill (the note under the cursor), Drum pattern (drum hits at the pad's note): rhythm / copies, no pitch computed. Acid line: its own KEY select (From the song / Set by hand), already on at first.
- Off = as before. Mutate, Polymeter, Markov already stepped along the detected scale before, so Off cannot leave it for them (owner question in HANDOVER).
- Not changed: the Arp / Chords root defaults are still set once per session (they are snapped when the option is on, so notes are in key anyway).

## Item 2: TRANSP. + / -
- Handler ~36212 `apRun(transposeSel, [+-1, true])`, the same `transposeSel` / `xposeScope` (~29062) as Alt+, Alt+. .
- Left out before: the Sect screen's selection (nothing moved); bus tracks not skipped (Song blocks, Pattern selection's other tracks).
- Now: Sect branch in `xposeScope`; `busTrack` skips in all three branches. Existing test rigs got `busTrack` / `busSay` stubs.
