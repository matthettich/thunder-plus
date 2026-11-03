# link/: Ableton Link for the desktop app (optional, GPL-2.0-or-later)

A native Node addon wrapping the Ableton Link SDK, loaded by `electron/main.js` when present. Licence and how to leave it
out: [NOTICE.md](NOTICE.md).

```
sh link/fetch-sdk.sh      # the Link SDK at the pinned tag → link/sdk (not committed)
node link/build.js        # → electron/link/thunder_link.node (+ LICENSE, NOTICE.md); Mac: arm64 + x86_64 joined
```

Needs a C++17 compiler (Xcode / MSVC 2022 / GCC 10+) and node-gyp (npm's own copy is used). Plain N-API, so one build
loads in Electron whatever its version (no electron-rebuild). `.github/workflows/desktop.yml` runs both steps before the
app build, with `continue-on-error`: a failure here never stops a release, the app then has no Link.

What the addon does (`src/thunder_link.cc`): `enable`, `startStopSync`, `now`, `snapshot(quantum)`, `setTempo`,
`start(atUs, quantum, sync)`, `stop`, `timeAtBeat`. No threads or callbacks into JS: `electron/link-session.js` polls
`snapshot` every 20 ms while Link is on and sends the page what changed.

Tests: `node --test tests/link.test.js` (the session against a fake addon; the real addon too when it is built), and
`node tools/link-test.mjs` (the page against a fake peer in headless Chrome).
