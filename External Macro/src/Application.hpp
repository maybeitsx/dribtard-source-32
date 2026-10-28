#pragma once

#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <future>
#include <memory>
#include <string>

#include "Process/ProcessManager.hpp"
#include "Overlay/OverlayWindow.hpp"
#include "Overlay/ImGuiUI.hpp"
#include "Macro/MacroEngine.hpp"
#include "Macro/MoonwalkMacro.hpp"
#include "Configuration/SettingsManager.hpp"
#include "Input/HotkeyManager.hpp"
#include "Licensing/LicenseClient.hpp"
#include "Update/Updater.hpp"
#include "Version.hpp"

class Application
{
public:
    Application() = default;
    ~Application() = default;

    bool Run(HINSTANCE hInstance);

private:
    ProcessManager m_ProcessManager;
    OverlayWindow m_Overlay;

    MacroEngine m_MacroEngine;
    MacroSettings m_MacroSettings{};
    MoonwalkEngine m_MoonwalkEngine;
    MoonwalkSettings m_MoonwalkSettings{};
    HotkeyManager m_Hotkeys;

    // After an emergency stop the hold loop stays stopped until its keybind
    // is released once, so a still-held key cannot restart it.
    bool m_MoonwalkBlockedUntilRelease = false;

    // License state. Declared after the macro engine so the background
    // validation thread stops before the engine is torn down.
    std::unique_ptr<Licensing::LicenseManager> m_License;
    char m_LicenseKeyBuffer[128]{};
    std::future<Licensing::LicenseState> m_LicenseCheck;
    std::atomic<bool> m_LicenseChecking{ false };

    // Auto-update state. Runs on worker threads after license validation
    // and periodically every few hours; never blocks the render loop or macro.
    std::unique_ptr<Update::Updater> m_Updater;
    bool m_PendingUpdateApply = false;

    bool m_UIVisible = true;

    // Config system: last-saved snapshot for autosave change detection,
    // plus debounce state so every keystroke does not hit the disk.
    std::string m_LastSavedConfigJson;
    bool m_ConfigDirty = false;
    std::chrono::steady_clock::time_point m_ConfigDirtySince{};
    int m_FrameCounter = 0;

    AppConfig CurrentConfig(ImGuiUI& ui);
    void ApplyConfig(const AppConfig& config, ImGuiUI& ui);
    void HandleConfigRequests(ImGuiUI& ui);
    void AutosaveConfig(ImGuiUI& ui);

    // Last hotkey-selected curve, shown as a top-right toast while the
    // menu is closed. Empty name means no toast is pending or visible.
    std::string m_CurveToastName;
    char m_CurveToastDigit = 0;
    std::chrono::steady_clock::time_point m_CurveToastExpiresAt{};
};
