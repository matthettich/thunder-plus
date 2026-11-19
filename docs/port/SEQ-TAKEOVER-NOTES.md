# SEQ TAKEOVER NOTES: the Song's lifetime under a running Seq (Part B, read-only first step)

Written 19 Nov 2026 by an analyst (no production code). Branch `n1/seq-takeover-notes`, from `native` at 956c377.
Brief: `docs/port/SEQ-LIVE-DESIGN.md` Part B. JS lines are `core/seq-core.js` (the generated copy of `makeSeq`; `msg()` handlers are
`core/core-processor.js` :327-356, same text at seq-core.js :1185-1213). C++ lines are `native/core/src/seq/seq.cpp` / `seq.h`.
"Not determined" means I did not find it in the files read; nothing here was run against the JS except the counts in section 3.

## 0. How the JS edit actually works (needed for everything below)

- `makeSeq(S, mode, o)` receives the SAME object as the processor's module `S` (play passes `S`). `song` does
  `Object.assign(S, d.S)` after deleting keys not in `d.S` (core-processor.js:328), `patch` writes keys / indices / `len` / `del`
  (:329-334). Arrays and sub-objects are replaced by identity, but the sequencer never holds one across a step: it reads
  `S.song[row][t]`, `S.chains[cid]`, `S.phrases[pid]`, `S.plen`, `S.insts`... at every step (e.g. :303, :332, :414, :544-545, :684).
  So everything read through `S` at step time is LIVE, at the next `step()` after the message.
- Three things are captured once and are NOT live: closures made at make time (hearT, busT, NIs, PFS, PF), and values copied into
  `api` / `tr` (bpm, ticks, positions).
- Module-level helpers used by the sequencer (`busPlan`, `ntr()`, `polyFrom()`/`isPolyT`, `tgtOwn`/`tgtFx` defaults, `flxCap`,
  `tgtEvs`' `S.mix`) read the MODULE `S`, not makeSeq's parameter. After a `song` / `patch` they are the same object. After a
  `load` (:327, `S = d.S`) they are NOT: the running sequencer keeps the old object while those helpers see the new one. Whether
  the page can send `load` with a sequencer running: not determined (the handler does not stop it). Treat `load` as stop + PLAY.
- `busPlanH = null; busPlanVal = null` (:328, :334, :327). `busPlan()` (seq-core.js:126) is called only from `makeSeq` at make time
  (:256 hearT, :258 busT via `isBusT`, :180). Nothing calls it after make. So the reset only forces the NEXT makeSeq (the next
  PLAY) to recompute; it has NO effect on a running sequencer. The comment at :251-252 states it as the owner's rule: "a track that
  becomes a bus while it plays keeps its notes until PLAY starts again".
- `keepObj(perf)` (core-processor.js:40-44; seq-core.js:898): `PFS = perfOf(S)` is taken once (:265) and holds the perf OBJECT.
  `keepObj(old, now)` copies `now`'s keys INTO `old` (shallow `Object.assign`, deleting keys missing in `now`) and returns `old`, so
  `S.perf` keeps its identity and `PFS.sc / mac / lfo / lk / xa / xb / xf` are the new values (but see D6: when the new song has NO
  perf, `keepObj` returns `now` = undefined, `S.perf` becomes undefined, `perfOf(S)` later makes a NEW freshPerf() and `PFS` is an
  orphan: `perfResolve` reads the fresh empty perf, while `PFS.lfo` / `PFS.lk` keep reading the old one. `patch` op `[perf,'del']`
  does the same, :331).
- Edits do not reach events already produced: the sequencer runs ahead of the playhead (lead 0.25 s default, up to 1.2 s hidden).
- **A correction to Part B's gate text:** "bpm in the song changed" does nothing in the JS. `api.bpm` is set from `S.bpm` ONCE
  (:262) and changed only by the `bpm` message (core-processor.js:354 -> `seq.bpm = v`), a TPO command, or by the page. The sequencer
  never reads `S.bpm` again (grep: :262 is its only read inside makeSeq; the processor's own `S.bpm` reads at :1448, :1489, :1535 are
  for effect sync when no sequencer runs). The test must send the `bpm` op to move the tempo; an edit of `S.bpm` alone must produce
  NO change in the JS (a good negative case).
- **Second correction:** PERFORM scenes / macros are only resolved at `perfDiff`, which runs at the first step (:468) and at an
  XFD / MAC / PRT command (:620-622), not every step. An edited scene is heard at the next perfDiff, not at the next step. LFOs
  (`PFS.lfo`, :526) and locks (`PFS.lk`, :623) ARE read every step / row.

## 1. The make-time table

Columns: value, JS line, C++ holder (line), and whether an in-place `song` / `patch` edit reaches the RUNNING JS sequencer.
"Live" = read from `S` at every use. C++ column: all song-derived tables are built in the constructor from `const Song& S_`, so a
successor built from the new song is "rebuilt at publish".

| Value | JS | C++ member (built at) | Reaches the running JS seq? |
|---|---|---|---|
| `once`, `o.*` opts, `wakeOk` | :250, :375 | `once_`, `o_`, `wakeOk_`, `PL_` (ctor :375, :388, :404) | not S-derived: n/a |
| `loop` | :250, :366 `api.setLoop` | `hasLoop_`, `loopY0_/Y1_` (setLoop :518) | run state, changed only by the `loop` op (core-processor.js:353) |
| `hearT` (which tracks' idle LFOs run; mode != song or `o.only`) | :255-257, uses `busPlan().targets` at make | `hasHear_`, `hearT_[33]` (buildBus :322-325) | NO. Stale until PLAY |
| `busT` (bus tracks play commands, not notes) | :258 via `isBusT` | `busT_[32]` (buildBus :293-321) | NO. Stale until PLAY (owner's rule, :251). `busPlanH = null` changes nothing here, see section 0 |
| `api.bpm`, `api.tks`, `api.grv`, `api.lpb` | :262, :464-466, :499, :598 | `bpm_`, `tks_`, `hasGrv_/grv_`, `hasLpb_/lpb_` (ctor :387) | NO for bpm / tks initial values (set once). `S.ticks` is re-read live only by a `TIC 00` command (:548); `S.lpb`, `S.dlen` live (below) |
| `PL` (live and not sim) | :265 | `PL_` | not S-derived |
| `PFS = perfOf(S)` (the object) | :265 | `scene_`, `macros_`, `lk_[33][4]`, `lfo_[33]`, `lfoIsArr_`, `ids_` (buildPerf :233-263) | YES for `sc`, `xa`, `xb`, `mac[i].tg`, `lk`, `lfo` (object kept by `keepObj`; read at perfDiff / each row / each step). PARTLY: orphaned if perf is deleted (D6). `perfOf(S)` creates `freshPerf()` at make when absent |
| `PF.xf`, `PF.mac[]` (non-live copy) | :267 `{xf:PFS.xf, mac:PFS.mac.map(m=>m.v)}` | `pfXf_`, `pfMac_`, `pfMacSet_` (:258) | NO. Copied at make; an edit of `perf.xf` or `perf.mac[i].v` is not seen (only a macro index beyond the copy's length reads `m.v` live, :213 `st.mac[i]==null`). PL: `PF` is the shared module `perfLive` (xf/mac set by the `perf` op :355, fill too) |
| `PF.sent`, `PF.pno`, `PF.part` | :267, :266 | `sentHas_/sentVal_/sentOrd_/sentN_`, `pnoHas_/pno_`, `pfPart_` | run state (see section 2) |
| `perfFirst` | :268 | `perfFirst_` | run state: the first step's `perfDiff` sends the scenes/macros once |
| `instsNow()` / `IS` | :272, :684 | `sInsts_[64]`, `parts_[].insts` (buildParts :278-290) | YES (live `S.insts`, `S.parts[PF.part].insts`; fallback `IS[ii]||IS[0]||S.insts[ii]||S.insts[0]`) |
| `partSnap()` / `partEvs()` (PRT 00 and PRT n) | :237-248, :622 | `own_.evs`, `parts_[].evs` (:281-289) | YES (computed at the PRT command from the live S) |
| `NIs = niOf(S)` | :273 | `NIs_` (ctor :378) | NO. Clamps `INS` only. Stale until PLAY (D2) |
| `lenOf`, `emptyLen`, `plpbOf` | :274, :277, :186 | `S_.plen/dlen/plpb/lpb` read in `lenOf` etc. (:413-419) | YES (`S.plen`, `S.dlen`, `S.plpb`, `S.lpb`) |
| seeds, `seeded`, `gscl`, `sseed`, `pseed`, `mtch` | :279, :292 | `isSeeded_/seeded_`, `hasGscl_`..., `hasSseed_/sseed_`, `hasPseed_/pseed_`, `mtch_` | run state. `pseed` is drawn lazily at the first roll (:299), so it is not a make-time value |
| `rvs` (chance rolls per track and place) | :294-301 | `rv_` + `cellSlot_` (buildCounters :327-332) | run state, KEPT FOR THE WHOLE PLAY even where the cell / row is later cleared (Maps only grow) |
| `T.nth` (NTH), `T.c1` (CND 1ST) Maps | :625, :651 | `nthCnt_/nthGen_/c1Cnt_` + `rowSlot_` (:334-338) | run state; `T.nth` is replaced by `fresh()` (JMP, wake, hand-over: C++ bumps `trGen_`); `T.c1` is not in `fresh()`'s list, so it is never reset (C++ `c1Cnt_`, no generation) |
| `sectId`, `settle`, `offInSong`, `r0` (the row), `wake` | :303-346, :414, :376 | `S_.cell`, `S_.chain`, `S_.phrase` (seq.cpp :188-190, :412, :426) | YES (all read `S.song / chains / phrases` live) |
| `mutedAt` (smute, master smuteM) | :413 `blockMuted(S,..)`, `masterBlockMuted(S,..)` | `S_.smute` (live in `mutedAt` :423) and `smuteM_`/`hasSmuteM_` (ctor :380) | YES |
| `ntr`, poly/mono split `polyFrom()` | :180-185, :347, :381, :631, :671, :793 `S.ntr`, `S.nmono` | `ntr_`, `polyFrom_` (ctor :376-377) | YES (`isPolyT` calls `ntr()` every time; `wake` and the idle-LFO test read `S.ntr`). Already-playing tracks are never switched off by a lower `ntr` |
| `flxCap` / `S.flxOld` | :182 | `flxOld_` (ctor :379) | YES |
| FX needs (`FX_NEEDS`, `S.tfx` slots for FLX/GAT/RTG wake) | :482 `slotWake` | `need_[33][8]` (ctor :381-382) | YES (reads `S.tfx[t]` live) |
| `swingOf()` | :443 | `S_.swing/swingB` read in `step` (:649) | YES |
| `S.tune`, `S.scale`, `S.sroot`, `S.limiter`... | :700, :696 | `S_.tune`, `S_.scale` (:802, :809) | YES |
| `vtrk`, `NSLOT`, `TFX` | module vars, `vars` op (:335) / `load` | `const SeqVars& V_` (a reference to the snapshot's vars) | YES (module vars read at use) |
| `tgtOwn` / `tgtFx` (target's own value) | :202-211, used in perfDiff, tgtEvs, lfoEvs, engEmit | `TgtId::own`, `fxOk` (intern :230; `tgtOwnOf` :207) | YES (computed at each use from live `S.mix`, `S.mfx`, `S.mout`, `S.tfx`) |
| chase of seeds / TCH / SKP / CHD (`chaseChance`) | :424-436 at make (row > 0) and at JMP (:462) | `chase_`, `chaseHasS_`, `chaseS_`, `chaseM_` (buildChase :351-367, one table for every row) | YES (a JMP walks the S as it is then) |
| `o.at` hand-over start | :354-364 | ctor :395-403 | make only |
| the song's start rows `tr[t].row` etc. | :347-349 | ctor :389-393 | make only (run state afterwards) |
| `T.fxs`, `T.scl`, `T.lk`, CUT.. state | in `tr` | `Tr` second half (:176-180) | run state |
| `arpOf` pairs | per step | `arpPool_[33*96*2]`, `arpUsed_` | scratch, valid until the next step |

Rows where C++ "rebuilt" is already equal to the live JS, at the swap, are the YES rows (the successor is built from the post-edit
song). The rows that matter are the NO rows and the run-state rows: section 5.

## 2. The run-state inventory (what `takeOver(old)` must do)

Sizes: `sizeof(Seq::Tr)` = **728 bytes** (measured by compiling a copy of the struct, seq.cpp:169-181 with FXC_N = 81), 33 tracks
= 24,024 B. All `Tr` members are ints / bools / doubles / fixed arrays: plain data, trivially copyable.

| Member | Size | Plain? | takeOver | Index-derived from the song? |
|---|---|---|---|---|
| `tr_` (33 x `Tr`) | 24,024 B (heap, `unique_ptr<Tr[]>`) | yes | COPY (or pointer move: it is not song-sized) | `row`, `cr`, `step`, `wr`, `r0`: POSITIONS in the old song (dangling on edit, section 4). `inst`: instrument slot. **`lk[4]` + `hasLk`: indices into `ids_`** (the `intern` order) -> must be remapped / the ids table must be stable. `fxsS/fxsP`: slot numbers (copied, never dereferenced). `rnl[81]/rnlHas`: keyed by command number (stable). `eng/engC/engO`: what the player has (copy) |
| `time_, base_, bt_, btT_, hasBase_, hasBtT_` | 40 + 2 B | yes | COPY | no |
| `bpm_, tks_, hasGrv_, grv_[2], hasLpb_, lpb_` | ~32 B | yes | COPY (`bpm_` as the running value; the new song's `bpm` is NOT applied, as the JS) | no |
| `mrand_` (Mulberry32 `s`, 4 B), `seeded_`, `isSeeded_` | 9 B | yes | COPY the state (`mrand_` supplies `Math.random`: lazy `pseed_` draw, rolls) | no |
| `hasSseed_, sseed_, hasPseed_, pseed_, mtch_, hasGscl_, gsclScale_, gsclRoot_` | ~32 B | yes | COPY | no |
| `hasLoop_, loopY0_, loopY1_` | 12 B | yes | COPY (a ctor loop comes from `opts`; the page's later `loop` op must survive) | rows / chain rows / steps of the OLD song; clamped by `setLoop` to the mode's range, no out-of-range |
| `pfXf_, pfFill_, pfPart_, perfFirst_` | 20 B | yes | COPY (**`perfFirst_` must be copied as false once stepped**, else the successor re-sends every scene and macro at its first step: extra events the JS never makes) | `pfPart_` = index into `S.parts`: stable by number; `instOf` / PRT test the bound (:510, :732) |
| `pfMac_`, `pfMacSet_` | `vector<double>` + `vector<uint8_t>`, size max(8, macros), 72 B at 8 | no (heap) | PREALLOCATE in the ctor (size from the NEW perf); copy `min(old, new)` elementwise; see D3 for the unset entries | index = macro number (user-visible, stable) |
| `pnoHas_[33][4], pno_[33][4]` | 132 + 1,056 B | yes | COPY | track x kind (cut/res/dec/tch), not ids |
| `sentHas_, sentVal_, sentOrd_, sentN_` | 13 N + 4 B (N = ids) | no (heap vectors) | REMAP or keep ids stable (see below); ORDER of `sentOrd_` must be kept (it is the Map's insertion order: removal events at :551 follow it) | **indexed by `ids_` index**: depends on the song's perf content |
| `wantHas_, wantVal_, wantOrd_, wantN_` | 13 N | no | NOT carried: scratch of `perfDiff`; zero in the successor (it clears itself from `wantOrd_` at the start) but sized to the successor's N | by `ids_` index |
| `trGen_[33]` | 132 B | yes | COPY, **together with `nthGen_`** (a counter is valid only while `nthGen_[i] == trGen_[t]`, nthSlot :346) | no |
| `rv_` | n x 2,048 B (cells) or 67,584 B | no (heap) | MOVE (pointer) if the layout is stable (section 3) | song mode: slot n = scan order of non-empty cells: **depends on the song** |
| `nthCnt_, nthGen_, c1Cnt_` | 3 x 33 x nthK x 4 B | no | MOVE if stable layout, else remap (section 3) | stride `nthK_` and slot numbers depend on the whole song. **A blind copy or pointer move into a successor with another `nthK_` indexes out of range or onto another row's counter** |
| `arpPool_`, `arpUsed_` | 50,688 B | scratch | NOT carried; allocate per successor (control thread). The consumer must copy `arp()` pairs inside `onStep` (the pointer is into the predecessor's pool, which the control thread frees): not determined how the player reads it |
| `badSets_, dropped_, maxEvs_, maxArp_` | 24 B | yes | COPY (statistics; the tests assert `dropped_ == 0`) | no |
| everything else (`S_`, `V_`, `mode_`, `o_`, `once_`, `wakeOk_`, `PL_`, `ntr_`, `polyFrom_`, `NIs_`, `flxOld_`, `smuteM_`, `need_`, `busT_`, `hearT_`, `ids_`, `scene_`, `macros_`, `lk_`, `lfo_`, `canon_`, `parts_`, `own_`, `sInsts_`, `cellSlot_`, `rowSlot_`, `nthK_`, `rvN_`, `chase*`) | constructed | no | REBUILT by the successor's ctor, except `busT_`, `hearT_`, `hasHear_`, `NIs_` which D1/D2 say to COPY | |

Hazards that are not in the table:
- `SnapSlot::adopt()` (snap.h:25) pops in a loop: `cur_` may be replaced several times in ONE call (A -> B -> C). `takeOver` must run for
  every pair inside the loop (B never stepped but must hold A's state when C takes over), or the design must say it chains
  through the intermediate. The slot is a generic template; the hook needs a policy or `T::takeOver`.
- The Part A `SeqRunner` holds a `Seq*`; it must be re-pointed on adopt.
- A snapshot with the SAME song but new vars (`vars` op, :335) needs its own publish: `SongSnap { Song song; ... }` owns the Song by
  value, so a vars-only message would have to re-parse or copy the Song. Suggest `std::shared_ptr<const Song>` in `SongSnap`.
- `o_.lx` (live transpose): the JS reads `lxp.st` live (:696); C++ has only the constant `o_.lx`. The successor is built with the
  host's current value; a change of `lxp` between edits is not seen by the C++ at all today (not Part B's, but it is a live-state gap).

### `ids_` and the sent-targets table (alternative to the lead's sorted-hash match)
`ids_` is built by `intern` in scan order (scene keys of A then B, macro targets, locks, LFOs, buildPerf :241-256). A target id that
leaves the song's perf while the sequencer keeps its `sent` value is NOT forgotten by the JS: the next `perfDiff` finds it in
`PF.sent` but not in `want` and emits the "back to own" event (:271). A successor whose `ids_` lacks the id cannot emit it. So the
matching table alone is not enough. Recommendation: an APPEND-ONLY id registry kept by the control thread for the whole PLAY
(copies of the strings), from which every successor builds `ids_` as `registry prefix + new ids`. Indices then never change, so
`sent*`, `Tr::lk` and `pno` copy straight across (vectors grown, O(N), no hashing) and a removed target is still emitted. Cap the
registry (say 4096) and fall back to a fresh state with a counted event if exceeded.

## 3. Counters

### 3.1 Today's layout
- `rv_` (song mode): `cellSlot_` is 256 x 33 int32 (33,792 B); a cell (row, track) with `S_.cell >= 0` gets a dense slot `n` in row-major
  scan order (:329); `rvN_ = n * 512`; a roll reads `rv_[cs * 512 + cr * 32 + step]` (:480). Sect / phrase mode: `rvN_ = 33 * 512` and the
  index is `t * 512 + cr * 32 + step` (:331, :481): **already position-keyed and independent of the song**.
- `rowSlot_`: one int32 per stored phrase row (`phraseData.size() / 12`); rows with NTH / CND / REP in any command column get a dense
  `nthK_` slot in `phraseData` order (:334-335). `nthCnt_`, `nthGen_`, `c1Cnt_`: `33 * max(1, nthK_)` u32 each, index `t * nthK_ + slot`
  (:344). JS key is `pid + ':' + step` per track (:625, :651); NTH and CND 1ST share a slot, `c1Cnt_` is never reset, `nthCnt_` resets
  with the track's generation (`fresh()`).
- `cellSlot_` / `rowSlot_` themselves are song-derived tables (immutable once built).

### 3.2 Bytes (arithmetic from the counts; the counts were read by `node` over the song JSON, the measurement program covered the copy / alloc cost only)

| Song | cells | rv_ (song mode) | nthK | nth x3 | cellSlot_ + rowSlot_ | Total |
|---|---:|---:|---:|---:|---:|---:|
| `sounds/ELECTRO.thunder` (11 tracks, 56 rows, 269 phrases) | 616 | 1,261,568 | 0 | 396 | 33,792 + 34,432 | 1,330,188 |
| ELECTRO + `withCommands` (the `electro-cmds` golden) | 616 | 1,261,568 | 27 | 10,692 | 33,792 + 34,432 | 1,340,484 |
| any song, sect or phrase mode | - | 67,584 | as song | as song | rowSlot_ | ~0.1 MB |
| worst the format allows today (every cell with a chain, every one of 4096 phrases x 32 rows holding NTH) | 8,448 | 17,301,504 | 131,072 | 51,904,512 | 33,792 + 524,288 | 69.8 MB |

Only ELECTRO exists in the repo (`find`: one `.thunder`). `native/tests/CENSUS.md` lists a second song, the owner's `acid.thunder`
(build 589), "not committed" and carrying no cell or NTH counts: its bytes are **not determined**. Per-song formula: `rv_ = cells x
2,048 B`; `nth = 396 B x max(1, nthK)`.

Position-keyed layout fixed by format and mode (the lead's proposal):
- `rv_` song mode: 256 x 33 x 16 x 32 = 4,325,376 u32 = **17,301,504 B for every song** (13.7 x ELECTRO's 1,261,568); sect / phrase: 67,584 B (as today).
- `nthCnt_ / nthGen_ / c1Cnt_` keyed by (track, phrase id, step): 33 x 4096 x 32 x 4 B x 3 = **51,904,512 B** for every song (39,000 x ELECTRO's
  396 B). **A fixed layout is a non-starter for the NTH / CND counters.** It is acceptable for `rv_` only if ~17 MB per PLAY is fine on
  the phone target (the phone's budget is not stated in the files I read: not determined).

Measured here (one small program, `g++ -O2`, this container; 3 runs; cold first run): `memcpy` of 17.3 MB = 12.4 / 5.0 / 2.0 ms; `calloc` of
17.3 MB = 0.05-1.7 ms (lazy zero pages, resident only when touched). A 128-frame block at 48 kHz is 2.67 ms. So **copying
the worst-case `rv_` in `takeOver` does not fit a block**; a pointer swap does. ELECTRO's 1.26 MB copy would be roughly 0.15-1 ms
(extrapolated from those numbers, not measured).

### 3.3 Recommendation
Not a monolithic fixed layout and not a per-edit remap of the data. Three small control-side registries, all APPEND-ONLY for the
life of one PLAY, committed only when `publish()` returns true (the slot is FIFO and a published snapshot is always adopted, so the
control thread's shadow equals what the audio side holds, even through the multi-adopt of section 2):

1. **`rv_` as a page table:** fixed table of 256 x 33 page pointers (67,584 B on 64-bit) in song mode; one 2,048 B page per (row, track)
   cell that held a chain at any time in this PLAY. A successor allocates pages for cells newly non-empty (control thread), `takeOver`
   swaps the pointer table (O(1)) or copies 67 KB (~5-20 us). Memory: ELECTRO = today's 1.26 MB + 67 KB; worst = 17.3 MB + 67 KB (same
   as the fixed layout, but only paid by songs that need it). Pages are never freed during a PLAY, which is what the JS does (its `rvs`
   Maps only grow): a cell cleared and refilled keeps its roll counts. `roll()` becomes `page ? page[cr*32+step] : dropped`.
   Sect / phrase mode: unchanged (`rv_` already song-independent): move the pointer.
2. **nth / c1 as a registry of (phrase id, step) -> slot,** slots never reused, counters stored in chunks of 256 slots (3 x 33 x 256 x 4 B
   = 101,376 B a chunk; ELECTRO and the cmds variant: 1 chunk, 101 KB; the 27 NTH rows need 27 slots). A new NTH row takes the next
   slot; a row that loses its NTH keeps its slot and its count, so removing and re-adding NTH behaves as the JS Map does. `nthSlot`
   indexes `chunk[slot >> 8][t * 256 + (slot & 255)]`: the stride no longer depends on the song. `takeOver` moves the chunk table.
   Worst (131,072 rows) = 512 chunks = 51.9 MB, but only for a song nobody has.
3. **ids_** append-only (section 2).

Rejected alternatives, with the numbers: (a) remap table old -> new slot applied in `takeOver`: ELECTRO 1.26 MB of counter data to copy per
edit (fits chunked), worst 17.3 MB (does not fit one block; two copies resident), and it forgets counters of cells the new song no
longer has, which the JS keeps; (b) fixed layout: 17.3 MB + 51.9 MB per PLAY.

## 4. Dangling positions

Setup: a track is at `Tr{row, cr, step, cnt, g...}` on the old song and the successor reads the new Song. C++ guards on every read:
`songCell` (row in 0..255, :188), `chain()` / `chainRow` (cr in 0..15, :189-190), `phrase()` (id in 0..4095, song.h), `rowOf` (step < 32,
:429), `lenOf` (id range, :413), `nthSlot` (:342), `roll` (:479), `cellSlot_` (:480). No `Tr` field is used as an unchecked array
index, except through the counters (section 3: only if their layout moves blindly) and `ids_` (`lk[]`, `sent*`: only if ids shift).

| Edit under the playhead | JS next `step()` | C++ `step()` on the new Song with the same `Tr` | Out-of-range? |
|---|---|---|---|
| **Song cell cleared** (`S.song[row][t] = -1`, track mid-block) | `sectId` -> -1 (:303), `cid<0`, `chRow = null`, `pid = -1` (:544-545); `r0(T,-1)` -> `ph` null -> no row (:414); `line` from `S.lpb` (`plpbOf(S,-1)`, :186); no events except LFOs / beat; `L = emptyLen()` (:748) = `S.dlen`; `T.step++`; at `step >= dlen`: `T.cr++`, `settle` sees `sectId < 0` -> not `once`, no loop -> `T.on = false; T.wr = row + 1` (:329); the track waits for `wake` (:384) or the song wrap (:779-783). So it ticks an empty pattern to the end of `dlen` first | `sectId` -> -1 (:412), `pidOf` -> -1 (:658-659), `rowOf` null (`phrase(-1)` is nullptr), `plpbOf(-1)` guarded (:415-419), `L = emptyLen()` (:867), `settle` identical (:431-457: `wr = min(row,256)+1`). Same | none |
| Cell cleared AHEAD of the playhead | read at settle time (:332): the track stops / wraps at that row | same (live read of the new Song) | none |
| **Chain row removed / chain nulled** (`S.chains[cid]` shortened or null; the C++ chain is fixed at 16 rows, an absent row is phrase -1) | `c[T.cr]` undefined -> `chRow = null` -> `pid = -1`: plays an empty pattern of `emptyLen()` rows; at its end `T.cr++`, `settle`: `cRow` falsy; `T.cr > 0` so not line 337's hold; falls to `T.row++; T.cr = 0` (:342) | `chainRow` false when the chain is null or `cr >= 16` (:190); phrase -1 rows give `ph < 0` -> same path (:445-446, :453) | none |
| **Phrase shortened below the step** (`S.plen[p] < T.step`) | the row is still read: `r0` returns `ph[T.step]` regardless of `L` (:414) so ONE more row plays; then `T.step++ >= L` -> next chain row (:759-766). With DIR: `T.cnt >= L` path (:754) | `rowOf` reads `ph + step*12` while `step < 32` (:429) -> the same one more row; `L = lenOf(pid)` (:867); `T.step >= L` -> next chain row | none. `T.step` is at most 31, always inside the 32 stored rows |
| Phrase deleted (null) | `ph` null -> no row; `L = lenOf(pid)`: `S.plen[id]||16` (:274) | `phrase()` null; `lenOf` reads `plen` (always 4096 entries, :413) | none |
| **Instrument removed** (`S.insts[ii]` null, or `len` shorter; `T.inst` / `r[IC]` still = ii) | `IS[ii] || IS[0] || S.insts[ii] || S.insts[0]` (:684) -> instrument 0 plays | `Song` always holds 64 `SongInst`; a missing entry is a DEFAULT instrument (`readInst`, song.cpp:190), `instOf` returns `sInsts_[clamp(ii,0,63)]` (:513). So the default sound, not instrument 0: **differs (D9)** | none |
| **`ntr` reduced** | the main loop never tests `ntr`: tracks already `on` keep playing (:542-543). `wake` (:381) and the idle-LFO test (:793) use the new `S.ntr`; `isPolyT` (:631, :671) changes at once (the mono / poly split moves, so columns 1-3 of some tracks start or stop playing) | `ntr_` / `polyFrom_` rebuilt from the new Song (:376-377): same at the swap. `tr_` always has 33 entries | none |
| **tfx slot removed / changed** | `slotWake` reads `S.tfx[t]` live (:482): no event for the missing slot. `T.fxs` (FXS) keeps its `{s,p}` and `FXV` still emits `fxp` for it (:656), which the player ignores. Perf targets: `tgtFx` null -> `tgtEvs` returns [] (:226) | `need_` rebuilt (:381-382); `tgtOwnOf` / `fxOk` rebuilt (:211-223, :539); `FXV` emits with the stored `fxsS/fxsP` (:763). Same | none (`fxsS` is never an index) |
| `S.song[row]` itself missing (rows cut with `len`) | `S.song[T.row][t]` throws a TypeError (:303) | `songCell` returns -1 | none; C++ is better |
| A row's `smute` changed | live (:413) | `S_.smute` (:423) | none |
| A perf target removed after it was sent | next `perfDiff` emits "own" for it and forgets it (:271) | needs the old id in the new `ids_`: **only with the append-only registry (section 2)**; otherwise silently kept in `sent*` out of range of the new `ids_` | **yes, if `sent*` is copied without a remap** |
| Counters of a cleared cell / removed NTH row | kept by the JS | kept with page table / registry; lost with a remap | yes (wrong values), not a memory error |

## 5. "Rebuilt differs from the JS"

Where a successor built from the new song behaves differently from the JS's in-place edit. For each: what is better for the
user, and a recommendation for the lead. (The lead rules.)

| # | Difference | Better for the user | Recommendation |
|---|---|---|---|
| D1 | `busT_` / `hearT_` / `hasHear_`: JS keeps the PLAY's set (owner's rule :251); a rebuilt one switches a track's notes off at once when it becomes a bus (and changes which idle LFOs run) | the owner's stated behaviour is the JS's; a bus appearing mid-play dropping notes is a surprise | **COPY from the predecessor** (33 + 33 bools). The first Seq of a PLAY builds them; successors never |
| D2 | `NIs_`: JS clamp of `INS` is the PLAY's value (:273), rebuilt = the song's current `ni` | indifferent (it only clamps an `INS` command) | COPY (bit-exact to the JS, costs 4 bytes) |
| D3 | Macro resting values: JS non-live `PF.mac` is a copy made at make (:267); C++ reads `macros_[i].v` of the song whenever `pfMacSet_[i] == 0` (:549). A successor reads the NEW song's `m.v`, so an edit of a macro's value IS heard, where the JS ignores it. (Live mode: the page sends the value with the `perf` op, `setMac`) | the JS's effect is a quirk; but exactness against the JS is the gate | `takeOver` writes, for every macro `i < old size`: `pfMac_[i] = old.pfMacSet_[i] ? old.pfMac_[i] : old.macros_[i].v; pfMacSet_[i] = 1`, i.e. the effective value in force. Macros beyond the old count stay unset (JS reads `m.v` there too) |
| D4 | `perfFirst_`: a ctor-fresh successor would call `perfDiff` at its first step and re-send every scene / macro target | no | COPY it (false once stepped). No automatic `perfDiff` at takeOver: a changed scene is heard at the next XFD / MAC / PRT, as the JS |
| D5 | `sent` targets removed from the perf: JS restores "own" at the next `perfDiff` (:271) | the JS's | append-only `ids_` registry (section 2) |
| D6 | Perf DELETED (`song` without `perf`, or `[perf,'del']`): JS orphans `PFS`: LFOs and locks of the OLD perf keep playing while scenes / macros come from a fresh empty perf (:197, :213, :526, :623) | rebuilt (no perf, no LFOs) is what the user did | keep REBUILT; record as a deliberate difference; the gate must not test it against the JS (or compare to a corrected JS: deleting perf then ends the LFOs) |
| D7 | `bpm` / `ticks` / `grv` / `lpb` in the song: JS ignores `S.bpm` after make | the JS's (the page sends the `bpm` op) | COPY `bpm_`, `tks_`, groove, `lpb_` from the old Seq; test a negative case |
| D8 | Counters of cleared cells / removed NTH rows (section 3) | the JS's | page table + chunk registry |
| D9 | Missing instrument: JS falls back to `IS[0]`, rebuilt gives a default `SongInst` (song.cpp:190) | JS fallback (the instrument the track used to share with 0 vs a silent default) | either teach `instOf` to use instrument 0 when the instrument is `null` in the document (Song::load knows `J.at(i)` is not an object), or accept. Whether the page can leave `S.insts[ii]` undefined or shorten the array in place: not determined; low priority |
| D10 | A `song` row missing: JS throws, C++ treats the cell as empty | C++ | keep |
| D11 | `chase_` table walk at publish (control thread): worst 256 x 33 x 16 x 32 rows of 6 command checks per edit (4.3 M visits); ELECTRO ~616 cells. Latency of one edit; not measured | n/a | measure before building; if slow, build it lazily on the first JMP or only when the song has SED / TCH / SKP / CHD |
| D12 | Vars (`vars` op, `V_`): JS reads module `TFX` live. A snapshot per vars message | the same | `shared_ptr<const Song>` in `SongSnap` |
| D13 | `load` while playing: JS runs on the old `S` with new module helpers (section 0) | n/a | treat as stop + PLAY; no takeOver |
| D14 | `lx` (live transpose): JS live, C++ constant | JS | out of Part B; pass the host's value to each successor |
| D15 | A successor adopted without having stepped (multi-adopt, section 2) | n/a | chain takeOver through each |

## 6. Proposed tests for the Part B gate (`tools/seq-golden.mjs --edit`)

Harness: one case = a song (ELECTRO or a variant made by an `edit` function like `withCommands`), a mode, and a list of edits
`{after: N, op: 'song' | 'patch' | 'bpm' | 'loop', ...}`. The JS side applies each edit with the SAME statements as the handler
(core-processor.js:328, :329-334; seq-core.js:1186-1192), ideally by driving `new Processor().msg(...)` inside the existing `vm`
harness (whether the processor is constructible in the vm: not determined; else copy the handler text and check by a unit test that it
equals the source). C++ side: a successor built from the edited song, `takeOver`, same step. Compare event lists, `pos`, and the
clock (`time, base, bt, btT, bpm`) per step exactly, like the live goldens.

ELECTRO facts for choosing steps: 131 bpm, `lpb` 8, `dlen` 32, every phrase `plen` 32, 56 song rows (0-55) of 32 steps each (1792 steps:
step n is song row `n >> 5`, phrase step `n & 31`), 11 tracks with cells in row 0 (0-9 and the master), each chain has one row, `perf` /
`parts` absent, `smute` present, `ntr` 16, 8 tfx slots per track. So variants are needed: **V-chain** (chains with 2-3 rows), **V-perf**
(perf with 2 scenes xa/xb, 2 macros with targets, one LFO, one lock, parts[0] with insts; XFD / MAC / PRT rows), **V-fx** (FLX / GAT /
RTG / FXV / FXO commands over `withCommands`), **V-seed** (SED, TCH, SKP, CHD, JMP, PRB, CHA, NTH, CND 1ST rows), **V-plen** (plen 16).

| # | Case | Song, step, edit | What it proves |
|---|---|---|---|
| T0 | identity | each of the 4 plain goldens + 4 live goldens: `takeOver` to an identical rebuilt song at EVERY step (1792 each) | bit-identical to the existing goldens |
| T1 | note ahead | ELECTRO, after 40: change a note in the phrase the track reaches at row 2 | live read, equal events |
| T2 | note under the playhead | after 40: edit row 41 of the current phrase (the step right after) | no miss |
| T3 | chain row changed | V-chain, after 70: chain[1] phrase / tsp changed; and chain[0].tsp +7 after 100 | transpose read each step; chain row switch |
| T4 | song cell cleared under the playhead | after 100 (row 3, step 4): `song[3][0] = -1`; and the whole row 3 | empty-pattern tick to `dlen`, `wr`, `wake`, song wrap (:779) |
| T5 | cell cleared ahead and refilled | after 100 clear (10, 2); after 300 refill | rvs / counters kept, wake |
| T6 | chain nulled / shortened under the playhead | V-chain on `cr = 1`: `chains[c] = null` and `[chains,'len',1]` | settle path `T.row++` |
| T7 | phrase shortened below the step | V-plen after 100 (phrase step 4): `plen[p] = 2`; after 111 `plen[p] = 1`; `phrases[p] = null` | one more row plays, then advance; DIR variant |
| T8 | phrase lengthened | V-plen: `plen 16 -> 32` mid-phrase | `L` read live |
| T9 | instrument removed | `insts[3] = null`; `[insts,'len',3]` | EXPECTED DIFFERENCE D9: record as a known divergence unless the lead picks IS[0] |
| T10 | `ntr` 16 -> 8 and 8 -> 16 after 50 | poly / mono split and `wake` | `polyFrom_` rebuilt = JS live |
| T11 | tfx slot removed / type changed | V-fx, after 60: `tfx[0][1] = null`; FLX / GAT / RTG / FXV rows around it | `need_`, `slotWake`, FXS stale index |
| T12 | perf scene changed | V-perf, after 40 (before an XFD row): scene value changed; the XFD / MAC / PRT row's `perfDiff` shows it, not the step before | scenes resolved at perfDiff; `perfFirst_` carried |
| T13 | perf LFO / lock changed | depth, rate, target, a new lock | read each step / row |
| T14 | perf target removed after sent | scene key removed after the first `perfDiff` | "own" restore event (D5) |
| T15 | perf deleted | `song` without `perf` | D6: record the divergence; compare with the corrected expectation |
| T16 | macro value edited | `perf.mac[i].v` changed, non-live and with `setMac` | D3: JS ignores; C++ must too |
| T17 | bpm | (a) `S.bpm` edit alone: no change in JS; (b) `bpm` op 97.3 after 40 then back; (c) with a `shift` | D7 negative case + the live ops combined with a takeOver |
| T18 | parts | PRT rows; edit `parts[0].insts` and `.mix` between | live `parts_`, `pfPart_` |
| T19 | smute | change a track's block mute and `smuteM` under the playhead | `smuteM_` |
| T20 | bus plan | `chainOn` / `chainPatch` edit making a track a bus after PLAY | D1: the JS keeps the notes; C++ copies `busT_` |
| T21 | counters: NTH renumbering | V-seed: after 200 insert an NTH row in an EARLIER phrase (the dense slot / `nthK_` stride shifts in today's layout) while a later NTH row has count 3; then remove the NTH row and add it back | registry / page table keeps counts, stride independent |
| T22 | counters: chance rolls | PRB / CHA rows; clear a cell, refill after 2 passes; `loop` {4,6} | `rv_` page kept (JS Map) |
| T23 | JMP chase after an edit | V-seed: edit a SED / TCH row, then a JMP to row 20 | `chase_` rebuilt, equal to the JS's live walk |
| T24 | seeds and random stream | V-seed with SED, RND, RNL, HUMAN: takeOver mid-stream | `mrand_`, `seeded_`, `pseed_` carried |
| T25 | modes | sect mode and phrase mode (and a phrase loop): edit the phrase under the playhead | `rv_` pointer move, `o_.sect` |
| T26 | multi-adopt | 3 snapshots published before one block; step | chain takeOver A -> B -> C |
| T27 | `loop` op then an edit | `loop` {y0,y1} sent, then a song edit | loop state copied |
| T28 | edit before the first step; edit between `play` and the first block | | base unset, `perfFirst_` still true |
| T29 | ASan: old snapshot freed (and poisoned) right after adopt, then step | | no use of the predecessor's Song / Seq / arp pool |
| T30 | `rt_harness` allocation guard around `takeOver` | | zero allocations (page / chunk growth only on the control thread) |
| T31 | TSan: control publishes 1000 patches while the audio side steps (the lead's), plus 1000 vars messages | | no race; registry commit only on a successful `publish()` |
| T32 | hostile: 4,096 distinct target ids across edits | | registry cap behaviour counted, no allocation on audio |

## 7. Not determined
- Phone memory budget (decides whether the fixed 17.3 MB `rv_` is acceptable); acid.thunder's cell and NTH counts; the control-thread cost of
  `buildChase` / `buildCounters` per edit; whether the page can send `load` mid-play; whether the page can leave `S.insts[ii]` undefined; how the
  player consumes `arp()` pairs; whether `new Processor().msg()` is constructible in the `vm` harness.
