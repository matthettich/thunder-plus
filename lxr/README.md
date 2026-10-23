# lxr/: the LXR-02's drum voices as Thunder Plus engines

GPL-3.0-only. Read [NOTICE.md](NOTICE.md) first: what is here, whose it is, and how to leave it out.

**What it does.** Four Synth-page engines, in the engine list's *LXR-02 firmware* group:

| Engine | Firmware voice | Notes |
|---|---|---|
| Firmware drum | drum 1–3 (the three are the same voice) | osc, FM, pitch envelope, transient, filter, LFO |
| Firmware snare | snare | osc + noise, repeats |
| Firmware cymbal | cymbal | three FM oscillators |
| Firmware hat | hi-hat | **Plays** closed or open (each has its own decay) |

Each engine's controls are the machine's own 0–127 knobs on its own pages, with its value names (waveforms, filter
types, LFO waves and sync rates, transient samples), read from the firmware's menu tables. **Hit** is how hard the
pad is struck when it renders (for the velocity target). A pad's LFO and velocity reach that voice's own knobs.

**File ▾ → Import LXR kit (.snd)…** reads a kit file from the machine's SD card (8 name bytes, then the sound
parameters) and puts its seven sounds on seven pads: drums 1–3, snare, cymbal, closed hat, open hat. A modulation
target on another voice is dropped: it can't sound on a single pad. The pad Presets list has the firmware's init kit
(*LXR-02 firmware* group).

**How a pad renders.** `lxr-engine.js` restores the module's start-up memory (a freshly powered-on machine, so a pad
renders the same every time), loads the init kit and then the pad's values the way the machine loads a kit
(`preset_sendDrumsetParametersImmediate`), lets a third of a second pass so the knob smoothing settles, triggers the
voice (`voiceControl_noteOn`, with Tune as the note), and runs that voice's blocks the way the mixer does
(`mixer_calcNextSampleBlock`: modulation reset, its LFO, its filter, its blocks, its sample-rate reduction) at the
machine's 44.14 kHz until it dies away. That is resampled to the app's rate (windowed sinc), then the pad's Level and
Drive apply. Not normalised: a kit's balance carries over. Not included: the pan, the output routing and the FX bus
(delay, reverb, compressor), which Thunder Plus has its own of; user samples on the SD card.

**Differences from the machine.** The hardware random number generator is a seeded xorshift (so noise repeats from
render to render); the voice faders are at the top; a tempo-synced LFO follows the song's tempo.

## Rebuilding

```sh
SYSROOT=/path/wasi-sysroot-25.0 RESDIR=/path/resdir sh lxr/build.sh
```

See the top of `build.sh` for the toolchain (clang 18 and wasi-sdk 25). To update the firmware, copy the same files
from a newer commit of https://github.com/erica-synths/lxr-desktop-app into `src/firmware/` (the list is what the
compiler reads: `clang -M` on the files `build.sh` compiles, plus the menu tables `src/tools/lxr_meta.py` reads), note
the commit in `build.sh` and `NOTICE.md`, rebuild, and bump `?v=` in `index.html` and `sw.js`.
