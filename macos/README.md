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

## First run

1. Launch `dribtard-mac.app`. It is an accessory app: no Dock icon, just the
   floating panel.
2. macOS asks for **Accessibility** permission, or you grant it in
   System Settings → Privacy & Security → Accessibility. Without it the app
   cannot post keys to Roblox and the global hotkeys do not fire. If hotkeys stay
   dead after that, also enable **Input Monitoring**.
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
  sweep the flick, then parry at the end of it with a fixed 20 ms hold, optional
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
