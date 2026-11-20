# verify-247: independent check of PR #247 (branch n1/followups-19nov, head 021c352)

Verifier build: Release, `native/build-verify247` (GCC 13, -j1). Sections marked NOT CHECKED were not finished (server restart); see the end.

## Verdict: MERGE AFTER FIXES (two small items, F1 and F2; no audio-path change is wrong for a value the UI can produce)

## 1. buildParts: table (JS = page `applyChop` :22333-:22339; old = native on `native`; new = PR head)
Part instruments are NOT clamped at load: `normParts` (index.html:20844) keeps `insts` as given; `partSnap` copies `S.insts` (clamped by the song loader :20377), `partLoad` copies back raw. Out-of-range values only come from a hand-edited song. JS reads: slc `I.slc|0`, sync `I.sync|0` (trigger: `I.sync` truthy), beats `I.beats||4`, grain `I.grain==null?1:I.grain|0`.
Native "new" values were measured with a probe (part song, loop 2 s, 120 bpm) and equal my emulation of the code; "old" is from the code.

Every UI value (slc 0..64, sync 0..3, beats 1..64 integers, grain 0..4) and every falsy value (missing, null, 0, "", false, NaN-as-string for slc/grain) equals the JS. `beats` falsy -> 4 (kept), `slc` 1.7 -> 1 (no chop, equals JS), `sync` true / "1" / 1.7 -> 1 (equals JS), `grain` 0 -> 0 and null -> 1 (equal JS): all CONFIRMED. No cell breaks the lead's rule.

Cells where the PR moved native AWAY from the JS (old equalled JS), all beyond the UI:
- slc: -3 (JS -3 = no chop, native 0 = no chop: same behaviour), 65 / 1000 (JS 65 / 1000 slices, native 64).
- sync: -3 (JS: truthy, runs applyChop, rate set, whole-loop cut; native 0: no chop at all, differs in behaviour), 64 / 65 / 1000 / "8" (JS stretch mode as given, native stMode 3), "NaN" string (JS trigger true but sy 0 returns early: same result).
- grain: -3 (JS -3, native 0), 64 / 65 / 1000 / "8" (native 4).
- beats: 1.7 (JS 1.7 beats, rate 2.353; native rounds to 2, rate 2.0: ROUNDING, not clamping, and 1.7 is a value the page uses unrounded), -3 (native 1), 65 / 1000 (native 64).
Not changed by the PR and different from JS (old already differed): beats "8" / true (JS keeps the raw value in ev.st.beats, native a number: same arithmetic), beats string "NaN" (JS NaN target, native 4). NaN cannot occur in JSON.

## Findings
- F1 (low-medium, wrong rationale + a future-host trap): the notes say sync > 3 clamped to 3 is "the same stretch branch". It is not: the page's stretch engine tests `st.mode === 3` for LOOP (index.html:22313); modes 4..N take the non-LOOP path (same as 2). Native clamps 5 -> 3 = LOOP. Nothing in native consumes `stMode` yet (only seq.cpp/seq.h/tests), so there is no audible change today, but a host reading `stMode` would get LOOP where the page gives the normal stretch. Fix: clamp sync > 3 to 2, or leave sy unclamped above 3 (it is only a field), and correct the note.
- F2 (low): `beats` is rounded (1.7 -> 2) where the page uses it raw. Clamp 1..64 without rounding (the song loader rounds, but the page does not for part instruments); change `to_int_sat(js_round(b),1,64)` to `clamp(b,1,64)`. Negative beats -3: JS gives a negative target (garbage), native 1: fine.
- F3 (info): the notes name "the commands in docs/port/FOLLOWUPS-19NOV-NOTES.md" but contain no build commands; I used `cmake -S native -B build -DCMAKE_BUILD_TYPE=Release` (native.yml).
- F4 (info): `nan_sweep` fails only on a crash/signal; output that is NaN is "allowed" and `failed blocks` is not asserted, so making a module emit NaN does NOT fail the sweep (see 4).

## 2. render_chop reversed test (mutations of voice.cpp:178, one at a time, Release)
- drop `- durSec`: KILLED (8 FAIL lines).
- drop `- ofsSec`: KILLED (6 FAIL lines).
- remove `std::max(0.0, ...)`: SURVIVES (0 fails, PASS). Because the ramp pad's slice ends exactly at the pad end, `dur - ofs - dur` is never negative. A test with `sync 2` and `rev 1` where the stretch scale > 1 (ofsSec + durSec > dur) would catch it. Severity low.

## 3. testPartClamp (seq_chop)
- seq.cpp reverted to `native`: FAILS (10 failures).
- beats 0 -> 1 (`: 4` -> `: 1`): FAILS (5 failures; beats 0 / null / "" / false lines).
Both CONFIRMED.

## 4. U31 follow-ups
- mutate-u31.sh: 49 `run` entries parsed (3 are multi-line); each old text occurs exactly once in its file (script-checked): CONFIRMED. The changed entry alone (Release build): "killed", `mutations: 1 killed, 0 not`, script ends 0 (13 min on this loaded machine, 5 targets rebuilt). The full 49-entry run: NOT CHECKED (about 10 h on this machine).
- nan_sweep reach: with `if (!isfinite(io.p[q])) abort()` added (enabled by an env var) to TrackIn, ChainOut (cmods.cpp) and HostIn, Out (wmods.cpp), the sweeps abort with exit 2 at exactly `chains trackIn.track = NaN`, `chains chainOut.track = NaN`, `waves hostIn.transpose = NaN`, `waves out.slot = NaN`: CONFIRMED that all four are reached. Clean code: chains 145 types / 9506 pairs, waves 130 / 5292, exit 0. A module emitting NaN output alone does not fail the sweep (F4); sources restored.
- mods-wasm-check.sh: clean PASS (185594 bytes, pin matches); corrupted chains worklet FAIL, corrupted waves worklet FAIL, both corrupted identically FAIL (pin mismatch). CONFIRMED. parity-u02.sh runs under `set -e`, so the FAIL stops it.
- chains_threads under TSan on fixed code, and ctest time: NOT CHECKED (see end). The CMake rule only switches to 30 s when `CMAKE_CXX_FLAGS` contains `fsanitize=thread`; default builds stay 3 s.

## 6. Files in the PR
11 files, all expected (core change, tests, tools, one notes file). Nothing that should not be there. `mods-wasm.sha256` pin verified above.

## NOT CHECKED at the time of writing
Full Release ctest (181 tests listed; the run was cut by the restart); ASan+UBSan runs of seq_chop, render_chop, nan_sweep, chains_threads, hostile, rt_harness, render_api_hostile; TSan chains_threads and its ctest time.
