# Review findings for Fable, 16 Nov (written by Sonnet readers; nothing here is fixed)

# Review B2, B4, B5 (read-only; 16 Nov)

## B2: maths helpers against V8 12.4.254 (Node 22)

Version: every CI job uses `node-version: 22` (.github/workflows/native.yml:34,107,132,151; tests.yml:23; desktop.yml:49; lockfile.yml:18). Node 22 here reports V8 12.4.254.21-node.57. Upstream source fetched from raw.githubusercontent.com/v8/v8 tag 12.4.254 (`src/base/ieee754.cc`, 3033 lines; `src/builtins/math.tq` for hypot). Network worked.

Method: extracted each function from both files, stripped comments and whitespace, tokenised (identifiers, numeric literals as written, punctuation) and diffed the token streams. A literal that differs in text shows up; I also compared values where the text differed.

Result for native/core/src/rt/fdlibm.cpp (all identical token for token apart from names/macros noted):
- `__ieee754_rem_pio2` (fdlibm.cpp:112), `__kernel_rem_pio2` (:437), `__kernel_cos` (:299), `__kernel_sin` (:667), `__kernel_tan` (:727): 0 differences (including the two_over_pi / npio2_hw / PIo2 tables, which sit inside those functions).
- `fdlibm_cos` (:832), `fdlibm_sin` (:941): only the V8_USE_LIBM_TRIG_FUNCTIONS `#if` removed (declared in the header comment; correct for node 22's fdlibm path).
- `fdlibm_tan` (:970), `fdlibm_log` (:861), `fdlibm_expm1` (:993), `fdlibm_atan` (:1216): 0 differences.
- `fdlibm_tanh` (:1113): 0 differences (all branches and constants, `tiny` volatile, `huge + x > one`, `ix < 0x40360000`, `ix >= 0x3FF00000` split identical).
- `fdlibm_log2` (:1164) and `k_log1p_` (:1151): identical. V8 keeps Lg1..Lg7 at file scope (ieee754.cc:1939); the port repeats them inside k_log1p_ with the same seven literals (checked text and values equal to V8's :1939-1945 and log's :1643-1649).
- `fdlibm_atan2` (:1328): identical except V8's NegateWithWraparound<int32_t>(lx) / SubWithWraparound(hx, 0x3FF00000) written as `(uint32_t)(0u - lx)` and `(int32_t)((uint32_t)hx - 0x3FF00000u)`. Same bits. pi_o_4, pi_o_2, pi, pi_lo, tiny identical.
- `v8_hypot` (:1423) against math.tq MathHypot (builtins/math.tq:397-445): same algorithm. max of |args| with NaN elements stored as 0, infinity wins over NaN, NaN next, max==0 gives 0, then sum of (|a|/max)^2 with Kahan compensation in the same order (`summand = n*n - comp; pre = sum + summand; comp = (pre - sum) - summand`), result `sqrt(sum) * max`. No difference. Caveat: this is the builtin as the interpreter/baseline runs it; I did not check whether TurboFan 12.4 lowers Math.hypot differently (I know of no such reducer in 12.4, but did not grep the compiler sources).
- Macros EXTRACT_WORDS, SET_HIGH_WORD, SET_LOW_WORD (fdlibm.cpp:48-98) equal V8's (ieee754.cc:54, 89, 99) with base::bit_cast replaced by memcpy bit_cast_.

**MISMATCH FOUND (not in the five named helpers, but in the same family): `js_exp` (native/core/src/rt/jsmath.cpp:35-72).** V8's `exp` (ieee754.cc:1447) has `E = 2.718281828459045` and, in the argument-reduction branch for |x| > 0.5 ln2 and < 1.5 ln2, `if (x == 1.0) return E;` The port drops that line. Measured by compiling jsmath.cpp alone (small g++ -O1 -ffp-contract=off): `js_exp(1.0)` = 0x1.5bf0a8b14576ap+1 = 2.7182818284590455, while V8 / Node 22 `Math.exp(1)` = Math.E = 0x1.5bf0a8b145769p+1 = 2.718281828459045 (checked in Node 22.23.4). One ulp off for exactly x == 1. Matters only if the JS ever evaluates Math.exp at exactly 1.0 (e.g. exp(-x*3) at x = -1/3, or an exp(1) constant); I found no literal `Math.exp(1)` in index.html or the worklets on beta, so likely rare, but it is a real bit difference and the test golden (test_runtime.cpp:41, 20000 random-ish values) cannot hit it. Fix is one line: `if (x == 1.0) return 2.718281828459045;` at the top of that branch.
Confidence: high (measured both sides).

Also compared (bonus) `js_pow` (jsmath.cpp:79-): same arithmetic and constants (ivln2_l is written 1.92596299112661746302e-08 against V8's ...746887e-08: parse to the identical double 0x1.4ae0bf85ddf44p-26). Differences are only spelling/UB-avoidance, plus (-1)**non-int and (x<0)**non-int return `(z-z)/(z-z)` where V8 returns signaling_NaN (both NaN; payload/sign bit could differ, harmless unless a golden hashes raw NaN bits: canon() in test_libm suggests it does not). The one deliberate comment at jsmath.cpp:~192 about V8's grouping of the final division is consistent with V8's `base::Divide(z * t1, (t1 - two) - (w + z * w))`.

Not checked: whether Node 22's shipped binary on the CI runner differs from the tag (the `-node.57` suffix is Node's patch level; Node patches to deps/v8 might touch ieee754.cc, I did not diff nodejs/node deps/v8 against v8 tag, but the live test_libm hashes are the check for that); sin/cos with libm trig switch is off in Node 22 as the header says (the ctest compares to Math.* in Node 22, which I did not run).

## B4: can the web app send a pump value list longer than the plugin's parameter count?

Short answer: no, not from the shipped files: every sender builds the list by mapping over the same `pump/pump-meta.js` parameter table, and the shipped `pump.wasm` has exactly the same parameter count per type (verified), so list length equals nparams always; only a meta/wasm skew would change that.

Verification: loaded beta's pump/pump-meta.js and pump/pump.wasm in Node and compared `PUMP_META.fx[t].params.length` with `pump_nparams(i)` for all 25 types: all equal (e.g. EQ 277, BAND 57, CHOP 82, MASTER 75, STRIP 38, VOX 62). Also the 13 PUMP_CH entries in chains/chains-worklet.js: `np` equals pump_nparams and max param index < np.

Senders traced (beta worktree at 814109e; origin/beta is now 9361306 but `git diff 814109e origin/beta -- pump` is empty, so pump files are the same):
1. index.html:20186 `tfxChain`: `pv: fx.params.map(p => f.pump[p.s] ?? p.def)`, fx = PM.fx[type] from pump-meta.js. Length = meta length, regardless of what keys a saved song holds (f.pump is an object keyed by symbol; unknown/old keys are ignored, missing ones default). So old songs cannot lengthen it.
2. index.html:20268 `pset` (FXV and MOD): replaces `pv` with `q.pv` from `pumpPv` (index.html:19881; copy in core/seq-core.js:115) = `D.pd.map(...)`, D.pd = fx.params (index.html:19957; the core gets a field-by-field copy at index.html:37522). Same length. Callers: index.html:22742 (fxpMsg), core/core-processor.js:626 and :677.
3. Writers in the worklet: index.html:20306-20307 `pumpRun` makes `new Float32Array(buffer, pump_values(h), s.pv.length)` and writes s.pv.length floats with NO clamp to pump_nparams (this is the code the U18 native clamp mirrors; the native comment "the JS never sends one" is correct as long as the invariant above holds). :20285-20286 `pumpNew`: writes into pump_stage (512 floats), capped at 512 (EQ 277 fits; typed-array OOB writes are dropped anyway); pump_new reads only the first nparams. :20952 `pumpLatencyOf`: stage, same.
4. chains/chains-worklet.js:5635-5643 (pump modules): stage write at q[1] over a 512 view; later writes through a view of length d.np (an out-of-range index would be silently dropped by the typed array, not overrun).
5. tools/pump-chains.mjs (generator of PUMP_CH from pump-meta.js) and tests/off-pump-latency.test.js:21 only feed the same meta.
Also `stable/` and `beta/` publishes have the identical code.

How it could still happen (all need meta and wasm to disagree, none present today):
- The owner updating pump-meta.js to the pin (MASTER 77) while leaving an older wasm (75), or the reverse of what happens today. PINS.md says the pin's MASTER is 77 and the shipped is 75; today the shipped pair is consistent (75 and 75), and a longer list would be 77 against a 75-parameter wasm: a 2-float overwrite of pump_values in the wasm heap (pump instance memory, no trap because it is inside the 16 MB initial memory).
- Service worker skew (sw.js:4 caches pump-meta.js?v=6 and pump.wasm?v=6 separately; network-first with cache fallback, sw.js:42-44): during a deploy a client could briefly hold one new and one old file. The ?v= bump is manual.
- No runtime check anywhere compares meta length with pump_nparams (grep: `pump_nparams` is not used by index.html, the worklets or tools/ on beta).
Not checked: song loading paths that migrate very old saves (pumpNorm1, index.html:19914) beyond reading that they clamp to meta symbols and default new ones; the native song loader's own pv builder (out of scope: it is native-side). Confidence: high for the shipped files, medium for "no other sender" (grep-based over index.html, core/, chains/, master-worklet.js, tools/, tests/).
Suggested hardening (not done): clamp `s.pv.length` to `P.pump_nparams(s.pt)` in `pumpRun`, as U18 did natively.

## B5: is the shipped pump.wasm built from the pinned pump commit?

Short answer: no. The shipped `pump/pump.wasm` and `pump/pump-meta.js` on beta come from an older pump commit than the pin `deb07d331b4f6a058abc847106be66bfc498faa3` (native/core/third_party/PINS.md:46), and nothing in thunder-plus builds them.

Evidence:
- Shipped meta stamp (pump/pump-meta.js:2) = sha1 30b389d8ede811c4375969aff83e2f9db93c6161; MASTER has 75 parameters (measured above). shipped pump.wasm: 1,599,108 bytes, sha256 416716cd3e678924e9b54bcdcae417f8c6296126279b942cecedb91222b60fa6; no build stamp inside (PINS.md:53 says the same).
- I fetched matthettich/pump at the pin (shallow, public) and ran its own meta step (wasm/pump_meta.cpp compiled with c++ -O0, then the python in wasm/build.sh:46-55): stamp = 72cc7e55578d5574bc58a08e8ebe58ecdb8ce12a, MASTER 77 parameters. This independently confirms PINS.md:60-61 (pin includes 30a3e95 "MASTER: resonance stage"; shipped does not). So the shipped wasm lacks two MASTER parameters the pin has, matching U18's ctest `pump` failure (77 vs 75, p_master latency 4454 vs 358).
- PINS.md:57-59 reports no byte-equal build from several candidate pump commits (pin CMake 1,601,290 B; 6f1ec29/3d53287/7b8cb1d 1,599,184 B; 4a1533b 1,599,823 B; ff046ce 1,599,488 B), so the exact source commit of the shipped wasm is unknown (possibly with uncommitted edits). I did not rebuild (wasi-sdk build is heavy on a shared machine).
- Producers: the pump repo's `wasm/build.sh` (header lines 2-14: "then copy OUT/pump.wasm and OUT/pump-meta.js to thunder-plus/pump/ ... and bump their ?v= there": a manual step). thunder-plus beta has no workflow that builds pump.wasm: .github/workflows has only beta-web.yml (copies beta to beta/ via tools/make-channel.sh), desktop.yml, lockfile.yml, tests.yml; pump's own .github/workflows/build.yml (deb07d3) has no step copying into thunder-plus (grep for wasm/thunder: only the ROOM page line). The history of pump/pump.wasm on beta is squashed. The native tree builds its own pump.wasm from the pin via native/tools/build-wasm.sh (CMake, wasi-sdk 25) for parity tests only; it never writes pump/pump.wasm in the web app.
- The synth pair is different: PINS.md:55-56 says pump-synth.wasm is byte-identical to the pin's build (not re-measured by me).
Not checked: the pump repo's older commits (stamp-only check done at the pin); wasi-sdk rebuild; whether some other pump-side workflow in another repo pushes the files.

# Review B1 + B3 (read-only; no code changes, nothing compiled)

Branch `native` (C++) vs `beta` worktree /home/claude/wt/beta-ref (JS). Line numbers marked "~" are approximate.
Pre-check: JS blocks (FlexCore..TFX, DegradeCore, PitchFxCore, applyFx, rvMix, chains shared block) are identical on native and beta.
`node native/tools/flex-spec.mjs --check` prints "flexspec.inc is current".

## B1  C++ vs JS differences

No operation-order, state init/reset, or js_* routing differences found in clkcores, idmcores, u17cores, Pitch correct or the BBExpr compiler.
All findings below are edge cases.

### B1-1  BBExpr exec hang via op 77/78 quirk  (severity HIGH/MEDIUM; confidence high, reproduced under node with a timeout)
- C++: native/core/src/modfx/bbexpr.cpp ~244-273 (unbounded `while (i < N)`, cases 50, 51, 77, 78)
- JS: chains/chains-worklet.js BBExpr exec, cases 50/51/77/78 (~4480+)
- The port is faithful, so JS hangs identically. A constant 50 or 51 operand is executed as a jump op.
  `(t && 51) + a` hangs at t=0; `(t || 51) + a` hangs when t != 0.
- Matters: user text (U29 pad engine) can freeze the audio thread. The 16 built-in formulas (bb_list.inc) are safe.
- Suggested fix (deviates from JS, so a decision for the owner): cap executed steps per exec.
- Also counts as a B3 item.

### B1-2  PK_tup out-of-bounds read  (LOW; confidence medium-high)
- C++: clkcores.cpp ~132 `kTup[(int)j]`; JS: index.html ~19193
- j = -1 when p is within 1e-9 below a beat boundary (5-TUPLET FEEL, q==5). JS reads undefined, giving NaN for one sample. C++ is UB.

### B1-3  bp[] reads past bpn  (LOW; confidence medium; currently unreachable)
- C++: clkcores.cpp:62 clk_spb `bp[n-1]`; :313 StepGateCore `bp[i]`. JS reads undefined if 1 < bpn < n.
- fxhost.cpp:249-251 guarantees bpn == n or 1, so safe there.
- modfx/mod_clk.cpp BeatClock sets n_ from the first block; bp_[i>=n_] would be stale 0.0 if ctx.block grows later.

### B1-4  pad_pitchfx null param  (LOW; confidence medium)
- C++: u17cores.cpp:183-184. JS `3 + null*4` = 3 (valid tail). C++ cannot hold null, so it becomes NaN and an empty vector is returned.
- JSON.stringify(NaN) gives null, so this is reachable from saved state.

### B1-5  sinT index 4097  (LOW/INFO; confidence medium)
- C++: modfx/util.h sinT. Tiny negative p gives p - floor(p) = 1, reading index 4097 of a 4097-entry table. JS yields NaN.

### B1-6  fxHermAt index conversion  (LOW; confidence medium)
- C++: modfx/bfutil.h `i32(i)-1`; JS uses ToInt32(i-1). Differs only for NaN/Inf/huge positions.

### B1-7  NaN to int casts, int overflow  (LOW; confidence medium)
- mod_gen.cpp Bytebeat `(int32_t)clamp(js_round(p[2]),0,255)` is UB for NaN (JS stores 0). Same pattern as `(int)NaN` in mod_cv PitchTrk.
- mod_voice PitchCorrect `++miss` int overflow after about 66 days continuous.

### B1-8  BBExpr tokenizer works on bytes  (LOW/INFO; confidence medium)
- bbexpr.cpp tokenizer and length check use bytes, not UTF-16 units. Changes error position/message only.

### B1-9  StepGateCore past-15 pattern  (INFO; confidence high)
- C++ gives an empty mask where the JS throws. Documented in the source.

## B3  Real-time safety of process paths

Confirmed known (not re-reported): mod_mods.cpp make()/m_.reset() reached from run()/open() in process; U17 pad_* return std::vector;
FFTR::get/hann (mutex + allocation) are called only from constructors. Pump slot wake-up was not examined (outside the files read).

### B3-1  BBProg::exec unbounded  (HIGH/MEDIUM) - same as B1-1.

### B3-2  Lazy statics in modfx::find()/regAll()  (LOW; confidence medium)
- modfx/modfx.cpp:20-31 allocate on first call. Safe only if first called on the control thread.

### B3-3  No ctx.block <= kBlock (128) check  (LOW/MEDIUM; confidence medium)
- mod_mods `cp()` and `fill(kBlock)` assume it. Only Convolve handles other block sizes. A larger host block would overrun fixed buffers.

### B3-4  Bounded but heavy (RT cost, not violations)
- mod_convolve.cpp `ready()` (:137) and process (:78) generate the first IR synchronously (terminates: finish() sets ready=inf). setup() is about 0.5-1 ms per IR change.
- Spectral/XStretch frame work lands in a single block (spike).
- mod_cv PitchTrk detection up to about 398x320 with two int `%` per iteration, every 4th block.

### B3-5  Loops confirmed bounded
u17 `while (rp < 0)`; Chaos n<8; Bytebeat module `while (ph_ >= 1)` (at most 5); fxhost queue loops (psetAt checks capacity);
XStretch; Convolve jobStep; Flex euclid loop (at most 16); Life (mod_misc.cpp ~198-240) loops are fixed 8x16x9, no allocation.

### B3-6  No new/push_back/resize/throw/locks found
In any process body read or grepped, other than the known items. All `.init(` calls are in constructors. Static-local arrays are constant-initialized.

## Coverage

Read in full (C++): native/core/src/fx/clkcores.cpp/.h, fx/idmcores.cpp/.h, fx/u17cores.cpp/.h, fx/fxhost.cpp,
modfx/bbexpr.cpp/.h, modfx/mod_clk.cpp, modfx/modfx.cpp/.h, modfx/util.h, modfx/bfutil.h.
Read in full (JS): beta-ref index.html ~18970-19480 (clkSpb, FlexCore, StepGateCore, RetrigCore, beatLock, bkHash, Grains/Scramble/VSamp/GlitchCore),
DegradeCore, ResonCore, PitchFxCore, applyFx, rvMix; chains/chains-worklet.js fx helpers + Pitch correct (~3984-4140), BBExpr (4480+),
Bytebeat/Chaos (4927+), fxBeats/FlexM/StepGateM/RetrigM (2905-2975). Docs: TODAY.md section B, TONIGHT.md review queue.
Read in part: mod_mods.cpp (10-100), mod_voice.cpp (1-200), mod_gen.cpp (100-181), mod_spectral.cpp (20-330), mod_convolve.cpp (1-572),
mod_cv.cpp (139-162, 470-546), mod_reverbs.cpp (60-80, 120-140), mod_delays.cpp (15-24), mod_misc.cpp (Life, ~196-245), flexspec.inc (head; verified by tool).
Grep only (process bodies not read): mod_basic.cpp, mod_stereo.cpp, mod_filters.cpp, rest of mod_misc/mod_delays/mod_reverbs/mod_cv/mod_voice,
fftr.cpp/h, fxbank.cpp, fxmap.cpp. B3 "none found" for these rests on grep for new/push_back/resize/throw/lock/vector only.
Scratch files in /tmp/rv only; nothing created in the repo.

## D3: ThreadSanitizer run
# ThreadSanitizer run of the native test suite (D3), 16 Nov

Source: branch n1/jsexp (origin/native 35bed8e + the js_exp fix). Not fixed anything.

## What ran
- clang 18.1.3 on Linux x86-64; `libclang-rt-18-dev` had to be apt-installed first (the TSan runtime was missing from the machine).
- cmake -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo, C/CXX/exe/shared flags `-fsanitize=thread -g`, -DTHUNDER_DEPS_DIR=/home/claude/deps; build dir /home/claude/b-tsan; `-j2`.
- `ctest -j2` with TSAN_OPTIONS="halt_on_error=0 second_deadlock_stack=1 history_size=4 log_path=.../tsan-log": 83 of 86 tests passed, 427 s.
- Races reported: NONE (0 "WARNING: ThreadSanitizer" in the ctest output, no tsan-log.* files).

## What could not be built / run (3 tests, "Not Run")
- `rt_core`, `rt_guard_selftest`, `song`: the targets `tests/rt_harness` and `tests/test_song` fail to LINK: rt_harness.cpp (lines 47-54) defines the global operator new / delete
  (the allocation guard), which collides with TSan's own (libclang_rt.tsan_cxx, tsan_new_delete.cpp): "multiple definition of operator new(unsigned long)" etc.
  Fixing needs the guard's operators compiled out (or turned into a hook) under TSan: not done (no fixing).

## Caveat on the meaning of a clean result
The only thread creation in native/ is android/.../jni.cpp (not built on Linux). The test suite is single-threaded, so a clean TSan run says little; races between the audio thread
and the UI/message thread are not exercised by any test.

## Independent verification of #189 (U22)
# Verify U22 (PR #189, n3/u22 @ 8360be9)

## 1. Gate real: PASS
- test_engine.cpp:132-150 renders each voice with eng_render_voice, SHA-256s the float32 bytes, compares to engine_golden.inc (and to the local Node ref on x86-64 Linux). Final `exact != ran` -> fail (line ~170).
- Script check: engine_golden.inc has 294 rows / 294 unique keys; every key exists in beta tests/golden/engines.json; sha256 and len match for all 294 (0 mismatch, 0 missing). 10 random spot checks identical. engine_voices.json key set == golden key set.
- No skip-as-pass path: missing golden/ref/status!=Ok (incl. kEngUnsupported), loader refusal, missing ref files all -> CHECK fail. Count is checked as fixture==golden==ref size (not a hard-coded 294).
- Caveats: 18 voices with crush/room/drive prove only the dry path (notes admit it). Hashes are Linux x86 only as local-Node check; beta golden is the gate everywhere.

## 2. Mutation: PASS (baseline: engine, engine_ref, engine_golden_current, rt_core all pass)
Fresh mutations, each caught (test rc=1), each reverted (worktree clean):
- saw/square osc constant `yy * 0.8` -> 0.81 (engine.cpp:131): 62 voices fail, 54 direct cases fail
- LP filter coefficient `(gg + lk)` -> `(gg + lk + 0.001)` (engine.cpp:266): 200 voices fail, 142 direct cases fail
- envelope time `-6.9` -> `-6.8` in env1 (engine.cpp:238): 241 voices fail, 142 direct cases fail
Gap: lpRes clamp 0.98->0.97 (:266): 0 of 294 voices fail; only 1 direct case catches it.

## 3. Scope: PASS
- Diff vs merge-base: only native/ and docs/port/U22-NOTES.md (15 files, API confirms). Goldens are added, none edited. No -ffast-math; -ffp-contract=off is set. Tolerances: none, memcmp/hash only.
- Math.*: sin/cos/exp/pow/tan/tanh/round/imul all go through js_*; floor/abs/fmod/ceil exact. Math.sqrt appears only in the un-ported sq01 helper. Compared renderLayer, voiceFilter, voiceAmp, voiceMod, blep, rng, makeOsc, renderLayersOnly line for line: faithful.

## 4. Allocation in render path
- Beyond notes: none heap. Stack only: EngVoice copied (eng_render_voice, eng_render_bound, eng_layers_len; a few KB each). Lazy static sin table (4097 js_sin) on first eng_sinT: a latency hit if first touched on the audio thread (rt test pre-warms it). Pad effects allocate std::vector (admitted).
