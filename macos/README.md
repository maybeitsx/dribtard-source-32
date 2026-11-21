# dribtard for macOS

A separate macOS build of dribtard. The Windows build in `../External Macro` is
not touched: it keeps its own project, sources and releases, and nothing in this
folder is compiled by it. The only shared item is the vendored Dear ImGui copy in
`../External Macro/external/imgui`, which is platform independent.

Everything the macro does on macOS goes through CoreGraphics/AppKit instead of
Win32: a CGEventTap for global keys, CGEvent keyboard/mouse injection, an
always-on-top NSPanel with a Metal view for the menu, and `CGEventSourceKeyState`
for the Moonwalk hold key.

## Requirements

- macOS 11 or newer (Apple silicon or Intel)
- Xcode command line tools (`xcode-select --install`)
- CMake 3.20 or newer

## Build

```sh
cmake -S macos -B macos/build -DCMAKE_BUILD_TYPE=Release
cmake --build macos/build --config Release
open macos/build/dribtard-mac.app
```

Tests (headless, fake input backend, nothing is sent to the desktop):

```sh
ctest --test-dir macos/build --output-on-failure
```

## Opening the prebuilt app

The bundle is signed ad-hoc, not notarised, so a copy that arrived through a
browser, Mail or AirDrop is quarantined and macOS refuses the first launch with
"cannot be opened". The zip ships `Fix and open.command` next to the app:

1. Drag `dribtard-mac.app` into `/Applications`.
2. Double-click **Fix and open.command**. Terminal opens, clears the quarantine
   flag, signs the app on that Mac and launches it.

From then on the app opens with a plain double-click. The helper is the same
three commands Apple's own "Open Anyway" performs:

```sh
xattr -dr com.apple.quarantine /Applications/dribtard-mac.app
codesign --force --deep --sign - /Applications/dribtard-mac.app
open /Applications/dribtard-mac.app
```

Or paste this single line into Terminal, which finds the app in `~/Downloads` or
`/Applications` on its own:

```sh
APP=$(ls -d ~/Downloads/dribtard-mac.app /Applications/dribtard-mac.app 2>/dev/null | head -1); xattr -dr com.apple.quarantine "$APP"; codesign --force --deep --sign - "$APP"; open "$APP"
```

Without Terminal at all: right-click the app → **Open**, or System Settings →
Privacy & Security → **Open Anyway**. On macOS 15 the right-click shortcut is
often absent, which is why the helper exists. A copy moved onto the Mac from a
USB stick with Finder carries no quarantine flag and opens on a double-click
with no dialog.

Only a Developer ID signature plus notarisation removes the step entirely for
every customer; that needs a Mac and an Apple Developer account, so the build
above cannot do it. The workflow already packages the helper into the artifact.

## First run

1. Launch `dribtard-mac.app`. It is an accessory app: no Dock icon, just the
   floating panel.
2. macOS asks for **Accessibility** permission, or you grant it in
   System Settings → Privacy & Security → Accessibility. Without it the app
   cannot post keys to Roblox and the global hotkeys do not fire; if they stay
   dead after that, also enable **Input Monitoring**. The app retries its key
   tap every two seconds, so granting the permission starts the menu key and
   key capturing straight away — no relaunch needed.
3. Click the Roblox window once so it is frontmost. The panel is click-through
   everywhere except its own card, so clicking the game normally still works.

## What it does

- **General** – the same four curve presets (Upwards / Backwards / Right / Left),
  custom angles, Roblox sensitivity, counts per degree, delay before the block,
  camera return, and the optional 1–4 curve hotkeys. Trigger `C`, ability `Q`,
  block `F` by default.
- **Moonwalk Tech** – hold `X` and the A/D tap loop runs (55 ms per direction,
  5 ms gap, never both at once).
- **Blade Ball** – the armed curve parry, matching the Windows 1.1.7 behaviour:
  sweep the flick, then parry at the end of it with a fixed 20 ms hold, its own
  Roblox sensitivity and flick angles (separate from the General page), optional
  ability follow-up, timing jitter, camera return. There is no parry ticker, no
  parry position, no parry hold control and no shield timer.
- **Settings** – menu key (`\`), accent colour, panel opacity, quit.
- **F12** is the emergency stop in every mode and cannot be rebound.
- Settings persist to `~/Library/Application Support/dribtard/last.json`,
  debounced and saved on exit.

## Deliberate differences from the Windows build

- **No licence validation yet.** `AppCore::Licensed()` is the single hook every
  start path already calls; wire the licence server check there before shipping.
- **No auto-updater and no Spotify widget.**
- **One screen.** The overlay covers the main display; multi-display setups and
  Spaces switching during a macro are not handled yet.
- **Focus.** Clicking the panel makes dribtard frontmost until you click the game
  again, because on macOS a window must be key to receive typing. The engines
  refuse to start while Roblox is not frontmost, exactly like Windows.
- **Game detection** uses the macOS client (bundle id prefix `com.roblox`, or the
  app name `Roblox`), not the Windows process name.
- **Settings are not interchangeable** with Windows: key codes are macOS virtual
  key codes, so the JSON is a different profile.

## Layout

```
macos/
  CMakeLists.txt          separate macOS project
  Info.plist              accessory app bundle
  src/App.mm              engines, gating, autosave, AppKit delegate
  src/Keys.{hpp,cpp}      macOS key codes and labels
  src/Macro/              structure, Moonwalk and Blade Ball engines
  src/Platform/MacInput   CGEvent injection, key state, Roblox detection
  src/Platform/Hotkeys    listen-only CGEventTap hotkey monitor
  src/Overlay/            transparent NSPanel + Metal + Dear ImGui frame loop
  src/UI/Menu             the panel: rail, four pages, key capture
  src/Configuration/      JSON settings in Application Support
  tests/MacroTests.cpp    engine tests with a fake input backend
```

## Verification status

This port was written on a Windows machine that has no macOS toolchain, so the
Xcode/CMake build has **not** been compiled or run. The pure-C++ sources
(engines, key table, settings, menu, tests) were syntax-checked on the host; the
Objective-C++ shell (AppKit window, Metal backends, CGEvent tap) has not been
compiled at all. Expect to fix small compile issues on the first macOS build, and
test the permission flow and a real game session before shipping it.
