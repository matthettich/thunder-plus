#!/bin/sh
# Prepares the LXR sources for a HOST build (U04): copies exactly what lxr/build.sh compiles (lxr/src/firmware/Core, lxr/src/stub, lxr/src/lxr_wasm.c) into OUT and applies the host-only patches
# below. lxr/ itself is never edited (the web app's lxr.wasm is built from it as it is). Usage: prepare-lxr.sh LXR_DIR OUT   (LXR_DIR = the repo's lxr/ folder)
# Every patch fails loudly (exit 1) if its text is not found exactly once: a new firmware pin must be looked at, not skipped.
set -e
L=$1; O=$2
[ -n "$L" ] && [ -n "$O" ] || { echo "usage: prepare-lxr.sh LXR_DIR OUT" >&2; exit 2; }
rm -rf "$O"; mkdir -p "$O"
cp -r "$L/src/firmware/Core" "$O/Core"; cp -r "$L/src/stub" "$O/stub"; cp "$L/src/lxr_wasm.c" "$O/lxr_wasm.c"
python3 - "$O" <<'PY'
import sys, os
O = sys.argv[1]
def patch(rel, pairs):
    p = os.path.join(O, rel); s = open(p, encoding='utf-8', newline='').read()   # newline='': some firmware files have CRLF line ends, kept as they are
    eol = '\r\n' if '\r\n' in s else '\n'
    for old, new in pairs:
        old = old.replace('\n', eol); new = new.replace('\n', eol)
        if s.count(old) != 1: sys.exit("lxr prepare: patch text not found exactly once in %s: %r" % (rel, old))
        s = s.replace(old, new)
    open(p, 'w', encoding='utf-8', newline='').write(s)

# 1. Placement attributes of the STM32 (CCM / ITCM sections): meaningless on a host, and a Mach-O section needs "segment,section". With them empty the variables are ordinary
#    .data / .bss, which is also what the module's power-on snapshot (stubs: lxr_host_snapshot) relies on. Headers only; no code changes.
patch('Core/Inc/Soundengine/config.h', [
  ('#define INDTCM __attribute__ ((section(".dtcm")))', '#define INDTCM'),
  ('#define INDTCMZ __attribute__ ((section(".dtcmz")))', '#define INDTCMZ'),
  ('#define IN_ITCM_RAM __attribute__((section(".itcm_text")))', '#define IN_ITCM_RAM')])
patch('Core/Inc/main.h', [
  ('#define INDTCM __attribute__ ((section(".dtcm")))', '#define INDTCM'),
  ('#define INDTCMZ __attribute__ ((section(".dtcmz")))', '#define INDTCMZ')])

# 2. modulationNode.c modNode_setOriginalValueChanged(): reads *(uint8_t*)parameterArray[idx].ptr for the node's destination without the `if(!p->ptr)` guard the three other
#    functions of the file have ("PATROT"). Destination 0 (a node that was never given one: modNode_init) has a NULL ptr: a host reads address 0 and crashes; the wasm reads its
#    linear memory's byte 0 and stores it in an originalValue nothing uses (paramArray_setParameter / modNode_updateValue return early on a NULL ptr). Trigger: lxr_load() on every kit.
patch('Core/Src/Soundengine/DSPAudio/modulationNode.c', [
  ('\t\tconst Parameter *p=&parameterArray[idx];\n\n\t\t\t//update orig value\n',
   '\t\tconst Parameter *p=&parameterArray[idx];\n\t\tif(!p->ptr) return;   /* Thunder host patch: see native/lxr/prepare-lxr.sh */\n\n\t\t\t//update orig value\n')])

# 3. MidiParser.c midiParser_ccHandler(): midiParser_originalCcValues is uint8_t[0xff] (255 entries). Two stores index it out of range:
#    (a) CC branch: paramNr = msg.data1 - 1 as uint16_t; for data1 == 0 (the glue's parameter 127: (p + 1) & 0x7f) that is 65535, and the store is [paramNr + 1] = [65536]: 64 KiB past the table.
#    (b) CC2 / NRPN branch: paramNr = msg.data1 + 1 + 127 reaches 383 (the NRPN data-entry path hands the handler a uint8_t of the active NRPN number: 276 seen with the init kit) and 255 itself.
#    The wasm stores the byte into whatever follows the array in its linear memory; the table is never read back by the sound path. The host patch skips a store that falls outside the table.
patch('Core/Src/Soundengine/MIDI/MidiParser.c', [
  ('\t\t\tmidiParser_originalCcValues[paramNr+1] = value.itg;', '\t\t\tif((uint32_t)paramNr+1 < sizeof(midiParser_originalCcValues)) midiParser_originalCcValues[paramNr+1] = value.itg;   /* Thunder host patch */'),
  ('\t\t\tmidiParser_originalCcValues[paramNr] = value.itg;', '\t\t\tif(paramNr < sizeof(midiParser_originalCcValues)) midiParser_originalCcValues[paramNr] = value.itg;   /* Thunder host patch */')])

# 4. Float -> integer conversions that are out of range (undefined behaviour in C). The wasm (non-trapping conversions) and the STM32 (VCVT) both SATURATE (NaN -> 0, a negative value
#    to an unsigned type -> 0); x86 wraps / returns 0x80000000. The page hears the saturating result, so the host patch routes every conversion that UBSan
#    (-fsanitize=float-cast-overflow) found through lxr_f2u32 / lxr_f2i32 / lxr_f2i16 (saturate to 32 bits, then the low 16 bits for an int16 target, as the wasm does).
#    Helpers: Core/Inc/lxr_host_cvt.h (written here). The sites are listed in docs/port/U04-NOTES.md. Also lxr_tab(): the [1024] reads of the 1024-sample wavetable rows.
open(os.path.join(O, 'Core/Inc/lxr_host_cvt.h'), 'w').write("""/* Thunder host patch (native/lxr/prepare-lxr.sh): saturating float -> integer conversions (the wasm's and the STM32's behaviour; C leaves out-of-range undefined). */
#ifndef LXR_HOST_CVT_H
#define LXR_HOST_CVT_H
#include <stdint.h>
static inline int32_t lxr_f2i32(double x){ if(x!=x) return 0; if(x>=2147483648.0) return INT32_MAX; if(x<=-2147483648.0) return INT32_MIN; return (int32_t)x; }
static inline uint32_t lxr_f2u32(double x){ if(x!=x) return 0; if(x<=0.0) return 0; if(x>=4294967296.0) return UINT32_MAX; return (uint32_t)x; }
static inline int16_t lxr_f2i16(double x){ return (int16_t)(uint16_t)((uint32_t)lxr_f2i32(x)&0xffffu); }
/* A wavetable row is 1024 long and the firmware reads [1024] (one past a row): the next row's first sample; past the last row of a table: that row's own first sample. */
static inline int16_t lxr_tab(const int16_t (*t)[1024], unsigned o, unsigned i){ unsigned f=o*1024u+i; if(i>1023u && o>=10u) f=o*1024u+(i&1023u); return ((const int16_t*)t)[f]; }
#endif
""")
import re
touched = set()
def rx(rel, pat, rep, count):
    p = os.path.join(O, rel); s = open(p, encoding='utf-8', newline='').read()
    s, n = re.subn(pat, rep, s)
    if n != count: sys.exit("lxr prepare: %s: pattern %r matched %d times, expected %d" % (rel, pat, n, count))
    open(p, 'w', encoding='utf-8', newline='').write(s); touched.add(rel)
D = 'Core/Src/Soundengine/DSPAudio/'
OS = D + 'Oscillator.c'
rx(OS, r'return (\(\(\([A-Za-z0-9]+\*f\)/REAL_FS\)\)\*[0-9]+) ;', r'return lxr_f2u32(\1) ;', 3)
rx(OS, r'fast_log2\(\(uint32_t\)\(f/440\.f\)\)', 'fast_log2(lxr_f2u32(f/440.f))', 1)
rx(OS, r'\(\(uint32_t\)\(modBuffer\[i\]\*osc->fmMod\)\)', '(lxr_f2u32(modBuffer[i]*osc->fmMod))', 5)
rx(OS, r'(const uint32_t (?:index2|oscPhase2)) = ((?:index|osc->phase) \+ pwm \* \(UINT32_MAX/2\));', r'\1 = lxr_f2u32(\2);', 2)
rx(OS, r'buf\[i\] = (oscOut|osc->output) \* gain;', r'buf[i] = lxr_f2i16(\1 * gain);', 10)
rx(OS, r'\b(table|sawTable)\[osc->tableOffset\]\[(itg2?(?:\+\+)?)\]', r'lxr_tab(\1, osc->tableOffset, \2)', 26)
# 5. calcNextOscSampleBlock / calcNextOscSampleFmBlock: a waveform number with no oscillator ("sample playback", whose code is commented out or #if 0) leaves the block buffer
#    UNWRITTEN: the page's wasm then outputs whatever the buffer held before (a state of its linear memory), a host reads uninitialised memory. The host patch writes zeros (silence).
rx(OS, r'(default:\s*)(calcUserSampleOscFmBlock\(osc,modBuffer,buf,size,gain\);)', r'\1memset(buf, 0, (size_t)size*sizeof(int16_t)); \2', 1)
rx(OS, r'default://sample playback', 'default: memset(buf, 0, (size_t)size*sizeof(int16_t));//sample playback', 1)
rx(D+'lfo.c', r'return (freq/\(LFO_SR\) \* 0xffffffff);', r'return lxr_f2u32(\1);', 2)
rx(D+'distortion.c', r'buf\[i\] = \(x\*32767\);', 'buf[i] = lxr_f2i16(x*32767);', 2)
rx(D+'Snare.c', r'buf\[j\] =   ?(snareVoice\.combinedVolume\s+\* tmp \* calcOnePole\( &snareVoice\.FaderVolumeFilter, adc_getFaderVolume\(3\)\));', r'buf[j] = lxr_f2i16(\1);', 2)
rx(D+'ResonantFilter.c', r'tmp = (fastTanh\(y1\) \* 0x7fff) ;', r'tmp = lxr_f2i32(\1) ;', 2)
rx(D+'ResonantFilter.c', r'tmp = (\(x-ugb\) \* FILTER_GAIN);', r'tmp = lxr_f2i32(\1);', 2)
rx(D+'transientGenerator.c', r'buf\[i\] = (transient->volume\*\(transientData\[transient->waveform-2\]\[phase\]<<8\) \* \(phase < TRANSIENT_SAMPLE_LENGTH\));', r'buf[i] = lxr_f2i16(\1);', 1)
rx(D+'CymbalVoice.c', r'buf\[j\] \*=  (cymbalVoice\.combinedVolume \* calcOnePole\( &cymbalVoice\.FaderVolumeFilter, adc_getFaderVolume\(4\)\));', r'buf[j] = lxr_f2i16(buf[j] * (\1));', 2)
rx(D+'HiHat.c', r'buf\[j\] \*=  (hatVoice\.combinedVolume \* calcOnePole\( &hatVoice\.FaderVolumeFilter, adc_getFaderVolume\(5\)\));', r'buf[j] = lxr_f2i16(buf[j] * (\1));', 2)
rx(D+'DrumVoice.c', r'buf\[i\] \*= (voiceArray\[voiceNr\]\.combinedVolume);', r'buf[i] = lxr_f2i16(buf[i] * (\1));', 1)
rx('Core/Inc/Soundengine/DSPAudio/BufferTools.h', r'buf\[i\] = (buf\[i\] \* currentGain);', r'buf[i] = lxr_f2i16(\1);', 3)
rx('Core/Src/Soundengine/MIDI/MidiParser.c', r'(\.lfo\.phaseOffset) = (value\.flt \* 0xffffffff);', r'\1 = lxr_f2u32(\2);', 4)
rx(D+'lfo.c', r'uint32_t incInt = inc;', 'uint32_t incInt = lxr_f2u32(inc);', 1)
rx(D+'ResonantFilter.c', r'tmp = (h \* FILTER_GAIN);', r'tmp = lxr_f2i32(\1);', 2)
for rel in sorted(touched):
    p = os.path.join(O, rel); s = open(p, encoding='utf-8', newline='').read()
    eol = '\r\n' if '\r\n' in s else '\n'
    open(p, 'w', encoding='utf-8', newline='').write('#include <string.h>' + eol + '#include "lxr_host_cvt.h"' + eol + s)
PY
echo "lxr sources prepared in $O"
