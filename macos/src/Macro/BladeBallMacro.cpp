#include "BladeBallMacro.hpp"

#include "Platform/MacInput.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>
#include <utility>
#include <vector>

namespace
{
    // Uniform +/- jitter, so the input is not a perfect metronome.
    int JitterMs(std::mt19937& rng, int jitterMs)
    {
        if (jitterMs <= 0)
            return 0;
        std::uniform_int_distribution<int> dist(-jitterMs, jitterMs);
        return dist(rng);
    }

    std::mt19937 MakeRng()
    {
        std::random_device device;
        return std::mt19937(device());
    }
}

BladeBallEngine::BladeBallEngine()
    : BladeBallEngine(MacroInputBackend{ MacInput::PostKey, MacInput::PostMouseMove, nullptr, nullptr })
{
}

BladeBallEngine::BladeBallEngine(MacroInputBackend input)
    : m_Input(std::move(input))
{
}

BladeBallEngine::~BladeBallEngine()
{
    Stop();
}

void BladeBallEngine::SetSettings(const BladeBallSettings& settings)
{
    std::lock_guard<std::mutex> lock(m_SettingsMutex);
    m_Settings = settings;
}

BladeBallSettings BladeBallEngine::GetSettings() const
{
    std::lock_guard<std::mutex> lock(m_SettingsMutex);
    return m_Settings;
}

void BladeBallEngine::SetLicenseValidator(std::function<bool()> validator)
{
    std::lock_guard<std::mutex> lock(m_SettingsMutex);
    m_LicenseValidator = std::move(validator);
}

bool BladeBallEngine::StartParry()
{
    if (m_Running.load())
        return false;

    BladeBallSettings settings;

    {
        std::lock_guard<std::mutex> lock(m_SettingsMutex);

        if (m_LicenseValidator && !m_LicenseValidator())
            return false;

        settings = m_Settings;
    }

    if (m_Worker.joinable())
        m_Worker.join();

    m_StopRequested.store(false);
    m_Running.store(true);

    try
    {
        m_Worker = std::thread(&BladeBallEngine::RunParry, this, settings);
    }
    catch (...)
    {
        m_StopRequested.store(true);
        m_Running.store(false);
        return false;
    }

    return true;
}

void BladeBallEngine::Stop()
{
    m_StopRequested.store(true);
    if (m_Worker.joinable())
        m_Worker.join();
    m_Running.store(false);
}

void BladeBallEngine::EmergencyStop()
{
    m_StopRequested.store(true);
}

bool BladeBallEngine::IsRunning() const
{
    return m_Running.load();
}

int BladeBallEngine::ParryCount() const
{
    return m_ParryCount.load();
}

bool BladeBallEngine::ShouldStop()
{
    return m_StopRequested.load();
}

void BladeBallEngine::SleepInterruptibleMs(int milliseconds)
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

void BladeBallEngine::RunParry(BladeBallSettings settings)
{
    const double countsPerDegree =
        settings.mouseCountsPerDegree * GetCurrentMultiplier(settings.robloxSensitivity);
    const int moveX = static_cast<int>(
        std::llround(settings.horizontalDegrees * countsPerDegree));
    // Roblox turns 4/3 as many vertical counts per degree as horizontal ones.
    const double verticalCountsPerDegree = countsPerDegree * kVerticalCountsPerDegreeScale;
    const int moveY = static_cast<int>(
        std::llround(-settings.verticalDegrees * verticalCountsPerDegree));

    std::mt19937 rng = MakeRng();
    int sentX = 0;
    int sentY = 0;
    std::vector<int> heldKeys;
    bool restored = false;

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
    auto waitSeconds = [&](double seconds)
    {
        const int ms = std::max(0, static_cast<int>(std::lround(seconds * 1000.0)) +
            JitterMs(rng, settings.jitterMs));
        SleepInterruptibleMs(ms);
    };
    auto moveTo = [&](double fraction)
    {
        if (ShouldStop())
            return;
        const int targetX = static_cast<int>(
            std::llround(static_cast<double>(moveX) * fraction));
        const int targetY = static_cast<int>(
            std::llround(static_cast<double>(moveY) * fraction));
        const int dx = targetX - sentX;
        const int dy = targetY - sentY;
        if (dx == 0 && dy == 0)
            return;
        if (m_Input.sendMouse(dx, dy))
        {
            sentX = targetX;
            sentY = targetY;
        }
        else
            m_StopRequested.store(true);
    };
    auto restoreCamera = [&]()
    {
        // Runs at most once per parry: the mid-sequence return and finish()
        // share this path. The exact inverse of the accepted counts puts the
        // camera back where the flick started.
        if (restored)
            return;
        restored = true;
        if (settings.restoreCamera && (sentX != 0 || sentY != 0))
        {
            if (!m_Input.sendMouse(-sentX, -sentY))
                m_StopRequested.store(true);
        }
        sentX = 0;
        sentY = 0;
    };
    auto finish = [&]()
    {
        for (int key : heldKeys)
            m_Input.sendKey(key, true);
        heldKeys.clear();
        restoreCamera();
        m_Running.store(false);
    };
    auto pressParry = [&]()
    {
        if (!keyDown(settings.parryKey))
            return;
        m_ParryCount.fetch_add(1);
        waitSeconds(kParryHoldSeconds);
        keyUp(settings.parryKey);
    };

    try
    {
        if (ShouldStop())
        {
            finish();
            return;
        }

        const double flickSeconds = std::max(0.0, settings.flickDurationSeconds);
        const int steps = std::max(1, static_cast<int>(std::ceil(flickSeconds * 120.0)));
        const double stepSeconds = flickSeconds / steps;

        // Sweep the camera through the whole flick first, then parry at the
        // end of it, so the deflect takes the full direction change.
        for (int i = 1; i <= steps && !ShouldStop(); ++i)
        {
            moveTo(static_cast<double>(i) / steps);
            waitSeconds(stepSeconds);
        }

        if (!ShouldStop())
            pressParry();

        if (ShouldStop())
        {
            finish();
            return;
        }

        restoreCamera();

        if (settings.abilityEnabled)
        {
            waitSeconds(settings.abilityDelaySeconds);
            if (!ShouldStop() && keyDown(settings.abilityKey))
            {
                waitSeconds(0.02);
                keyUp(settings.abilityKey);
            }
        }

        finish();
    }
    catch (...)
    {
        finish();
    }
}
