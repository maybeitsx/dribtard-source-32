#include "MacroEngine.hpp"

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
        const auto& preset = kCurvePresets.at(static_cast<size_t>(settings.curveType));
        effective.horizontalDegrees = preset.horizontalDegrees;
        effective.verticalDegrees = preset.verticalDegrees;
        effective.secondaryDelaySeconds = preset.delayBeforePressingSeconds;
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
    // sentY is the vertical travel the camera actually made (see Run), so its
    // exact inverse returns the camera to its starting pitch. The settings stay
    // in the signature so return policy can be extended without changing the
    // macro input boundary.
    return -sentY;
}

// Keys that need KEYEVENTF_EXTENDEDKEY when sent as scan codes.
static bool IsExtendedKey(int vk)
{
    switch (vk)
    {
    case VK_RMENU:
    case VK_RCONTROL:
    case VK_INSERT:
    case VK_DELETE:
    case VK_HOME:
    case VK_END:
    case VK_PRIOR:
    case VK_NEXT:
    case VK_LEFT:
    case VK_RIGHT:
    case VK_UP:
    case VK_DOWN:
    case VK_NUMLOCK:
    case VK_DIVIDE:
    case VK_SNAPSHOT:
        return true;
    default:
        return false;
    }
}

// Use hardware scan codes for games that ignore virtual-key-only input.
bool SendHardwareKey(int virtualKey, bool keyUp)
{
    const UINT scan = MapVirtualKeyW(static_cast<UINT>(virtualKey), MAPVK_VK_TO_VSC);
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    if (scan == 0)
    {
        input.ki.wVk = static_cast<WORD>(virtualKey);
        input.ki.dwFlags = keyUp ? KEYEVENTF_KEYUP : 0;
    }
    else
    {
        input.ki.wScan = static_cast<WORD>(scan);
        input.ki.dwFlags = KEYEVENTF_SCANCODE;
        if (IsExtendedKey(virtualKey))
            input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
        if (keyUp)
            input.ki.dwFlags |= KEYEVENTF_KEYUP;
    }
    return SendInput(1, &input, sizeof(INPUT)) == 1;
}

MacroEngine::MacroEngine()
    : MacroEngine(MacroInputBackend{ SendHardwareKey, SendRelativeMouse, GetCursorPosition, SetCursorPosition })
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

    // Engine-level license gate: refuses to start without a valid license.
    // Application also checks before calling Start(), so bypassing one check
    // alone is not enough.
    {
        std::lock_guard<std::mutex> lock(m_SettingsMutex);

        if (m_LicenseValidator && !m_LicenseValidator())
            return false;
    }

    if (m_Worker.joinable())
        m_Worker.join();

    m_StopRequested.store(false);
    m_Running.store(true);
    m_Worker = std::thread(&MacroEngine::Run, this,
        GetEffectiveCurveSettings(GetSettings()));
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
    // The worker checks at most every 2 ms during waits, then releases only
    // keys it actually held, using its original settings snapshot.
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
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dx = dx;
    input.mi.dy = dy;
    input.mi.dwFlags = MOUSEEVENTF_MOVE;
    return SendInput(1, &input, sizeof(INPUT)) == 1;
}

bool MacroEngine::GetCursorPosition(int& x, int& y)
{
    POINT point{};
    if (!GetCursorPos(&point))
        return false;
    x = point.x;
    y = point.y;
    return true;
}

bool MacroEngine::SetCursorPosition(int x, int y)
{
    return SetCursorPos(x, y) != 0;
}

void MacroEngine::SleepInterruptible(double seconds)
{
    if (seconds <= 0.0)
        return;
    const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    while (!ShouldStop())
    {
        const auto now = std::chrono::steady_clock::now();
        if (now >= end)
            break;
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(end - now);
        if (remaining.count() > 2)
            std::this_thread::sleep_for(std::min(remaining, std::chrono::milliseconds(2)));
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
    // Guards the camera return so it is sent exactly once per run. The
    // desktop cursor is deliberately left alone: teleporting it back with
    // SetCursorPos is a second camera input the game can see, which made
    // the camera visibly reset twice.

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
            // Key releases are the sole permitted input after focus loss,
            // so injected keys cannot remain stuck on the desktop.
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
        // Releases of accepted input are always allowed, even after a stop.
        // Runs at most once per macro: the mid-sequence return and the
        // finish() cleanup share this path, and the second call is a no-op.
        // Vertical return uses the travel the camera actually made, not the
        // counts discarded past the pitch limit.
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
        const int steps = std::max(1, static_cast<int>(std::ceil(settings.rotationDurationSeconds * 120.0)));
        const double stepDelay = settings.rotationDurationSeconds / steps;
        for (int i = 1; i <= steps && !stopped(); ++i)
        {
            const double fraction = static_cast<double>(i) / steps;
            moveCamera(static_cast<int>(std::llround(horizontalCounts * fraction - sentX)),
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
