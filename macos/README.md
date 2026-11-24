# dribtard for macOS

A separate macOS build of dribtard. The Windows build in `../External Macro` is
not touched: it keeps its own project, sources and releases, and nothing in this
folder is compiled by it. The only shared item is the vendored Dear ImGui copy in
`../External Macro/external/imgui`, which is platform independent.

Everything the macro does on macOS goes through CoreGraphics/AppKit instead of
Win32: a CGEventTap for global keys, CGEvent keyboard/mouse injection, an
always-on-top NSPanel with a Metal view for the menu, and `CGEventSourceKeyState`
for the clicker's hold key.

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
   flag, restores the app's execute permission, signs the app on that Mac and
   launches it.

From then on the app opens with a plain double-click. The helper performs the
same repairs as Apple's own "Open Anyway", plus the permission fix:

```sh
xattr -cr /Applications/dribtard-mac.app
chmod -R u+rwX,go+rX /Applications/dribtard-mac.app
chmod +x /Applications/dribtard-mac.app/Contents/MacOS/dribtard-mac
codesign --force --deep --sign - /Applications/dribtard-mac.app
open /Applications/dribtard-mac.app
```

The `chmod` step matters for a bundle that lost its execute bit: macOS then
reports "cannot be opened" or "is damaged and can't be opened" before the app
even starts. A zip created on Windows (including archives repacked there) does
not store Unix permissions, so those copies always need it.

Or paste this single line into Terminal, which finds the app in `~/Downloads` or
`/Applications` on its own:

```sh
APP=$(ls -d ~/Downloads/dribtard-mac.app /Applications/dribtard-mac.app 2>/dev/null | head -1); if [ -z "$APP" ]; then echo "Move dribtard-mac.app into /Applications first"; else xattr -cr "$APP"; chmod -R u+rwX,go+rX "$APP"; chmod +x "$APP/Contents/MacOS/dribtard-mac"; codesign --force --deep --sign - "$APP"; open "$APP"; fi
```

If double-clicking the helper does nothing, run it through `sh`:

```sh
sh "/Applications/Fix and open.command"
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
2. macOS asks for **Accessibility** and **Input Monitoring**. Grant both in
   System Settings → Privacy & Security: Accessibility lets the app post keys
   to Roblox, and Input Monitoring lets its listen-only tap see the menu key
   and every other hotkey. Without Input Monitoring the tap cannot be created
   at all, so the menu opens on launch but the key that closes it does nothing;
   the panel's status line then reads "Hotkeys off: allow Input Monitoring".
   The app retries its key tap every two seconds, so granting the permission
   starts the menu key straight away — no relaunch needed.
3. Click the Roblox window once so it is frontmost. The panel is click-through
   everywhere except its own card, so clicking the game normally still works.

## What it does

- **General** – the same four curve presets (Upwards / Backwards / Right / Left),
  custom angles, a Roblox slider (0-10) and box value (the engine decimal,
  0.001-100), counts per degree, delay before the block, camera return, and the
  optional 1-4 curve hotkeys. Trigger `C`, ability `Q`, block `F` by default.
- **Clicker** – hold or toggle `X` for a left or right click stream, 1 to 1500 CPS
  (default 16). Every rate uses the same path: press, hold about a third of the
  gap so the game can sample it, release, then spin out the rest of 1/CPS. The
  Structure curve pauses it. Blade Ball does not. A focus blip under 500 ms, or
  a hold-key blip under 150 ms, does not stop a running stream. F12 stops it
  and will not restart a hold until the key is released.
- **Blade Ball** – the armed curve parry, matching the Windows 1.1.13 behaviour:
  sweep the flick (the sweep is three quarters of the duration you set), then
  parry at the end of it with a fixed 20 ms hold. Hotkey mode (1 Up, 2 Back,
  3 Left, 4 Right) and the four presets (Up 10/60, Back 180/35, Right 70/18,
  Left -70/18) apply only while armed. Custom angle keeps your own degrees.
  Its Roblox slider and box value are separate from the General page. Optional
  ability follow-up and camera return. There is no timing jitter.
- **Config** – named profiles: save the whole setup, load it back, delete it.
  The last-used settings still autosave, exactly like Windows.
- **Settings** – the Windows two-column layout: Client (accent, notifications,
  card and panel colours), Keybinds (menu, trigger, ability), Appearance (icon
  glow, window halo, menu opacity, tab fade), Menu mode (Portable/Classic) and
  Miscellaneous (chat detection, darken background, panic key, restore
  defaults, unload).
- **Chat detection** (Settings → Miscellaneous) pauses every macro keybind while
  the in-game chat is open: `/` opens it, Enter sends the message and brings the
  keybinds back, and Escape closes the chat and brings them back too. `F12` and
  the menu key stay live. Detection only listens in the Roblox window and clears
  itself if the game loses focus.
- **UI parity** – the same 900x580 glass panel, icon rail, five tabs, card
  headers, animated switches, trajectory preview, accent-coloured controls and
  top-right notices as the Windows build. Sliders, keys and pages use the same
  measurements and palette, with a rounded 16 px UI font: SF Pro Rounded when
  the system provides it, Arial Rounded as the fallback.
- **F12** is the emergency stop in every mode and cannot be rebound.
- Settings persist to `~/Library/Application Support/dribtard/last.json`,
  debounced and saved on exit.

## Deliberate differences from the Windows build

- **No licence validation yet.** `AppCore::Licensed()` is the single hook every
  start path already calls; wire the licence server check there before shipping.
  The Windows licence client is WinHTTP + DPAPI, so it is a separate piece of
  work on macOS (URLSession + Keychain).
- **No auto-updater and no Spotify widget.** The Windows updater replaces a
  signed `.exe`; the macOS build ships as a zipped, ad-hoc signed `.app` from
  the GitHub Actions workflow, so it needs its own feed and swap logic.
- **Stream proof, memory watch, debug tools and Roblox rejoin are Windows-only.**
  They use Windows capture APIs, process memory access or the Windows Roblox
  client; none of them has a working macOS counterpart here.
- **One screen.** The overlay covers the main display; multi-display setups and
  Spaces switching during a macro are not handled yet.
- **Focus.** The menu sitting in front still counts as the game being focused
  while Roblox is running, so clicking the panel does not pause the clicker.
  The engines still refuse to start when some other app is frontmost.
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
  src/Macro/              structure, clicker and Blade Ball engines
  src/Platform/MacInput   CGEvent injection, key state, Roblox detection
  src/Platform/Hotkeys    listen-only CGEventTap hotkey monitor
  src/Overlay/            transparent NSPanel + Metal + Dear ImGui frame loop
  src/UI/Menu             the panel: rail, four pages, key capture
  src/Configuration/      JSON settings in Application Support
  tests/MacroTests.cpp    engine tests with a fake input backend
```

## Verification status

This port now tracks the Windows 1.1.35 menu feature set (five tabs, configs,
appearance, chat detection, clicker output key). It is written on a Windows
machine that has no macOS toolchain, so the Xcode/CMake build has **not** been
compiled or run here. What was checked locally is the portable C++ layer
(`Menu.cpp`, `Settings.cpp` and the three engines), which passes an MSVC
syntax-only compile; the Objective-C++ files (`App.mm`, `Overlay.mm`) and the
Metal/AppKit runtime are unverified. GitHub Actions builds `macos/` on a Mac
runner when that tree is pushed. Expect to test the permission flow and a real
game session before shipping it.
