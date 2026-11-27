# Thunder Plus — code review

**Date:** 27 Nov 2026 · **Branch reviewed:** `main` at `045df9f`
**Kind:** read-only review — no application code was changed to produce this document
**Audience:** the agent (or person) that will do the fixing. Every finding has an ID, an exact location, evidence, impact, a suggested fix and an acceptance test.

> **Before you start:** read `CLAUDE.md` and the top of `HANDOVER.md`. Day-to-day work goes on `beta`; `main` only by merging `beta` when the owner asks. If you change anything in `index.html` that the generators copy, run `node tools/make-core.mjs` and `node tools/make-fx-cores.mjs` afterwards. Bump `?v=`, `sw.js`'s `CACHE` and `BUILD` for every page you touch. Add a `HANDOVER.md` entry at the end.

---

## 1. Scope and method

### Reviewed

| Area | Files | Size |
|---|---|---|
| The app | `index.html` | 39,572 lines / 3.98 MB (3.48 MB of it one `<script>` block, lines 3867–39544) |
| Audio worklets | `master-worklet.js`, `chains/chains-worklet.js`, `waves/waves-worklet.js`, `pump/synth/pump-synth-worklet.js` | ~14k lines |
| Generated audio-thread core | `core/seq-core.js` (generated from `index.html` + `core/core-processor.js`) | 1,733 + 853 lines |
| Page ↔ native bridge | `thunder-core-client.js` | 1,065 lines |
| Desktop shell | `electron/main.js`, `preload.js`, `updater.js`, `path-allow.js`, `io-helper.js`, `link-session.js` | ~730 lines |
| Offline / release | `sw.js`, `package.json`, `.github/workflows/*` | — |
| Builders | `tools/make-core.mjs`, `tools/make-fx-cores.mjs`, `tools/make-channel.sh` | — |

### Not reviewed (out of scope)

- `dsp/**` C++ (SoLoud/DAFx effects, `tfx.wasm` sources), `io/**` C++ (miniaudio/libremidi vendored), `link/**` C++ — a separate review would be needed, using the audio-callback rules already in `.coderabbit.yaml`.
- `beta/` and `stable/` were not reviewed line by line (they are build artefacts; see P3-4).
- Test *content* was only spot-checked; the harness was run (P2-7).

### How the findings were produced

Static reading plus scripted scans (`grep`, `awk`, node) over the real files, and one runtime check of `newer()` in isolation. No claim here is taken from the project's own notes where it could be verified directly. Reproduce everything with §9.

---

## 2. Executive summary

The codebase is **far better than its shape suggests**. Comments are unusually honest (known bugs, previous regressions, measured numbers), the worklets are written with real-time discipline (no allocation in `process()`, NaN guards, silence shortcuts, pooled buffers), the Electron file layer has a genuine allow-list, and the modal/keyboard lifecycle is consistently guarded. This is not "spaghetti" in the sense of random code — it is **one enormous, well-documented file with no structural safety net**.

That is where the risk sits. Ranked:

1. **Silent failure is the house style.** 1,033 `catch` blocks in `index.html`; **735 are literally empty**. Only 75 log anything. When audio, storage, IndexedDB or a worker fails, the app carries on and says nothing.
2. **The backup system can fail silently and is sized to exhaust storage.** Every 3 minutes it base64-encodes *all sample audio* and keeps **10 copies** in IndexedDB; a failed backup is swallowed by `catch(e){}`.
3. **The service worker is all-or-nothing.** One missing file in the precache list aborts `install`, so the app silently stops receiving offline updates — and nothing tests the list's completeness.
4. **No static gate at all.** 3.5 MB of JavaScript in one global scope; no lint, no typecheck, no bundler. One mistyped identifier is a blank app and CI would not notice.
5. **The desktop updater cannot compare prereleases.** Verified: `newer('1.3.0','1.3.0-beta.2') === false`, so a tester on `1.3.0-beta.2` is never told `1.3.0` shipped.
6. **Memory is capped by entry count, never by bytes** — undo keeps 200 full song JSONs; `smpHeld` keeps every sample seen in the session.

Nothing in this review found a crash-on-load or an obviously wrong-audio bug. The dangerous class is **data loss and silent degradation**, which is what the P1 items target.

---

## 3. P1 — fix first (data loss, wrong behaviour, silent breakage)

### P1-1 · Desktop updater treats a prerelease as newer than its final release

**Where:** `electron/updater.js:20–24` (`newer`), used at `:130` (`ghCheck`) and in the electron-updater handlers.

**Evidence (run, not inferred):**

```
newer('1.3.0', '1.3.0-beta.2')  -> false      // wrong: the final release IS newer
newer('1.2.0', '1.3.0-beta.2')  -> false      // correct by accident
```

`newer()` splits on `.` and `parseInt`s each part, so `1.3.0-beta.2` becomes `[1,3,0,2]` and sorts **above** `1.3.0` (`[1,3,0]`). Consequences:

- A tester running `1.3.0-beta.2` is told "You have the newest Thunder Plus" when `1.3.0` ships, and the GitHub-fallback announcement never fires.
- `told.add(version)` de-duplication keys on the raw string, so mixed formats (`v1.3.0` vs `1.3.0`) can announce twice.

**Fix:** replace with a real semver comparison — parse the `-prerelease` part separately; a version with a prerelease suffix is always *lower* than the same version without one; compare numeric prerelease identifiers numerically. No dependency needed (~25 lines), but unit-test it.

**Acceptance:** new `tests/updater-version.test.js` asserting at least: `1.3.0 > 1.3.0-beta.2`, `1.3.0-beta.2 > 1.3.0-beta.1`, `1.3.1 > 1.3.0`, `1.10.0 > 1.9.9`, `1.3.0 > 1.3.0-rc.1`, and existing `1.2.9 < 1.2.10`.

---

### P1-2 · Backups: 10 base64 copies of all sample audio, failures swallowed

**Where:** `index.html:27161–27172` (`BK_N`, `backupNow`, the 3-minute timer), `index.html:26785` (`projText`), `index.html:15088` (`kitToJSON`), `index.html:15084` (`layerToJSON`), `index.html:15056–15062` (`smpToJSON` → `smpB64`).

**Evidence:**

- `projText()` = `JSON.stringify({… kit: kitToJSON(false) })`. With `noSmp === false`, `layerToJSON` calls `smpToJSON(L.sample)`, which returns `{sr, name, id, b64: smpB64(S.data)}` — PCM converted to Int16 and **base64-encoded inline**.
- `backupNow()` stores that string in IndexedDB (`kvSet('bk' + Date.now(), text)`) and keeps the last **10** (`BK_N = 10`), deleting older ones only after the new one is written.
- The whole body is wrapped in `try { … } catch(e){}` — an **empty catch**. A quota failure or IndexedDB abort is invisible; `restoreBackup` later says "No backups yet" and the user has no reason to suspect anything.
- `smpB64()` (`index.html:15056`) has **no cache**: every backup re-encodes every sample Float32 → Int16 → base64 **on the main thread**, using `String.fromCharCode.apply(null, subarray)` in 32,768-byte chunks.

**Arithmetic (illustration, not a measurement):** one 5-minute mono take at 48 kHz ≈ 28.8 MB as 16-bit; base64 adds ~33% → ~38 MB per backup → **~384 MB** for 10 backups of a project containing only that sample, on top of ~29 MB in the sample store. This grows linearly with total audio and is rewritten every 3 minutes.

**Impact:** likely quota exhaustion (`QuotaExceededError`, browser/Electron eviction), hundreds of ms of main-thread jank every 3 minutes on a phone, and — worst — a silent hole in the safety net the UI promises.

**Fix (do these together):**

1. **Store references, not audio.** Backups should use `kitToJSON('ref')` (the `'ref'` mode already exists and is what undo uses via `kitUndoJSON()` at `index.html:15276`), because the samples already live in IndexedDB keyed by id.
2. **Then fix the coupling that creates:** `smpTidy` (`index.html:~15235–15250`) builds its `used` set from live voices, `smpHeld`, `smbRecent`, IRs, presets and the `KEY`/`TK` localStorage strings — **it does not know about backups**. With ref-only backups, tidy would delete audio a backup needs. Add the refs named by `bkIndex` entries (and `rescueIndex`) to `used`.
3. **Make failure loud.** Replace the empty `catch` with `tpLog(...)` plus a one-shot `toastErr('Backups are failing — export a project file')` — the same pattern `storeWrite()` at `index.html:15251` already uses correctly and is a good model.
4. **Don't re-encode what didn't change.** Cache the encoded string per sample id (invalidate on replace), or encode lazily only for project *files*, never for backups.

**Acceptance:** a test that (a) builds a kit with a fake sample, runs two backups, and asserts the stored text contains no `b64` key; (b) asserts `smpTidy` keeps an id that only a backup references; (c) an injected `kvSet` failure surfaces a toast and a log entry.

---

### P1-3 · One missing file stops the service worker from ever updating again

**Where:** `sw.js:19–22`.

```js
self.addEventListener('install', e => { e.waitUntil(caches.open(CACHE).then(c =>
  c.addAll(FILES.map(f => new Request(f, {cache:'reload'}))) …
```

`Cache.addAll()` rejects if **any** request fails (404, network blip, a file renamed between deploys). A rejected `install` means the new worker is discarded and the **old one stays active indefinitely** — the app keeps serving the previous precache, and the next deploy fails to install for the same reason. Only the `OPTIONAL` list is fault-tolerant; everything the app loads at boot is in `FILES`.

**Impact:** silent, permanent loss of offline support and of SW-delivered updates after a single missing/renamed asset. Nothing tests that `FILES` is complete or resolvable.

**Fix:**

1. Replace `addAll` with a per-file loop: `Promise.allSettled(FILES.map(f => c.add(new Request(f, {cache:'reload'}))))`, then log (do not throw) the failures.
2. Add a repo check (node script, run in `tests.yml`) that every `src`/`href` in `index.html`, `chains/index.html` and `waves/index.html` that carries a `?v=` appears in `sw.js` at the **same** `?v=`, and that each path exists. The scan in §9 shows exactly two misses today (P3-5) and no version mismatches — keep it that way mechanically.
3. Bump `CACHE` and the `?v=` values together; today `CACHE='thunderplus-v686'` is hand-maintained alongside ~60 `?v=` numbers and `BUILD`.

**Acceptance:** the checker fails on a deliberately wrong `?v=`, and a simulated 404 for one file still activates the new worker.

---

## 4. P2 — robustness, memory, performance

### P2-1 · 735 empty `catch` blocks; failures are invisible by default

**Where:** across `index.html`. Reproduce:

```bash
grep -o 'catch *( *[A-Za-z_]* *)[ ]*{[ ]*}' index.html | wc -l   # 735
grep -o 'catch *( *[A-Za-z_]* *)[ ]*{' index.html | wc -l        # 1033 total
grep -o 'catch *( *[A-Za-z_]* *)[ ]*{[^}]*\(tpLog\|console\)' index.html | wc -l  # 75 log
```

Some are defensible (`localStorage.setItem` in a private window, `canvas.getContext` on a phone, `try { f._up() } catch(e){}` in a module-load fan-out). Many are not:

| Line | Swallowed | Why it matters |
|---|---|---|
| `8943` | `lnTempo()` / `prerender()` | a failed re-render after a tempo change leaves pads at the wrong tempo, silently |
| `27171` | `backupNow()` body | see P1-2 — the safety net fails silently |
| `15138–15159` | sample-trim interval | an exception stops memory being reclaimed with no trace |
| `20831` | the 500 ms poll loop | a dead subscription poll is indistinguishable from an idle one |
| `8784` | sample-rate guess (`new AudioContext`, `close()`) | |

Also: `tpLog` (`index.html:~3130`) caps itself at 200 entries, so a long session's diagnostics are overwritten (P3-9).

**Fix (scoped, not a mass rewrite):** change only the catches on **persistence, audio-engine setup, worker/engine messaging and the sequencer timers** to `tpLog('<what failed>', e)`. Leave the cosmetic ones. First-pass target:

```bash
grep -n 'catch *( *[A-Za-z_]* *)[ ]*{[ ]*}' index.html | grep -Ei 'save|store|backup|localStorage|indexedDB|kvg|smp|worker|prerender|audio|ctx|engine|seq'
```

**Acceptance:** no empty catch remains on those paths; a forced throw in `backupNow` and in `prerender` produces a `tpLogs` entry (`tests/app-logic.test.js` already shows how to test `tpLog`).

---

### P2-2 · Undo is capped by entry count, never by bytes

**Where:** `index.html:22403` (`const undo = {past:[], future:[], …}`), caps at `:22425`, `:22446`, `:22459`, plus `:9593`, `:15281`, `:19173`, `:26146` — all `if (undo.past.length > 200) undo.past.shift()`.

Each entry is `JSON.stringify(S)` — the **whole song** — or a whole kit (`kitUndoJSON()`). 200 entries of a large song is the biggest JS-heap consumer after sample audio, and nothing bounds it by size. `undoQuiet` merging only helps one kind of edit.

**Fix:** add a byte budget (track `entry.j.length`, drop oldest until total < ~32 MB), always keeping at least the newest entry. Do not simply reduce the count — the owner relies on deep undo.

**Acceptance:** a test that pushes 500 large synthetic entries and asserts the summed byte size stays under budget and the newest entry survives.

---

### P2-3 · Main-thread re-encode of all audio every 3 minutes

Covered by **P1-2 step 4**. Listed separately because it is the *measurable* user-visible symptom (jank) rather than the data risk. `smpB64` (`15056`) also uses `String.fromCharCode.apply` with 32k arguments — slow and close to engine argument limits; a chunked `TextDecoder`/manual base64 or a cached result is safer.

**Acceptance:** with ~30 MB of samples, a backup produces no long task > 50 ms (measure with `PerformanceObserver({entryTypes:['longtask']})`, or simply time `projText()` in a test with a fake sample).

---

### P2-4 · Permanent timers that never stop for a hidden tab

**Where:** `index.html:8943` (100 ms), `index.html:24485` (50 ms), `index.html:15138` (60 s), `index.html:27172` (3 min).

The file contains very deliberate battery work elsewhere (`bgArm()`, `idleWake()`, `hmParked`, `sndFrame` parking when hidden, the 10 Hz idle deck loop) — these four bypass all of it. The 50 ms one is worst: 20 wake-ups/second, forever, for a tempo-settle stamp.

**Fix:** gate each callback on `document.hidden` (and, for the tempo one, only while a change is pending), or fold them into the existing idle loop. Read `docs/audits/2026-11-10-battery.md` first so you don't undo a deliberate decision.

**Acceptance:** with the tab hidden, none of the four runs its body (spy on the callbacks, or add a debug counter).

---

### P2-5 · The same PERFORM constants are initialised twice; load order is load-bearing

**Where:** `index.html:22326` and `index.html:22756` — both declare

```js
var PERF_SCENES = 16, PERF_MACROS = 8, PERF_PARTS = 16, PLFO_MAX = 4, PLFO_RATES = [...], PLFO_SHAPES = [...];
```

The comment at `22326–22330` explains why: the saved song is read before the PERFORM block runs, so the first copy exists only to satisfy `normSong`. The second copy **silently overwrites it at load**. Any future edit to the first block is dead code; any divergence between the two is a heisenbug.

The same comment records what getting this wrong already cost: *"a song with a track LFO threw … so the app opened the starter beat instead and autosaved it over the song"* (build 660). A data-loss failure mode driven purely by declaration order.

**Fix:** hoist the constants into one block above the saved-song load and delete the duplicate. `tests/startup-song.test.js` already asserts that everything `normSong` touches is declared above the load — extend it to assert no top-level name is declared twice (a ~20-line acorn check; `tools/` already has acorn).

**Acceptance:** the duplicate-name check reports zero duplicates and `tests/startup-song.test.js` stays green.

---

### P2-6 · Native I/O helper is dead for the rest of the session after two crashes

**Where:** `electron/io-helper.js:createRestartGate` (`maxDeaths = 2`), used in `electron/main.js` (`ioGate.canStart()` → `{ok:false, why:'helper stopped'}`).

The design (restart once, then fall back to `getUserMedia`) is deliberate and right for a crash loop, but there is **no way back** without quitting the app, and the page only receives a reason string. One `MessagePortMain` misuse already killed the helper twice in the field (see the comment at the bottom of `io-helper.js`), so this will recur.

**Fix:** reset `deaths` on an explicit user action (the existing Settings → Audio "Reset audio engine" path, or a dedicated "Restart input helper" row) and keep the fallback. Also surface `deaths()` in the playback report so a bug report says *why* native I/O vanished.

**Acceptance:** `tests/io.test.js` gains a case: die twice → start refused → reset → start succeeds.

---

### P2-7 · `npm test` cannot run end-to-end as configured

**Where:** `package.json` (`"test": "node --test tests/*.test.js"`), 217 test files.

Observed (Node v24.22.1, 2 vCPU):

- `timeout 240 npm test` → **exit 124** (killed), preceded by a page of `✖ … 'Promise resolution is still pending but the event loop has already resolved'` for many files.
- The same files run alone pass: `tests/app-logic.test.js` 27/27, `tests/startup-song.test.js` 17 pass + 1 skip, `tests/worklet-memory.test.js` 13/13.

So the failures are the runner starving itself (217 files, several heavy — `worklet-memory` alone takes ~8 s), not broken tests. A suite that cannot complete is not a gate, which matters a lot before a refactor of this size.

**Fix:** add `--test-concurrency=4` (or shard by directory), and give the workflow an explicit timeout. Check the real CI figure in `.github/workflows/tests.yml` before choosing numbers.

**Acceptance:** `npm test` exits 0 in a fresh checkout within a documented wall-clock budget.

---

### P2-8 · No static analysis gate on 3.5 MB of JavaScript

There is no lint, no typecheck and no bundle step. `package.json` has only `test`, `test:golden:update` and `sync`. The only static guards are two generator `--check` modes (`tools/make-core.mjs`, `tools/make-fx-cores.mjs`, run by `tests/core.test.js` / `tests/fx-cores.test.js`).

What a linter would catch here today, at zero risk: undeclared identifiers (`no-undef`), unreachable code after `return`, duplicated object keys, `==`/`!=` slips, unused variables left by a bad edit. What it must **not** do: style, formatting, or reformatting the owner's one-line idiom.

**Fix:** add ESLint (flat config) with `no-undef`, `no-unused-vars` (as `warn`), `no-dupe-keys`, `no-unreachable`, `no-constant-condition`, `no-fallthrough`, running only on the extracted inline script (to a temp file in the CI step), `electron/`, `thunder-core-client.js` and `sw.js`. Expect a large initial `no-undef` list because the script relies on browser globals — declare them; do not silence per line.

**Acceptance:** the step runs in CI and a deliberately misspelled function name fails the build.

---

## 5. P3 — hardening and hygiene

### P3-1 · Permission handlers ignore who is asking

**Where:** `electron/main.js:120–121`:

```js
session.defaultSession.setPermissionRequestHandler((wc, perm, cb, d) => cb(ALLOW.has(perm) || (perm === 'media' && audioOnly(d))));
session.defaultSession.setPermissionCheckHandler((wc, perm, origin, d) => ALLOW.has(perm) || (perm === 'media' && audioOnly(d)));
```

`ALLOW` includes `midiSysex`, `fileSystem`, `clipboard-read`. Neither handler checks `wc === win.webContents` or that `origin` is `app://thunder`, so any frame the session ever serves inherits them. Navigation is locked down (`will-navigate`, `will-frame-navigate`, `setWindowOpenHandler`), which is good — but permission checks should not depend on that staying airtight forever.

**Fix:** require the requesting `webContents` to be the app window's and the origin to be `app://thunder` (reuse `isAppUrl` from `path-allow.js`).

### P3-2 · CSP is report-only and allows inline script

**Where:** `electron/main.js:70–89`. Documented as intentional ("so nothing can break"), with `'unsafe-inline'` unavoidable while the app is one inline `<script>`. That removes the second line of defence if an injection ever appears. A path forward exists only after a build step or nonces — do not attempt it as part of a bug-fix pass.

### P3-3 · `eval` and `new Function` ship in the production app

- `index.html:39425` gates a test API on `?seqtest` in the URL; `index.html:39541` is `ev(src){ return eval(src); }` inside it.
- `index.html:39502/39504` use `new Function(...)` to compile effect-worklet source for the FX map.

The `?seqtest` surface is a deliberate debug tool, but it is present in packaged desktop builds. Consider compiling it out for release, or at least document the exposure. The `new Function` sites are how the worklet blob is built — leave them, but they are why a future CSP cannot drop `'unsafe-eval'`.

### P3-4 · `beta/` and `stable/` duplicate the app in the repository

`beta/` (23 MB) and `stable/` (12 MB) are generated copies (`tools/make-channel.sh`) published under `/beta/` and `/stable/`. `beta/` is produced by CI (`.github/workflows/beta-web.yml`, which correctly refuses to touch anything outside `beta/`); `stable/` is a manual snapshot (build 338 per the script header). Both have READMEs.

The cost is not disk: **every code search now returns three copies**, and a fixing agent will eventually edit the wrong one (`chains/chains-worklet.js` exists 3×; `index.html` differs three ways). Both also carry their own `sw.js` with a different cache prefix, so fixes must be applied to the source.

**Fix (cheap):** have `make-channel.sh` inject a first-line `GENERATED — do not edit; see tools/make-channel.sh` banner into every generated file, and add both folders to `.coderabbit.yaml` exclusions. Do not delete them without the owner's say-so.

### P3-5 · Precached files vs. referenced files: two gaps

A scan of `index.html`, `chains/index.html` and `waves/index.html` against `sw.js` `FILES` found **no `?v=` mismatches** and **no missing `.js`/`.wasm`**, but two referenced images are not precached:

```
chains/index.html: icons/icon-192.png      (i.e. chains/icons/icon-192.png)
waves/index.html:  icons/icon-192.png      (i.e. waves/icons/icon-192.png)
```

They are picked up by the runtime cache after one online visit, so this is cosmetic — but the checker in P1-3 should make it visible either way.

### P3-6 · The runtime cache is unbounded and keyed by full URL

`sw.js:52–58` writes any same-origin (and Google-Fonts) `GET` response into `CACHE`. Because keys include the query string, each `?v=` bump creates entries that `caches.match(req)` will never match again and that are never individually evicted — they disappear only when the whole versioned cache is replaced. Long-lived installs accumulate every asset version they have ever fetched.

**Fix:** skip caching when the response has `Cache-Control: no-store`, cap by entry count, and/or normalise keys by stripping `?v=` (harmless here because the SW already version-stamps `CACHE`).

### P3-7 · Offline navigation with a query string can miss

`sw.js:38–40` handles navigations network-first with `caches.match(req)` as fallback. `Cache.match` defaults to `ignoreSearch: false`, and the precache key is `./index.html`, so an offline load of a URL with a query that has never been *successfully fetched online* (e.g. `?shared=1`, `?seqtest`) falls through to an error page. The network-first handler does cache each successful navigation under its own URL, so this only bites on first-ever offline use.

**Fix:** fall back to `caches.match(new URL(req.url).pathname, {ignoreSearch:true})`, or to the cached `'./index.html'` entry.

### P3-8 · `path-allow.js` does synchronous disk IO per granted path

**Where:** `electron/path-allow.js:34` — `save()` uses `writeFileSync` + `mkdirSync` and is called from `allow()` on every dialog pick, drop and boot-time argv path. It blocks the main process, which also drives the window and update checks. Small, but the wrong thread for it.

**Fix:** debounce to a microtask/`setImmediate` and use `fs.promises`, or write only when the list actually changed.

### P3-9 · Diagnostics are capped at 200 entries and never exported by default

`tpLog` (`index.html:~3130`) keeps 200 entries in `window.tpLogs`; the playback report (`pbLog`, `pbCopy`) is separate and manual. For field bug reports, a rotating buffer that survives a reload (or is attached to the crash path) would be worth more than a larger cap.

---

## 6. Architecture: what "spaghetti" actually means here

### Measured shape

| Metric | Value |
|---|---|
| `index.html` total | 39,572 lines / 3.98 MB |
| One `<script>` block | 3,482,082 bytes (lines 3867–39544) |
| Top-level declarations in that block | **3,145**, all in one global scope (no modules, no imports) |
| Average line length (script) | 97 chars |
| Lines > 300 chars | 497 |
| Longest line | **2,225 chars** (line 29158) |
| `addEventListener` / `removeEventListener` | 765 / 48 |
| `localStorage` references | 209 |
| `catch` blocks | 1,033 (735 empty) |

### Duplication you must not "fix" blindly

There are four copies of parts of this codebase by design. **All are generated from `index.html`**, and two generators have a `--check` mode that CI runs:

- `core/seq-core.js` ← `makeSeq` and everything it reads, cut out of `index.html` by acorn, plus the hand-written `core/core-processor.js`. `tests/core.test.js` fails if stale. **Edit the source, then run `node tools/make-core.mjs`.**
- `FlexCore` / `StepGateCore` / `RetrigCore` / `clkSpb` in `chains/chains-worklet.js` and `waves/waves-worklet.js` ← cut out by `tools/make-fx-cores.mjs`. `tests/fx-cores.test.js` guards it.
- `beta/`, `stable/` ← `tools/make-channel.sh` (P3-4).
- `pump/ui/*.js` (eq, comp, limit, meter, multiband, calm, tone, band, wide, heat, master, gate, tune, chop) are **hand-written siblings**, not generated, sharing `pump/ui/kit.js` (861 lines) as their window framework. This is the one place with genuinely repeated UI logic across ~14 files. If asked to reduce duplication, start here, not with the generated files.

### Structural problems, in order of consequence

1. **No module boundaries.** 3,145 names in one scope means every edit is potentially global, and a redeclared `function` silently wins. Nothing prevents the next duplicate (the only one today is the deliberate `var PERF_*` pair, P2-5).
2. **Ordering is semantic.** Boot reads a saved song before most of the app exists, so definitions must sit above it; the file already documents two data-loss regressions caused by that (TDZ on a `const` further down, build 674; missing PERFORM sizes, build 660). Any hoisting refactor must keep `tests/startup-song.test.js` green.
3. **One-line style at scale.** Bodies on the declaration line save vertical space but defeat diff review: a change to a 2,225-character line is invisible in a PR. CodeRabbit is configured (`.coderabbit.yaml`) but cannot review what it cannot see. "One statement per line" would give the biggest review-quality win per unit of risk — but that is an owner decision, not a bug fix.
4. **The HTML is the compiler input.** Both generators parse `index.html` with acorn. A "harmless" refactor (wrapping code in an IIFE, moving `makeSeq`, changing a `const` to a property) can break generation while the app still works. Always run both `--check`s after touching copied code.
5. **The worklets are the good part.** `master-worklet.js` and the chains/waves/synth processors show real-time discipline: pre-allocated buffers, no object literals in `process()`, NaN/Infinity scrubbing with documented reasoning, silence shortcuts with exact wake conditions, pooled 64 KB rings (`RD_POOL`), and comments that explain *why*. Do not "modernise" these.

---

## 7. What was checked and found sound (do not redo)

Classic leak/bug sites in an app like this, each inspected and **already correct**:

- **Modal/dialog lifecycle.** Every `document.addEventListener('keydown', …)` inside a sheet is paired with a `removeEventListener` in that sheet's `done()`, and `done()` guards on identity (`if (askOpen !== done) return;`, `if (pbOpen !== done) return;`): `askSheet` (`9473/9480`), preset sheet (`12523/12545`, `12649/12683`), sample browser (`16746/16755`), recorder (`17472/17492`), generator panels (`31016/31026`, `31663/31681`), render progress (`15399/15408`), sheet shell (`38580`). `askSheet` also closes any open sheet first.
- **CPU panel** (`36378–36398`): `cpuClose()` clears the 500 ms interval **and** removes the keydown listener. (A naive grep for `clearInterval(P.iv)` reports a false positive — the clear uses `cpuPanel.iv`.)
- **Mouse selection** (`36636–36707`): `mselEnd()` cancels the rAF and clears the auto-scroll interval.
- **MIDI outbox** (`18094`): the 15 ms interval clears itself when the queue empties (`mobFlush`); the hidden-page ledger is capped at 4,096 entries.
- **Sample memory** (`15138`): a 60 s trim interval releases samples no longer referenced by pads, undo, presets or IRs, and prunes `IRDATA` (which used to leak across opened projects).
- **`rcdAnim`** (`17144`): the rAF chain self-terminates when playback ends or the UI closes.
- **Workers/render pipeline** (`8862–8912`): blob URL revoked right after `new Worker`, one job per pad (`pend[]`), stale results rejected via `padRev[]`, jobs rejected on worker death (`rwDown`).
- **Master worklet:** NaN/Infinity scrubbing of settings (`fin()`), `ratio <= 0` and `lo/hi <= 0` guards, `dead` flag so the node can be collected, silence shortcut with documented 1e-9 tolerance, `Date.now()` load meter with its coarse-clock caveat noted.
- **Electron file layer:** allow-list (session + persistent), per-channel extension checks (`isProj` / `isOut`), sender-frame + origin check (`fromApp`), atomic save (`.saving` + rename), traversal-guarded `app://` handler, navigation/window-open guards, single-instance lock, `sandbox: true` + `contextIsolation: true`.
- **HTML injection:** the app consistently uses `el()` (which sets `textContent`), and `esc()` exists at `19387`; sampled `innerHTML` sites build markup from numeric/hex values, and names flow through `toast()`/`Option()`/`textContent`. **No confirmed injection vector found** — but only 4 uses of `esc()` across 45 `innerHTML` sites means future edits are unaudited.
- **Blob URL "leaks":** 12 `createObjectURL`, 3 revokes — the unreleased ones are one-per-context worklet module URLs (`16813`, `16842`, `18933`, `21500`, `21841`, `24981`, `25403`). Bounded by audio contexts per session, so not a practical leak.

---

## 8. Suggested work packages

In this order; each is independently shippable and testable. **Do not** reformat the app, extract modules, or "clean up" the one-line style as part of these.

| # | Package | Files | Test to add/extend | Done when |
|---|---|---|---|---|
| 1 | Semver comparison in the updater | `electron/updater.js` | `tests/updater-version.test.js` (new) | P1-1 cases pass |
| 2 | Backups: refs not base64; loud failure; tidy keeps backup refs | `index.html` (27161–27172, 15056–15088, ~15235) | new test with a fake sample | no `b64` in a backup; failure toasts |
| 3 | Service worker resilience + precache checker | `sw.js`, `tools/`, `.github/workflows/tests.yml` | checker script | a 404 no longer blocks install; checker catches a wrong `?v=` |
| 4 | Log the consequential empty catches | `index.html` (targeted list in P2-1) | extend `app-logic.test.js` | no silent catch on save/backup/engine/scheduler paths |
| 5 | Undo byte budget | `index.html` (~22403) | new unit test | memory bounded, newest entry kept |
| 6 | Visibility-gate the four permanent timers | `index.html` | spy test | no body runs while hidden |
| 7 | De-duplicate the PERFORM constants | `index.html` (22326 / 22756) | extend `startup-song.test.js` + duplicate-name check | zero duplicate top-level names |
| 8 | Helper reset path | `electron/main.js`, `electron/io-helper.js` | `tests/io.test.js` | start after reset succeeds |
| 9 | Test-runner concurrency + CI timeout | `package.json`, `.github/workflows/tests.yml` | — | `npm test` exits 0 |
| 10 | ESLint gate (narrow ruleset) | new config + workflow step | — | a typo fails the build |
| 11 | P3 hardening (permissions, SW cache, `ignoreSearch`, async allow-list save) | `electron/main.js`, `sw.js`, `electron/path-allow.js` | — | manual/unit checks as listed |

**Rules for whoever implements this** (from `CLAUDE.md` and repo conventions):

- Day-to-day work goes on `beta`; `main` only by merging `beta` when the owner asks.
- Changing anything in `index.html` that the generators copy (`makeSeq`, `trackGain`, `curve`, `shapeData`, `sfPick`, `sfVoice`, `pumpPv`, `clkSpb`, `FlexCore`, `StepGateCore`, `RetrigCore`) requires running both `--check`/regenerate tools and committing the output.
- Bump `?v=` for every page script touched, `sw.js`'s `CACHE` and `BUILD`, per the normal routine.
- `npm test`, `tools/recorder-check.mjs` and `tools/browser-check.mjs` must stay green; anything needing a device test gets a line on the owner's punch list in `HANDOVER.md`.
- Add a `HANDOVER.md` entry at the end of the session.

---

## 9. Appendix — commands used (all read-only)

```bash
# sizes and shape
wc -l index.html master-worklet.js thunder-core-client.js core/*.js
awk 'NR>3866 && NR<39545 { n++; len+=length($0); if (length($0)>m){m=length($0);ml=NR} if (length($0)>300) big++ }
     END { printf "lines=%d avg=%.0f >300=%d longest=%d at %d\n", n, len/n, big, m, ml }' index.html
awk 'NR>3866 && NR<39545 && /^(function |async function |const [A-Za-z_$]+ *=|let [A-Za-z_$]+ *=|var [A-Za-z_$]+ *=)/' index.html | wc -l

# error handling
grep -o 'catch *( *[A-Za-z_]* *)[ ]*{[ ]*}' index.html | wc -l
grep -o 'catch *( *[A-Za-z_]* *)[ ]*{' index.html | wc -l

# timers, listeners, rAF
grep -n 'setInterval(' index.html
grep -n 'document\.addEventListener\|window\.addEventListener\|^addEventListener' index.html

# updater comparison (the failing case)
node -e "const newer=(a,b)=>{const A=String(a).split('.').map(n=>parseInt(n,10)||0),B=String(b).split('.').map(n=>parseInt(n,10)||0);for(let i=0;i<Math.max(A.length,B.length);i++){const d=(A[i]||0)-(B[i]||0);if(d)return d>0}return false};console.log(newer('1.3.0','1.3.0-beta.2'))"   # -> false

# tests
timeout 240 npm test                           # exit 124 here (runner capacity)
node --test tests/app-logic.test.js            # 27/27
node --test tests/startup-song.test.js         # 17 pass, 1 skip
node --test tests/worklet-memory.test.js       # 13/13
```

**Caveats:** this is a static review. The app was not run, no song loaded, no audio recorded, no desktop build exercised — so latency/audio-quality claims and anything needing a real device remain unverified. `dsp/`, `io/` and `link/` C++ were not reviewed. Line numbers are against HEAD `045df9f` and will drift.
