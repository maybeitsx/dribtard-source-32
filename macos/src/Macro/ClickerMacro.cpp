#include "ClickerMacro.hpp"

#include <utility>

ClickerEngine::ClickerEngine()
    : ClickerEngine(nullptr)
{
}

ClickerEngine::ClickerEngine(ClickerClickFn click)
    : m_Click(std::move(click))
{
}

ClickerEngine::~ClickerEngine()
{
    Stop();
}

void ClickerEngine::SetSettings(const ClickerSettings& settings)
{
    ClickerSettings clamped = settings;
    clamped.cps = ClampClickerCps(settings.cps);
    if (clamped.mode != ClickerMode::Hold && clamped.mode != ClickerMode::Toggle)
        clamped.mode = ClickerMode::Hold;
    if (clamped.button != ClickerButton::Left && clamped.button != ClickerButton::Right)
        clamped.button = ClickerButton::Left;

    {
        std::lock_guard<std::mutex> lock(m_SettingsMutex);
        m_Settings = clamped;
    }

    m_Cps.store(clamped.cps, std::memory_order_relaxed);
    m_Button.store(clamped.button == ClickerButton::Right ? 1 : 0, std::memory_order_relaxed);
}

ClickerSettings ClickerEngine::GetSettings() const
{
    std::lock_guard<std::mutex> lock(m_SettingsMutex);
    return m_Settings;
}

void ClickerEngine::SetLicenseValidator(std::function<bool()> validator)
{
    std::lock_guard<std::mutex> lock(m_SettingsMutex);
    m_LicenseValidator = std::move(validator);
}

bool ClickerEngine::Start()
{
    if (m_Running.load())
        return false;

    {
        std::lock_guard<std::mutex> lock(m_SettingsMutex);
        if (m_LicenseValidator && !m_LicenseValidator())
            return false;

        m_Cps.store(ClampClickerCps(m_Settings.cps), std::memory_order_relaxed);
        m_Button.store(
            m_Settings.button == ClickerButton::Right ? 1 : 0,
            std::memory_order_relaxed);
    }

    if (m_Worker.joinable())
        m_Worker.join();

    m_Clicks.store(0, std::memory_order_relaxed);
    m_StopRequested.store(false);
    m_Running.store(true);

    try
    {
        m_Worker = std::thread(&ClickerEngine::Run, this);
    }
    catch (...)
    {
        m_StopRequested.store(true);
        m_Running.store(false);
        return false;
    }

    return true;
}

void ClickerEngine::Stop()
{
    m_StopRequested.store(true);
    if (m_Worker.joinable())
        m_Worker.join();
    m_Running.store(false);
}

void ClickerEngine::EmergencyStop()
{
    m_StopRequested.store(true);
}

bool ClickerEngine::IsRunning() const
{
    return m_Running.load();
}

int ClickerEngine::ClickCount() const
{
    return m_Clicks.load(std::memory_order_relaxed);
}

bool ClickerEngine::ShouldStop() const
{
    return m_StopRequested.load(std::memory_order_relaxed);
}

void ClickerEngine::SpinFor(double seconds)
{
    if (seconds <= 0.0)
        return;

    const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    while (!ShouldStop() && std::chrono::steady_clock::now() < end)
    {
#if defined(__aarch64__)
        asm volatile("yield");
#elif defined(__x86_64__) || defined(__i386__)
        asm volatile("pause");
#endif
    }
}

void ClickerEngine::Run()
{
    // Same sequence at every CPS: press, hold for part of the gap so the game
    // can sample it, release, then spin out the rest of 1/CPS.
    try
    {
        while (!ShouldStop())
        {
            const int cps = ClampClickerCps(m_Cps.load(std::memory_order_relaxed));
            const bool right = m_Button.load(std::memory_order_relaxed) != 0;
            const double interval = 1.0 / static_cast<double>(cps);

            const auto began = std::chrono::steady_clock::now();

            if (m_Click)
            {
                m_Click(right);
            }
            else
            {
                auto sendEdge = [&](bool up)
                {
                    for (int attempt = 0; attempt < 4 && !ShouldStop(); ++attempt)
                    {
                        if (MacInput::PostMouseButton(right, up))
                            return;
                        SpinFor(0.0001);
                    }
                };

                sendEdge(false);
                SpinFor(interval / 3.0);
                sendEdge(true);
            }

            m_Clicks.fetch_add(1, std::memory_order_relaxed);

            const double elapsed = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - began).count();
            SpinFor(interval - elapsed);
        }
    }
    catch (...)
    {
    }

    m_Running.store(false);
}
