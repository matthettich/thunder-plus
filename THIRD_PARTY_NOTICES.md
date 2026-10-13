# Third-party code

...Thunder's synthesis and effects module (`dsp/`) and the tracker's track-effects module (`dsp/tfx/`),
both compiled to WebAssembly and embedded in `index.html`, are built from the following MIT-licensed projects. The wrappers and the build
script, including a few small patches, are in the `dsp` folder. Effects and oscillators that come from these projects are
given generic names in the app (Cross-mod, Space reverb, Lo-fi…); each one's source is credited below and in the app.

## Mutable Instruments: Plaits, Rings, Clouds, Beads, Elements, Warps (and stmlib)

https://github.com/pichenettes/eurorack. Copyright Emilie Gillet.
"Mutable Instruments", "Plaits", "Rings", "Clouds", "Beads", "Elements" and "Warps" are names of Mutable Instruments'
products; ...Thunder is not affiliated with or endorsed by Mutable Instruments. Warps is the app's Cross-mod effect.

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

## Fonts

The `fonts` folder holds Jost (https://github.com/indestructible-type/Jost, Copyright Owen Earl, indestructible type*;
subset to Latin and the symbols ...Thunder uses) and JetBrains Mono (https://github.com/JetBrains/JetBrainsMono,
Copyright The JetBrains Mono Project Authors). Both are under the SIL Open Font License 1.1, included as
`fonts/OFL-Jost.txt` and `fonts/OFL-JetBrainsMono.txt`. The Matrix skin uses Share Tech Mono
(https://github.com/google/fonts/tree/main/ofl/sharetechmono, Copyright Carrois Type Design), also under the
SIL Open Font License 1.1, included as `fonts/OFL-ShareTechMono.txt`.
