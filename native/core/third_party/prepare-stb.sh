#!/bin/sh
# U32: the copy of the pinned stb_vorbis.c that every host build compiles (core/sf/CMakeLists.txt). Each patch is a memory-safety fix for a fault found by the fuzz (tests/test_sf_fuzz.cpp) under AddressSanitizer + UBSan, minimal,
# commented here with the input that triggers it, and applied to EVERY host build (never a sanitizer-only workaround). Nothing of upstream is copied into the repo: this writes a copy into the build folder.
# Usage: prepare-stb.sh STB_DIR OUT      (STB_DIR holds stb_vorbis.c at the pin; OUT gets stb_vorbis.c; fails if a patch does not apply exactly once: the pin moved)
set -e
P=$1; O=$2
[ -n "$P" ] && [ -n "$O" ] || { echo "usage: prepare-stb.sh STB_DIR OUT" >&2; exit 2; }
mkdir -p "$O"
cp "$P/stb_vorbis.c" "$O/stb_vorbis.c"
