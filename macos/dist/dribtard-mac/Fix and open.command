#!/bin/sh
# dribtard: clears Gatekeeper's quarantine, signs the app locally, then opens it.
#
# Double-click this file. It runs in Terminal, prints what it is doing and
# launches dribtard. Nothing is installed and nothing else is needed.

set -u
cd "$(dirname "$0")" || exit 1

APP="$PWD/dribtard-mac.app"
if [ ! -d "$APP" ] && [ -d "/Applications/dribtard-mac.app" ]; then
    APP="/Applications/dribtard-mac.app"
fi

if [ ! -d "$APP" ]; then
    echo "Could not find dribtard-mac.app next to this script, or in /Applications."
    echo
    echo "Move dribtard-mac.app next to this file (or into /Applications), then"
    echo "double-click this script again. Press Return to close."
    read -r _
    exit 1
fi

echo "Fixing $APP"
xattr -dr com.apple.quarantine "$APP" 2>/dev/null
chmod +x "$APP/Contents/MacOS/dribtard-mac" 2>/dev/null
codesign --force --deep --sign - "$APP" 2>/dev/null ||
    echo "note: re-signing reported an error, which is usually harmless here"

echo "Opening dribtard..."
open "$APP"

echo
echo "Done. dribtard has no Dock icon: its panel floats over Roblox."
echo "Press the backslash key ( \\ ) to show or hide it."
echo "You can close this window."
