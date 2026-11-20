# Reset track: what belongs to one track (build 650)

Owner, 19 Nov: "Can we make command+shift+t clear everything on a track the same way Renoise does?" Asked what it should remove: "I think Renoise deletes the track, which would essentially be" a full reset. The track stays in its slot (Thunder Plus has fixed track slots; other tracks do not shift).
Line numbers are `index.html` of this build (650). `NT = 32`, `MT = NT` is the master column, `ntr()` the tracks in use.

## The data model, per piece (and what a reset does)

| What | Where | New song's default | Reset does |
|---|---|---|---|
| Song cells | `S.song[y][t]`, 256 rows x (NT+1); a cell is a block number (`freshSong` 21257) | -1 | every row of the track set to -1 |
| Blocks (chains) | `S.chains[v]`: 16 slots `[phraseId, transpose]`; one pool of NPH = 4096, shared by every track and the master column; colour in `S.bcol[v]` | null | freed (`null`, colour deleted) only if no other song cell (any track, the master column) plays it; else left as is |
| Phrases | `S.phrases[id]` (rows of RW values), `S.plen[id]`, `S.plpb[id]`; NOT per track: a phrase belongs to whatever blocks reach it (`phraseTracks` 33613 counts per track; `purgePhrases` 15278 counts use by song cells -> blocks -> phrases) | null, `plen = dlen`, `plpb = 0` | freed like `purgePhrases` frees them, only if no remaining block (on the song or not) holds it; else left as is |
| Block mutes | `S.smute[y]` bit t (t < 32), `S.smuteX` (NT > 32), `setBlockMute` 21686; the master's are `S.smuteM` (not touched) | 0 | bit cleared on every row |
| Mixer strip | `S.mix[t]` = `freshMix()` (21104): vol 224, trim 128, pan 128, mute, solo, sc (always false), nm, col. There is no `out` key in this build. MONO / POLY (`S.ntr`, `S.nmono`, `S.hmono`) are song settings, not per track | `freshMix()` | all keys back to `freshMix()` (name and colour too) |
| Track effects | `S.tfx[t]` = FXSLOTS slots (`freshChainFx` 20209); a slot is `{type, on, ...params}` or a pump effect with `pump` / `pst` state, all in the song JSON | all null | all slots null; the live graph is taken down by `fxChanged(t)` (23974: `pumpSyncT`, `rtFx` -> `pl.setFx`, `PumpFX._notify`), the path every removal uses (`fxSwap` 20192 only sets the slot); `fxMem` (slot memory, session only) of the track is dropped so an effect added again starts from its defaults |
| CHN | `S.chainOn[t]` | false | false, unless the track is a bus (see below) |
| The ...chains patch | `S.chainPatch.modules`: `trackIn` / `chainOut` name the track in `params.track` (1-based), `params.sck` / `scin.src` name sidechain sources; `busPlan` 20774 derives buses and sends from it | none | NOT touched (see "Choices for the lead") |
| Sidechain Source of effects | `f.sck` on slots (`sckTrack`) of any track or the master | 0 | the track's own slots go with the slots; other tracks' slots that listen to this track are left as they are |
| PERFORM | `S.perf`: scenes `sc[i].v{id:val}`, macros `mac[i].tg[]`, locks `lk[t]`, LFOs `lfo[t]` (LFOs live on the track whose target they move, `tgtList` 21898). A target id is `vol:t`, `pan:t`, `cut:t`..., `fx:t:s:p[:b]` | none | `lk[t]`, `lfo[t]`, and every scene value, macro target, other lock slot and other LFO whose id names track t are removed; nothing about other tracks changes |
| Parts | `S.parts[i]`: `mix[t] = [vol, pan]`, `fx['t:s']` (`partSnap` 21953) | `[224, 128]`, none | this track's entry back to `[224, 128]`, its `fx` keys deleted (else a PRT would bring the old level back) |
| Per-track engine state | runtime only: voices (`vs[t]` in the player, `T(t)` in the core), LFO / TVL / TPN overrides, MIDI notes on the track | - | voices let go as STOP / the STP command lets them go (see below) |
| Track voice | `kit.voices[NPAD + t]` with a voice per track (`vtrk`) | stock | NOT touched: the kit is sound design, not the song ("New project: the kit is unchanged") |
| Instruments, MIDI settings | `S.insts` and the kit are global; the MIDI port / channel is per instrument | - | NOT touched. There are no per-track MIDI settings |
| Arrange / clips | Arrange draws `S.song`; `clip` and `sxPicks` are UI state | - | `clip` is cleared when a block was freed (as `purgePhrases`), picks on the track are dropped |
| Names, colours | `S.mix[t].nm`, `.col` | '' / 0 | cleared with the strip |

Already existing "reset" code reused: `freshMix`, `freshChainFx`, the way `purgePhrases` frees blocks and phrases, `fxChanged` for the teardown, `trackHolds` (26405) and `ntrRemap` / `perfRemap` (26476 / 26458) as the list of everything that names a track.

## Which track is "under the cursor" (`resetTrackFocus`, `focusT` 31640)
Song, and Arrange holding the keys: the song cursor's column. Sect, Phrase (Pattern view too): `cTrack`. Mixer: the cursor's strip; the MASTER row is no track. FX and MOD: `cFxT`. INST, PROJECT, the Synth page and the waves / chains screens: none. The master column: none ("The master is not a track: there is nothing to reset."). No track in focus: a toast, nothing changes. A track past `ntr()` is clamped by `focusT` as the rest of the panel does.

## Shared blocks and phrases
Counting: the song cells of every track and the master column are scanned after this track's cells are cleared; a block of the track that is still on any cell stays. A phrase of a freed block that any block still in `S.chains` holds (also a block that is not on the song: it may be unfinished work) stays. So: other tracks play exactly what they played. The one visible side effect that cannot be avoided: a song loops as long as its longest track, so if this was the only longest track the loop gets shorter.

## While playing
Page sequencer: `makeSeq` reads `S` in place at each step, and `rt.pl.play({type:'stp', track})` (22921) lets the track's voices go (also its MIDI notes, `pmNotes`). Audio-thread core: the song reaches it by the usual `coreDiff` patch, sent at once instead of after 100 ms, then a new message `{op:'stp', track}` (core-processor.js 354: voices let go as STOP lets them go, a voice booked to start later never sounds). Notes the core had already sent up to the page are page voices and are covered by the STP above.

## Not covered / limits
- Live LFO / TVL / TPN overrides of the track stay until STOP (the track is silent; nothing in the song names them any more).
- Effects' sample data (a convolution IR) is in `S.irs`, shared by the song; not touched.
- Undo restores everything above exactly: all of it is in the song JSON (`undo.cur`); the only thing not restored is `fxMem` (slot memory, a convenience).
