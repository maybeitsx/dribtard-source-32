#!/bin/sh
# dribtard: clears Gatekeeper's quarantine, repairs permissions, signs the app
# locally, then opens it.
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

# 1. Quarantine. macOS sets this on anything that arrived through a browser,
#    Mail or AirDrop, and Gatekeeper refuses the first launch of an app that
#    is not notarised by Apple.
xattr -cr "$APP" 2>/dev/null

# 2. Execute permission. A zip made on Windows, or a copy moved with some
#    tools, drops the executable bit. macOS then reports the app "cannot be
#    opened" or "is damaged" before a single line of it runs.
chmod -R u+rwX,go+rX "$APP" 2>/dev/null
chmod +x "$APP/Contents/MacOS/dribtard-mac" 2>/dev/null

# 3. Signature. The shipped bundle is ad-hoc signed, and the signature can be
#    stale after a copy or a quarantine strip, so sign it again on this Mac.
codesign --force --deep --sign - "$APP" 2>/dev/null ||
    echo "note: re-signing reported an error, which is usually harmless here"

if ! codesign --verify --deep "$APP" 2>/dev/null; then
    echo "warning: the signature still does not verify. If the app will not"
    echo "open, move it into /Applications and run this helper again."
fi

echo "Opening dribtard..."
if ! open "$APP"; then
    echo
    echo "macOS still refused to launch it. Open System Settings -> Privacy &"
    echo "Security, scroll to Security, click \"Open Anyway\" next to the"
    echo "message about dribtard-mac, then run this helper once more."
    echo "Press Return to close."
    read -r _
    exit 1
fi

echo
echo "Done. dribtard has no Dock icon: its panel floats over Roblox."
echo "Press the backslash key ( \\ ) to show or hide it."
echo "You can close this window."
