# DISPATCH NOTES: wiring step 3 (D2 routing, D3 noShape)

**IN THE MIDDLE OF: code, tests and the ELECTRO gate are DONE (pushed); the full ctest in Release and under the sanitizers is running; then the PR and CI.**

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

## 2. What exists to route TO, and the decision per route

| Route | What exists (merged) | Decision | What the renderer does now |
|---|---|---|---|
| MUT / GEN variation | The core never makes one: the PAGE renders it (`varBuf` -> `renderVoice`, JS synth engines, `varLevel`) and posts `vbuf {k: ii:vk:pad}` (`coreVar` index.html:23561; handler `core-processor.js:338`). The native side has no voice synthesis (U34c). The render C API took only pads. | **(b) new host input**: `thunder_render_set_variation(r, ii, kind 'g'/'m', num, pad, l, frames, sr)`, same style as `set_pad` (copied, validated before anything changes, 512 MiB budget shared with the pads, one-pad frame limit, `E_STATE` after the first read, `frames == 0` removes) plus a count limit (4096) | a `SH_VK` note plays the variation keyed `(ii, kind, num, pad)`; **missing: SILENT** and `notes_var_missing` (never the base pad; a host whose page would play the base pad hands in the pad's own samples) |
| live engines (`kv.live`) | `LiveShell` (U26: bass, vox, acid, mach), `xeng_live`, waves live; "NOT wired into the mixer or the sequencer". Needs per-track node management, event decoding, `poly` voice sets and the PDC latency (31 / 15 / 127 samples). | **(c) cannot be wired yet** | new `thunder_render_set_pad_route(r, pad, THUNDER_RENDER_ROUTE_LIVE)`: the pad's notes are SILENT, `notes_live`; the note still takes the track over (`pool_.take`: the page's live note takes the track over too, row 4). A host that wants the page's own safety net for a live pad (its rendered twin, "its twin's sound with the same values", index.html bassNode comment; what `song-ref.mjs` / the ELECTRO gate use) leaves the pad on the default SAMPLER route |
| SoundFont (`kv.sf`) | `SfPool` (`sf/sf_live.h`, U32 merged): note / take / off / mod / stp / render, same seam as `VoicePool`. It needs an `SfPad` {Font, Preset, PresetPcm}: a parsed font, a decoded preset and the pad's params, which the host would have to hand in (font bytes up to 2 GiB, decode budgets, bank / prog / params). No render input for any of it. | **(c) for this PR**, the piece to add next is a host input for the font (`set_pad_sf`) and a `SfPool` member rendered into `mix_.inputs` next to `pool_` | `ROUTE_SF`: SILENT, `notes_sf`, takes the track over (the core's `sfNote` does) |
| null kit entry | n/a | already silent (no buffer) | `notes_no_pad` (the page returns without taking the track over: no `take`, as before) |
| `eng` / `chc` / `tempo` / MIDI events | engine targets act on live nodes, `chc` / `tempo` on the chains node (PR #227, not merged; not depended on) | (c) | dropped as before, now COUNTED (`events_ignored`) |

## 3. D3: `noShape`

`varRec` (:247) and `varBuf` / `varVoice` look at the NOTE'S instrument `S.insts[ev.ii]`: `slc > 1` or `sync` -> the pad as it is. That is a property of the event's instrument, not of the pad (two instruments can share a pad), so it is not a kit field and the C API needs nothing new: `SongRenderer::dispatch` takes the instrument from `e.ii` (a whole index in 0..63, as the JS's array index), and for such an instrument copies the pad's `VoiceBuf` with `noShape = true` and skips the variation lookup. `VoicePool` already honoured `noShape` (`note()`, `shapedReverse()`), so a reversed + shaped note of such an instrument plays (reversed, unshaped) instead of being left to the page, as `getBuf(pad, rev)` does. `notes_unshaped` counts notes that had a MUT / GEN / CUT / RES / DEC and were left unshaped for this reason.

## 4. Tests (`native/tests/test_render_dispatch.cpp`, ctest `render_dispatch`)

DC pads (base 0.2, MUT variation 0.4, GEN variation 0.1): the level of the output says which buffer sounded, silence is exactly 0. Cases: MUT plays the variation (ratio 2 to the base); GEN its own, not the MUT of the same number; missing variation (none, wrong kind, number, instrument, pad): exact silence + `notes_var_missing`; replace / remove a variation; LIVE and SF routes (with and without a buffer): exact silence + their counts, another pad unaffected; null kit entry; MIDI-only instrument; sliced (slc 4) and synced MUT play the pad (with and without a variation handed in), slc 1 is not sliced; DEC shapes a plain instrument and not a sliced or synced one; the C API equals a direct `SongRenderer` drive bit for bit (block sizes 1, 64, 128, 333, 4096, whole) with a variation and a live route set, stats equal; hostile arguments of both new functions (null handle, every number out of range, NaN / inf, limits, 4096-variation table, the 512 MiB budget shared with the pads, E_STATE after the first read). Mutation checks: with the `noShape = true` line removed 3 checks fail; with the variation not used 5 fail.

## 5. Gates (this machine, Release GCC 13)

`render-compare.sh sounds/ELECTRO.thunder` with a reference made in this session (`THUNDER_REF=/tmp/u34a-ref`, the old reference was older than the checkout and was remade): BEFORE (binary from 33d136d) / AFTER:
plain -87.4 / -87.4 dB, `--fx` -73.0 / -73.0, `--master` -72.8 / -72.8. The six WAVs (plain, fx, master x page / core timing) are byte-identical (`cmp`) before and after: ELECTRO has no MUT / GEN and no sliced or synced instrument with shaping, so nothing there changes (the numbers are the reference's Chrome run, which is why they differ from U34a's -72.3 / -75.1).
`node tools/seq-golden.mjs --core` (headless Chrome; JS only, this change touches no JS): electro, electro-mid, electro-loop, electro-cmds all ok.
