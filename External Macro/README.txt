# dribtard

External Windows macro controller with an attach/status overlay.

## Default controls

- Menu Keybind (show/hide menu): `\` by default; click its key or Change to rebind
- Trigger key (you press): `C`
- Ability key: `Q`
- Block key: `F`
- Moonwalk Tech hold key: `X`
- Emergency stop: `F12`
- Hotkey mode (optional toggle): `1` Upwards, `2` Backwards, `3` Left, `4` Right
- Unload button: on the Settings tab (Miscellaneous card); it stops the
  macro and closes the application completely

## Overlay appearance

The menu uses a dark, frosted-glass control-panel design with animated switches,
a live trajectory display, and a configurable animated accent light. The accent,
card color, opacity and glow strengths can be adjusted in Settings. Existing saved
appearance settings are preserved; Restore defaults applies the new palette.
The brand logo uses a full mip chain from `assets/macro_icon.png` so it stays
crisp at menu size.

The translucency depends on the overlay using a premultiplied alpha swap
chain. If a machine cannot provide one, the overlay automatically falls
back to the previous colour-key transparency. Setting the environment
variable `DRIBTARD_LEGACY_PRESENT=1` forces that fallback.

## Settings tab

The menu has four tabs, switched with the icon rail on the left. In Compact mode the panel only
pops up while the pointer is over that rail (or inside the open panel).
Classic mode keeps the rail and the menu always open.
Tabs:

- General: keybinds, camera curve and privacy (the default tab).
- Moonwalk Tech: a separate hold-triggered A/D tap loop.
- Config: save, load and manage named configurations.
- Settings: Limit Menu FPS, Menu mode, Appearance, and the Unload button.

Limit Menu FPS caps how often the menu redraws while it is open, which cuts GPU
and CPU load: choose any value from 10 to 480 FPS (60 by default). A cap can
only lower the rate, never raise it, so anything at or above your monitor's
refresh rate behaves like no cap at all; only the menu is paced, the hotkey loop
keeps its normal timing when the menu is closed. Both settings are kept in
memory and reset when the app restarts.

## Moonwalk Tech tab

Moonwalk Tech is a separate macro with its own tab and its own hold keybind
(`X` by default, rebindable on that tab). Press and hold the hold key while
the game is focused and the loop repeats:

| Step | Duration |
| --- | ---: |
| A down | 55 ms |
| A up, then pause | 5 ms |
| D down | 55 ms |
| D up, loop back to A down | - |

A and D are never held at the same time. Releasing the hold key (or moving
focus away from the game) stops the loop and releases whichever direction it
was holding. F12 emergency stop also stops it, and the loop stays stopped
until the hold key is released once. Moonwalk Tech runs only with a valid
license and does not run alongside the main macro. The hold key is stored with
the rest of the configuration.

## Curve settings

Choose a Curve Type:

| Curve Type | Horizontal | Vertical | Delay Before Pressing |
| --- | ---: | ---: | ---: |
| Upwards | 30 | 170 | 0.350 s |
| Backwards | 180 | 100 | 0.350 s |
| Right | 80 | 30 | 0.350 s |
| Left | -80 | 30 | 0.350 s |

Tick Hotkey Mode to switch curves from the keyboard: `1` selects Upwards,
`2` selects Backwards, `3` selects Left, and `4` selects Right (top-row and
numpad digits both work). Switching this way leaves custom mode so the chosen
preset takes effect immediately. Hotkey mode is off by default. Each switch
pops a small notification in the top right naming the new curve; it shows for
about 2.5 seconds even while the menu is closed (the overlay appears
click-through only for the notification, then hides again).

Tick Custom Curve Angle to edit Horizontal Rotation and Vertical / Pitch.
Each time you enable Custom Curve Angle, it copies the currently selected
preset's horizontal angle, vertical angle, and delay into the editable fields.
For example, Backwards starts custom mode at 180H, 100V, and 0.350 s delay.
The preset dropdown is disabled while custom mode is active. Unticking restores
preset mode; enabling again replaces previous custom edits with that preset.

Rotation Duration and Key Tap Hold are hidden from the
menu, not removed from the engine. Their defaults remain 0.10 s and 0.0 s
respectively. Mouse Counts Per Degree sits under Roblox Sensitivity (default 5.0). Roblox Sensitivity remains visible
(default 0.625, which gives the old 1.6 multiplier). Mouse input is scaled
by 1 / Roblox Sensitivity; values below 0.01 are clamped to 0.01. Settings are in memory only and reset when the app restarts.

## Macro sequence

1. Focus the target game window, then press C (or your configured Trigger Key). The trigger is ignored while any other window is focused.
2. Move the camera using the selected curve.
3. Delay 0.10 s.
4. Press Q, hold for 0.02 s, release Q.
5. Return the camera by reversing the exact mouse movement the macro sent.
6. Delay Before Pressing (0.350 s for all presets).
7. Tap F twice.

Delay Before Pressing is the existing delay before the block key, not the
separate delay before the ability key. Internal qBeforeCamera, instantMode,
and other timing settings are unchanged.

Input uses hardware scan codes (SendInput + KEYEVENTF_SCANCODE).

## Menu keybind

Menu Keybind changes the keyboard key that shows/hides the menu. F12 stays
reserved for emergency stop; mouse buttons cannot be used as the menu key.
The captured key is consumed, so rebinding does not also toggle or start the
macro. If menu and trigger share a key, the menu action takes priority.
Settings, including the menu keybind, reset when the app restarts.

## Camera return

The macro returns the camera by reversing the exact relative mouse counts it
successfully sent, including for the Backwards preset. This keeps the return
aligned with the actual outward input instead of truncating it to a fixed
angle. Do not move the mouse manually during the macro. As this external
SendInput macro cannot read the camera's true pitch, games that discard mouse
movement at a pitch limit may still prevent exact restoration from arbitrary
starting views. Stop and emergency stop also reverse accepted movement and
release held keys.

## Antivirus detections and false-positive review

Several engines have reported generic or machine-learning detections for some
builds of this program. Labels such as CrowdStrike's
`Win/malicious_confidence_60% (D)`, Elastic's `Malicious (high Confidence)`,
MaxSecure's `Trojan.Malware.300983.susgen`, McAfee's `Ti!EDD1BC9DB2B3`,
SecureAge's `Malicious`, and Symantec's `ML.Attribute.HighConfidence` do not
identify a specific malware family or describe a malicious action. CrowdStrike
says its VirusTotal scanner uses an ML confidence score; the `(D)` suffix and
the other vendors' precise feature matches are not explained by these labels.
"High confidence" is vendor-specific, not a shared probability or a vote among
engines. A generic label is a reason to investigate, not proof of a false
positive or proof of malware. VirusTotal aggregates vendor verdicts; it does
not determine whether a file is safe.

This program does synthesize keyboard/mouse input (`SendInput`), polls keys
for user-configured hotkeys (`GetAsyncKeyState`), enumerates processes/windows
to align its visible overlay, and opens the selected target process with only
`SYNCHRONIZE` access to detect exit. Those behaviors can overlap with behaviors
used by unwanted automation and may contribute to heuristic/ML suspicion, but
there is no remote-thread creation, target-memory read/write, persistence,
download, or credential collection in this source. These observations do not
prove which feature caused any vendor verdict; the vendors must confirm that.

For a responsible false-positive review, scan the exact release artifact and
record its version and SHA-256, verify it was built from this source, and send
the file and a concise behavior description to each detecting vendor through
its official submission/support process. Include the exact detection name and
scan date. Do not modify the program to evade scanners or tell users to disable
protection; only the detecting vendor can confirm and correct its verdict.

References: [VirusTotal: how it works](https://docs.virustotal.com/docs/how-it-works),
[VirusTotal: false positives](https://docs.virustotal.com/docs/false-positive),
[CrowdStrike ML scanner](https://www.crowdstrike.com/en-us/blog/crowdstrike-machine-learning-virustotal/),
[Microsoft SendInput](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput),
and [Microsoft OpenProcess](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-openprocess).

## Build and tests

Use Developer Command Prompt for VS 2022:

```cmd
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -T v143
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

Executable: build\Debug\dribtard.exe

For a release build, use the same configure step and then:

```cmd
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Release executable: build\Release\dribtard.exe. Ship it together with the
assets folder next to it (the build copies it automatically); imgui.ini is
created at runtime and does not need to be distributed. The release binary uses
the Visual C++ runtime, so the VC++ 2015-2022 x64 redistributable has to be
installed on the machine that runs it.

Tests exercise presets, preset-to-custom copying, menu key rebinding, hidden
settings, exact camera return counts, the full macro sequence, stop key cleanup,
and ImGui menu interactions. The macro-sequence test uses a fake
input backend, so no desktop or game input is injected.
The generated build directory is not part of the source package.
