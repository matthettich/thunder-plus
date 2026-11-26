#!/bin/sh
# H7: add NSMicrophoneUsageDescription to a built bundle's Info.plist (macOS asks the person the first time an audio input opens; without the key the open is refused). $1 = the bundle's Contents dir.
P="$1/Info.plist"
/usr/libexec/PlistBuddy -c 'Delete :NSMicrophoneUsageDescription' "$P" >/dev/null 2>&1
/usr/libexec/PlistBuddy -c 'Add :NSMicrophoneUsageDescription string Thunder Plus records from your audio interface when you use Record a sample.' "$P"
