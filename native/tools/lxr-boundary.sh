#!/bin/sh
# U04: the closed-build boundary of the LXR module (GPL-3.0-only). Usage: lxr-boundary.sh BUILD_DIR [OFF]
#  1. SOURCES: nothing under native/core (outside the seam header/source and xeng's use of them), native/tests/CMakeLists.txt's core targets or the tools includes the module's
#     header, the firmware's headers or a firmware symbol; the core never defines THUNDER_HAVE_LXR.
#  2. BINARIES: libthunder_core.a (always) and, with OFF, every object / test binary of the build contain no symbol of the module or the firmware.
# Exit 1 on a finding.
set -e
B=$1; MODE=$2; [ -d "$B" ] || { echo "usage: lxr-boundary.sh BUILD_DIR [OFF]" >&2; exit 2; }
HERE=$(cd "$(dirname "$0")/.." && pwd); bad=0
# 1. sources of the core
hits=$(grep -rnE 'lxr_module\.h|lxr_wasm|lxr_host_|THUNDER_HAVE_LXR|thunder::lxr|Soundengine/|stm32f7xx|voiceArray|snareVoice|hatVoice|cymbalVoice|midiParser_|lxr_init|lxr_render|lxr_trigger' "$HERE/core" --include='*.cpp' --include='*.h' --include='*.c' --include='*.txt' --include='*.cmake' 2>/dev/null | grep -v '/third_party/' || true)
[ -z "$hits" ] || { echo "LXR boundary: the core mentions the module / firmware:"; echo "$hits"; bad=1; }
# the only files of the core that name LXR at all
names=$(grep -rliE 'lxr' "$HERE/core" --include='*.cpp' --include='*.h' --include='*.c' --include='*.txt' 2>/dev/null | sed "s|$HERE/||" | sort | tr '\n' ' ')
echo "files of the core that say LXR: $names"
for f in $names; do case "$f" in core/include/thunder/lxr_seam.h|core/src/lxr/lxr_seam.cpp|core/src/engine/xeng.h|core/src/engine/xeng_voice.cpp|core/CMakeLists.txt|core/pump/CMakeLists.txt) ;; *) echo "LXR boundary: unexpected file $f"; bad=1;; esac; done
# (core/pump/CMakeLists.txt only has a comment about leaving pump's own GPL LXR folder out.)
# 2. binaries
chk() { s=$(nm -A "$1" 2>/dev/null | grep -E ' [TDBRWtdbr] ' | grep -E 'lxr_init|lxr_render|lxr_trigger|lxr_load|lxr_wasm|lxr_host_|thunder3lxr|voiceArray|snareVoice|midiParser_|parameterArray|FX_BUFFER' || true); [ -z "$s" ] || { echo "LXR boundary: $1 has module / firmware symbols:"; echo "$s" | head; bad=1; }; }
chk "$B/core/libthunder_core.a"
if [ "$MODE" = OFF ]; then
  for t in $(find "$B" -type f -perm -u+x -name 'test_*' | head -200); do chk "$t"; done
  [ ! -d "$B/lxr" ] || { echo "LXR boundary: $B/lxr exists in an OFF build"; bad=1; }
fi
[ $bad = 0 ] && echo "LXR boundary: ok" || exit 1
