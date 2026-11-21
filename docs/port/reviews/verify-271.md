# Verification of PR #271 (`n1/web-follow-20nov` -> `native`)

Verifier: independent session. Branch under review at `f3dbc4b6`; web truth `beta` at `cdd9439`. Built Release, `-j1`, in a private build directory (deleted afterwards).

## VERDICT: MERGE AFTER FIXES

One real defect (F1: reset track does not stop LIVE engine voices; the author's reason for declining it is factually wrong on this branch) and one small deviation in parts (F2). Everything else in items 1, 2, 4, 5 and 6 matches the web source, and I could not break the goldens or the pin. F1 has a reproducing test (`native/tests/test_stptrack_live.cpp`, committed on `review/verify-271`, ctest `stptrack_live`) and a patch that I built and ran green (below).

## Findings

### F1. FIX BEFORE MERGE: `Players::stopTrack` leaves LIVE engine voices sounding
- Native: `native/core/src/render/players.h:36` `void stopTrack(int track, double t) { pool_.stopTrack(track, t); sfpool_.stopTrack(track, t); }`. The class has a third pool, `LivePool live_` (`players.h:60`, accessor `:50`, counted in `voices()` `:42`), added by PR #265 which this branch contains (merge-base with `origin/native` 8055034 is `origin/native` itself). `Players::dispatch` already forwards the sequencer's STP to it (`players.cpp:19` `live_.stp(e)`) and `Players::stopAll` too (`players.cpp:92`); `stopTrack` is the only per-track stop that skips it. `LivePool` has no `stopTrack` (`engine/live_pool.h:91-95`: off, mod, stp, tempo, stopAll).
- Web: `core/core-processor.js:354` and `core/seq-core.js:1229`: `case 'stp': ... for (const v of this.T(d.track)) this.kill(v, at, 0.004, true)`. A live engine's voice IS in `T(track)` (`core-processor.js:704-716`, `liveStart(ev, L, ..., T, ...)`; `T.length = 0` only for the mono take-over) and `kill()` has an explicit live branch (`core-processor.js:534-538`): `v.stopAt = at; if (t < B.lastOn - 1e-6) { gate/trig cancel(t) } G.set(0, t)`, i.e. gate low at `max(at, currentTime)`, and a note booked later is taken back. The first line of `kill` (`!v.live`) only excludes live voices from the "never started" shortcut; they get their own equivalent.
- Native has that live branch already: `LivePool::kill(Node&, double at)` (`live_pool.cpp:308-314`: `curStop/stopAt`, `cancelFrom` when `t < lastOn`, NoteOff booked).
- The author's note (`docs/port/WEB-FOLLOW-20NOV-NOTES.md`, "declined": "there is no live-engine pool in this tree") is false for this tree.
- Reproduction: `build/tests/test_stptrack_live` (ctest `stptrack_live`). A held (no release) live Bass note on track 0; energy 0.3 s after the stop: control 2787, sequencer STP 0 (the rig can hear a stop), `stopTrack(0)` **2787** (still sounding), `stopTrack(3)` 2787. Fails on the PR as is.
- Fix (verified: after it `stptrack_live`, `stptrack`, `live_pool`, `players` all pass; I did not rerun the other 200 tests with it):
```
--- a/native/core/src/engine/live_pool.h        (after `void stp(const SeqEvent& e);`)
+  void stopTrack(int track, double t);   // reset track: held live voices of the track let go at max(now, t); a note booked at or after it is taken back
--- a/native/core/src/engine/live_pool.cpp      (after LivePool::stp)
+void LivePool::stopTrack(int track, double t) {
+  if (!(track >= 0 && track < kVoiceTracks)) return;
+  const double at = std::max(now_, t == t ? t : 0.0);   // Math.max(currentTime, d.t == null ? 0 : d.t)
+  if (!std::isfinite(at)) { st_.refused++; return; }
+  for (Node* n : byTrack_[track]) if (n->inT) { kill(*n, at); n->inT = false; }
+}
--- a/native/core/src/render/players.h:36
-  void stopTrack(int track, double t) { pool_.stopTrack(track, t); sfpool_.stopTrack(track, t); }
+  void stopTrack(int track, double t) { pool_.stopTrack(track, t); sfpool_.stopTrack(track, t); live_.stopTrack(track, t); }
```
  Also update the comment ("BOTH pools" -> three) and the notes file. What `Players::stopTrack` must do for live voices: for the track, every node voice still in the track list (`inT`) gets the live `kill` at `max(now, t)`, and leaves the list; nothing else (no `stop=true` flag is needed: the live kill already takes back a later booked note).

### F2. FOLLOW-UP: a PART instrument's `lines` is truncated natively, the web uses the raw value
- Web: `index.html:23793` `const instBeats = I => (I.lines | 0) > 0 ? I.lines / (S.lpb || 4) : (I.beats || 4)`: the test is `|0`, the value is RAW. Parts are not normalised (`normParts`, `index.html:22161`, only touches instruments with `sx` / an OS key).
- Native: `seq.cpp:312` (buildParts) `x.lines = js_to_int32(jnum(I.get("lines")))` (an `int`), `instBeats` divides that int (`seq.cpp:549`). Same for song instruments is fine (the loader rounds, `index.html:21677`).
- Run in node with the pinned `instBeats`: `lines 2.9` -> web 0.725 beats, native 0.5; `lines 4294967297` -> web 1073741824.25, native 0.25 (wrap); `"8"` -> both 2; 5000 -> both 1250. The hand tests assert the native values as if they were the web's (`test_seq_chop.cpp:244`, `:247`), so they would pass while wrong.
- Impact: only a hand-written / hostile part with a fractional or > 2^31 `lines`; the page writes integers. Fix: keep a `double linesRaw` (and `lines|0 > 0` as the condition) for parts; correct the two assertions. NOTE-grade for users, FOLLOW-UP for fidelity.

### F3. FOLLOW-UP: numeric string in an `...L` field reads as 0 (author-disclosed)
- Web: `index.html:5132` `envLinesOf`: `o[k+'L'] != null ? +o[k+'L'] || 0 : msLines(...)`: `"8"` is 8, `[]` is 0, `[5]` is 5.
- Native: `engine_json.cpp:12` `lnum`: a string or array gives 0. `jsPlus(Json)` (`song/song.cpp:42`, string-to-number already implemented) is the exact `+x`; use it for non-null values and map NaN to 0. A song the page saves has numbers there (it writes them from number inputs), so this needs a hand-edited file. Fix is one line plus a test; not blocking.
- Related, NOTE: `std::min/std::max` differ from `Math.min/max` for NaN. `linesMs` can only yield NaN in the web for a saved `...L` of Infinity at lpb Infinity (`1e999` in a file); native gives 64000. Not reachable from the page.

### F4. NOTE (for U37): what the host must do with `env_lines.cpp`
It is a faithful port (below), but unreferenced. U37 must: (a) call `eng_env_lines_fit(V, &hold, song.bpm, song.lpb)` immediately before `eng_render_voice`, with `hold = isSyn ? V.len : 0` (web `index.html:5065`, `:9686`); (b) use the SONG's bpm/lpb as they are when the render starts (the web's `songBpm()`/`songLpb()`, `:5119`, `:5126`), never a tempo command's; (c) re-render the pad when the settled bpm/lpb changes: the page's `padBpm` (`:8915`) keys the cache on `bpm + lpb/100` through `lnTempo` (`:8905`, 300 ms quiet, 0.5 % tolerance while playing); (d) own the gating that `relOf` does and `eng_layer_rel_ms` / `eng_voice_grel_ms` do NOT: only `L.on && L.level > 0 && L.mode === 'ADSR'` layers count and `Math.max(1, envMs(V,'gU','gRel') || 1)` for `gMode ADSR` (`index.html:5013-5015`), and `Math.max(30, r)` for blip / x-engines.

### F5. NOTE: the live OLDSCHOOL settled tempo is not modelled
Disclosed by the author and correct: `applyChop`'s `q = osy && live && bpm === S.bpm ? stSettled() : null` (`index.html:23771`) uses a 300 ms wall-clock window; native always uses the exact tempo, as the page's offline paths do. No action.

### F6. NOTE / FOLLOW-UP: test tool hygiene (declined CodeRabbit points, see below)
`seq-golden-web.mjs:91` (and the edit and chop equivalents) writes a missing golden in compare mode and passes. Harmless in this PR (the C++ tests compare to committed files and fail on a missing one; the tool did not modify any tracked file when I ran it), but a deleted live golden would be re-created by `seq_edit_current` and still pass.

## Per-item results

**1. `applyChop` / `instBeats`: matches, except F2.**
- `instBeats`: `lines > 0 ? lines / songLpb : beats` with `songLpb = S_.lpb ? S_.lpb : 4`: identical to `index.html:23793`. It is the SONG's lpb: the web reads the global `S.lpb`; the comment at `index.html:23790` says "a phrase's own LPB and the LPB command do not change it"; native's phrase / command lpb is a different function (`plpbOf`, `seq.cpp:455-458`) that `instBeats` does not use.
- `ev.stBeats = js_round(beatsI * 1e6) / 1e6`: the web DOES round there: `index.html:23775` `beats:Math.round(beatsS * 1e6) / 1e6`. `target` uses the unrounded value on both sides. Effect on existing songs: a song instrument's `beats` is an integer 1..64 (loader clamp), so no event changes. Only a hand-written PART with a `beats` of more than 6 decimals changes, and it now agrees with the web (it did not before).
- OLDSCHOOL: `osy = sy === 3 && sx === 1` (web `:23770`); `gs = round(osGrainSecs * 1e6)/1e6` (`:23777`) with `osGrainSecs` (`:23673`) = `(ou|0) > 0 ? OS_DIVS[min(6, ou|0)] * 60 / bpm / (lpb||4) : min(250, max(0.5, +og || 12)) / 1000`; native `kDiv = {0, 1, .5, .25, .125, .0625, .03125}` equals `OS_DIVS` (`:23670`) exactly (powers of two), same operation order `((d*60)/bpm)/lpb`; `rp/x/pt = orp|0, ox|0, opt|0`, native clamps to uint8/int8 which the loader's ranges (0..3, 0..50, -24..24) already guarantee. Hits rule: web `:23780` takes equal parts when `I.smode && st.sx === 1 && st.rp` (`st` exists only for `sy >= 2`, osy implies 3); native `I.smode && !(osy && I.orp != 0)` takes hits otherwise: same.
- Existing songs byte-identical: the PR diff touches goldens `seq-edit-bus.json` (changed), `voice_golden.inc` (+1 line, 22 hashes unchanged), new `seq-chop-lines`, `-lines-edit`, `-parts`, `-sx`, `envlines-web`. `seq-chop-all`, `-rev`, `-edit`, the live-clock goldens and the 23 other edit goldens are not in `git diff --stat origin/native...`. Full ctest passes with them.

**2. `takeOver` and `busT_`: correct.**
- Web `api.rebus` (`index.html:22307`, `seq-core.js:279`) re-reads `isBusT` (= `busPlan().feeds`) into the running sequencer's `busT`; the audio-thread core calls it on EVERY `load`, `song`, `patch` message (`seq-core.js:1202-1209`), after clearing `busPlanH/busPlanVal`; `vars` and `kit` do not. `hearT` is `const`-computed once at make (`index.html:22299-22301`, no code writes it afterwards): stays the PLAY's. Native: the successor `Seq` builds `busT_` from its own song in `buildBus` (`seq.cpp:318-325`, `bus_plan(S_.chainPatch, S_.chainOn)`), `takeOver` now leaves it (`seq.cpp:433-435`) and still copies `hearT_/hasHear_` (`:435`).
- Every case: track becomes a bus / stops being one: the successor's own plan = the web's recomputed set from the next row; takeover with no chain edit: same song plan gives the same set, a no-op (the unchanged 23 edit goldens and `seq_takeover` prove it). One divergence class I did not test: a bus plan that depends on web `vars` (`vtrk`, `NSLOT`, `TFX`), which `bus_plan` does not read natively either (pre-existing, `BUS-INTO-BUS-NOTES`).
- Re-ran the web dump: `node native/tools/seq-golden-web.mjs --edit` (all 24 edit cases, `ok edit-bus: 1792 rows, 216 differ`) and `--chop` against the committed goldens: all OK, exit 0; the pin reproduced from a fresh `git archive origin/beta` is byte-identical (no diff). So `seq-edit-bus.json` is exactly what beta produces.

**3. Reset track: sampler and SoundFont pools correct, live pool missing (F1).**
- `VoicePool::stopTrack` (`voice.cpp:230`), `SfPool::stopTrack` (`sf_live.cpp:208`) mirror `seq-core.js:1229`: `at = max(currentTime, t == null ? 0 : t)`, `0 <= track < NT`, `kill(v, at, 0.004, true)` over the track list, list cleared. The sampler pool is bit-exact against the web's own core (`voice` case `stp_msg`, hash in `voice_golden.inc`).

**4. `env_lines.cpp` vs `envLinesFit`: faithful.** Operation order identical (`linesMs`, `msLines`, `envLinesOf`, `base`, `t`, `need`, `len`, `hold`), tempo = the song's (`songBpm`, `songLpb` with the `|| 120` / `|| 4`, `Math.max(1, ..)` and `Math.round`), clamps 64000 / 300, `bar = 240 / max(1, bpm)`, `hold` falsy (0, NaN) gives 0. bpm 0 / NaN gives 120, lpb 0 / NaN gives 4, negative bpm gives 1 inside `linesMs`; the 160-case golden covers bpm 0 / NaN, lpb 0.2 / 2.6. `envlines_current` re-ran: "current (160 cases, 139 with lines)". Only NaN-in-`min/max` differs (F3). What U37 must do: F4.

**5. Song loading: matches** `lines` (`int(o.lines,0) > 0 ? clamp(int,1,1024) : delete`, `index.html:21677`; native `cInt(.., +-2^30)` then clamp: negative, 0, NaN, null, huge, `"8"`, Infinity all agree), `sxLoad` (`:23642`: `trunc(+sx)`, only the `(sync 3, sx 1)` pair exists in `SY_PICK` `:23627`; `n()` maps null / '' / false to the default, non-finite to the default, clamps, rounds ints: native `sxNum` equal), `normParts` (sync `trunc || 0` clamped 0..3 then `sxLoad`, only when `'sx' in o` or an OS key is present: native `!I.get(k).missing()`). Differences: arrays (`+[]` is 0) read as the default natively (garbage input only), the `...L` strings (F3), parts' `lines` (F2).

**6. Pin and tools.** `native/tests/web-ref/BETA-COMMIT` = `cdd943949e2cb73e31dab82e9ca3127c8b4b2ad5` = current `origin/beta`; `seq-core.js` and `core-processor.js` are `cmp`-identical to `git show origin/beta:...`; re-running `web-pin.mjs` on a fresh archive of beta changes nothing in the tree (the excerpt is reproducible). Nothing is fetched at build or test time: the Node tests read local files plus `sounds/ELECTRO.thunder` from the repo; CI already has Node (`native.yml:53`). Size: `web-ref` 239 KB; goldens about 450 KB (`envlines-web.json` 157 KB, four chop goldens 50 KB each); whole PR about 3.7k lines, roughly 1.1 MB of new-side file sizes in total. Same-owner code, so no licence issue. The root snapshot (`core/seq-core.js`, `index.html`) is still old for the other dump tools (author's note 1 to the lead): a decision, not a defect.

**7. Mutations (each alone, reverted; the worktree's `native/core` is clean).**
| mutation | caught by |
|---|---|
| `instBeats` divides by a phrase / LPB-command lpb (phrase 0's `plpb` or the `LPB` command value, else the song's) | `seq_chop`: `chop-lines` and `chop-lines-edit` goldens fail (first differing row 6; 5 of 7 equal). The hand test at lpb 8 would not catch it (it changes the song lpb, not a phrase's). |
| `VoicePool::stopTrack` and `SfPool::stopTrack` with `stop = false` | `voice` (golden hash `stp_msg`) and `stptrack` (the booked note 1 ms after the stop blips). `voice_golden_current` stays green (it compares the committed hash to the web, not to native). |
| `takeOver` copies `busT_` again | `seq_takeover` (`edit-bus`, both variants fail at row 32). `seq_takeover_threads` stays green. |
| (F1) `Players::stopTrack` without the live pool | nothing in the PR: only the new `stptrack_live` |
All three planned mutations are caught, so the new tests can fail. The weak spot is F1 (no test of the third pool) and the hand-written parts assertions (F2).

**8. Full ctest, Release, `-j1`:** 204 tests, 203 passed (1 of them skipped: `pumpsynth_refused`, as on `native`), 1 failed: my new `stptrack_live` (F1). Without it 203 of 203 (the PR's own state is all green). CI on `f3dbc4b6`: Linux GCC, Linux clang, macOS universal, Windows MSVC, wasm, Android NDK, Android shell, `test`, CodeRabbit all success; Linux sanitizers still in progress when I last looked; the APK job skipped.

## CodeRabbit points the author DECLINED
1. "stopTrack misses a mono-displaced voice still booked; canceled future voices keep their slot": **right to decline.** The web handler walks the same per-track list (`T(track)`), a mono take-over empties that list while the old voice stays in `alive` (`core-processor.js:443`, `:583`), and `kill(.., true)` sets `end = t0` so the slot is released when the voice is reached (`:532`). Native mirrors it; a deliberate deviation needs the owner's ruling. Residual: native's pools have a fixed capacity (64), the web's do not, so a flood of cancelled-but-not-yet-started voices holds slots there: a follow-up, not a parity bug.
2. "Reset-track must also stop the live-engine notes": **wrong to decline** (F1; evidence above).
3. Golden harness hardening (read-only compare mode, an edit case's `differ` assertion, adding `lines` to an instrument that lacks it, multi-line `instBeats` in `web-pin.mjs`): **acceptable to defer** (F6). The compare-mode write is a real, cheap fix (fail when the golden is missing) and I would take it in the same follow-up; the others are hardening of an already green tool. Of the points the author took, I checked the portable NaN (`engine.h` now uses `std::numeric_limits`, MSVC job green) and the `core-processor.js` pin (reproduced above).

## Reproduction
```
git worktree add wt origin/review/verify-271 && cmake -S wt/native -B b -DCMAKE_BUILD_TYPE=Release && cmake --build b -j1 --target test_stptrack_live && b/tests/test_stptrack_live   # fails on the PR, passes with the F1 patch
node wt/native/tools/seq-golden-web.mjs --edit; node wt/native/tools/seq-golden-web.mjs --chop; node wt/native/tools/envlines-dump.mjs --check
```
