#pragma once

#include "Keys.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <functional>
#include <mutex>
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

inline constexpr std::array<CurvePreset, 4> kCurvePresets{ {
    { "Upwards", 30.0, 170.0, 0.350 },
    { "Backwards", 180.0, 100.0, 0.350 },
    { "Right", 80.0, 30.0, 0.350 },
    { "Left", -80.0, 30.0, 0.350 }
} };

// Preset lookup that cannot throw for an out-of-range value from an old or
// hand-edited config; it falls back to Backwards.
inline const CurvePreset& CurvePresetFor(CurveType type)
{
    const std::size_t index = static_cast<std::size_t>(type);
    return kCurvePresets[
        index < kCurvePresets.size()
            ? index
            : static_cast<std::size_t>(CurveType::Backwards)];
}

struct MacroSettings
{
    int triggerKey = Keys::C;     // key YOU press to start the macro
    int activationKey = Keys::Q;  // key the macro presses before the camera move
    int secondaryKey = Keys::F;

    CurveType curveType = CurveType::Backwards;
    bool customCurveAngle = false;
    bool hotkeyMode = false;

    double horizontalDegrees = 180.0;
    double verticalDegrees = 50.0;

    double rotationDurationSeconds = 0.10;
    // Your in-game Roblox mouse sensitivity; input is scaled by 1 / this.
    double robloxSensitivity = 0.625;
    // Horizontal mouse counts per degree at sensitivity 1.0.
    double mouseCountsPerDegree = 5.0;

    // false: Look, Delay, Q tap, Restore, Delay, F, F
    // true : Q is held down through the camera move
    bool qBeforeCamera = false;

    double activationHoldSeconds = 0.02;
    double activationLeadSeconds = 0.010;
    double initialDelaySeconds = 0.10;
    double secondaryDelaySeconds = 0.35;
    double secondaryHoldSeconds = 0.0;

    bool instantMode = true;
    bool restoreCamera = true;
};

// Hotkey mode: 1 -> Upwards, 2 -> Backwards, 3 -> Left, 4 -> Right. Accepts the
// top-row digits and the numeric keypad.
inline bool CurveTypeForHotkey(int key, CurveType& out)
{
    switch (key)
    {
    case Keys::One:
    case Keys::Keypad1:
        out = CurveType::Upwards;
        return true;
    case Keys::Two:
    case Keys::Keypad2:
        out = CurveType::Backwards;
        return true;
    case Keys::Three:
    case Keys::Keypad3:
        out = CurveType::Left;
        return true;
    case Keys::Four:
    case Keys::Keypad4:
        out = CurveType::Right;
        return true;
    default:
        return false;
    }
}

MacroSettings GetEffectiveCurveSettings(const MacroSettings& settings);

// Smallest Roblox sensitivity used in the inverse; 0.0 would divide by zero.
inline constexpr double kMinRobloxSensitivity = 0.01;
// Roblox clamps camera pitch to +/-80 degrees and discards counts past it.
inline constexpr double kRobloxPitchLimitDegrees = 80.0;
// Roblox turns 0.0015*pi rad per vertical count versus 0.002*pi horizontal,
// so vertical needs 4/3 as many counts per degree.
inline constexpr double kVerticalCountsPerDegreeScale = 4.0 / 3.0;
double GetCurrentMultiplier(double userRobloxSensitivity);
int GetVerticalReturnCounts(const MacroSettings& settings, int sentY);

// Injectable input boundary so tests can verify the sequence without sending
// anything to the desktop.
struct MacroInputBackend
{
    std::function<bool(int, bool)> sendKey; // macOS key code, keyUp
    std::function<bool(int, int)> sendMouse;
    std::function<bool(int&, int&)> getCursor;
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
