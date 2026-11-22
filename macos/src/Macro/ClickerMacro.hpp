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

enum class ClickerButton
{
    Left,
    Right
};

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
    ClickerButton button = ClickerButton::Left;
    int cps = 16;
};

// true = right button. A false return does not stop the loop.
using ClickerClickFn = std::function<bool(bool rightButton)>;

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
    std::atomic<int> m_Button{ 0 };
    std::atomic<int> m_Clicks{ 0 };

    mutable std::mutex m_SettingsMutex;
    ClickerSettings m_Settings{};
    std::function<bool()> m_LicenseValidator;

    std::thread m_Worker;
};
