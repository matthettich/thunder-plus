# link/: copyright, licence and trademark notice

This folder is **Ableton Link for the Thunder Plus desktop app**: a small native addon (`thunder_link.node`) that the
desktop app's main process loads, if it is there, to keep Thunder Plus in time with other Link apps on the network.

## Licence

The addon is built from the **Ableton Link SDK**, Copyright 2016 Ableton AG, Berlin, which is free software under the
**GNU General Public License version 2 or (at your option) any later version**, or, for proprietary software, under a
separate licence from Ableton (link-devs@ableton.com). This folder and the built addon are therefore under the
**GPL-2.0-or-later**. The full licence text is in [LICENSE](LICENSE).

- **The Link SDK**: not committed here; `fetch-sdk.sh` clones https://github.com/Ableton/link at tag `Link-4.1.1`,
  commit `5ba907ae027a8c2ec8824463e37296dcefc5e9be` (15 Oct 2026), into `link/sdk`, and checks the commit. It brings
  its one submodule, **asio-standalone** (Christopher M. Kohlhoff, Boost Software License 1.0), which Link uses for its
  networking.
- **Thunder Plus's additions**, Copyright (C) 2026 the Thunder Plus authors, under the same licence: `src/thunder_link.cc`
  (the N-API interface the app calls), `binding.gyp`, `build.js` and `fetch-sdk.sh`, and the built `thunder_link.node`.

The Corresponding Source of `thunder_link.node` is this folder together with the Link SDK at the commit above:
`sh link/fetch-sdk.sh && node link/build.js` rebuilds it.

`electron/link-session.js` and the page's Link code in `index.html` are Thunder Plus's own and contain no Link code; they
talk to the addon only through the functions in `src/thunder_link.cc`, and do nothing when the addon is not there.

## Trademarks

"Ableton" and "Ableton Link" are Ableton AG's names. Thunder Plus is not affiliated with, approved by or endorsed by
Ableton. The name is used to say what the feature is and follows Ableton's Link integration guidelines (the setting is
labelled "Ableton Link", in full, with its state Enabled / Disabled).

## Leaving it out

The rest of Thunder Plus does not need this folder. Delete `link/` (and `electron/link/` if it was built locally) and the
desktop app builds and runs as before; Settings → Audio & MIDI then says Ableton Link is "Not available in this build".
The website never has Link. Do this before distributing a closed build of the desktop app, unless Ableton has granted a
proprietary Link licence for it.
