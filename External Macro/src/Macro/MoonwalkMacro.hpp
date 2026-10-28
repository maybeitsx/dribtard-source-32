#pragma once

#include "MacroEngine.hpp"

#include <atomic>
#include <functional>
#include <mutex>
#include <thread>

// Moonwalk Tech: a hold-triggered tap loop that is independent of the main
// macro sequence. Press and hold the keybind and the engine repeats
// A down -> wait -> A up -> wait -> D down -> wait -> D up until released.
struct MoonwalkSettings
{
    int holdKey = 'X';
};

// Fixed timing of the tech: each direction is held for 55 ms and the two taps
// are separated by a 5 ms pause so A and D are never down together.
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

    // Optional license gate, mirroring MacroEngine: Start() refuses to run
    // while the callback returns false.
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
