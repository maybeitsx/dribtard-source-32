#include "MoonwalkMacro.hpp"

#include "Platform/MacInput.hpp"

#include <algorithm>
#include <chrono>
#include <utility>
#include <vector>

namespace
{
    constexpr int kFirstKey = Keys::A;
    constexpr int kSecondKey = Keys::D;
}

MoonwalkEngine::MoonwalkEngine()
    : MoonwalkEngine(MacroInputBackend{ MacInput::PostKey, nullptr, nullptr, nullptr })
{
}

MoonwalkEngine::MoonwalkEngine(MacroInputBackend input)
    : m_Input(std::move(input))
{
}

MoonwalkEngine::~MoonwalkEngine()
{
    Stop();
}

void MoonwalkEngine::SetSettings(const MoonwalkSettings& settings)
{
    std::lock_guard<std::mutex> lock(m_SettingsMutex);
    m_Settings = settings;
}

MoonwalkSettings MoonwalkEngine::GetSettings() const
{
    std::lock_guard<std::mutex> lock(m_SettingsMutex);
    return m_Settings;
}

void MoonwalkEngine::SetLicenseValidator(std::function<bool()> validator)
{
    std::lock_guard<std::mutex> lock(m_SettingsMutex);
    m_LicenseValidator = std::move(validator);
}

bool MoonwalkEngine::Start()
{
    if (m_Running.load())
        return false;

    {
        std::lock_guard<std::mutex> lock(m_SettingsMutex);

        if (m_LicenseValidator && !m_LicenseValidator())
            return false;
    }

    if (m_Worker.joinable())
        m_Worker.join();

    m_StopRequested.store(false);
    m_Running.store(true);

    try
    {
        m_Worker = std::thread(&MoonwalkEngine::Run, this);
    }
    catch (...)
    {
        m_StopRequested.store(true);
        m_Running.store(false);
        return false;
    }

    return true;
}

void MoonwalkEngine::Stop()
{
    m_StopRequested.store(true);
    if (m_Worker.joinable())
        m_Worker.join();
    m_Running.store(false);
}

void MoonwalkEngine::EmergencyStop()
{
    m_StopRequested.store(true);
}

bool MoonwalkEngine::IsRunning() const
{
    return m_Running.load();
}

bool MoonwalkEngine::ShouldStop()
{
    return m_StopRequested.load();
}

void MoonwalkEngine::SleepInterruptibleMs(int milliseconds)
{
    if (milliseconds <= 0)
        return;

    const auto end = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(milliseconds);

    while (!ShouldStop())
    {
        const auto now = std::chrono::steady_clock::now();
        if (now >= end)
            break;

        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(end - now);

        if (remaining.count() >= 1)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        else
            std::this_thread::yield();
    }
}

void MoonwalkEngine::Run()
{
    std::vector<int> heldKeys;

    auto keyDown = [&](int key)
    {
        if (ShouldStop())
            return false;

        if (!m_Input.sendKey(key, false))
        {
            m_StopRequested.store(true);
            return false;
        }

        heldKeys.push_back(key);
        return true;
    };
    auto keyUp = [&](int key)
    {
        const auto held = std::find(heldKeys.begin(), heldKeys.end(), key);
        if (held == heldKeys.end())
            return;

        if (m_Input.sendKey(key, true))
            heldKeys.erase(held);
        else
            m_StopRequested.store(true);
    };

    try
    {
        while (!ShouldStop())
        {
            if (!keyDown(kFirstKey))
                break;
            SleepInterruptibleMs(kMoonwalkTapHoldMs);
            keyUp(kFirstKey);
            if (ShouldStop())
                break;

            SleepInterruptibleMs(kMoonwalkGapMs);
            if (ShouldStop())
                break;

            if (!keyDown(kSecondKey))
                break;
            SleepInterruptibleMs(kMoonwalkTapHoldMs);
            keyUp(kSecondKey);
        }
    }
    catch (...)
    {
    }

    for (int key : heldKeys)
        m_Input.sendKey(key, true);

    heldKeys.clear();
    m_Running.store(false);
}
