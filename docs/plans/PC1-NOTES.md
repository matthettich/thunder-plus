# PC1 notes: native page client, loader + switch + inert tests

Branch `pc1/core-client-loader` (from `beta` 524948d, build 676). Design: `docs/port/N2-PAGE-CLIENT.md` (lead's checkout), sections 0, 1, 4, 5, 6 (PC1), 9.

## Done
- `index.html`: `NC`, `NCON`, `loadClient` (one block before `srGuess`); branch 2 (`srGuess`), branch 3 (`padSR`), branch 18 (`updCore` segment: option and getter and toast text); one call site at the end of the last script block; `BUILD` 677 and one `CHANGES` line.
- `thunder-core-client.js` (new, root): IIFE, `window.__thunderClient`, `attach(HOST_API)`, `hello` handshake, `detach()`. Nothing else.
- `sw.js`: `'./thunder-core-client.js?v=1'` in `OPTIONAL`; cache `thunderplus-v677`.
- `tests/native-client-inert.test.js`: parts (a), (b), (c), plus the client's handshake against a fake core and the sw entry.

## Cited lines re-located (design W: -> beta 524948d)
| Design | Found at |
|---|---|
| `srGuess` block W:8758-8765 | 8761-8766 (`let srGuess` at 8761 before the edit) |
| `padSR` W:8901 | 8901 |
| `coreWanted` W:25338 | 25338 (unchanged) |
| `updCore` W:15961 | 15961 |
| `aset` defaults W:8717 | 8717 |
| `BUILD` | 27235 |
| `CORE_URL` W:25339 | 25341 |
| `prerender` W:8987 | 8987 |
| `sw.js` OPTIONAL `beta:sw.js:8` | 8 |

## Deviations from the design (each leaves the switch-off path more untouched)
1. `NC` / `NCON` / `loadClient` are `const` arrows (no `window` property), not function declarations: no new global object property at all (test checks this).
2. Branch 2: only the `else` arm gets the condition (`else if (!(window.thunderCore || aset.core === 'native'))`), so `aset.rate` still sets `srGuess` in every mode. The design wrapped the whole block.
3. Loader URL is `thunder-core-client.js?v=1`, not `?v=BUILD`: it must equal the `sw.js` OPTIONAL string or an offline start misses the cache; bump both together when the file changes.
4. Branch 18: the third value is listed only when `window.thunderCore` exists (lead's decision on owner question 5; hidden on the web copy). The getter shows `'native'` only then. The "Web audio (old)" fourth value and the reason line (1.3) are not in PC1.
5. `HOST_API` in PC1 is `{S, kit, aset, toast, tpLog, now, prerender, build}` (the design's full list comes with later PRs; `build` and `prerender` added for the handshake).
6. The client turns `on` only when the `hello` reply has `v === 1`, `caps.nativeAudio` and a usable `sr` (G1). An N0 shell (no `sr`) stays off, so Android today behaves as before.
7. Tests cut the pieces out of `index.html` and run them in a `vm` (as other tests do), instead of loading the whole 39k-line page; no `tests/golden/native-client-off.json` (no `corePost` path is touched in PC1, so there is nothing to record).
8. `BUILD` bumped to 677 with a `CHANGES` line and `sw.js` cache: the repo convention (HANDOVER "changelog line + BUILD + sw.js cache bump per page change", and `startup-song.test.js` requires a `CHANGES` entry for `BUILD`).

## Left (not PC1)
Branches 4-17, 19-22; the client's pads / song / events; `native-client.test.js`.
