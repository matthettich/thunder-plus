#!/bin/sh
# U33: the patched copy of the pinned pump checkout that HOST builds compile (core/pump/CMakeLists.txt). Every patch is a read or write outside a table found by AddressSanitizer in the fuzz of the
# synths (tests/test_pumpsynth_fuzz.cpp), minimal, and commented here with the file, the line, the table's size and what triggers it. The owner's code is not copied into the repo: this makes
# a copy in the build folder. The wasm builds (build-wasm.sh, the web app's pump.wasm / pump-synth.wasm) are NOT patched: their bytes stay what the pin builds (third_party/PINS.md).
# Usage: prepare-pump.sh PUMP_PIN_DIR OUT      (OUT gets common/ wasm/ plugins/ ; fails if a patch does not apply exactly once: the pin moved)
set -e
P=$1; O=$2
[ -n "$P" ] && [ -n "$O" ] || { echo "usage: prepare-pump.sh PUMP_DIR OUT" >&2; exit 2; }
rm -rf "$O"; mkdir -p "$O"
for d in common wasm plugins; do cp -r "$P/$d" "$O/$d"; done
[ ! -d "$O/plugins/machine" ] || { echo "prepare-pump.sh: plugins/machine (GPL) is in the checkout" >&2; exit 1; }
# WOB (plugins/wob/WobEngine.hpp, synthSample(), the SUB oscillator): `subPh += inc / (set.subOct ? 4.0 : 2.0); if (subPh >= 1.0) subPh -= 1.0;` wraps once. A pitch above the sample rate (the note,
# Octave +2, Tune, Wobble pitch and a sample rate of 22050 Hz or less: MIDI note 127, Octave 2, Tune 12 at 22050 Hz gives inc 7.3) makes inc / 2 larger than 1, so subPh keeps growing and tab(subPh)
# reads sinT[(int)(subPh * 4096)] and sinT[... + 1]: the table has 4097 entries. A read up to the whole address space past it: SIGSEGV in the web wasm too (a trap, once the index leaves the 16 MB
# memory). The patch wraps the phase with floor, as the partials above (`phA[k] -= std::floor(phA[k])`) do; for subPh in [1, 2) floor is 1, so every sound that did not read outside the table is unchanged.
python3 - "$O/plugins/wob/WobEngine.hpp" <<'PY'
import sys; p=sys.argv[1]; s=open(p).read()
old="            if (subPh >= 1.0) subPh -= 1.0;\n"
new="            if (subPh >= 1.0) subPh -= std::floor(subPh);   // Thunder (prepare-pump.sh): was `subPh -= 1.0`, which left subPh above 1 and tab() reading past sinT[4096] when inc / 2 > 1\n"
if s.count(old)!=1: sys.exit("WobEngine.hpp patch failed (the sub phase wrap is not there exactly once)")
open(p,'w').write(s.replace(old,new,1))
PY

# BandWaves (common/dsp/Synth.hpp, read()) and GLOSS's DigiWaves (plugins/gloss/GlossEngine.hpp, read()): both index a table with `(int)(p * n)` and expect the phase p in [0, 1). Every caller keeps its phase there with
# `ph += inc; if (ph >= 1.0) ph -= 1.0;`, which wraps ONCE: with inc >= 1 (a pitch at or above the sample rate: MIDI note 127 at 8000 Hz; GLOSS's voice / supersaw / digital machines at 8000 Hz and Tune +67 reproduce it) ph stays
# at or above 1 and the read goes past the end of the table: BandWaves' tables hold n + 3 floats (259 to 2051; one table per octave), DigiWaves' tables kN + 1. Heap over-read (found by AddressSanitizer in the fuzz; in the web wasm
# the same read lands in linear memory: no trap for a small overshoot, garbage in the sample). The patch wraps the phase into [0, 1) at the top of read() (floor; NaN and infinity read position 0): for p already in [0, 1) the code
# does nothing, so every sound that did not read outside a table is unchanged.
python3 - "$O/common/dsp/Synth.hpp" "$O/plugins/gloss/GlossEngine.hpp" <<'PY'
import sys
def patch(path, old, new):
    s=open(path).read()
    if s.count(old)!=1: sys.exit(path+": patch failed (the read() header is not there exactly once)")
    open(path,'w').write(s.replace(old,new,1))
WRAP="        if (!(p >= 0.0 && p < 1.0)) { p -= std::floor(p); if (!(p >= 0.0 && p < 1.0)) p = 0.0; }   // Thunder (prepare-pump.sh): the phase is not always in [0, 1) (inc >= 1 wraps once); NaN reads 0\n"
patch(sys.argv[1], "    static inline double read(const std::vector<float>& d, double p)\n    {\n", "    static inline double read(const std::vector<float>& d, double p)\n    {\n"+WRAP)
patch(sys.argv[2], "    inline double read(int w, int m, double p) const\n    {\n", "    inline double read(int w, int m, double p) const\n    {\n"+WRAP)
PY
