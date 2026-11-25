# Verification of PR #310 (C2a, `n1/c2-cdiff`, head cf5e9b7)

Verifier: independent (did not build it). Method: read `cdiff.{h,cpp}` against the page's `chnDiff` (index.html:19941), then ran the page's real `chnDiff` (extracted by name into a Node vm, as the PR's tool does, but with my own generator and my own deep-equality) against the C++ on about 64,000 pairs.

## VERDICT: MERGE

No input found where the C++ class or the param list (content and order) differs from the page plus the lead's deep-equality ruling. Two LOW findings and some notes; none blocks C2b.

## Findings (by severity)

### 1. LOW: the PR's golden lets 3 real mutants of cdiff.cpp survive
I seeded 13 mutations of my own in cdiff.cpp (detached worktree, undone after each). 9 were killed by `ctest chains_diff`. 3 real ones survived, 1 was an equivalent mutant:
- A: `truthy(Number)` as `> 0` instead of `!= 0` (so `st2: -1` reads as falsy). Survives: no golden pair has a negative or fractional `st2` / `ti2` against an absent one.
- G: Bool ids all keyed the same (`true` and `false` are the same module id). Survives: no pair with `[{"id":true}]` vs `[{"id":false}]`.
- L: `null === null` made false in the param compare (a null param equal on both sides would become Structural). Survives: no pair has the SAME null param on both sides; the golden only has null in next against a number in prev.
- B (equivalent mutant): `!c.isObj()` weakened to `c.isNull()` for cables; an array cable still throws via the missing `from`.
The implementation is right in all three; these are test gaps. My hand cases kill all three (`native/tools/verify-c2a-gen.mjs`, the first lines of `hand2`). Fix: add those pairs to `chains-diff-dump.mjs` (`st2:-1` vs absent, `st2:-1` vs `-5`, ids `true` vs `false`, `null` vs `false` id, same `null` param both sides).
Killed: C (index limit 4294967295), D (cable `from` string index off by one), E (deep-equal compares number text), F (text cut at 201), I (null in array join), J (fb by truthiness), K (ti2 dropped from the reload rule), M (format prefix), N (id truncated).

### 2. LOW: same-document shortcut contradicts the ruling's depth limit
`strict_eq` on two `Ref` values is a handle identity (`a.ref == b.ref`, cdiff.cpp:63-64) tried before `deep_eq`. When `prev` and `next` are the SAME `Json` document and a param holds 65 or more nested containers, the result is Same, while two separate parses of the same text give Structural (the ruling: "deeper is not equal"). Evidence (`/tmp/vc2a/same.cpp`): depth 64: same handle Same, two parses Same; depth 65: same handle Same, two parses Structural; depth 100: same handle Same, two parses Structural. `test_chains_diff.cpp:85` pins the identity behaviour ("JS identity makes every object equal to itself"). Not reachable from LiveHost (it always has two parsed songs) and harmless (an equal document needs no swap), so LOW. Fix if wanted: drop the identity shortcut for `Ref` and let `deep_eq` decide, or note it in the header.

### 3. INFO: other observations
- The golden is about 1 MB (the PR notes say the same); `chains_diff_golden_current` needs Node (skipped without it).
- `bus_plan_equal` also compares `feedLen` (only `nFeeds` is read by the mixer); conservative: at most one extra plan swap when a bus has more than 32 feeds.
- Lone surrogates in param NAMES: Node writes U+FFFD when a file is saved; only a harness artifact in my comparison, not a defect.
- Hash collision attacks on `unordered_map<std::string>` not considered (patches are the user's own documents).

## Evidence per check

### 1. Line by line against chnDiff
Read all of cdiff.cpp against index.html:19941-19962. Matches, with the reasoning verified by differential runs:
- `if (!next) return []` is `!truthy(next)` (cdiff.cpp, top of `chains_diff`); reload rule: `format` string compare, `Array.isArray(prev.modules)`, `!st2 !== !st2`, `!ti2 !== !ti2` with JS truthiness for every JSON type (`0`, `-0`, `""`, `null`, `false` falsy; `"0"`, `[]`, `{}` truthy). Hand cases: st2 `"0"` vs `[]`, `0` vs `-0`, `""` vs `null`, `1` vs `2`, `{}` vs `"x"`, `-1` / `-5` / `-0.5`, all equal to the page.
- `(x.modules || [])`: falsy gives empty, a non-array truthy value (`"x"`, `5`, `{}`) throws, so Structural. Hand cases `modules: 0, "", null, false, "x", {}` and `cables: null, "x", {}, 0`.
- Map semantics: first position, last value; SameValueZero on ids (1 vs "1", 1 vs 1.0, -0 vs 0, true/null/missing/array/object ids, `"1e21"` vs `1e21`, `"0.0000001"` vs `1e-7`). OMap implements this; hand cases and generator case 6/11.
- Cable key: JS string conversion per part (numbers via `js_number_to_string`, null, undefined, arrays by join with null as empty, objects `[object Object]`, strings by UTF-16 unit, `from` as string / number / false / object with "0"/"1"). Cables that are null, a number, an array, or have from/to missing or null throw. fb compares `=== true`. Hand cases for each.
- for-in order (integer-like keys first ascending, up to 4294967294): hand case `{"b","1","a","0","4294967295","4294967294"}` then `9007199254740991`, `"01"`, `"-1"`, `"1e3"`, `" 1"`: matches Node (and my mutant C is killed by the PR golden too). `params` as string, array, number, true, null, `""`: for-in over units / indices; `a.length` read; Object.prototype names (`constructor`, `toString`, `__proto__`, `hasOwnProperty`) all match.
- text: `slice(0,200)` in UTF-16 units with 3-byte and 4-byte characters, a surrogate pair split at 200, lone surrogate vs pair: match (cases `x*199 + 😀`, `x*198 + 😀`, `é*200 + Z`, `€*201` vs `€*200`, `\ud83d` vs `😀`).
- Numbers: `0` vs `-0`, `1` vs `1.0`, `9007199254740993` vs `9007199254740992`, `0.1` vs `0.10000000000000001`, `1e-400` vs `0`, `1e999` (Infinity: Structural), `5e-324`: match. `null` param vs missing / vs number, both directions: match.
- Deep equality (ruling): object vs array, key order, duplicate keys last-wins both sides, `-0` leaf, nested arrays, depth 64 equal / 65 not, object in prev vs number in next (Params op), number in prev vs object in next (Structural), null vs object, bool vs object: match my own independent deep-equal (canonical-string with sorted keys, depth 64).

### 2. Own adversarial cases and the randomised differential
- Files (all in this branch): `native/tools/verify-c2a-gen.mjs` (generator + expectations from the real chnDiff, deep-equal drop by my own `deq`), `native/tools/verify-c2a-cmp.mjs`, `native/tests/verify_c2a_diff.cpp` (C++ driver, not wired into CMake), `native/tools/verify-c2a-fuzz.mjs` (token-level text mutations of ELECTRO's patch), `native/tools/verify-c2a-big.mjs`, `native/tools/verify-c2a-mut.sh`.
- 146 hand-written cases (30 raw-text + 116 in `hand2`, none copied from the golden), plus 11 fixed seeds (1 to 11) x about 4,000 generated pairs (16 mutation kinds on ELECTRO's chainPatch), plus 20,000 token-fuzzed pairs (fixed seed 12345): about 64,000 pairs.
- Result: 0 mismatches in class or param list (id, name, value, order, sign of zero). Class mix of the generated pairs: roughly Same 7%, Params 18%, Structural 75%. (My first runs showed 2 + a few mismatches; all were harness artifacts: `String(-0)` loses the sign, and lone surrogates saved to a file as U+FFFD. Fixed in the comparator.)
- Driver built with `-fsanitize=address,undefined -fno-sanitize-recover=undefined`.

### 3. Robustness
- ASan + UBSan over all of the above, and over `big.jsonl` below: no report, no crash, no exception.
- Deep nesting: params nested 60, 64, 65, 100, 250 deep (arrays and objects) in both documents; cable `from`/`to` nested 250 deep; 300 deep is a JSON parse error (kJsonMaxDepth 256), so it never reaches chains_diff. Results as expected (64: Same, 65 and more: Structural), no stack problem (deep_eq recursion is bounded by 64, to_js_string by 258).
- Cost (Release, `-O2`, one process each, includes parsing the input): 20000 modules + 20000 cables identical 0.15 s; one param changed in the last module 0.15 s; 20000 cables all with the same key 0.12 s; 20000 modules with the same id 0.07 s; one module with 100,000 params 0.12 s; 100,000 integer-like keys in reverse order 0.13 s; a 900,000 character string for params and text 0.10 s; 150,000-key object param 0.09 s; 300,000-element array param 0.09 s; 20000 modules each with an object param 0.11 s; 200 cables with 2000-element array parts 0.01 s. Nothing quadratic found; every walk is linear (hash maps for ids, cables, keys).
- Memory: the only failure path is `std::bad_alloc` (core built `-fno-exceptions`, so it would terminate; the contract says bad_alloc only; the same as the rest of the core).

### 4. bus_plan_equal
`Mixer::setPlan` copies the plan (mix.cpp:221); `rebuild`, `mix_audible`, `pdc_plan`, `ChainsHost::keepMask` and render read `feeds`, `nFeeds`, `feedMask`, `targets`, `pure`, `pre`, `srcAll`, `busAll`, `loopTo` (written by bus_plan, mix.cpp:154) and `loop`. cdiff.cpp's `bus_plan_equal` compares every one of these plus `feedLen`, per track for `t < kSongNT`, and only the first `nFeeds[t]` (clamped 0..kMixFeedMax) feed entries. Nothing uninitialised is read: the `BusPlan()` constructor (mix.cpp:102) zeroes `feeds` and every array, and `pure`, `pre`, `loop` have default initialisers. No `memcmp`, so no padding comparison.

### 5. The golden tool
In a scratch copy (/tmp/vscratch, since removed): unmodified: `is current (266 cases ...)` exit 0. One byte of the golden changed ("Same" to "Samx"): exit 1 (not current). `chnDiff` body changed (the fb compare flipped; separately the text slice 200 to 199): exit 1 each. `chnClone` made unparseable: exit 2 with a message. The expectation derivation: one place, the `kept` filter in `run()` (chains-diff-dump.mjs), which drops only a `param` op whose value is an object/array that is deep-equal to prev's last-wins module's `params[name]`, with a `deepEq` that has the depth limit at 64. Beyond the ruling: the `typeof o.id === 'string'` condition and the finite-number test, which are in the task spec ("string id"). I found nothing more.

### 6. ctest and mutation re-seeding
`ctest -R "chains_diff|busplan|chains_song"` on the PR head, Release, `native/build-v`: 7 of 7 passed (busplan_ref, busplan, busplan_bib, busplan_verify266, chains_song, chains_diff, chains_diff_golden_current); first run showed busplan_verify266 "Not Run" only because I had not built that target. `chains_diff: 266 pairs (Same 99, Params 46, Structural 121 of which 19 throw in the page; 20 raw-text), 0 failure(s)`. Mutations: see finding 1.

### 7. Build hygiene
`git diff --stat origin/native...cf5e9b7`: only `docs/port/C2A-NOTES.md`, `native/core/CMakeLists.txt` (one added source, cdiff.cpp), `native/core/src/chains/cdiff.{h,cpp}`, `native/tests/CMakeLists.txt`, `native/tests/golden/chains_diff_cases.json`, `native/tests/test_chains_diff.cpp`, `native/tools/chains-diff-dump.mjs`. No "lxr" in cdiff.{h,cpp}; the `lxr` hits in native/core are all older (lxr_seam, CMakeLists line 30), none added by the PR. No unistd, alloca, VLAs; the largest stack arrays are `char[40]`, `char[12]`. `-Wall -Wextra` clean with g++ and clang++ (`-Wconversion -Wshadow` also gave 0 warnings in cdiff.cpp). Standard C++17 only.

## What I could not check
- MSVC itself (no compiler here); only read for portability.
- The PR's own test under ASan/UBSan (I sanitised my own driver, which links the same cdiff.cpp and json.cpp).
- macOS / Windows CI behaviour of the golden check (the repo notes a platform difference for other goldens).
- A Node other than 22 (the golden check ran on v22.23.5).
- Byte-invalid UTF-8 input (the page would see U+FFFD; the Json reader's handling is its own concern).
- Hash-flooding of the unordered_maps.
