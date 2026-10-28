#include "Application.hpp"

#include <cstring>
#include <sstream>

#include "Configuration/SettingsManager.hpp"
#include "Overlay/Renderer.hpp"
#include "Logging/Logger.hpp"
#include "Licensing/LicenseConfig.hpp"

extern ID3D11Device* g_pd3dDevice;
extern ID3D11DeviceContext* g_pd3dDeviceContext;

bool Application::Run(HINSTANCE hInstance)
{
    // Startup cleanup: remove any leftover dribtard.old.exe or dribtard.new.exe
    // from a previous auto-update cycle.
    Update::Updater::CleanupOldFiles();

    auto& settings = SettingsManager::Get();

    if (!m_Overlay.Initialize(
        hInstance,
        L"dribtard"))
    {
        Logger::Error(
            "Failed to initialize overlay window.");

        return false;
    }

    Renderer renderer;

    if (!renderer.Initialize(m_Overlay.hwnd))
    {
        Logger::Error(
            "Failed to initialize DirectX 11 renderer.");

        m_Overlay.Shutdown();

        return false;
    }

    m_Overlay.SetPerPixelAlpha(renderer.UsesPerPixelAlpha());

    if (renderer.UsesPerPixelAlpha())
    {
        Logger::Info(
            "Overlay composites with per-pixel alpha; "
            "translucent UI is available.");
    }
    else
    {
        Logger::Info(
            "Overlay fell back to colour-key transparency; "
            "menu translucency and background dimming are unavailable "
            "on this path.");
    }

    ImGuiUI ui;

    if (!ui.Initialize(
        m_Overlay.hwnd,
        g_pd3dDevice,
        g_pd3dDeviceContext))
    {
        Logger::Error(
            "Failed to initialize ImGui.");

        renderer.Shutdown();
        m_Overlay.Shutdown();

        return false;
    }

    // Config system: reopen exactly where the user left off. The last-used
    // settings load first so every widget below starts from saved values.
    {
        AppConfig startup = CurrentConfig(ui);

        AppConfig saved;

        if (SettingsManager::LoadLast(saved))
        {
            ApplyConfig(saved, ui);
            startup = saved;

            Logger::Info(
                "Loaded last-used settings.");
        }

        m_LastSavedConfigJson = SettingsManager::ToJson(startup);
        ui.SetConfigList(SettingsManager::ListConfigs());
    }

    m_MacroEngine.SetSettings(
        m_MacroSettings);
    m_MoonwalkEngine.SetSettings(
        m_MoonwalkSettings);

    // License setup. Network validation runs on worker threads; the render
    // loop below never blocks on the network.
    m_License = std::make_unique<Licensing::LicenseManager>(
        Licensing::kServerUrl,
        Licensing::kServerPublicKey);

    m_Updater = std::make_unique<Update::Updater>(
        Licensing::kServerUrl,
        Licensing::kServerPublicKey,
        Licensing::kReleasePublicKey,
        Version::kAppVersion);
    m_Updater->StartBackgroundLoop();

    // Engine-level gate (defense in depth): even if a caller forgets the
    // IsValid() check, the engine still refuses to start unlicensed.
    const auto licenseValid = [this]()
    {
        return m_License && m_License->IsValid();
    };
    m_MacroEngine.SetLicenseValidator(licenseValid);
    m_MoonwalkEngine.SetLicenseValidator(licenseValid);

    if (m_License->LoadSavedKey())
    {
        strcpy_s(
            m_LicenseKeyBuffer,
            sizeof(m_LicenseKeyBuffer),
            m_License->SavedKey().c_str());
    }

    // Start from the re-verified offline cache so the key screen shows the
    // right state before the first online check completes.
    {
        const Licensing::LicenseState cached = m_License->LoadCachedState();

        if (!m_License->SavedKey().empty() &&
            cached.status != Licensing::LicenseStatus::Unknown)
        {
            Logger::Info(
                "Loaded cached license state.");
        }
    }

    auto startLicenseCheck = [this](const std::string& key)
    {
        if (m_LicenseChecking.exchange(true))
            return;

        m_LicenseCheck = std::async(
            std::launch::async,
            [this, key]()
            {
                Licensing::LicenseState state = m_License->ValidateNow(key);
                if (state.status == Licensing::LicenseStatus::Valid && m_Updater)
                {
                    m_Updater->NotifyLicenseValid(key);
                }
                m_LicenseChecking.store(false);
                return state;
            });
    };

    if (!m_License->SavedKey().empty())
        startLicenseCheck(m_License->SavedKey());

    // Background re-validation every 15 minutes. The callback only logs;
    // the main loop below performs MacroEngine::Stop() so all engine calls
    // stay on the main thread.
    m_License->StartBackgroundLoop([](const std::string& reason)
    {
        Logger::Error(
            "License became invalid in the background: " + reason);
    });

    if (m_ProcessManager.Attach(
        settings.TargetProcessName))
    {
        std::stringstream ss;

        ss << "Attached to "
           << settings.TargetProcessName
           << ". PID: "
           << m_ProcessManager.GetProcessId();

        Logger::Info(ss.str());
    }
    else
    {
        Logger::Info(
            "Target process not found. "
            "Waiting for game to launch...");
    }

    bool running = true;
    bool captureExclusionChanged = false;
    bool captureExclusionFailed = false;

    while (running)
    {
        const auto frameStart = std::chrono::steady_clock::now();

        MSG msg{};

        while (PeekMessage(
            &msg,
            nullptr,
            0U,
            0U,
            PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);

            if (msg.message == WM_QUIT)
                running = false;
        }

        if (!running)
            break;

        /*
            Global hotkey handling.

            A key being changed in the UI gets consumed first,
            so it cannot accidentally start the macro or toggle
            the interface.
        */
        const int pressedKey =
            m_Hotkeys.PollPressedKey();

        if (pressedKey != 0)
        {
            // Emergency stop remains available even while rebinding a key.
            if (pressedKey == VK_F12)
            {
                m_MacroEngine.EmergencyStop();
                m_MoonwalkEngine.EmergencyStop();

                // A still-held moonwalk key must not restart the loop; wait
                // for the user to release it once.
                m_MoonwalkBlockedUntilRelease = true;
            }
            else if (ui.IsCapturingKey())
            {
                ui.SetCapturedKey(
                    pressedKey,
                    m_MacroSettings,
                    m_MoonwalkSettings);

                m_MacroEngine.SetSettings(
                    m_MacroSettings);
            }
            else
            {
                CurveType hotkeyCurve{};
                if (m_MacroSettings.hotkeyMode &&
                    CurveTypeForHotkey(pressedKey, hotkeyCurve))
                {
                    // Digit keys switch the curve preset and take effect
                    // immediately, so leave custom mode (which hides the
                    // preset selection) and push the change to the engine.
                    m_MacroSettings.curveType = hotkeyCurve;
                    m_MacroSettings.customCurveAngle = false;

                    m_MacroEngine.SetSettings(
                        m_MacroSettings);

                    // Remember the switch for the top-right toast, shown
                    // even while the menu is closed.
                    const size_t presetIndex =
                        static_cast<size_t>(hotkeyCurve);

                    m_CurveToastName =
                        kCurvePresets.at(presetIndex).name;

                    m_CurveToastDigit =
                        (pressedKey >= VK_NUMPAD1 && pressedKey <= VK_NUMPAD4)
                        ? static_cast<char>('1' + (pressedKey - VK_NUMPAD1))
                        : static_cast<char>(pressedKey);

                    m_CurveToastExpiresAt =
                        std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(2500);
                }
                else if (pressedKey ==
                    ui.GetToggleKey())
                {
                    m_UIVisible = !m_UIVisible;
                    if (m_UIVisible)
                        ui.RestartEntranceAnimation();

                    m_Overlay.SetVisible(
                        m_UIVisible);
                }
                else if (pressedKey ==
                    m_MacroSettings.triggerKey)
                {
                    // Focus gate: the trigger key only starts the macro
                    // while the target game window is focused, so typing
                    // the same key in other windows cannot fire the macro.
                    if (m_ProcessManager.IsTargetFocused())
                    {
                        m_MacroEngine.SetSettings(
                            m_MacroSettings);

                        // License gate (hotkey path): no valid license, no macro.
                        // The engine has its own validator too, so removing one
                        // check alone cannot bypass licensing.
                        if (!m_MacroEngine.IsRunning() &&
                            !m_MoonwalkEngine.IsRunning() &&
                            m_License && m_License->IsValid())
                        {
                            m_MacroEngine.Start();
                        }
                    }
                }
            }
        }

        /*
            Reattach if the target process has closed.
        */
        if (!m_ProcessManager.IsRunning())
        {
            if (m_ProcessManager.GetHandle() != nullptr)
            {
                Logger::Info(
                    "Target process terminated. Detaching...");

                m_ProcessManager.Detach();
            }

            if (m_ProcessManager.Attach(
                settings.TargetProcessName))
            {
                std::stringstream ss;

                ss << "Attached to "
                   << settings.TargetProcessName
                   << ". PID: "
                   << m_ProcessManager.GetProcessId();

                Logger::Info(ss.str());
            }
        }
        else
        {
            m_Overlay.SyncWithTarget(
                m_ProcessManager.GetTargetWindow());
        }

        /*
            Draw UI and process UI button requests.
        */
        bool startRequested = false;
        bool stopRequested = false;
        bool emergencyStopRequested = false;
        bool exitRequested = false;
        bool captureExcluded = m_Overlay.IsCaptureExcluded();
        captureExclusionChanged = false;

        ui.SetProcessStatus(
            m_ProcessManager.IsRunning(),
            m_ProcessManager.GetProcessId());

        // Collect a finished license check without ever blocking the loop.
        if (m_LicenseCheck.valid() &&
            m_LicenseCheck.wait_for(std::chrono::seconds(0)) ==
                std::future_status::ready)
        {
            try
            {
                m_LicenseCheck.get();
            }
            catch (...)
            {
                m_LicenseChecking.store(false);
            }
        }

        const bool licensed = m_License && m_License->IsValid();
        const Licensing::LicenseState licenseState =
            m_License ? m_License->CurrentState() : Licensing::LicenseState{};

        // A license that became invalid stops the macro and returns to the
        // key screen.
        if (!licensed && m_MacroEngine.IsRunning())
            m_MacroEngine.Stop();
        if (!licensed && m_MoonwalkEngine.IsRunning())
            m_MoonwalkEngine.Stop();

        /*
            Moonwalk Tech is hold-triggered: its keybind runs the A/D loop for
            as long as it is held. It only runs while the target game window is
            focused and the license is valid, and it never runs alongside the
            main macro. Releasing the key (or losing focus) stops the loop and
            releases whatever direction it was holding.
        */
        const bool moonwalkHeld =
            !ui.IsCapturingKey() &&
            HotkeyManager::IsKeyDown(m_MoonwalkSettings.holdKey) &&
            m_ProcessManager.IsTargetFocused();

        if (!moonwalkHeld)
            m_MoonwalkBlockedUntilRelease = false;

        const bool moonwalkAllowed =
            licensed && moonwalkHeld && !m_MoonwalkBlockedUntilRelease;

        if (moonwalkAllowed &&
            !m_MoonwalkEngine.IsRunning() &&
            !m_MacroEngine.IsRunning())
        {
            m_MoonwalkEngine.SetSettings(m_MoonwalkSettings);
            m_MoonwalkEngine.Start();
        }
        else if (!moonwalkAllowed && m_MoonwalkEngine.IsRunning())
        {
            m_MoonwalkEngine.Stop();
        }

        ui.SetMoonwalkRunning(
            m_MoonwalkEngine.IsRunning());

        // Feed current update state to the UI footer banner without blocking.
        if (m_Updater && licensed)
        {
            const Update::UpdateState uState = m_Updater->CurrentState();
            const bool hasBanner =
                !uState.manifest.version.empty() &&
                (uState.status == Update::UpdateStatus::UpdateAvailable ||
                    uState.status == Update::UpdateStatus::Downloading ||
                    uState.status == Update::UpdateStatus::ReadyToApply ||
                    uState.status == Update::UpdateStatus::Failed);

            const std::string errText =
                (uState.status == Update::UpdateStatus::Failed)
                ? uState.statusText
                : "";

            ui.SetUpdateBanner(
                hasBanner,
                uState.manifest.version,
                uState.manifest.notes,
                uState.status == Update::UpdateStatus::Downloading,
                uState.status == Update::UpdateStatus::ReadyToApply,
                uState.progress,
                errText);
        }
        else
        {
            ui.SetUpdateBanner(false, "", "", false, false, 0.0f);
        }

        renderer.BeginFrame(m_Overlay.hwnd);

        if (m_UIVisible)
        {
            if (licensed)
            {
                ui.Render(
                    m_MacroSettings,
                    m_MoonwalkSettings,
                    startRequested,
                    stopRequested,
                    emergencyStopRequested,
                    m_MacroEngine.IsRunning(),
                    captureExcluded,
                    captureExclusionChanged,
                    captureExclusionFailed,
                    exitRequested);

                m_MacroEngine.SetSettings(
                    m_MacroSettings);
                m_MoonwalkEngine.SetSettings(
                    m_MoonwalkSettings);

                HandleConfigRequests(ui);

                if (ui.ConsumeUpdateNowRequest() && m_Updater)
                {
                    // Stop the macros first when the user clicks "Update now".
                    if (m_MacroEngine.IsRunning())
                        m_MacroEngine.Stop();
                    if (m_MoonwalkEngine.IsRunning())
                        m_MoonwalkEngine.Stop();

                    m_PendingUpdateApply = true;
                    const Update::UpdateState uState = m_Updater->CurrentState();
                    if (uState.status != Update::UpdateStatus::ReadyToApply &&
                        uState.status != Update::UpdateStatus::Downloading)
                    {
                        m_Updater->StartDownloadAsync();
                    }
                }
            }
            else
            {
                bool activateRequested = false;
                bool licenseExitRequested = false;

                ui.RenderLicenseScreen(
                    m_LicenseKeyBuffer,
                    sizeof(m_LicenseKeyBuffer),
                    m_LicenseChecking.load(),
                    licenseState.statusText,
                    licenseState.expiryText,
                    activateRequested,
                    licenseExitRequested);

                if (activateRequested && m_License)
                {
                    m_License->SaveKey(m_LicenseKeyBuffer);
                    startLicenseCheck(m_License->SavedKey());
                }

                if (licenseExitRequested)
                {
                    Logger::Info(
                        "Unload requested from the license screen. Closing...");

                    running = false;
                }
            }
        }

        // Last-used settings autosave: any change (menu edit, hotkey
        // switch, rebinding) is written back a moment after it settles,
        // so the next launch reopens exactly where the user left off.
        AutosaveConfig(ui);

        /*
            Curve-change toast. Visible top-right for a few seconds after
            a hotkey switch, even while the menu itself is closed. The
            overlay stays hidden exactly as before except while the toast
            is up, when it is shown click-through so the game keeps input.
        */
        const bool toastActive =
            !m_CurveToastName.empty() &&
            std::chrono::steady_clock::now() < m_CurveToastExpiresAt;

        if (!m_UIVisible && toastActive)
        {
            m_Overlay.ShowToast();

            ui.RenderToast(
                m_CurveToastName.c_str(),
                m_CurveToastDigit);
        }
        else if (!m_UIVisible && !m_CurveToastName.empty() && !toastActive)
        {
            if (!m_CurveToastName.empty())
            {
                m_CurveToastName.clear();
                m_CurveToastDigit = 0;
            }

            if (m_Overlay.IsVisible())
                m_Overlay.SetVisible(false);
        }

        if (captureExclusionChanged)
        {
            if (!m_Overlay.SetCaptureExcluded(captureExcluded))
            {
                captureExclusionFailed = true;
                Logger::Error("Could not change capture exclusion. This feature requires supported Windows capture APIs.");
            }
            else
            {
                captureExclusionFailed = false;
                Logger::Info(captureExcluded
                    ? "Overlay capture exclusion enabled."
                    : "Overlay capture exclusion disabled.");
            }
        }

        // License gate (UI button path): no valid license, no macro. The two
        // macros never run at the same time.
        if (startRequested && m_License && m_License->IsValid())
        {
            if (m_MoonwalkEngine.IsRunning())
                m_MoonwalkEngine.Stop();
            m_MacroEngine.Start();
        }

        if (stopRequested)
            m_MacroEngine.Stop();

        if (emergencyStopRequested)
            m_MacroEngine.EmergencyStop();

        // Apply a verified update only after the user explicitly clicked
        // "Update now" and the download + verification finished.
        if (m_PendingUpdateApply && m_Updater)
        {
            const Update::UpdateState uState = m_Updater->CurrentState();
            if (uState.status == Update::UpdateStatus::ReadyToApply)
            {
                m_PendingUpdateApply = false;
                if (m_MacroEngine.IsRunning())
                    m_MacroEngine.Stop();

                std::string applyError;
                if (m_Updater->ApplyAndRestart(applyError))
                {
                    Logger::Info(
                        "Update applied and new process launched. Exiting current process...");
                    running = false;
                }
            }
            else if (uState.status == Update::UpdateStatus::Failed)
            {
                m_PendingUpdateApply = false;
            }
        }

        /*
            Unload button: leave the main loop so the normal shutdown path
            releases the macro, DirectX resources and the overlay window and
            the process exits completely.
        */
        if (exitRequested)
        {
            Logger::Info(
                "Unload requested from the menu. Closing the application...");

            running = false;
        }

        // Frosted backdrop behind the menu: the acrylic blur window sits under
        // the overlay and covers only the menu's rectangle, shown while the UI is.
        {
            const ImVec4 blurRect = ui.BlurRect();
            m_Overlay.SetBlurRect(
                static_cast<int>(blurRect.x),
                static_cast<int>(blurRect.y),
                static_cast<int>(blurRect.z),
                static_cast<int>(blurRect.w),
                ui.BlurTintAlpha(),
                m_UIVisible && ui.BlurActive());
        }

        renderer.EndFrame();

        /*
            Frame pacing. A cap from the Settings tab only sleeps out the rest
            of the frame budget, so it can lower the menu's rate but never push
            it above the monitor's refresh rate. Without a cap the loop keeps
            its short sleep so hotkey polling stays responsive.
        */
        const int fpsCap =
            m_UIVisible ? ui.MenuFpsCap() : 0;

        if (fpsCap > 0)
        {
            const auto budget =
                std::chrono::microseconds(1000000 / fpsCap);

            const auto spent =
                std::chrono::steady_clock::now() - frameStart;

            if (spent < budget)
            {
                const auto remaining =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        budget - spent);

                if (remaining.count() > 0)
                    Sleep(static_cast<DWORD>(remaining.count()));
            }
        }
        else
        {
            Sleep(5);
        }
    }

    // Final flush so the very last change is never lost, even if the
    // debounced autosave above had not fired yet.
    SettingsManager::SaveLast(CurrentConfig(ui));

    m_MacroEngine.Stop();
    m_MoonwalkEngine.Stop();

    ui.Shutdown();
    renderer.Shutdown();
    m_Overlay.Shutdown();

    return true;
}

AppConfig Application::CurrentConfig(ImGuiUI& ui)
{
    AppConfig config;
    config.macro = m_MacroSettings;
    config.moonwalk = m_MoonwalkSettings;
    config.ui = ui.GetUiSettings();
    config.targetProcessName = SettingsManager::Get().TargetProcessName;
    return config;
}

void Application::ApplyConfig(const AppConfig& config, ImGuiUI& ui)
{
    m_MacroSettings = config.macro;
    m_MoonwalkSettings = config.moonwalk;
    SettingsManager::Get().TargetProcessName = config.targetProcessName;
    ui.SetUiSettings(config.ui);
    m_MacroEngine.SetSettings(m_MacroSettings);
    m_MoonwalkEngine.SetSettings(m_MoonwalkSettings);
}

void Application::HandleConfigRequests(ImGuiUI& ui)
{
    // Named configs requested from the Settings tab's Configs card.
    if (const std::string saveName = ui.ConsumeConfigSaveRequest();
        !saveName.empty())
    {
        const std::string clean = SettingsManager::SanitizeName(saveName);

        if (!clean.empty() && SettingsManager::SaveNamed(clean, CurrentConfig(ui)))
        {
            ui.SetConfigStatus("Saved \"" + clean + "\".");

            // A named save is also the user's latest state.
            m_LastSavedConfigJson = SettingsManager::ToJson(CurrentConfig(ui));
            SettingsManager::SaveLast(CurrentConfig(ui));
        }
        else
        {
            ui.SetConfigStatus("Invalid name. Use letters, numbers, spaces, - _ .");
        }

        ui.SetConfigList(SettingsManager::ListConfigs());
    }

    if (const std::string loadName = ui.ConsumeConfigLoadRequest();
        !loadName.empty())
    {
        AppConfig loaded;

        if (SettingsManager::LoadNamed(loadName, loaded))
        {
            ApplyConfig(loaded, ui);
            ui.SetConfigStatus("Loaded \"" + loadName + "\".");

            m_LastSavedConfigJson = SettingsManager::ToJson(loaded);
            SettingsManager::SaveLast(loaded);
        }
        else
        {
            ui.SetConfigStatus("Could not load \"" + loadName + "\".");
        }

        ui.SetConfigList(SettingsManager::ListConfigs());
    }

    if (const std::string deleteName = ui.ConsumeConfigDeleteRequest();
        !deleteName.empty())
    {
        if (SettingsManager::DeleteNamed(deleteName))
            ui.SetConfigStatus("Deleted \"" + deleteName + "\".");
        else
            ui.SetConfigStatus("Could not delete \"" + deleteName + "\".");

        ui.SetConfigList(SettingsManager::ListConfigs());
    }

    // Keep the rendered list fresh (e.g. files added while running).
    if (++m_FrameCounter % 120 == 0)
        ui.SetConfigList(SettingsManager::ListConfigs());
}

void Application::AutosaveConfig(ImGuiUI& ui)
{
    const std::string json = SettingsManager::ToJson(CurrentConfig(ui));

    if (json == m_LastSavedConfigJson)
    {
        m_ConfigDirty = false;
        return;
    }

    const auto now = std::chrono::steady_clock::now();

    if (!m_ConfigDirty)
    {
        m_ConfigDirty = true;
        m_ConfigDirtySince = now;
        return;
    }

    // Debounce: write once the settings have settled for 2 seconds.
    if (now - m_ConfigDirtySince < std::chrono::seconds(2))
        return;

    if (SettingsManager::SaveLast(CurrentConfig(ui)))
        m_LastSavedConfigJson = json;

    m_ConfigDirty = false;
}
