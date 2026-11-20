# Master cell: double-click drops an empty block (build 657) — code notes

Line numbers are `index.html` on this branch (after the build 657 edits; the code they describe was read before them).

## Data
- `S.song[row][track]`: 256 rows, tracks `0..NT-1` (NT = 32, l.18852) plus the master column `MT = NT` (l.18858). A cell holds a **sect number** (a "block") or -1.
- `S.chains[sect]`: 16 rows of `[phrase, transpose]`; `S.phrases[p]` the phrase; `S.plen[p]` its length (default `S.dlen || 16`), `S.plpb[p]`.
  `NPH = 4096` (l.18855) is the limit for both sects and phrases. Phrases are per block (a new block gets its own), not per track.
- A **block** with sects hidden (`!sectsOn`, l.25967) is one sect holding one phrase; Arrange shows the phrase number (`songShown`, l.21717).
  The master cell is the same thing (a sect with one phrase) but its phrase takes only the three command columns; it is NOT what makes the row's track phrases.
- Visible tracks: `ntr()` (l.26265, `S.ntr` = MONO + POLY). Arrange draws `t < ntr()` (drawArr, about l.33620); tracks past it keep their data and are not drawn.
  Grey / striped / struck cells are styles, not "hidden": `z` = block with no data (striped, i.e. an empty phrase), `o` = OFF-only block (struck), `m` = muted block,
  `tm` = the track is muted / not audible (grey column; header number struck). Muted and bus tracks are visible, so the fill includes them (as the screenshot: track 15, 2F2).
- Master cell style: class `gx`; `o` (orange, struck) = `sectMstOff` (l.21555): a master block that only switches things off (FLX 00, GAT 00, RTG 0Y, FXO X0 …).
  So 2CB in the owner's hand-made rows is a block like that, reused on two rows. A NEW empty block draws `z` (striped), as the track ones do. Owner confirmed: a new block.

## What a double-click / double-tap does today
- Arrange pointer handling: `arrGrid` pointerdown (l.33955) starts `arrPress`; `arrUp` (l.~33975) calls `arrGo(y, t, mouse click count >= 2)`; `arrClicks` (the system's click count) catches slow double-clicks.
  Touch / pen: hold 450 ms mutes. The browser's `dblclick` never reaches the grid (cells are redrawn between clicks), so `arrGo` times it itself: `arrTap` 380 ms, or `sysDbl`.
- `arrGo` (l.33780): row number tap moves the cursor (`data-ry`, in pointerdown).
  - Beside another screen (`!asongOn() && tv !== 'song'`), empty cell: second tap within 380 ms makes a new pattern (`arrNewAt`) — but `t !== MT`, so the master did nothing.
  - On the Song screen (`asongOn()`, Arrange as Song): a filled cell, double: opens its phrase (`go('phrase')`), master included. An empty track cell, double: `newBlock` then opens it. The master empty cell: nothing (`t !== MT`). Single click: cursor only.
- Classic text Song (`songGrid`): `gridDown` (l.~35040): first tap moves the cursor, a tap on the cursor's cell is EDIT = `editTap` (l.27691). Mouse and touch go the same way.
  `editTap` on an empty song cell (sects hidden, nothing picked up) calls `newBlock(c, -1)` at once (a lone Enter / EDIT, no double needed); on a filled one a second press within 380/450 ms = `editDouble` = a unique copy.
- Keyboard equivalent exists: Enter / EDIT on an empty song cell (l.27732), including the master cell.

## How a new empty block is made
- `newBlock(c, -1)` (l.27774): `freePair()` (l.21653: a sect and a phrase with the same number when both free, else `freeSect()` + `freePhrase()`), `freshSect`, `freshPhrase`,
  `S.plen[pid] = S.dlen || 16`, `setCell('song', x, y, cid)` (which also applies UNIQUE PER TRACK `mstUniq`, bus refusals `busNo`), `after()` = `saveS()` + `draw()`, then a toast. "Empty" = `isEmptyPhrase` / `isEmptySect` (l.21540 / 21537).
- Out of numbers: `toast('No free sects left' / 'No free phrases left')`, nothing written. No command fills a whole row today.
- Per-track "new" numbering is global: the next free number, so a row of new blocks is consecutive when the free numbers are.

## Undo, playback
- `saveS()` (l.21533) -> `snap()` (l.21400) coalesces everything changed in one tick into ONE undo step (JSON of S). `coreSong()` hands the song to the running sequencer; `songVer++`.
  So any song-grid edit written in one go and followed by one `saveS()` is one step and reaches the audio sequencer like other edits. An empty phrase makes no sound.

## Built (build 657)
- `masterDrop(y)` (l.27825), after `newBlock`: filled master -> nothing (caller's filled-cell action); a visible track cell filled (or master...) -> case A = `newBlock` on the master;
  all visible track cells empty -> case B: count free (`min(free sects, free phrases)` against `ntr() + 1`), refuse with a toast (needed / free / CLEAR UNUSED PHRASES) or write all cells directly and `saveS()` once.
- Callers: `arrGo` (Arrange as the Song screen, and the side panel), `editTap` (Song screen / keyboard Enter on an empty master cell).
