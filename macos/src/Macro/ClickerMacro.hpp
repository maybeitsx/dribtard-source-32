#pragma once

#include "Keys.hpp"
#include "Platform/MacInput.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <thread>

enum class ClickerMode
{
    Hold,
    Toggle
};

/*
    Mouse outputs use negative values so a real macOS key code (0-255) can be
    stored in the same setting, exactly like the Windows build where a virtual
    key and a mouse button share one field.
*/
enum ClickerMouseOutput : int
{
    ClickerMouseLeft = -1,
    ClickerMouseRight = -2
};

inline bool ClickerOutputIsMouse(int outputKey)
{
    return outputKey < 0;
}

inline bool ClickerOutputIsRight(int outputKey)
{
    return outputKey == ClickerMouseRight;
}

inline constexpr int kClickerMinCps = 1;
inline constexpr int kClickerMaxCps = 1500;

inline int ClampClickerCps(int cps)
{
    if (cps < kClickerMinCps)
        return kClickerMinCps;
    if (cps > kClickerMaxCps)
        return kClickerMaxCps;
    return cps;
}

struct ClickerSettings
{
    int key = Keys::X;
    ClickerMode mode = ClickerMode::Hold;
    // What the clicker presses: a mouse button (negative) or any key.
    int outputKey = ClickerMouseLeft;
    int cps = 16;
};

// Receives the output key that should be pressed once. A false return does not
// stop the loop: one rejected click must not kill the stream.
using ClickerClickFn = std::function<bool(int outputKey)>;

class ClickerEngine
{
public:
    ClickerEngine();
    explicit ClickerEngine(ClickerClickFn click);
    ~ClickerEngine();

    ClickerEngine(const ClickerEngine&) = delete;
    ClickerEngine& operator=(const ClickerEngine&) = delete;

    void SetSettings(const ClickerSettings& settings);
    ClickerSettings GetSettings() const;

    void SetLicenseValidator(std::function<bool()> validator);

    bool Start();
    void Stop();
    void EmergencyStop();

    bool IsRunning() const;
    int ClickCount() const;

private:
    void Run();
    bool ShouldStop() const;
    void SpinFor(double seconds);

    ClickerClickFn m_Click;

    std::atomic<bool> m_Running{ false };
    std::atomic<bool> m_StopRequested{ false };
    std::atomic<int> m_Cps{ 16 };
    std::atomic<int> m_OutputKey{ ClickerMouseLeft };
    std::atomic<int> m_Clicks{ 0 };

    mutable std::mutex m_SettingsMutex;
    ClickerSettings m_Settings{};
    std::function<bool()> m_LicenseValidator;

    std::thread m_Worker;
};
