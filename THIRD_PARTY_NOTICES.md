# Third-party code

...Thunder's synthesis and effects module (`dsp/`), the tracker's track-effects module (`dsp/tfx/`) and the modular
modules' module (`dsp/mods/`), all compiled to WebAssembly (the first two loaded by `index.html`, the third embedded in
`chains/chains-worklet.js` and `waves/waves-worklet.js`), are built from the following MIT-licensed projects. The wrappers
and the build scripts, including a few small patches, are in the `dsp` folder. Effects, oscillators and modules that come
from these projects are given generic names in the app (Cross-mod, Space reverb, Lo-fi, Random generator, Function
generator…); each one's source is credited below and in the app.

## Mutable Instruments: Plaits, Rings, Clouds, Beads, Elements, Warps, Marbles, Tides (and stmlib)

https://github.com/pichenettes/eurorack. Copyright Emilie Gillet.
"Mutable Instruments", "Plaits", "Rings", "Clouds", "Beads", "Elements", "Warps", "Marbles" and "Tides" are names of
Mutable Instruments' products; ...Thunder is not affiliated with or endorsed by Mutable Instruments. Warps is the app's
Cross-mod effect. Marbles is the …chains/…waves Random generator and Tides (2) the Function generator (`dsp/mods/`); the
build script makes two small patches to them (sample-rate-independent time constants in Marbles' clock follower, and a
restart for Tides' clock follower).

## Airwindows: Density, ToTape6, EQ, Pressure4, Galactic, TapeDelay2, DeRez2, PurestDrive, Air3, Verbity2, ToVinyl4, Dubly, Cabs and Tube2

In the app DeRez2 is Lo-fi, PurestDrive is Drive, Air3 is Air, Verbity2 is Room reverb, ToVinyl4 is Vinyl, Dubly is Dub tape,
Cabs is Speaker cab and Tube2 is Tube.

https://github.com/airwindows/airwindows. Copyright (c) 2018 Chris Johnson.

## DaisySP: Chorus, Decimator, Flanger, Phaser, Tremolo, Overdrive, Wavefolder, Autowah, PitchShifter, LadderFilter

https://github.com/electro-smith/DaisySP. Copyright (c) 2020 Electrosmith, Corp. Only the MIT-licensed main library is
used (not the separate DaisySP-LGPL modules).

## Cloud Seed (CloudSeedCore)

https://github.com/GhostNoteAudio/CloudSeedCore. Copyright (c) 2024 Ghost Note Audio / Ghost Note Engineering Ltd.
The app's Space reverb. Its delay buffers are shrunk at build time to fit phones (see `dsp/tfx/build.sh`).

## LXR firmware (lxr/, GPL-3.0, optional)

The Firmware drum, snare, cymbal and hat engines and the LXR kit import are the LXR-02 drum machine's firmware
(https://github.com/erica-synths/lxr-desktop-app, Copyright (C) 2012-2026 Erica Synths), compiled to WebAssembly. Unlike
everything else listed here they are under the **GNU General Public License version 3** with the firmware's additional
terms, and they live entirely in the `lxr` folder with their source, licence and notice (`lxr/NOTICE.md`). The app runs
without that folder. Thunder Plus is not affiliated with or endorsed by Erica Synths; "Erica Synths", "LXR" and "LXR-02"
are Erica Synths' names.

---

The Comb resonator, Transient shaper, Trance gate, Multiband squash and Frequency shifter (`dsp/tfx/fx_extra.cc`), and the
…chains Steps, Clock, Repeat and Vocoder modules, are ...Thunder's own code.

All four projects above are released under the MIT License:

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
associated documentation files (the "Software"), to deal in the Software without restriction,
including without limitation the rights to use, copy, modify, merge, publish, distribute,
sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or
substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES
OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

## …pump effects (pump/): the owner's own code, with pffft

`pump/pump.wasm` (the pump EQ, COMP, LIMIT, CALM, TONE, BAND, WIDE, HEAT and METER bus / master effects) is built from
…pump (https://github.com/matthettich/pump, © wake, the author of Thunder Plus): not third-party code. It includes
**pffft** (Julien Pommier's FFT, in the version with Hayati Ayguen's and Dario Mambro's changes), whose licence (BSD-like, from FFTPACK)
asks for this notice with binary copies:

```
Copyright (c) 2020  Dario Mambro ( dario.mambro@gmail.com )
Copyright (c) 2019  Hayati Ayguen ( h_ayguen@web.de )
Copyright (c) 2013  Julien Pommier ( pommier@modartt.com )

Copyright (c) 2004 the University Corporation for Atmospheric
Research ("UCAR"). All rights reserved. Developed by NCAR's
Computational and Information Systems Laboratory, UCAR,
www.cisl.ucar.edu.

Redistribution and use of the Software in source and binary forms,
with or without modification, is permitted provided that the
following conditions are met:

- Neither the names of NCAR's Computational and Information Systems
Laboratory, the University Corporation for Atmospheric Research,
nor the names of its sponsors or contributors may be used to
endorse or promote products derived from this Software without
specific prior written permission.  

- Redistributions of source code must retain the above copyright
notices, this list of conditions, and the disclaimer below.

- Redistributions in binary form must reproduce the above copyright
notice, this list of conditions, and the disclaimer below in the
documentation and/or other materials provided with the
distribution.

THIS SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE CONTRIBUTORS OR COPYRIGHT
HOLDERS BE LIABLE FOR ANY CLAIM, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES OR OTHER LIABILITY, WHETHER IN AN
ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS WITH THE
SOFTWARE.
```

## Fonts

The `fonts` folder holds Jost (https://github.com/indestructible-type/Jost, Copyright Owen Earl, indestructible type*;
subset to Latin and the symbols ...Thunder uses) and JetBrains Mono (https://github.com/JetBrains/JetBrainsMono,
Copyright The JetBrains Mono Project Authors). Both are under the SIL Open Font License 1.1, included as
`fonts/OFL-Jost.txt` and `fonts/OFL-JetBrainsMono.txt`. The Matrix skin uses Share Tech Mono
(https://github.com/google/fonts/tree/main/ofl/sharetechmono, Copyright Carrois Type Design), also under the
SIL Open Font License 1.1, included as `fonts/OFL-ShareTechMono.txt`.

## Algorithms used in our own code (no third-party code)

- **Plate reverb** (…chains/…waves): written from the topology published in Jon Dattorro, "Effect Design, Part 1:
  Reverberator and Other Filters", J. Audio Eng. Soc. 45(9), 1997. No code was copied.
- **Spring reverb**, **Ensemble**, **Ring mod**, **Limiter**, **Meter** and the other Oct 2026 utilities are original code.
- **Voice engine** (…Thunder Plus synth slots): original code. Its letter-to-sound rules are written after the English
  rules in H. S. Elovitz et al., "Automatic Translation of English Text to Phonetics by Means of Letter-to-Sound
  Rules", NRL Report 7948 (US Naval Research Laboratory, 1976), a US government work in the public domain; the
  formant synthesizer follows the published design of D. H. Klatt, "Software for a cascade/parallel formant
  synthesizer", JASA 67(3), 1980. No code was copied.
- **Degrade** (pad FX, track FX, …chains/…waves module): original code.
- **Resonator** (pad FX, track FX, …chains/…waves module): original code.
- **Pluck string / String** (Karplus–Strong, after K. Karplus and A. Strong, "Digital Synthesis of Plucked-String and Drum Timbres", Computer Music Journal 7(2), 1983): original code.
- **Effects added Oct 2026 (pads, track FX and …chains/…waves modules), all original code written from published descriptions:**
  - Spectral and Extreme stretch: the short-time Fourier transform and phase vocoder (Crochiere 1980; Flanagan & Golden
    1966); the stretch follows Nasca's published description of phase-randomised extreme stretching. No Paulstretch code
    (it is GPL) was used or translated.
  - Convolution reverb: partitioned FFT convolution (Gardner 1995; Wefers 2015); its built-in impulse responses use the
    image method for early reflections (Allen & Berkley 1979) and velvet noise (Karjalainen & Järveläinen 2007), and RBJ
    "Audio EQ Cookbook" filters for the cab and telephone types.
  - Pitch correct: YIN pitch detection (de Cheveigné & Kawahara 2002) and pitch-synchronous splicing (PSOLA).
  - Vowel filter: formant frequencies after Peterson & Barney (1952).
  - Reverse echo's Swell: a feedback delay network (Jot & Chaigne 1991).
  - Rotary speaker, Granular delay, Envelope filter, and the module versions of Flex, Step gate, Retrigger, Pitch delay
    and Pitch reverb.
- **Synth engines added Oct 2026, original code:** Vector (vector synthesis and wave sequencing), Scanned (after
  Verplank, Mathews & Shaw, "Scanned Synthesis", ICMC 2000), Resynth (sinusoidal analysis and resynthesis after McAulay &
  Quatieri 1986 and Serra's spectral modelling; YIN for the pitch; Grandke 1983 for the frequency estimates), Bytebeat
  (a formula compiler of our own; the classic formulas among its presets are the short one-liners shared by viznut and
  others in 2011), and Chaos (the Lorenz, Rössler, Chua, Thomas and Duffing systems and the logistic and Hénon maps,
  integrated from their published equations).

## Third-party sounds: FluidR3 Mono GM (General MIDI SoundFont)

`sounds/FluidR3Mono_GM.sf3` is the General MIDI sound set used by Engine: SoundFont (downloaded by the app on first use).
It is MuseScore's MS_General v0.1 fork of FluidR3Mono_GM, from https://github.com/musescore/MuseScore (`share/sound`),
MIT-licensed; its full notice is in `sounds/FluidR3Mono_License.md`, which must travel with it.

FluidR3 (original version) by Frank Wen, Copyright © 2000-2002, 2008. Mono conversion (FluidR3Mono) by Michael Cowgill,
Copyright © 2014-17. Adaptation for MS_General.sf2 by S. Christian Collins, Copyright © 2018. Temple Blocks instrument
provided by Ethan Winer, Copyright © 2002. Drumline Percussion provided by Michael Schorsch, Copyright © 2016.
Released under the MIT license (see `sounds/FluidR3Mono_License.md`).

The SoundFont player itself (the .sf2/.sf3 parser and voice code in `index.html`) is original code written from the
SoundFont 2.04 specification. The Piano, Tine EP, Organ and Swarm engines are original code with no third-party sources.
