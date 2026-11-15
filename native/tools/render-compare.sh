#!/usr/bin/env bash
# U09: WORKPLAN section 5's gate "a whole-song render within -80 dB of today's".
#   native/tools/render-compare.sh <song.thunder>
# 1. The reference (REFERENCE B): the web app's own render path in headless Chrome (native/tools/song-ref.mjs: renderOut's steps and
#    pass(), 48 kHz, with every effect, chains, mastering, the limiter and the live engines switched off: see that file). It is too big to
#    commit (40 MB for ELECTRO): it is made when missing or older than the song (Node, playwright and Chrome needed) into
#    $THUNDER_REF (default /tmp/thunder-ref)/<name>, and checked against the committed summary native/tests/golden/song-ref/<name>.json.
# 2. thunder-render (built into $THUNDER_BUILD, default /tmp/u09) renders the switched-off copy of the song from the same rendered pads,
#    twice: with the voices timed as the page player's (--page-timing: what the reference's voices are) and as the core's own.
# 3. The verdict is the first: PASS at or below -80 dB of the reference's peak. The second figure is printed with it and is NOT part of
#    the verdict: the web app's page player (which renders) and its audio-thread core (which the port's voices follow, unit U08) time a
#    voice differently in two places (voice/voice.h, setPageTiming), so a song with slides, bends or a note a hair above a sample
#    differs there between the web app's own two players, and so between the reference and the core-timed render.
set -euo pipefail
SONG="${1:?usage: render-compare.sh <song.thunder>}"
HERE="$(cd "$(dirname "$0")" && pwd)"; ROOT="$(cd "$HERE/../.." && pwd)"
NAME="$(basename "$SONG" .thunder)"; REF="${THUNDER_REF:-/tmp/thunder-ref}/$NAME"; BUILD="${THUNDER_BUILD:-/tmp/u09}"
case "$SONG" in /*) SONGP="$SONG";; *) SONGP="$ROOT/$SONG"; [ -f "$SONGP" ] || SONGP="$PWD/$SONG";; esac
[ -f "$SONGP" ] || { echo "render-compare: no such song: $SONG"; exit 2; }
command -v node >/dev/null || { echo "render-compare: Node is needed (the reference and the comparison)"; exit 2; }
if [ ! -f "$REF/ref.f32" ] || [ ! -f "$REF/index.json" ] || [ "$SONGP" -nt "$REF/ref.f32" ] || [ "$HERE/song-ref.mjs" -nt "$REF/ref.f32" ]; then
  echo "render-compare: making the reference in headless Chrome (a minute or two)..."
  (cd "$ROOT" && node "$HERE/song-ref.mjs" "$SONGP" "$REF") || { echo "render-compare: the reference could not be made (Chrome + playwright needed: CHROME=, PLAYWRIGHT=)"; exit 2; }
else
  node -e '
    const fs = require("fs"), c = require("crypto"), [ref, gp] = process.argv.slice(1);
    const h = c.createHash("sha256").update(fs.readFileSync(ref + "/ref.f32")).digest("hex");
    if (!fs.existsSync(gp)) { console.log("reference: " + ref + " (no committed summary to check it against)"); process.exit(0); }
    const g = JSON.parse(fs.readFileSync(gp, "utf8")), s = fs.existsSync(ref + "/summary.json") ? JSON.parse(fs.readFileSync(ref + "/summary.json", "utf8")) : null;
    if (g.sha256 === h) { console.log("reference: " + ref + ", the same bits as the committed summary (sha256 " + h.slice(0, 16) + ", Chrome " + g.chrome + ")"); process.exit(0); }
    let d = 1; if (s && s.frames === g.frames) { d = 0; for (let i = 0; i < g.fingerprint.length; i++) d = Math.max(d, Math.abs(g.fingerprint[i] - s.fingerprint[i])); }
    console.log("reference: " + ref + ": sha256 differs from the committed summary; largest fingerprint difference " + d.toFixed(5) + (d <= 2e-4 ? " (the same render to the fingerprint precision)" : ": THE REFERENCE HAS MOVED"));
  ' "$REF" "$ROOT/native/tests/golden/song-ref/$NAME.json"
fi
BIN="$BUILD/tools/thunder-render/thunder-render"
if [ ! -x "$BIN" ]; then cmake -S "$ROOT/native" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release >/dev/null && cmake --build "$BUILD" -j8 --target thunder-render >/dev/null; fi
OUT="${TMPDIR:-/tmp}/thunder-render-$NAME"
"$BIN" "$REF/song.thunder" "$REF" "$OUT-page.wav" --page-timing
set +e
node "$HERE/render-diff.mjs" "$OUT-page.wav" "$REF/ref.f32" "voices timed as the page player's (the reference's own): "; A=$?
"$BIN" "$REF/song.thunder" "$REF" "$OUT-core.wav" >/dev/null; RC=$?
node "$HERE/render-diff.mjs" "$OUT-core.wav" "$REF/ref.f32" "voices timed as the core's (not part of the verdict; see voice/voice.h setPageTiming): "
set -e
[ $RC -eq 0 ] || { echo "render-compare: FAIL: the core-timed render reported lost events or a voice that never ended (exit $RC)"; exit 1; }
if [ $A -eq 0 ]; then echo "render-compare: PASS  $NAME within -80 dB of the web app's render (reference B: Chrome, renderOut's path, effects / chains / mastering / limiter / live engines off)"; else echo "render-compare: FAIL  $NAME is above -80 dB"; exit 1; fi
