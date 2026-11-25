# C2a notes: `chains_diff` and `bus_plan_equal`

**STATUS: done, pushed, PR open (not merged).** `ctest chains_diff` (234 golden pairs) and `chains_diff_golden_current` pass on the final head; ASan + UBSan clean; 19 of 19 seeded faults die.

## What was built
- `native/core/src/chains/cdiff.{h,cpp}` (in `thunder_core`): `ChainsDiffKind`, `ChainsParamOp`, `ChainsDiff`, `chains_diff(Json prev, Json next)`, `bus_plan_equal(a, b)`, exactly the C2-DESIGN "C2a" interface.
- `native/tools/chains-diff-dump.mjs`: pulls the REAL `chnClone` and `chnDiff` out of `index.html` by name (regex on the source text; exit 2 with a message if either is not found or does not run), runs them in a Node vm on every pair, writes `native/tests/golden/chains_diff_cases.json`. `--check` regenerates in memory and fails on drift; `--list` prints one line per case. `ctest chains_diff_golden_current` runs `--check` (Linux with Node).
- `native/tests/test_chains_diff.cpp`, ctest `chains_diff`: every pair classified as the page's ops say, same param list in the same order (id, name, value); same-document case; `bus_plan_equal` (equal plans, every field, every track's masks, entries past `nFeeds` ignored); a hostile-shape sweep (every node of a real patch replaced by 14 hostile literals, both directions, plus the same document on both sides; about 1500 calls) that must not crash; deep nesting.
- Golden: 234 pairs (Same 89, Params 44, Structural 101; 19 of the Structural ones are cases where the page THROWS; 15 pairs carry raw JSON text for duplicate keys, `1e999`, `-0`, lone surrogates). Bases: `sounds/ELECTRO.thunder` (full set of single edits: param to number / string / bool / null / array / object, new param key, key only in prev, add, remove, rename, connect, disconnect, fb set / cleared / false / "true" / 1, text added / removed / changed / past 200 units, st2 / ti2 removed, prev missing / null / not a patch / modules not array, next missing / null / false / `{}`), the chains host's golden-song patches (c, f x6, g, k: a smaller set of edits each), and about 130 synthetic pairs (hostile shapes, duplicate ids, id types, params as string / array / number, for-in order, UTF-16 slice edges, cable key string conversions, 600-module and 1000-param sizes).

## Decisions (where the contract left a choice)
1. **A param op on a module whose id is not a string is Structural** (a number, null, missing or bool id): `ChainsParamOp::id` is a `std::string` and the engine's by-id lookup is for string ids. The page would send the op; the golden's expectation applies the same rule, so the test pins it.
2. **`next` falsy -> Same; `next` truthy but not a patch (`{}`, `5`) is read as the page reads it** (not a special case): with `prev` a valid patch having `st2` it is the reload rule (Structural); with no `st2` / `ti2` it reads as "no modules" and every prev module is a `remove` (Structural unless prev has none). "Both missing" -> Same.
3. **Duplicate ids** follow JS `Map`: the position of the FIRST occurrence, the value of the LAST (also for `OMap` of cables, and for duplicate JSON keys, which `Json::get` already resolves as last-wins). A duplicate id's first module is therefore never examined (golden: `syn/dup-ids-first-never-read` is Same although its first copy differs). Param ops come in the order of first occurrences in `next`.
4. **Param key order is JS for-in order**: canonical array-index keys ascending, then the others in first-insertion order (golden `syn/param-order-*`, raw cases). `params` may also be an array / string (for-in over indices / UTF-16 units) and `a["length"]` is read like JS; strings are compared by UTF-16 unit, `text` is cut at 200 UTF-16 units (a surrogate pair split at 200 compares as the lone half). All pinned by the golden against the page.
5. **Names that only exist on `Object.prototype`** (`constructor`, `toString`, `__proto__` without an own member) read as absent in `a[k]` (a function is never `===` a JSON value). Golden cases `syn/param-constructor-name`, `-tostring-name`, `-proto-name-text`.
6. **Object / array `!==` is identity**: a param whose value is an object or array is "different" unless both sides are the same `Json` handle. So `chains_diff(p, p)` with the same document is Same, and two separately parsed twins with an object-valued param are Structural (what the page does with two clones). A module id that is an object / array never equals anything (the handle cannot be hashed): the only divergence from JS is two ids that are the same object in one shared document (not reachable through the page, which clones).
7. **Cable keys** use JS string conversion of `from[0]`, `from[1]`, `to[0]`, `to[1]` (numbers through `js_number_to_string`, null -> "null", missing -> "undefined", arrays -> join(","), objects -> "[object Object]", strings by UTF-16 unit), so `[1,"o"]` and `["1","o"]` are the same cable and `["a.b","c"]` collides with `["a","b.c"]`, as in the page.
8. The class is decided by the first Structural cause: the page's order of evaluation (adds, removes, params, text, cables) is not reproduced because a throw and any non-param op are all Structural. Only the order of the PARAM list is contractual and is tested.
9. `bus_plan_equal` compares `nFeeds`, `feedLen`, the first `nFeeds[t]` entries of each feed list (the rest is not part of the plan: always zero from `bus_plan`), `feedMask`, `targets`, `loopTo`, `srcAll`, `busAll`, `pure`, `pre`, `loop`.
10. The file is compiled `-fno-exceptions` like the rest of `thunder_core` (no `try`, no throwing library call: no `at()`, `stoi`...); the only failure is an allocation failure. Control thread only (it allocates). No `lxr` in `native/core`. MSVC: standard C++17 only, no VLAs, no big stack arrays.

## Mutation table (each seeded in `cdiff.cpp` one at a time, test run, then undone; the file is unmodified in the commit)
| # | Fault | Result (first failing pair) |
|---|---|---|
| M1 | text compare always "equal" | killed, 8: `electro/text-added` |
| M2 | duplicate id: first module wins instead of last | killed, 8: `syn/dup-ids-last-wins-change-in-last` |
| M3 | for-in order: index keys not sorted numerically | killed, 2: `syn/param-order-index-keys-raw-unsorted` (survived the first golden, which only had keys JSON.stringify had already sorted: raw pair added) |
| M3b | for-in order: index keys not moved before the others | killed, 5: `syn/param-order-index-keys-first` |
| M4 | feedback mark ignored | killed, 4: `electro/fb-mark-set` |
| M5 | non-finite param value (1e999) accepted as a Params op | killed, 1: `syn/raw-infinity-value` |
| M6 | `ti2` left out of the reload rule | killed, 1: `electro/ti2-removed` |
| M7 | text not cut at 200 UTF-16 units | killed, 2: `electro/text-beyond-200-units-ignored` |
| M8 | non-string module id accepted as a param op | killed, 3: `syn/ids-number-param-change` |
| M9 | null module does not throw (survived at first; pair with null in BOTH documents added) | killed, 1: `syn/module-null-in-both-throws` |
| M10 | id-set size check dropped (a removed module unseen) | killed, 4: `electro/remove-module` |
| M11 | cable without from / to does not throw (survived at first; both-sides pairs added) | killed, 2: `syn/cable-without-from-in-both-throws` |
| M12 | cable key: null part written as empty string | killed, 1: `syn/cable-key-null-part-equals-string-null` (survived at first) |
| M13 | number ids keyed as strings (1 equals "1") | killed, 2: `syn/ids-number-vs-string` |
| M14 | `st2` compared by number, not truthiness | killed, 1: `electro/st2-string-0-vs-1-both-truthy` |
| M15 | param op value read from prev | killed, 42: `electro/param-one` |
| M16 | param list reversed | killed, 16: `electro/param-two-modules-order` |
| M17 | `bus_plan_equal` ignores `loop` | killed, 1 |
| M18 | `bus_plan_equal` ignores `busAll` | killed, 33 |
| M19 | `bus_plan_equal` compares feed entries past `nFeeds` | killed, 2 |

## For the lead to rule on
- Decision 1 (non-string ids are Structural) and decision 6 (object identity) are my reading of the contract; say if C2b wants numeric ids to flow through as param ops (then `ChainsParamOp::id` needs another type).
- The golden is 1 MB (`electro` full set; the other songs a few edits each). If that is too large for the repo, cut the `many-*` pairs in the tool.
- Only the targets this unit touches were built here; the rest of the tree was not rebuilt (the change to `thunder_core` is one added source file, and `native/tests/CMakeLists.txt` one appended block).

## Results (final head)
- Build: `cmake -S native -B native/build-c2a -DCMAKE_BUILD_TYPE=Release -DTHUNDER_DEPS_DIR=/home/claude/deps`, `cmake --build native/build-c2a --target test_chains_diff -j2`.
- `ctest -R chains_diff` in `native/build-c2a`: `chains_diff` Passed, `chains_diff_golden_current` Passed (2/2). Output: `chains_diff: 234 pairs (Same 89, Params 44, Structural 101 of which 19 throw in the page; 15 raw-text), 0 failure(s)`.
- `node native/tools/chains-diff-dump.mjs --check`: current (234 cases).
- The test linked with ASan + UBSan (`-fsanitize=address,undefined`, cdiff.cpp, json.cpp and the test instrumented): same output, no report.
