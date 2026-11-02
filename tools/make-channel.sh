#!/usr/bin/env bash
# Publish a copy of the web app from a git ref into a sub-folder of this repo (e.g. beta/ or stable/), so it runs at
# https://matthettich.github.io/thunder-plus/<dir>/ next to the main app without touching it.
#
#   tools/make-channel.sh <ref> <dir> <cache-prefix> [label]
#   tools/make-channel.sh origin/beta beta thunderbeta- "beta"
#
# What it does (the same steps used by hand for stable/ at build 338):
#   - git archive of <ref>, without sounds/ (the copy reads ../sounds/), electron/, tests/, tools/, docs/, stable/, beta/
#     and the repo's own notes;
#   - sounds/ paths in index.html and sw.js point one folder up;
#   - the service worker's cache is renamed <cache-prefix>… and only clears its own old caches, so the main app's
#     cache and this copy's never delete each other;
#   - the page title gets "(label)" so it is clear which copy is open.
# Saved songs and kits are shared with the main app (same site, same browser storage).
set -euo pipefail
REF=${1:?ref}; DIR=${2:?dir}; PREFIX=${3:?cache prefix}; LABEL=${4:-$DIR}
ROOT=$(git rev-parse --show-toplevel); cd "$ROOT"
case "$DIR" in beta|stable) ;; *) echo "only beta/ or stable/ (got '$DIR')"; exit 1 ;; esac

rm -rf "$DIR"; mkdir -p "$DIR"
git archive "$REF" | tar -x -C "$DIR" \
  --exclude='sounds' --exclude='electron' --exclude='tests' --exclude='tools' --exclude='docs' \
  --exclude='stable' --exclude='beta' --exclude='.github' --exclude='HANDOVER.md' --exclude='package.json' \
  --exclude='gs-tracker.png' --exclude='.gitignore'
SHA=$(git rev-parse --short "$REF")

# ../sounds/
sed -i -E "s#(['\"\`])sounds/#\1../sounds/#g" "$DIR/index.html"
sed -i -E "s#'\./sounds/#'../sounds/#g" "$DIR/sw.js"
# cache name and its clean-up
sed -i -E "s#const CACHE = 'thunderplus-#const CACHE = '${PREFIX}#" "$DIR/sw.js"
sed -i -E "s#k\.startsWith\('thunderplus-'\)#k.startsWith('${PREFIX}')#" "$DIR/sw.js"
grep -q "const CACHE = '${PREFIX}" "$DIR/sw.js" || { echo "sw.js cache name not found"; exit 1; }
grep -q "startsWith('${PREFIX}')" "$DIR/sw.js" || { echo "sw.js clean-up filter not found"; exit 1; }
# title
sed -i -E "0,/<title>([^<]*)<\/title>/s##<title>\1 (${LABEL})</title>#" "$DIR/index.html"

cat > "$DIR/README.md" <<EOF
# Thunder Plus: ${LABEL} copy

Made by \`tools/make-channel.sh\` from \`${REF}\` (commit ${SHA}). Do not edit files here: change the source branch and
publish again. Runs at https://matthettich.github.io/thunder-plus/${DIR}/ and shares saved songs and kits with the main app.
EOF
echo "$DIR/ ← $REF ($SHA)"
