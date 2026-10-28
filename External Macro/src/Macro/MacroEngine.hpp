#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

enum class CurveType
{
    Upwards,
    Backwards,
    Right,
    Left
};

struct CurvePreset
{
    const char* name;
    double horizontalDegrees;
    double verticalDegrees;
    double delayBeforePressingSeconds;
};

inline constexpr std::array<CurvePreset, 4> kCurvePresets{{
    { "Upwards", 30.0, 170.0, 0.350 },
    { "Backwards", 180.0, 100.0, 0.350 },
    { "Right", 80.0, 30.0, 0.350 },
    { "Left", -80.0, 30.0, 0.350 }
}};

struct MacroSettings
{
    int triggerKey = 'C';     // key YOU press to start the macro
    int activationKey = 'Q';  // key the macro presses before the camera move
    int secondaryKey = 'F';

    CurveType curveType = CurveType::Backwards;
    bool customCurveAngle = false;
    bool hotkeyMode = false;

    // Retain the custom values when selecting a preset.
    double horizontalDegrees = 180.0;
    double verticalDegrees = 50.0;

    double rotationDurationSeconds = 0.10;
    // Your in-game Roblox mouse sensitivity. The macro scales its mouse input
    // by 1 / robloxSensitivity. 0.625 reproduces the old 1.6 default.
    double robloxSensitivity = 0.625;
    // Horizontal mouse counts per degree at Roblox sensitivity 1.0, edited
    // under Roblox Sensitivity in the menu; scaled by 1 / robloxSensitivity
    // at run time.
    double mouseCountsPerDegree = 5.0;

    // false: timeline order  -> Look, Delay, Q tap, Restore, Delay, F, F
    // true : Q goes down before the camera and is held through the move
    bool qBeforeCamera = false;

    // How long the Q tap is held (timeline mode).
    double activationHoldSeconds = 0.02;

    // (qBeforeCamera mode) Q goes down this long BEFORE the camera moves.
    double activationLeadSeconds = 0.010;

    double initialDelaySeconds = 0.10;
    double secondaryDelaySeconds = 0.35;
    double secondaryHoldSeconds = 0.0; // hold time per key tap (0 = instant down/up)

    bool instantMode = true;
    bool restoreCamera = true;
};

// Hotkey mode mapping: 1 -> Upwards, 2 -> Backwards, 3 -> Left, 4 -> Right.
// Accepts both the top-row digit keys ('1'-'4') and the numeric keypad.
// Returns true and sets `out` when `virtualKey` is one of those keys.
inline bool CurveTypeForHotkey(int virtualKey, CurveType& out)
{
    switch (virtualKey)
    {
    case '1':
    case VK_NUMPAD1:
        out = CurveType::Upwards;
        return true;
    case '2':
    case VK_NUMPAD2:
        out = CurveType::Backwards;
        return true;
    case '3':
    case VK_NUMPAD3:
        out = CurveType::Left;
        return true;
    case '4':
    case VK_NUMPAD4:
        out = CurveType::Right;
        return true;
    default:
        return false;
    }
}

// Presets override only the two angles and the existing secondary-action delay.
// Hidden timing/calibration settings and stored custom values are unchanged.
MacroSettings GetEffectiveCurveSettings(const MacroSettings& settings);

// Injects one hardware scan-code key event through SendInput. Shared by the
// macro engines so both send identical input that games accept.
bool SendHardwareKey(int virtualKey, bool keyUp);
// Return the exact inverse of the vertical mouse counts emitted by the macro.
int GetVerticalReturnCounts(const MacroSettings& settings, int sentY);

// Smallest Roblox sensitivity used in the inverse; 0.0 would divide by zero.
inline constexpr double kMinRobloxSensitivity = 0.01;
// Roblox clamps camera pitch to +/-80 degrees and discards mouse counts past
// that limit. The macro assumes the camera starts level (pitch 0).
inline constexpr double kRobloxPitchLimitDegrees = 80.0;
// Roblox turns 0.0015*pi rad per vertical mouse count versus 0.002*pi rad per
// horizontal count, so vertical needs 4/3 as many counts per degree.
inline constexpr double kVerticalCountsPerDegreeScale = 4.0 / 3.0;
// CurrentMultiplier = 1.0 / UserRobloxSensitivity, with the input clamped to
// kMinRobloxSensitivity (NaN is treated as the minimum) so the result is finite.
double GetCurrentMultiplier(double userRobloxSensitivity);

// Injectable input boundary lets tests verify the macro sequence without
// sending any mouse or keyboard events to the user's desktop.
struct MacroInputBackend
{
    std::function<bool(int, bool)> sendKey; // virtual key, keyUp
    std::function<bool(int, int)> sendMouse;
    // Retained for tests/future use. The engine never teleports the desktop
    // cursor: the game sees that as a second camera input (double reset).
    std::function<bool(int&, int&)> getCursor; // desktop cursor position (x, y)
    std::function<bool(int, int)> setCursor;
};

class MacroEngine
{
public:
    MacroEngine();
    explicit MacroEngine(MacroInputBackend input);
    ~MacroEngine();

    MacroEngine(const MacroEngine&) = delete;
    MacroEngine& operator=(const MacroEngine&) = delete;

    void SetSettings(const MacroSettings& settings);
    MacroSettings GetSettings() const;

    // Optional license gate: when set, Start() refuses to run while the
    // callback returns false. Unset means allowed (existing tests unaffected).
    void SetLicenseValidator(std::function<bool()> validator);

    bool Start();
    void Stop();
    void EmergencyStop();

    bool IsRunning() const;

private:
    void Run(MacroSettings settings);
    bool ShouldStop();
    void SleepInterruptible(double seconds);

    static bool SendRelativeMouse(int dx, int dy);
    static bool GetCursorPosition(int& x, int& y);
    static bool SetCursorPosition(int x, int y);
    MacroInputBackend m_Input;

    std::atomic<bool> m_Running{ false };
    std::atomic<bool> m_StopRequested{ false };

    mutable std::mutex m_SettingsMutex;
    MacroSettings m_Settings{};
    std::function<bool()> m_LicenseValidator;

    std::thread m_Worker;
};
