#!/bin/sh
# Fetches the Ableton Link SDK (GPL-2.0-or-later, or Ableton's proprietary licence) into link/sdk at a pinned tag and commit,
# with its one submodule (asio-standalone, Boost Software License). link/sdk is not committed (.gitignore); the CI workflow
# and link/build.js run this first. Change LINK_TAG and LINK_SHA together, and say so in link/NOTICE.md.
set -e
LINK_TAG=Link-4.1.1
LINK_SHA=5ba907ae027a8c2ec8824463e37296dcefc5e9be
D=$(cd "$(dirname "$0")" && pwd)
if [ -f "$D/sdk/.thunder-sha" ] && [ "$(cat "$D/sdk/.thunder-sha")" = "$LINK_SHA" ]; then echo "link/sdk: $LINK_TAG already here"; exit 0; fi
rm -rf "$D/sdk"
git clone --quiet --depth 1 --branch "$LINK_TAG" --recurse-submodules --shallow-submodules https://github.com/Ableton/link.git "$D/sdk"
GOT=$(git -C "$D/sdk" rev-parse HEAD)
if [ "$GOT" != "$LINK_SHA" ]; then echo "link/sdk: $LINK_TAG is $GOT, expected $LINK_SHA: not building against it" >&2; rm -rf "$D/sdk"; exit 1; fi
echo "$LINK_SHA" > "$D/sdk/.thunder-sha"
echo "link/sdk: $LINK_TAG ($LINK_SHA)"
