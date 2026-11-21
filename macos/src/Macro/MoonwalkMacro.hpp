#pragma once

#include "MacroEngine.hpp"

#include <atomic>
#include <functional>
#include <mutex>
#include <thread>

// Moonwalk Tech: hold the keybind and the engine repeats
// A down -> 55 ms -> A up -> 5 ms -> D down -> 55 ms -> D up until released.
struct MoonwalkSettings
{
    int holdKey = Keys::X;
};

inline constexpr int kMoonwalkTapHoldMs = 55;
inline constexpr int kMoonwalkGapMs = 5;

class MoonwalkEngine
{
public:
    MoonwalkEngine();
    explicit MoonwalkEngine(MacroInputBackend input);
    ~MoonwalkEngine();

    MoonwalkEngine(const MoonwalkEngine&) = delete;
    MoonwalkEngine& operator=(const MoonwalkEngine&) = delete;

    void SetSettings(const MoonwalkSettings& settings);
    MoonwalkSettings GetSettings() const;

    void SetLicenseValidator(std::function<bool()> validator);

    bool Start();
    void Stop();
    void EmergencyStop();

    bool IsRunning() const;

private:
    void Run();
    bool ShouldStop();
    void SleepInterruptibleMs(int milliseconds);

    MacroInputBackend m_Input;

    std::atomic<bool> m_Running{ false };
    std::atomic<bool> m_StopRequested{ false };

    mutable std::mutex m_SettingsMutex;
    MoonwalkSettings m_Settings{};
    std::function<bool()> m_LicenseValidator;

    std::thread m_Worker;
};
