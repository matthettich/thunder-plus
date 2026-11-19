# DISPATCH NOTES: wiring step 3 (D2 routing, D3 noShape)

**IN THE MIDDLE OF: step 1 done (tables below); step 2 / 3 code next (variation input, route input, noShape from the song's instrument); tests after.**

Branch `n1/dispatch` (from `native` 33d136d). JS lines are from this checkout (`core/core-processor.js`, `index.html`); C++ lines are `native/core/src/render/render.cpp` before this change.

## 1. The JS, every branch (what plays for a `note`)

There are TWO JS players, and the port has to say which one is its specification:
* the **audio-thread core** (`core/core-processor.js` `ev()` :607, `mine()` :559): plays what it can and returns false for the rest, which the PAGE's player then plays;
* the **page's player** (`index.html` `makePlayer` `startNote` :22009-22030), which is what an offline render (`renderOut`, `pass()`) uses: it plays everything.
An offline render has no page player to fall back on, so the native renderer has to be both: the table gives the core's routing (what `mine()` decides) and, where the core says "the page plays it", the page's answer.

| # | Event / condition | Core (`ev()` :607-659) | Page player offline (`startNote` :22009) |
|---|---|---|---|
| 1 | `type` not note, `pad == null` or `track == null` | `false` (:656), nothing sounds in the core | not a note |
| 2 | note, instrument `mo == 1` (MIDI gear only) (:657) | nothing sounds | (the page sends it to MIDI) |
| 3 | `kv[pad]` null (a slot the kit does not have) (`mine` :561 `!k` -> false; `liveNote` :694 `!lk` -> false; then `take(ev)` :659 = the track's voices give way) | silent in the core; page: | `if (!kit.voices[ev.pad]) return` (:22010): silent, NO takeover |
| 4 | `kv.live` (live engine pad: bass, vox, acid, mach, xeng, pump synth, waves) (`mine` :561 false) | `liveNote` (:693): drives the live node `lnode()` (:681); node missing -> false, the page plays | `isLivePad` (:22013): `bassNode` / `polyVox` / `polyPs` (live node in the offline context, always available offline); node null -> falls on to the pad's render (rows 8-10) |
| 5 | `kv.sf` and the core has the preset (`sfPads.has(pad)`) (`mine` :562) | `startNote` :577 -> `sfNote` :434 (the sf player) | `kv.engine === 'sf'` -> page `sfNote` (:22016) |
| 6 | `kv.sf`, preset not sent yet | false: the page plays it | the page's `sfNote` |
| 7 | `ev.st` (stretched, SYNC) (`mine` :563) | false: the page's stretch cache | `getBuf(pad, rev, st)` (the stretched pad) |
| 8 | `ev.vk` (MUT / GEN) or `ev.tw` (CUT / RES / DEC), no `st` (`mine` :564 -> `varRec` :245) | `varRec` result `{data, sr}` or `{data, sr, tw}` -> `startNote` (rows 11-14) | `varBuf(ev, ac)` (:11225) else `getBuf(pad, rev, st)` |
| 9 | none of the above, pad render held (`coreBufs.has(pad)`) (:565) | `startNote`: the PAD, unshaped | `getBuf`: the pad (reversed copy when `rev`) |
| 10 | none of the above, no pad render | false: the page plays it | `getBuf` null: `return`, silent |

`varRec(ev)` (:245-258):

| # | condition | what plays |
|---|---|---|
| 11 | instrument `I = S.insts[ev.ii]` exists with `slc > 1` or `sync` (:247) | the PAD as it is (`coreBufs.get(pad)`, null if none): **no variation, no shaping** (D3). `varBuf` (:11228) / `varVoice` (:11174) say the same |
| 12 | `ev.vk`, variation buffer `varBufs[ii:vk:pad]` missing (:249) | null: the core does not play it; the page makes the variation (`varVoice` -> `genVoice` / `mutVoice` -> `renderVoice`) and plays it (the base pad only when `varVoice` gives null: no kit voice, or `mutVoice` threw) |
| 13 | `ev.vk`, variation present, no `tw` | the variation buffer (mono, `sr` its own) |
| 14 | `ev.vk` or not, `ev.tw`, and `ev.rev` (:256) | null: the page plays it (shapes, THEN reverses: cannot be streamed) |
| 15 | `ev.tw`, not rev | `{data: base.data, data2: null, sr, tw}`: the voice's reader shapes while it plays (CUT / RES / DEC), mono |
| 16 | no `vk`, no `tw` here | not reached from `mine` (needs `vk || tw`) |

Where the variation buffers come from: made on the PAGE (`varBuf` :11225 -> `renderVoice`, JS synth engines), handed to the core as `vbuf {k: ii:vk:pad, pad, data, sr}` (`coreVar` index.html:23561, handler `core-processor.js:338` `varIn`), dropped when a pad's `buf` changes (`:337` `varDrop(pad)`) or the instruments change (:`patch`). The core never makes one. MUT keeps the pad's loudness (`varLevel`), done by the page before it hands the buffer in.

Other events in `ev()` (not notes): `fxc`/`fxreset`/`fxp` (effect nodes), `chc` (chains node switch :633), `tempo` (chains transport :638), `beat`, `stp`, `eng` (engine targets: live nodes only, :649), `tvl`/`tva`/`tpn`/`mvol`/`mout` (mix), `off`, `mod`. `chc` / `tempo` / `eng` act on nodes that exist only with chains or live engines.

## 1b. C++ `dispatch` before this change (`render.cpp:20-42`)

| # | event | what plays |
|---|---|---|
| a | `stp`, `off`, `mod` (track >= 0), mix events, `beat`, `fxc` / `fxp` (effects on) | as JS |
| b | `note`, track < 0 | nothing, no stat |
| c | instrument `mo == 1` | silent, `notesMidiOnly` (as JS row 2) |
| d | pad not 0..255 or no buffer | silent, `notesNoPad` (JS rows 3 and 10, except that JS row 3 via the core also takes the track over; the page does not) |
| e | reversed + `tw` and `!buf.noShape` | `pool_.take` (the track's voices give way), silent, `notesShapedRev` (JS row 14) |
| f | everything else | `pool_.note` with the pad's buffer: **base pad even when `SH_VK`** (rows 12-13 wrong); **a live or sf pad plays whatever buffer the host gave** (rows 4-6 undefined); `noShape` never set (row 11 wrong: a sliced / synced instrument's CUT / RES / DEC shaped) |
| g | `eng`, `chc`, `tempo`, MIDI events, anything else | dropped, not counted |

## The differences (work list)

* **D2a** `SH_VK`: base pad instead of the variation (rows 12-13).
* **D2b** live pads (row 4) and sf pads (rows 5-6) have no route: the host's buffer plays.
* **D2c** null kit entry (row 3): already silent via "no buffer"; only the stat name.
* **D3** `noShape` (row 11).
* **Others, NOT fixed here (see "Choices for the lead"):** (i) the C++ sequencer is the audio-thread core's `seq-core.js`, where `applyChop` is empty (:5), so a sliced / synced instrument's events carry no `ofs` / `durSec` / `st` chop and no stretch (the page's own sequencer, which an offline render uses, does chop: index.html:22333); the pad plays from its start, unchopped. (ii) `eng` / `chc` / `tempo` events are dropped without a count. (iii) row 3 in the core takes the track over (`take`), the page does not: the native side follows the page (no takeover), as before.
