# Desktop beta icon: notes (19 Nov 2026)

Owner, 19 Nov: "invert the colors of the beta's icon so it's easy to tell which is which ... the brown dog should be grey and the grey brown. Only on the beta."

## Where the desktop icon comes from
- Only one master exists: `icons/plus-icon-512.png` (512 x 512 RGBA, flat art, transparent background since 4 Nov). No SVG, no layered file, no `.icns` / `.ico` in the repo.
- `electron/package.json` `build.mac.icon`, `build.win.icon` and `build.linux.icon` all point at `app/icons/plus-icon-512.png`; electron-builder 26 makes the `.icns` (Mac), `.ico` (Windows, NSIS and portable) and the AppImage icon from that one PNG at build time on each runner. There is no `build/` resource folder, no DMG background and no tray icon.
- `electron/main.js` uses `app/icons/plus-icon-512.png` as the window icon. `electron/copy-app.js` copies the web app's `icons/` into `electron/app/` before every build (`electron/app/` is not committed).
- Web-only icons (not used by the desktop shell, unchanged): `plus-icon-192.png` (favicon, manifest, media session), `plus-icon-maskable-512.png` and `plus-apple-touch-icon.png` (yellow plate kept), and the `icons/icon-*` set.

## The icon
Two dogs' faces split down a thin vertical divider (x = 255): left a brown dog (bear-like, round ear on the left), right a grey dog (husky-like, tall pointed ear). Flat art, no gradients or shadows. Exact fills (from the pixels):
- outline / eyes / nose / divider: ink (22, 22, 26)
- brown dog's fur: (181, 115, 63); its muzzle: tan (236, 201, 160)
- grey dog's fur: (139, 147, 156); its face mask and muzzle: cream (251, 247, 239); a cream sliver of forehead sits on the brown dog's side of the divider
- tongue: pink (240, 138, 168)
- background: fully transparent; the outer edge is anti-aliased ink at partial alpha.

## Beta vs stable today
- Same `appId` (`com.matthettich.thunderplus`), same `productName` ("Thunder Plus"), same user-data folder, same publish target. A beta is told apart only by its version (`1.3.0-beta.N`) and by being a GitHub pre-release (`desktop.yml` input `prerelease`, tag pattern `v*.*.*`). Installed copies on the normal channel ignore pre-releases.
- Nothing about the packaging differed before this change: a beta install REPLACES the stable app on the Mac (same bundle id and name).

## What was done
- `tools/make-beta-icon.py` recolours the 512 master (the only one) in linear light. Each pixel is unmixed as ink share t plus the nearest pure fill; the fill is swapped and t kept, so anti-aliased edges blend correctly and no halo appears. Mapping: brown <-> grey; tan (brown dog's muzzle) -> cream; cream on the grey dog's side -> tan; the cream sliver on the left stays cream. Outline, eyes, nose, tongue, divider and alpha are untouched (pixel counts of each fill match the originals exactly, swapped). The 192 is a premultiplied Lanczos downscale of the new 512.
- Output: `electron/icons-beta/plus-icon-512.png` and `-192.png`.
- `electron/copy-app.js`: when `THUNDER_BETA=1`, the two files replace `app/icons/plus-icon-512.png` / `-192.png` inside `electron/app/` only. `.github/workflows/desktop.yml` sets `THUNDER_BETA=1` for a manual run with "Beta" ticked or a pushed tag containing `-beta`; otherwise 0.
- Background: transparent, as the stable icon already is (shape and padding identical, so the Dock size is the same). Checked on light and dark backgrounds at 512 / 128 / 64 / 32 px in `docs/beta-icon-compare.png`: the ink outline is dark on a dark dock in both stable and beta, the fills carry the read.
