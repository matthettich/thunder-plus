#!/bin/sh
# H8: sign a built bundle with the hardened runtime and the entitlements of mac/Thunder.entitlements.
#   sh mac/sign.sh "build/desktop/Thunder Plus.app"                    ad hoc ("-"): what CI does, so the bundle runs and the entitlements can be checked
#   sh mac/sign.sh "Thunder Plus.app" "Developer ID Application: NAME (TEAMID)"   a real release (needs the Apple Developer membership); then notarise: xcrun notarytool submit ... --wait; xcrun stapler staple
set -e
APP="$1"; ID="${2:--}"
HERE="$(cd "$(dirname "$0")" && pwd)"
codesign --force --deep --options runtime --timestamp=none --entitlements "$HERE/Thunder.entitlements" --sign "$ID" "$APP"
codesign --verify --deep --strict "$APP"
codesign -dv "$APP" 2>&1 | head -8
