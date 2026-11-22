#pragma once

#include "MacroEngine.hpp"

#include <array>
#include <atomic>
#include <functional>
#include <mutex>
#include <thread>

/*
    Blade Ball: the parry (block) is F, the ability is Q. The shield a parry
    raises stays up for 500 ms and a parry that deflects nothing starts a 2 s
    cooldown. A curve shot sweeps the camera and parries at the end of it.
*/

inline constexpr int kParryShieldMs = 500;
inline constexpr int kParryWhiffCooldownMs = 2000;
// The 500 ms shield starts at the press, so the hold only has to be long
// enough for the game to register a block.
inline constexpr double kParryHoldSeconds = 0.020;

struct ParryCurvePreset
{
    const char* name;
    double horizontalDegrees;
    double verticalDegrees;
};

// Order matches CurveType: Upwards, Backwards, Right, Left.
inline constexpr std::array<ParryCurvePreset, 4> kParryCurvePresets{{
    { "Up", 10.0, 60.0 },
    { "Back", 180.0, 35.0 },
    { "Right", 70.0, 18.0 },
    { "Left", -70.0, 18.0 }
}};

inline const ParryCurvePreset& ParryCurvePresetFor(CurveType type)
{
    const std::size_t index = static_cast<std::size_t>(type);
    return kParryCurvePresets[
        index < kParryCurvePresets.size()
            ? index
            : static_cast<std::size_t>(CurveType::Right)];
}

struct BladeBallSettings
{
    // Pressing this runs one curve parry. It must differ from the parry key.
    int triggerKey = Keys::V;
    int parryKey = Keys::F;
    int abilityKey = Keys::Q;
    bool abilityEnabled = false;
    double abilityDelaySeconds = 0.12;

    // The camera sweeps this far, then the parry is pressed at the end of it.
    double horizontalDegrees = 30.0;
    double verticalDegrees = 0.0;
    double flickDurationSeconds = 0.09;
    double robloxSensitivity = 0.625;
    double mouseCountsPerDegree = 5.0;
    bool restoreCamera = true;

    CurveType curveType = CurveType::Right;
    bool customCurveAngle = true;
    bool hotkeyMode = false;

    // While armed the Blade Ball keys are live and the Structure curve is
    // refused, so two curves can never fire in the same game.
    bool armed = false;
};

inline BladeBallSettings EffectiveParrySettings(const BladeBallSettings& settings)
{
    BladeBallSettings effective = settings;
    if (!settings.customCurveAngle)
    {
        const ParryCurvePreset& preset = ParryCurvePresetFor(settings.curveType);
        effective.horizontalDegrees = preset.horizontalDegrees;
        effective.verticalDegrees = preset.verticalDegrees;
    }
    return effective;
}

class BladeBallEngine
{
public:
    BladeBallEngine();
    explicit BladeBallEngine(MacroInputBackend input);
    ~BladeBallEngine();

    BladeBallEngine(const BladeBallEngine&) = delete;
    BladeBallEngine& operator=(const BladeBallEngine&) = delete;

    void SetSettings(const BladeBallSettings& settings);
    BladeBallSettings GetSettings() const;

    void SetLicenseValidator(std::function<bool()> validator);

    // One curve parry: sweep, parry at the end, return the camera, optional
    // ability tap.
    bool StartParry();

    void Stop();
    void EmergencyStop();

    bool IsRunning() const;

    // Parries sent since the engine was created.
    int ParryCount() const;

private:
    void RunParry(BladeBallSettings settings);
    bool ShouldStop();
    void SleepInterruptibleMs(int milliseconds);

    MacroInputBackend m_Input;

    std::atomic<bool> m_Running{ false };
    std::atomic<bool> m_StopRequested{ false };
    std::atomic<int> m_ParryCount{ 0 };

    mutable std::mutex m_SettingsMutex;
    BladeBallSettings m_Settings{};
    std::function<bool()> m_LicenseValidator;

    std::thread m_Worker;
};
