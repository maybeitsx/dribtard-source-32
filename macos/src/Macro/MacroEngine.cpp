#include "MacroEngine.hpp"

#include "Platform/MacInput.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <utility>
#include <vector>

MacroSettings GetEffectiveCurveSettings(const MacroSettings& settings)
{
    MacroSettings effective = settings;
    if (!settings.customCurveAngle)
    {
        const auto& preset = CurvePresetFor(settings.curveType);
        effective.horizontalDegrees = preset.horizontalDegrees;
        effective.verticalDegrees = preset.verticalDegrees;
        effective.secondaryDelaySeconds = preset.delayBeforePressingSeconds;
        effective.triggerKey = settings.triggerKey;
    }
    return effective;
}

double GetCurrentMultiplier(double userRobloxSensitivity)
{
    const double clamped = std::isnan(userRobloxSensitivity)
        ? kMinRobloxSensitivity
        : std::max(kMinRobloxSensitivity, userRobloxSensitivity);
    return 1.0 / clamped;
}

int GetVerticalReturnCounts(const MacroSettings&, int sentY)
{
    return -sentY;
}

MacroEngine::MacroEngine()
    : MacroEngine(MacroInputBackend{ MacInput::PostKey, MacInput::PostMouseMove, nullptr, nullptr })
{
}

MacroEngine::MacroEngine(MacroInputBackend input)
    : m_Input(std::move(input))
{
}

MacroEngine::~MacroEngine()
{
    Stop();
}

void MacroEngine::SetSettings(const MacroSettings& settings)
{
    std::lock_guard<std::mutex> lock(m_SettingsMutex);
    m_Settings = settings;
}

MacroSettings MacroEngine::GetSettings() const
{
    std::lock_guard<std::mutex> lock(m_SettingsMutex);
    return m_Settings;
}

void MacroEngine::SetLicenseValidator(std::function<bool()> validator)
{
    std::lock_guard<std::mutex> lock(m_SettingsMutex);
    m_LicenseValidator = std::move(validator);
}

bool MacroEngine::Start()
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

    const MacroSettings settings = GetEffectiveCurveSettings(GetSettings());

    m_StopRequested.store(false);
    m_Running.store(true);

    // A worker thread that cannot be created leaves the engine idle instead of
    // terminating the process.
    try
    {
        m_Worker = std::thread(&MacroEngine::Run, this, settings);
    }
    catch (...)
    {
        m_StopRequested.store(true);
        m_Running.store(false);
        return false;
    }

    return true;
}

void MacroEngine::Stop()
{
    m_StopRequested.store(true);
    if (m_Worker.joinable())
        m_Worker.join();
    m_Running.store(false);
}

void MacroEngine::EmergencyStop()
{
    m_StopRequested.store(true);
}

bool MacroEngine::IsRunning() const
{
    return m_Running.load();
}

bool MacroEngine::ShouldStop()
{
    return m_StopRequested.load();
}

bool MacroEngine::SendRelativeMouse(int dx, int dy)
{
    return MacInput::PostMouseMove(dx, dy);
}

bool MacroEngine::GetCursorPosition(int& x, int& y)
{
    (void)x;
    (void)y;
    return false;
}

bool MacroEngine::SetCursorPosition(int x, int y)
{
    (void)x;
    (void)y;
    return false;
}

void MacroEngine::SleepInterruptible(double seconds)
{
    if (seconds <= 0.0)
        return;

    const auto end = std::chrono::steady_clock::now() +
        std::chrono::duration<double>(seconds);

    while (!ShouldStop())
    {
        const auto now = std::chrono::steady_clock::now();
        if (now >= end)
            break;

        // 1 ms slices keep the 20 ms holds and the 350 ms delay close to
        // their settings.
        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(end - now);

        if (remaining.count() >= 1)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        else
            std::this_thread::yield();
    }
}

void MacroEngine::Run(MacroSettings settings)
{
    const double currentMultiplier = GetCurrentMultiplier(settings.robloxSensitivity);
    const double countsPerDegree = settings.mouseCountsPerDegree;
    const double horizontalCounts = settings.horizontalDegrees * countsPerDegree * currentMultiplier;
    const double verticalCountsPerDegree = countsPerDegree * currentMultiplier * kVerticalCountsPerDegreeScale;
    const double verticalCounts = -settings.verticalDegrees * verticalCountsPerDegree;
    const int moveX = static_cast<int>(std::llround(horizontalCounts));
    const int moveY = static_cast<int>(std::llround(verticalCounts));
    int sentX = 0;
    int sentY = 0;

    // Roblox discards mouse counts past its pitch limit. Track the pitch the
    // camera actually reaches (assumed level at start) so the reset undoes only
    // that travel. The full curve is still sent.
    const double countsPerPitchDegree = verticalCountsPerDegree;
    double pitchDegrees = 0.0;
    double actualY = 0.0;
    std::vector<int> heldKeys;

    auto stopped = [&]() { return ShouldStop(); };
    auto keyDown = [&](int key)
    {
        if (stopped())
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
        if (held != heldKeys.end())
        {
            // Releases stay allowed after a stop so no key can remain stuck.
            if (m_Input.sendKey(key, true))
                heldKeys.erase(held);
            else
                m_StopRequested.store(true);
        }
    };
    auto moveCamera = [&](int dx, int dy)
    {
        if (stopped() || (dx == 0 && dy == 0))
            return;
        if (m_Input.sendMouse(dx, dy))
        {
            sentX += dx;
            sentY += dy;
            if (countsPerPitchDegree > 0.0)
            {
                const double before = pitchDegrees;
                pitchDegrees = std::clamp(before - dy / countsPerPitchDegree,
                    -kRobloxPitchLimitDegrees, kRobloxPitchLimitDegrees);
                actualY -= (pitchDegrees - before) * countsPerPitchDegree;
            }
            else
                actualY += dy;
        }
        else
            m_StopRequested.store(true);
    };
    bool cameraRestored = false;
    auto restoreCamera = [&]()
    {
        if (cameraRestored)
            return;
        cameraRestored = true;
        const int actualYCounts = static_cast<int>(std::llround(actualY));
        if (settings.restoreCamera && (sentX != 0 || actualYCounts != 0))
        {
            if (!m_Input.sendMouse(-sentX, GetVerticalReturnCounts(settings, actualYCounts)))
                m_StopRequested.store(true);
        }
        sentX = sentY = 0;
        actualY = 0.0;
        pitchDegrees = 0.0;
    };
    auto finish = [&]()
    {
        for (int key : heldKeys)
            m_Input.sendKey(key, true);
        heldKeys.clear();
        restoreCamera();
        m_Running.store(false);
    };
    auto tapKey = [&](int key, double holdSeconds)
    {
        if (keyDown(key))
        {
            SleepInterruptible(holdSeconds);
            keyUp(key);
        }
    };

    // An exception escaping this worker thread calls std::terminate; finish()
    // releases held keys and returns the camera instead.
    try
    {
        if (stopped()) { finish(); return; }
        if (settings.qBeforeCamera)
        {
            keyDown(settings.activationKey);
            SleepInterruptible(settings.activationLeadSeconds);
            if (stopped()) { finish(); return; }
        }

        if (settings.instantMode || settings.rotationDurationSeconds <= 0.0)
            moveCamera(moveX, moveY);
        else
        {
            const int steps = std::max(1,
                static_cast<int>(std::ceil(settings.rotationDurationSeconds * 120.0)));
            const double stepDelay = settings.rotationDurationSeconds / steps;
            for (int i = 1; i <= steps && !stopped(); ++i)
            {
                const double fraction = static_cast<double>(i) / steps;
                moveCamera(
                    static_cast<int>(std::llround(horizontalCounts * fraction - sentX)),
                    static_cast<int>(std::llround(verticalCounts * fraction - sentY)));
                SleepInterruptible(stepDelay);
            }
            if (!stopped())
                moveCamera(moveX - sentX, moveY - sentY);
        }
        if (stopped()) { finish(); return; }

        SleepInterruptible(settings.initialDelaySeconds);
        if (settings.qBeforeCamera)
            keyUp(settings.activationKey);
        else
            tapKey(settings.activationKey, settings.activationHoldSeconds);
        if (stopped()) { finish(); return; }

        restoreCamera();
        SleepInterruptible(settings.secondaryDelaySeconds);
        if (stopped()) { finish(); return; }

        tapKey(settings.secondaryKey, settings.secondaryHoldSeconds);
        if (stopped()) { finish(); return; }
        tapKey(settings.secondaryKey, settings.secondaryHoldSeconds);
        finish();
    }
    catch (...)
    {
        finish();
    }
}
