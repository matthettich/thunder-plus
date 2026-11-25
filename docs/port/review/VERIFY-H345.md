# Independent verification of N2 H3 / H4 / H5 (#317, #318, #319), 25 Nov 2026

Verifier: Claude Fable 5.2 (read-only review; no Android SDK, nothing compiled or run). Heads read: h3 `a4527e1`, h4 `852fc49`, h5 `c01304d`
(h3 / h4 / h5 are now only merges of the H2 verifier fixes over what is described here; the H3 / H4 / H5 Kotlin and the workflow are unchanged by them).
Files read in full: `SongFiles.kt`, `FileModel.kt`, `UpdateCheck.kt`, `MainActivity.kt`, `ShellPolicy.kt`, `Bridge.kt` (file routing), `build.gradle.kts`,
`AndroidManifest.xml`, `.github/workflows/native.yml` (parses: `yaml.safe_load` OK), `ANDROID-CHECKLIST.md`, N2-NOTES H3 / H4 / H5, PICKUP "Owner decisions, 25 Nov 08:56".
Line numbers are those of the file at the branch head named (SongFiles.kt and FileModel.kt at h3; workflow / gradle / UpdateCheck at h4; MainActivity / ShellPolicy at h5).

## Verdicts

| PR | Verdict |
|---|---|
| #317 H3 (songs / samples folders, open, save) | **MERGE AFTER FIXES**: H3-1 (backup failure must stop an in-place save), H3-2 (refuse an empty save), H3-3 (OOM limits). |
| #318 H4 (release APK, signing, new-build check) | **MERGE AFTER FIXES**: H4-1 (owner's key must not reach PR builds), H4-2 (delete the keystore file). The check itself is safe. |
| #319 H5 (fold, Back, keyboard, screen off) | **MERGE AFTER FIXES**: H5-1 (the owner's decision 3, swipe-away, is not implemented and the checklist expects the opposite). Back / fold / keyboard are correct by reading. |

No finding says "do not merge". Nothing here is a crash on the audio thread.

## Findings

### H3 (#317)

**H3-1  HIGH  `SongFiles.kt:219-225` and `:233-242`  (a failed backup does not stop the overwrite)**
`save()` calls `backup()` (which swallows every failure: "A failure here never stops the save") and then opens the file with mode `"wt"`, which TRUNCATES it at once, then writes.
Scenario: the phone is nearly full (the usual state of a phone with a producer's samples). `backup()` does `File.writeBytes(old)` into `filesDir`: it fails with ENOSPC and is caught. The save goes on: `"wt"` truncates the song to 0 bytes, `os.write` fails with ENOSPC, the reply is `write failed`. The song on disk is gone, there is NO backup, and the page may still hold the only copy (until the owner closes it). The same chain applies when `readBytes(old)` returns null (file > 128 MiB or an unreadable provider read) and when `filesDir` cannot be created.
Also: the same truncate-then-write window means a process kill, a provider error, or a pulled SD card during the write leaves a short / empty file (the notes accept this and rely on the backup).
Fix: make `backup()` return Boolean. Refuse the save (`{ok:false, error:"cannot keep a backup, not saved", noBackup:true}`) when it is false, unless the page sends an explicit `force` AND the file is gone / unreadable (`now == null`). Check free space first (`filesDir.usableSpace > old.size + bytes.size`). Better still: write the new bytes to the app's private dir first, check them, and only then open the target with `"wt"`; and on a failed or `short` write try once to restore the backup into the target at once (do not leave the restore to the owner).

**H3-2  MED  `SongFiles.kt:79` and `:212-215`  (an empty / missing `text` overwrites a song with 0 bytes)**
`j.optString("text")` returns `""` when the key is missing or null, and `save()` encodes and writes it. A page bug, a truncated message, or a message built before the song finished serialising erases the file. (The first backup would hold the content, but a second such save, or the H3-1 conditions, would not.)
Fix: in `file.save` / `file.saveAs` require `j.has("text")` and `text.isNotEmpty()` (a real project is never empty; the page's `JSON.stringify` always has `{`); answer an error otherwise. Also refuse when the new size is a small fraction (say < 1/20) of the old one unless the page sent `force`.

**H3-3  MED  `SongFiles.kt:33-34, 181-208`  (OutOfMemoryError kills the process, and with it the sound)**
`MAX_TEXT` is 128 MiB and `MAX_SAMPLE` 64 MiB. One 100 MiB project is held as `ByteArray` (100) + `ByteArrayOutputStream` copy (up to 200 at peak, `toByteArray` copy) + decoded `String` (100 to 200) + the `JSONObject` + `toString()` (another 100 to 200) + `main.post` + WebMessage copy: well over an Android heap (no `largeHeap`). The same for a 64 MiB sample: base64 is 85 MB of chars, JSONObject and `toString` triple it. `OutOfMemoryError` is an `Error`, so `catch (e: Exception)` at lines 64 and 138 never sees it: the executor thread dies with an uncaught exception and the default handler kills the whole process, including the foreground service and the native engine that is playing. (The code's own comment says "would not fit a WebMessage anyway".) `file.opened` has the same exposure from a file manager (`onIntent`), where the file is chosen by someone else.
Fix: lower the caps to what a WebMessage and the heap can carry (songs 16 MiB, matching `maxSongBytes` in `LiveBridge`; samples 16 to 24 MiB until the binary `take` route exists, or send samples in chunks), check `size` from the provider BEFORE reading, and catch `OutOfMemoryError` / `Throwable` in `handle` and `onResult` and answer an error.

**H3-4  MED  `SongFiles.kt:37, 64, 129`  (one worker thread, no timeouts: a stuck provider stops every file call, including `file.ready`)**
Everything (list, open, save, picker results, `file.ready` which flushes `file.opened`) is queued on ONE thread and each SAF call blocks with no limit. A cloud provider (Drive, OneDrive) or a sleeping SD card that stalls in `openInputStream`, `query` or `openOutputStream` blocks the queue for as long as the provider likes: the page's later saves and `file.ready` never run and no reply comes. The page then shows a hung Save with no error.
Fix: a second executor for listing / reading samples (so a slow sample folder does not hold a save), `Future.get(timeout)` style watchdog that answers `{ok:false, error:"the provider did not answer", timeout:true}` for the page (the blocked call keeps running, but the page is told), and make the page treat a missing reply after N seconds as a failed save.

**H3-5  MED  `SongFiles.kt:271-279` and `:261-267`  (Save As leaves an empty song file behind on a failed write)**
`createDocument` makes the file, then `writeNew` fails (disk full, provider error): the reply is an error, but the 0-byte `Name.thunder` stays in the songs folder. The list is newest-first, so the empty file sits at the TOP of the owner's song list, and opening it says "empty, unreadable".
Fix: in `writeNew` failure (and `short` result), call `DocumentsContract.deleteDocument(cr, uri)` for a file the shell just created (not for the system's CREATE_DOCUMENT result: the user chose that one; there delete too, it is still the shell's new file).

**H3-6  LOW  `SongFiles.kt:226-228`  (a stale size after write is reported as an incomplete save)**
Some providers (cloud, network) report the old cached `COLUMN_SIZE` right after the stream closes. The shell then answers `short:true` ("not saved completely") for a file that is fine, and does NOT update `s.stamp`, so the next save is refused as `changed:true` against the shell's own write.
Fix: on a size mismatch re-query once after ~200 ms and re-read the file's length through `openFileDescriptor(uri,"r").statSize`; on a still-unresolved mismatch update `s.stamp` to `after` anyway (the conflict that follows is false).

**H3-7  LOW  `SongFiles.kt:132, 293` (+ `:75`)  (persisted permissions are never released: the tree grant can be evicted)**
Every opened file (`REQ_OPEN`, VIEW with the persistable flag) takes a persisted grant and none is ever released; `file.forget` and picking a new folder do not release the old tree either. Android keeps at most 128 (API 29) or 512 (API 30+) persisted grants; on 30+ the OLDEST is dropped silently, which is the folder grant picked first. After enough opens the songs folder "disappears" (`treeOf` returns null) with no user action.
Fix: do not persist single-file grants (a session needs the grant only while the process lives; keep the URI string, not the grant) or release the least recently used file grants; call `releasePersistableUriPermission` in `file.forget` and when a folder is replaced.

**H3-8  LOW  `SongFiles.kt:294`  (re-opening the same file replaces the session stamp)**
`onIntent` -> `openUri` does `sessions[key] = Session(...)` with a FRESH stamp. If the owner has the file open in the page with unsaved edits, and the file changed underneath, then opens it again from the file manager ("Open with"), the stamp is refreshed; if the page keeps its old text (its decision), the next save no longer shows `changed:true`. Fix: keep the old session when the key exists and only replace it when the page confirms it took the new text (`file.opened` ack), or send `reloaded:true` and let the page decide.

**H3-9  LOW  `SongFiles.kt:289-294` and the manifest SEND filter  (a shared file is saved "in place" onto the sender's cache copy)**
For SEND the URI is usually another app's cache file. If the sender grants write (rare) a save succeeds on a temp copy and the owner believes the original was saved. The reply carries `writable` from the flags but no `shared:true`. Fix: add `shared:true` for ACTION_SEND and never offer in-place save for it (page: Save As).

**H3-10  LOW  `SongFiles.kt:294`, manifest  (the Intent that opened a file can be delivered again)**
`i.action = null` edits the in-process Intent only. After the process was killed and the task is restored from recents, the system re-delivers the ORIGINAL VIEW intent and the file is opened again (and `file.opened` arrives at a page that has just started, which is fine), but if its URI grant was temporary it is gone: `openUri` fails silently (`Log.i` only). Fix: send `{t:'file.opened', ok:false, error}` instead of nothing.

**H3-11  LOW  `SongFiles.kt:105-120`  (a lost picker result leaves `pending` set)**
`pending` is cleared only in `onResult` or on a `startActivityForResult` exception. If the picker's process dies or the activity is finishing, every later pick / open / saveAs answers "another picker is open" until the app restarts. Fix: a 5-minute timeout on `pending`, or clear it in `onDestroy` of the activity that started it.

**H3-12  LOW  `FileModel.kt` `songFileName`  (names)**
Checked: separators, reserved characters and control characters become `_`, leading / trailing dots trimmed, 80 chars, `.thunder` added. `"..."` -> `Song.thunder`, `"a.json"` stays `.json` (fine). Two gaps: names are not normalised for case on providers that fold case (`unique` handles that) and an 80-character cut can split a surrogate pair (`take(80)` on UTF-16): an emoji at the cut produces a lone surrogate in the name, which some providers refuse. Fix: cut on code points.

Checked and found fine: `underneath()` logic (mtime and size, GONE when the query fails: a save over a missing file asks first, never writes); the stamp is taken BEFORE the read (`openUri`), so a write during the read shows as changed; a revoked permission makes `stampOf` return null (-> `gone`) and a forced write then fails with `SecurityException` -> `readOnly` (nothing lost); `createDocument` never overwrites (providers rename on a clash); the BOM and invalid-UTF-8 / lone-surrogate handling; no file IO on the main thread (`onResult` and the intent path only enqueue; `takePersistableUriPermission` on main in `onIntent` is a quick binder call); rotation / fold cannot interrupt a save (the activity is not recreated and the worker is process-wide); the reply of a save finished after the activity was recreated goes to the new Bridge and is dropped by the page (ids). A save killed with the process is the H3-1 truncation risk.

### H4 (#318)

Workflow checked line by line.
Fine: top-level `permissions: contents: read`; `contents: write` only on the two publish jobs; the publish jobs run only for `workflow_dispatch` with `release` ticked, or a PUSH whose head commit message contains `[release-apk]` (never a pull request); no `${{ }}` of untrusted text is interpolated into a shell (only `matrix.channel`, `github.run_number`, `steps.ks.outputs`); the secrets are passed through `env:` (masked), not echoed; the step prints only a fixed sentence; `-DEBUGKEY` is the fallback when any of the four secrets is empty, and fork PRs get empty secrets from GitHub, so a fork builds the DEBUGKEY APK; the Gradle side (`haveOwnerKey`) requires the file to exist and all three values non-empty, otherwise it signs with `ci-debug.keystore`, in agreement with the workflow's `signed` flag; beta `app.thunderplus.android.beta`, stable `app.thunderplus.android`, test `app.thunderplus.nativetest`: three ids, they cannot overwrite each other; `versionCode` is `github.run_number`, which only increases within the workflow; only `out/` is uploaded (the keystore is in `$RUNNER_TEMP`, never in `out/`); `gh release create` runs with `--prerelease --latest=false` and a `native-android-...` tag (the desktop updater reads `/releases/latest` and `v<digit>` only).

**H4-1  MED  `native.yml` `android-release-build` ("The owner's keystore" and "Assemble the release APK" steps)  (the owner's signing key reaches every same-repo pull-request build)**
The job runs on `pull_request`, and for a pull request from a branch of THIS repository GitHub does pass the secrets. The Gradle build scripts, the CMake files and any plugin of the PR are then executed with `ANDROID_KEYSTORE_PASS`, `ANDROID_KEY_PASS`, the alias and the decoded keystore path in the environment. A PR that adds three lines to `build.gradle.kts` (or a commit of an agent worker that is wrong or prompt-injected) can print, upload to an artifact, or `curl` the key; and a PR build is signed with the REAL key and uploaded as an artifact (30 days), indistinguishable from a legitimate one. Many branches here are written by workers (`n1/*`, `pc*`, `m*`).
Fix: sign with the owner's key only for `push` to `native` and `workflow_dispatch` (`if: github.event_name != 'pull_request'` on the secret steps; PRs always build `-DEBUGKEY`), and consider a protected GitHub Environment (`environment: android-release`) holding the four secrets with branch rules, so only `native` and manual runs can read them.

**H4-2  LOW  `native.yml` ("The owner's keystore" step)  (the keystore file is never removed)**
`$RUNNER_TEMP/owner.keystore` stays on the runner for the rest of the job, readable by the Gradle build, `apksigner`, and every later step and action (`upload-artifact` is limited to `out/`, but still). The asked-for rule is "removed after use".
Fix: a final `- if: always()  run: rm -f "$RUNNER_TEMP/owner.keystore"` after the assemble step (before the packaging step; the APK is signed by then), and `chmod 600` on creation (`umask 077`).

**H4-3  LOW  `native.yml` ("Name the APK ... check the signature")  (the signature check cannot fail the build)**
Both `aapt2` and `apksigner verify ... | grep ... || true` always succeed, so an APK with the wrong key (a debug-signed build in the `-signed` name, or the reverse) is published without a failure. Fix: when `signed=signed`, fail if the printed SHA-256 equals the repository debug key's (store its fingerprint in the workflow); when `signed=DEBUGKEY`, fail if it does not.

**H4-4  LOW  `native.yml` `android-release-publish`  (re-runs and cancelled runs)**
(a) `gh release create` fails when a re-run of the same run number meets an existing tag (add `--clobber`-like handling: skip when `gh release view "$TAG"` succeeds). (b) The matrix has no `fail-fast: false`: a failure of beta cancels stable. (c) The workflow's `concurrency` has `cancel-in-progress: true`: a second push to `native` can cancel a `[release-apk]` run after beta was published and before stable. (d) A `workflow_dispatch` from ANY branch with `release` ticked publishes `native-android-<ch>-<N>` with the highest N, which the in-app check offers to everybody as "new build" (tags are not branch-aware). Fix: restrict the publish jobs to `github.ref == 'refs/heads/native'`; set `fail-fast: false`; give the release workflow its own concurrency group with `cancel-in-progress: false`.

**H4-5  LOW  `build.gradle.kts:15-19`, version codes**
`versionCode = run_number` is monotonic only while this workflow file and run counter continue. If the workflow is ever renamed / re-created, `run_number` restarts at 1 and Android refuses to install the lower versionCode over the existing app ("package conflicts / downgrade"), and the app-side check treats real new builds as older. Fix: add an offset constant (e.g. `VERSION_BASE = 1000`) now, or derive from the commit count.

**H4-6  LOW  `ci-debug.keystore` (public key)  (a public-key signed APK installs over a DEBUGKEY install)**
Anyone can build an APK for `app.thunderplus.android(.beta)` signed with the repository's published debug key and Android will install it over a DEBUGKEY build of the owner (same signature). Acceptable only because DEBUGKEY builds are for testing: the release notes already say so; make sure the first build the owner keeps real songs in is signed with the owner's key (the notes do say this).

"Check for new build" (`UpdateCheck.kt`, `MainActivity.runUpdateCheck`):
Fine: one GET of `https://api.github.com/repos/matthettich/thunder-plus/releases?per_page=50` (HTTPS, public repo confirmed: `gh api` reports `private: false`), on its own thread (`thunder-update-check`), 8 s timeouts, no identifier except the User-Agent `thunder-plus-android`; it NEVER downloads or installs: the dialog's button opens `html_url` in the browser; offline gives an exception caught -> "Could not check: UnknownHostException" (a toast on the button, silent at start-up); the start-up check is OFF by default; the INTERNET permission is the owner's decision 4.

**H4-7  LOW  `UpdateCheck.kt:78-88` and `MainActivity.kt:123-132`  (dialog on a finished activity; leaks)**
`ui.post { AlertDialog.Builder(this)...show() }` runs after the 8 s wait with no `isFinishing` / `isDestroyed` check. If the system destroyed the activity meanwhile (Back no longer finishes it, but memory pressure and `finish()` do), `show()` throws `WindowManager.BadTokenException` on the main thread: an uncaught crash that also kills the playing engine (same process). The connection is never `disconnect()`ed and the error body is never read on non-200.
Fix: guard with `if (isFinishing || isDestroyed) return@post` and `c.disconnect()` in a `finally`.

**H4-8  LOW  `UpdateCheck.kt:17-18`  (`per_page=50` can miss the Android release)**
Releases are listed newest first across the WHOLE repository (desktop `v1.3.x`, betas, N0 tests, and now two Android tags per publish). Fifty newer non-Android releases push the newest Android build off the first page: the check says "No beta build is published yet" forever. Fix: request `per_page=100` and, if no match, say "could not find" rather than "none published", or query `GET /git/matching-refs/tags/native-android-<channel>-` (tags only, one call, sorted by name, not numeric: compare numbers in code).

**H4-9  LOW  `MainActivity.kt:128`  (the release URL is trusted as is)**
`html_url` from the response is passed to `ACTION_VIEW` without checking that it starts with `https://github.com/matthettich/thunder-plus/releases/`. TLS to api.github.com makes this safe in practice; the check is one line. Fix: validate the prefix, else open the repo's releases page.

### H5 (#319)

Checked: every `configChanges` token the policy lists is in the manifest (`ShellPolicyTest` holds it), so fold / unfold, rotation, split screen, keyboard attach, font size and language do NOT recreate the activity: the same WebView resizes, `onConfigurationChanged` only re-applies insets and posts the panel refresh, the engine, MIDI host and service are untouched (they never reference the activity); `onPause` stops only the panel refresh, so the screen-off audio is the service's; Back: with the registered callback on API 33+ and `onBackPressed` below, `ShellPolicy.back` returns WEB_BACK only when the WebView has history, otherwise `moveTaskToBack(true)`: the app is never finished and the sound KEEPS PLAYING (owner decision 2 is met); the callback class is held as `Any` so API 29 to 32 never load it; the Back callback is unregistered in `onDestroy`; hardware keys (space, letters) go to the WebView by default (the activity overrides no key handler).

**H5-1  MED  (contradicts owner decision 3; `ThunderService.kt` has no `onTaskRemoved`, `ANDROID-CHECKLIST.md` 2.6, N2-NOTES H5)  (swipe-away while playing)**
PICKUP "Owner decisions, 25 Nov 08:56" (3): swiping the app away from recents while playing must STOP playback and save a recovery copy of unsaved changes in `onTaskRemoved`, with "Restore unsaved changes" offered at the next open. None of this exists in H3 / H4 / H5. Today the result is the opposite: the activity is destroyed (`onDestroy`: `webView.destroy()`), the foreground service and the native engine keep playing with no page, and the owner's unsaved edits are lost with the WebView. The checklist item 2.6 and N2-NOTES even say "expected: the sound continues", which is the old guess, not the decision.
Fix (can be a follow-up PR if the owner accepts one more build with the gap, but the checklist must say so): `ThunderService.onTaskRemoved` -> ask the page for its unsaved project (a `shell.beforeClose` bridge message with a 1.5 s deadline, answered with the text), write it to `filesDir/recovery/<songKey>.thunder-recovery` atomically (temp file + rename, in the app's private storage, NOT through the SAF worker), then `LiveEngine.stop()` / `quit` and `stopSelf()`; on the next `file.ready` send `{t:'file.recovery', name, text, mtime}` for the page to offer. Change checklist 2.6 to "sound stops; notification goes; reopen offers the recovery". Related, same fix: the system can also destroy the activity in the background while the service keeps the process alive, which reloads the page on return and loses unsaved edits in the same way; the page should push a recovery copy on every edit burst (debounced) rather than only at task removal.

**H5-2  LOW  `ANDROID-CHECKLIST.md` 2.5  (predictive-back preview)**
Item 2.5 says the back preview "shows the home screen". With an app-registered `OnBackInvokedCallback` (which `registerBack` always registers) the system shows NO cross-activity / home preview. The step would fail for the wrong reason. Fix: reword to "no preview and no jump: Back sends the app to the background".

**H5-3  LOW  `MainActivity.kt:97-102`, minSdk 29  (keyboard on Android 10)**
`setDecorFitsSystemWindows(false)` plus `Type.ime()` in the inset listener works from Android 11 (API 30); on Android 10 the IME inset is not dispatched to a listener without `fitsSystemWindows`, and with the decor not fitting `adjustResize` does nothing, so the keyboard covers the lower page. The owner's phone (Fold 6, Android 14+) is unaffected. Fix: acceptable to leave (minSdk 29 is nominal); or raise minSdk to 30.

**H5-4  LOW  `ShellPolicy.kt` / manifest  (the h5 branch is behind h4's fixes at the time of the first read)**
h5 was `4b248b1` (behind h4's verifier-fixes merge) when read; it is now `c01304d` and contains them. Merge order: #317 -> #318 -> #319 with `origin/native` merged in once, as the stack plan in PICKUP says; nothing to fix.

## Per-PR list of required fixes (summary)

- **#317 H3:** H3-1, H3-2, H3-3 required. H3-4, H3-5 strongly advised (same PR, small). H3-6..H3-12 may follow.
- **#318 H4:** H4-1 and H4-2 required (both are a few lines of YAML). H4-3, H4-4 (restrict publish to `refs/heads/native`) advised; H4-7 (one `isFinishing` guard) advised.
- **#319 H5:** H5-1 must be built (or explicitly deferred by the owner with the checklist changed: a swipe-away leaves the engine playing with no page today). H5-2 is a wording fix.

Not verified (needs a device): that `ACTION_OPEN_DOCUMENT_TREE` grants survive a reboot on the Fold, the VIEW filters on the owner's file manager, the SAF providers' timestamp resolution (the check relies on it), the real keyboard / fold behaviour, and whether `file.*` replies of several MiB cross the port without a stall on the main thread (`sendToPage` posts them to the main thread).
