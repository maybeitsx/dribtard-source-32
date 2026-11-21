#pragma once

/*
    The macOS input boundary. Everything the macro engines and the app send or
    read goes through here, so the engines stay platform-neutral and tests can
    substitute their own backend.

    Posting keyboard and mouse events to another application (Roblox) requires
    the user to grant this app Accessibility permission in System Settings ->
    Privacy & Security -> Accessibility. The app prompts on first launch.
*/
namespace MacInput
{
// One key event posted to the HID event tap, using the macOS virtual key code.
bool PostKey(int keyCode, bool keyUp);

// One relative mouse move. Games read the delta fields of the event, which is
// what the camera turn consumes.
bool PostMouseMove(int dx, int dy);

// True while the physical key is down, used by the hold-triggered Moonwalk.
bool IsKeyDown(int keyCode);

// Accessibility / input monitoring permission state.
bool IsTrusted();
void RequestTrust();

// Roblox detection: any running instance, and whether it is the frontmost app.
bool GameRunning();
bool GameFocused();
} // namespace MacInput
