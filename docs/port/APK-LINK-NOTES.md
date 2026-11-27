# APK-LINK: one fixed link to the newest beta test APK

Direct link (install from the phone, no login, plain .apk):
`https://github.com/matthettich/thunder-plus/releases/download/android-beta-test/thunder-plus-beta-test.apk`

- Job `android-beta-test-link` in `.github/workflows/native.yml` (needs `android-release-build`; runs on pull requests from this repository, manual runs and pushes to `native`).
- It publishes `thunder-plus-beta-test.apk` and `BUILD.txt` (branch, PR, head sha, run, page BUILD number, UTC time, signing) to ONE rolling pre-release, tag `android-beta-test`: created if missing, else files replaced (`--clobber`) and the notes rewritten from BUILD.txt. The newest run wins; `concurrency` keeps two runs from interleaving.
- A PR build is signed with the public debug key (`-DEBUGKEY`, said in BUILD.txt): it will not install over an owner-signed build.
- Updater check: the tag is not `v<digit>` and the release is a pre-release. `electron/updater.js` reads `/releases/latest` (never a pre-release) and `/releases/tags/v<version>`; the Android app's Check for new build (`UpdateCheck.kt`) only matches `^native-android-(stable|beta|test)-<N>$`; `native/desktop` has no updater. All ignore it. Never retag it into those forms.
- The existing `android-release` and `android-release-publish` jobs are unchanged.
