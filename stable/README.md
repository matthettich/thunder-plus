# Thunder Plus: stable copy (build 338)

A frozen copy of the app as it was at build 338 (desktop 1.2.57), for when the latest build misbehaves:
https://matthettich.github.io/thunder-plus/stable/

It shares the browser's saved song and kit with the main app (same site), so work carries across, but sounds made with
engines newer than build 338 (Fold, Gloss, Tract, the pump drum machines, Life, Freeze, Wob, the Breaks) play silent here.
It never updates; it is replaced on purpose when a newer build is declared stable. Made from commit 6cf0d34 with
`git archive`, without sounds/ (it reads ../sounds/), electron/, tests/, tools/ and docs/; its service worker caches as
`thunderstable-*` so the main app's doesn't clear it.
