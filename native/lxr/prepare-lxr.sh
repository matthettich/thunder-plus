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
PY
echo "lxr sources prepared in $O"
