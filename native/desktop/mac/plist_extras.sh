#!/bin/sh
# H8: add the .thunder document type / UTI to a built bundle's Info.plist. $1 = the bundle's Contents dir, $2 = mac/Info-extra.plist. (The microphone text is mac/add_mic_key.sh, H7.)
set -e
P="$1/Info.plist"
/usr/libexec/PlistBuddy -c 'Delete :CFBundleDocumentTypes' "$P" >/dev/null 2>&1 || true
/usr/libexec/PlistBuddy -c 'Delete :UTExportedTypeDeclarations' "$P" >/dev/null 2>&1 || true
/usr/libexec/PlistBuddy -c "Merge $2" "$P"
plutil -lint "$P"
