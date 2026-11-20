# Verification of PR #259 (build 658, envelope times in lines)

VERDICT: MERGE AFTER FIXES (one trivial rebase; one should-fix on tempo drag).

Head 4f37c68 vs origin/beta. Independent harness (/tmp/v259, loads the ENGINE block of each index.html in a vm like tools/engine-golden.js).

## 1. Old sounds bit-identical: voiceMod skip
Code read: `voiceMod` is stateless (hashRnd is a pure hash of (n, k); no RNG draw, LFO phase, S&H or follower is carried between calls), so skipping it cannot change later state. The skip tests `V.r1amt||..||V.r7amt` (truthiness); voiceMod uses `V['r'+i+'amt'] || 0`: the same test, so 0, -0, NaN, '', null, undefined, false are all "no amount" in both. Strings such as '0' or 'abc' are truthy, so voiceMod still runs in both.

Results (random voices, 1-4 layers of every data-free layer type, ~12% engine voices, pad amp on/off, mod routes with amounts 0/-0/1e-9/-1e-9/NaN/undefined/null/''/false/'0'/'abc'/tiny/real, random LFO/env/seq fields):
- 3000 voices, all-finite LFO/env/seq fields: 2999 identical; 1 hash differs (B|154) but both outputs are 100 percent NaN (routes with amount 'abc', voiceMod runs on both sides): NaN bit pattern only, not a real difference. 0 real differences.
- 3000 voices with corrupt LFO/seq fields (Sync outside 0..8, Infinity/NaN rate or depth or seqRate/seqDepth, non-array seq) plus zero amounts: 20 differ. In 19 the OLD build outputs NaN (all samples or most) because `s * dep * 0` is NaN when s or dep is non-finite (zero amount does not protect); the PR renders clean audio. In 1 (B|1064) the old output is finite but built from a NaN pitch term; the PR is the sane one. These voices cannot come from the UI (sync is a 0..8 selector, rates are clamped sliders) and were already broken; the PR does not change any voice that rendered correctly. Note, not a regression.
- Factory / presets / layer presets / layer types / engine presets / starter kit / ELECTRO.thunder (2142 voices): `ENGINE_GOLDEN=all node --test tests/engine-golden.test.js` on the PR head: pass, 0 differ from the golden hashes. Plus 858 of them re-rendered with corrupt/zero mod fields on both builds: 1 differs (a mutated corrupt-field voice, same NaN class as above; not individually inspected).
- Verdict item 1: CONFIRMED. No old voice that rendered correctly changes.

## 2. Saving and old builds
- ms mode saves nothing new: freshLayer / mkVoice are untouched; diffing the JSON keys of a plain voice on both builds: no new key. CONFIRMED.
- Lines mode saves `aU:'l'`, `atkL holdL decL relL` (and `fU`, `fAtkL fDecL`; `gU`, `gAtkL..`) plus the ms fields as a cache (rounded to 0.1 ms at the last edit, NOT clamped to the old slider maxima). Loaded by origin/beta's loader (mkVoice + Object.assign + freshLayer(j)): loads, renders, nothing throws, all samples finite. It plays the cached ms: 4-bar attack at 120 BPM / LPB 8 = 8000 ms ramp, but the saved Max len is unchanged (6 s), so an old build cuts the swell at 6 s at about 75 percent level. Expected and acceptable, but the old build will not show the lines.
- Normalisers: there are none (voiceFromJSON is Object.assign). The render path is safe anyway: `linesMs` clamps (NaN/strings/negative to 0, Infinity or huge to 64 s a stage); `len` and `hold` capped at 300 s. Tried: strings, NaN, negative, null, Infinity, unit set without lines (falls back to ms equivalent, which at LPB 1 / BPM 40 rounds a short ms to 0 lines), 1e9 lines: no NaN, no non-finite sample.
- Worst case (this machine, idle, Node): 8 layers AHD, atk+hold+dec each at the UI maximum of 8 bars at BPM 40 / LPB 1: 144 s, 26.4 MB, 30.2 s render. Corrupt 1e9 lines everywhere: 192 s, 35.2 MB, 38.7 s. The hard ceiling is 300 s = 57.6 MB mono per pad (not reached: 3 x 64 s per layer). Phone render of such a pad will take minutes (Worker, UI not blocked). Ordinary 4-bar swell (one layer, 120 BPM): about 0.4 s.

## 3. Maths
Attack ramp is linear (env1: t/atk). Measured by linear fit of windowed RMS of a pink noise layer, AHD, to the plateau: full level at 3.91 s (expected 4.000, 120 BPM / LPB 4 / 32 lines), 1.844 (1.832; 131/8/32), 14.81 (14.657; 131/8/256), 16.14 (16.0; 120/4/128), 48.25 (48.0; BPM 40 / LPB 1 / 32). Error -2.1 to +1.1 percent, which is the noise of the estimator; fractions at 10/25/50/75 percent of the attack are 0.10/0.25/0.52/0.77. `linesMs` exact in the unit test (32 lines, 120, 4 = 4000.000 ms). Only the attack was measured by rendering; hold, decay, release, filter env and pad amp go through the same `linesMs` in `envLinesFit` (read) and tests/env-lines.test.js, not separately rendered by me: PARTLY CHECKED.
Note: stored values are lines, so an LPB change keeps the line count: "4 bars" at LPB 8 shows as "8 bars" at LPB 4 (same rows, so it still ends on the same pattern row). Punch-list wording "still fits the bars" is right for BPM only. A line count above 32 x LPB after lowering LPB is allowed (slider pinned, render fine).

## 4. Re-rendering
- `padBpm` returns the old expression exactly when `voiceLines(v)` is false, so ordinary pads cost one cheap `some` and keep their cache key. S.bpm and S.lpb are integers, so `bpm + lpb/100` is collision-free. CONFIRMED.
- Re-render is lazy: stale() is checked on play (ensurePlay), showCur, prerender; the old buffer keeps playing until the Worker's render lands, install() swaps the cache and coreBuf() posts a new buffer; voices already sounding hold the old array. Same path as any pad edit: no hung note. (Not run in a browser.)
- F2 (should fix): not debounced. During a TEMPO drag (or Link / MIDI clock changing S.bpm) every played lines-mode pad calls bgRender; when a render lands and padBpm differs, it goes again immediately. Each is seconds of Worker time for a long swell, queued behind other pad renders. Fix: in padBpm-driven staleness for lines pads, ignore a tempo change younger than about 300 ms (a `tempoSettledAt` timestamp set where S.bpm changes; stale() returns false for lines pads until settled, with a trailing prerender()), or let bgRender wait for a quiet period.

## 5. Unit switching and editing
- Parser (extracted, run at LPB 1/4/8/16): never NaN. 4b, 2b8, 2.8, 12, 0, "4 bars", "2 bars + 8 lines", 4B all fine; "-1", "b", "", letters, "1e3", ".5" return null (edit cancelled, silently); "9b", "2b99" clamp to the 8-bar max silently, "1.5" = 1 bar + 5 lines. F3 (note): no message on clamp or reject, contrary to the brief.
- Nudge: arrows = bar, Shift = line, step buttons same, clamped 0..32 x LPB; NaN gives NaN-safe 0 via `+v||0`. Slider and typed edits go through syncMs so ms cache stays current.
- MS to lines rounds to the nearest line (within half a line); an atk of 0.5 ms becomes 0 lines and returns as 0.1 ms. Back to ms: clamps to the row maximum (atk 2000, dec/rel 6000) silently: a 4-bar attack becomes 2000 ms with no message. F4 (should fix, small): toast when a switch to ms clamps.
- One undo step per edit: each handler calls changed() once (setU calls o.on() once); I did not run the page undo: NOT CHECKED beyond reading.

## 6. Sequencers and exports (read, not run in a browser)
No length cap: the page player plays the whole AudioBuffer; core 'buf' stores the whole transferred array in coreBufs with no size check and reads by index (the Rd ring is only the CUT/RES/DEC shaper); offline render (25245) and pad export (15218) call renderVoice, which runs envLinesFit with the page's bpm/lpb; the Worker gets lpb. A 4-bar swell at 120 BPM (8 s plus decay) is about 1.7 MB. NOT run: real playback, the core with a 9 MB post. Memory note: coreBuf copies (slice) before transfer, so a 57 MB buffer briefly doubles.

## 7. Tests
- tests/env-lines.test.js: 10/10. tests/stability-batch1.test.js: 3/3; the changed line only adds `lpb:songLpb(),` to the expected postMessage string: not weakened.
- Engine golden ENGINE_GOLDEN=all: pass.
- Full `node --test --test-concurrency=1 tests/*.test.js` on the head: 1563 tests, 1535 pass, 1 fail, 27 skipped. The failure is FlexCore ... bit-identically to build 562.
- Same test on a clean worktree of origin/beta (9eaad2a): 13 of 14 pass, the same test fails. Not caused by the PR.
- Why CI is green: tests.yml runs `npm test` after `actions/checkout@v4` with the default depth 1; the test reads build 562 with `git show b0cb7cc:index.html`, which does not exist in a shallow clone, so `skip2` / `OLD.html` makes it skip. Locally the commit exists and the Flex NaN fixes of build 647 (intended) change FlexCore output, so it fails. The test is stale, not a bug (needs its base moved to build 647 or a skip once 647 is intended).
- Conventions: BUILD 658 = sw.js CACHE v658; CHANGES entry present; HANDOVER entry and punch list (5 items) present; no U+2028/2029; no stray files. F1: origin/beta is now at build 657 (PR #256 etc.); merging conflicts in 4 to 5 places (BUILD, sw.js, CHANGES, HANDOVER); trivial rebase, keep 658.
