# lxr/: copyright, licence and trademark notice

This folder is the **LXR engines** of Thunder Plus: the drum, snare, cymbal and hi-hat voices of the LXR-02 drum
machine's firmware, compiled to WebAssembly and played as Thunder Plus engines.

## Licence

Everything in this folder is free software under the **GNU General Public License version 3** (GPL-3.0-only), with
the additional permission and the additional terms (GPL-3.0 section 7) that come with the LXR firmware, which are in
[`src/firmware/NOTICE.md`](src/firmware/NOTICE.md). The full licence text is in [LICENSE](LICENSE).

- **The LXR firmware**: `src/firmware/` holds the firmware files this build reads, unchanged, with the firmware's own
  LICENSE and NOTICE.md. Copyright (C) 2012-2026 Erica Synths. Source:
  https://github.com/erica-synths/lxr-desktop-app, commit `a43cd4fcda23ec3d577dc425261c3eb28bd5abff` (23 Oct 2026).
- **Thunder Plus's additions**, Copyright (C) 2026 the Thunder Plus authors, under the same licence and terms:
  `src/lxr_wasm.c` (the interface the app calls; it reproduces what the firmware does when a kit is loaded, a voice
  is triggered and a block is mixed), `src/stub/` (stand-ins for the STM32 HAL, the Cortex-M DSP instructions and ST's
  USB header), `src/tools/lxr_meta.py`, `src/lxr-engine.src.js`, `build.sh`, and the built `lxr.wasm` and
  `lxr-engine.js`.

The Corresponding Source of `lxr.wasm` and `lxr-engine.js` is this folder: `build.sh` rebuilds both from `src/`.

## Modified version

This is a **modified version** of the LXR firmware's sound engine (GPL-3.0 section 7(c)): it runs in a web page or
desktop app, not on the machine; it renders one voice at a time; its engines are named "Firmware drum", "Firmware
snare", "Firmware cymbal" and "Firmware hat". It is **not official Erica Synths firmware or software**, and Erica
Synths provides no support for it.

## Trademarks

Thunder Plus is not affiliated with, approved by or endorsed by Erica Synths. "Erica Synths", "LXR" and "LXR-02" are
Erica Synths' names; they appear here only to say truthfully where this code comes from and which kit files it reads
(GPL-3.0 section 7(e) terms in `src/firmware/NOTICE.md`).

## Leaving it out

The rest of Thunder Plus does not need this folder. Delete `lxr/` and the app runs without the Firmware engines and
the LXR kit import (the page's `lxr/lxr-engine.js` script tag then fails quietly; it can be removed too). Do this
before distributing Thunder Plus under terms other than the GPL.
