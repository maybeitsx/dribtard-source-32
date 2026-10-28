#pragma once

#include <windows.h>
#include <d3d11.h>
#include <imgui.h>

#include <cstddef>
#include <string>
#include <vector>

#include "../Macro/MacroEngine.hpp"
#include "../Configuration/SettingsManager.hpp"

class ImGuiUI
{
public:
    bool Initialize(
        HWND hwnd,
        ID3D11Device* device,
        ID3D11DeviceContext* context);

    void SetProcessStatus(
        bool attached,
        DWORD pid);

    // True while the hold-triggered Moonwalk Tech loop is running, so the
    // header status pill reflects it as well.
    void SetMoonwalkRunning(bool running) { m_MoonwalkRunning = running; }

    void SetCapturedKey(
        int virtualKey,
        MacroSettings& settings,
        MoonwalkSettings& moonwalk);
    int GetToggleKey() const { return m_ToggleKey; }
    void SetToggleKey(int virtualKey) { m_ToggleKey = virtualKey; }

    // Snapshot/restore every setting the config system persists (menu key,
    // accent, glows, opacity, dimming, FPS cap). Used for save/load and
    // for the last-used-settings autosave.
    UiSettings GetUiSettings() const;
    void SetUiSettings(const UiSettings& ui);

    // Named-config list shown on the Config tab. The UI only renders the
    // list; the application performs the filesystem work and polls the
    // pending action after each frame.
    void SetConfigList(const std::vector<std::string>& names);
    void SetConfigStatus(const std::string& message);
    std::string ConsumeConfigSaveRequest();
    std::string ConsumeConfigLoadRequest();
    std::string ConsumeConfigDeleteRequest();

    // Update banner state shown in the footer when a signed update is available.
    void SetUpdateBanner(
        bool available,
        const std::string& version,
        const std::string& notes,
        bool downloading,
        bool readyToApply,
        float progress,
        const std::string& errorText = "");

    bool ConsumeUpdateNowRequest();

    /*
        Settings edited on the menu's Settings tab. The application reads them
        back every frame: MenuFpsCap() returns 0 when the cap is switched off.
    */
    int MenuFpsCap() const { return m_LimitMenuFps ? m_MenuFpsCap : 0; }
    bool DarkenBackground() const { return m_DarkenBackground; }
    float DimOpacity() const { return m_DimOpacity; }
    float PanelOpacity() const { return m_PanelOpacity; }
    ImVec4 AccentColor() const { return m_AccentColor; }
    bool IsPortableMode() const { return m_PortableMode; }
    // True while the menu panel is shown. The panel only opens while the
    // pointer is over the icon rail (or inside the open panel).
    bool IsMenuOpen() const { return m_MenuOpen; }
    // Restart the entrance animation whenever the overlay is shown again.
    void RestartEntranceAnimation() { m_AppearAnim = 0.0f; }

    // Frosted backdrop for the menu, in overlay client coordinates
    // (left, top, right, bottom). BlurActive() is false when no menu is drawn.
    ImVec4 BlurRect() const { return m_BlurRect; }
    bool BlurActive() const { return m_BlurActive; }

    // Strength of the light tint over the blur. The Menu opacity slider's
    // minimum (0.30) gives a pure blur; higher values add a stronger tint.
    unsigned BlurTintAlpha() const
    {
        const float t = (m_PanelOpacity - 0.30f) / 0.70f;
        const float clamped = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        return static_cast<unsigned>(clamped * 96.0f + 0.5f);
    }

    static constexpr int kMinMenuFpsCap = 10;
    static constexpr int kMaxMenuFpsCap = 480;

    bool IsCapturingKey() const;

    void Render(
        MacroSettings& settings,
        MoonwalkSettings& moonwalk,
        bool& startRequested,
        bool& stopRequested,
        bool& emergencyStopRequested,
        bool macroRunning,
        bool& captureExcluded,
        bool& captureExclusionChanged,
        bool captureExclusionFailed,
        bool& exitRequested);

    // Draw a small top-right notification; no-op when not initialized.
    void RenderToast(const char* presetName, char digit);

    // License gate screen shown whenever the license is not valid: key input,
    // Activate button, status line and expiry info. No-op when not initialized.
    // activateRequested is set when the user presses Activate; exitRequested
    // mirrors the main menu unload action.
    void RenderLicenseScreen(
        char* keyBuffer,
        std::size_t keyBufferSize,
        bool checking,
        const std::string& statusText,
        const std::string& expiryText,
        bool& activateRequested,
        bool& exitRequested);

    void Shutdown();

private:
    friend struct ImGuiUITestAccess;

    enum class CaptureTarget
    {
        None,
        TriggerKey,
        ActivationKey,
        SecondaryKey,
        ToggleKey,
        MoonwalkHoldKey
    };

    // The menu shows one page at a time; the floating vertical icon rail
    // on the left switches between our four tabs.
    enum class Page
    {
        General,
        Moonwalk,
        Configs,
        Settings
    };

    void ApplyTheme();
    void DrawWindow(
        MacroSettings& settings,
        MoonwalkSettings& moonwalk,
        bool& startRequested,
        bool& stopRequested,
        bool& emergencyStopRequested,
        bool macroRunning,
        bool& captureExcluded,
        bool& captureExclusionChanged,
        bool captureExclusionFailed,
        bool& exitRequested);
    void DrawToast(const char* presetName, char digit);
    void DrawSidebarRail(
        const ImVec2& railMin,
        const ImVec2& railMax,
        bool anyRunning,
        bool& exitRequested);
    void DrawGeneralPage(
        MacroSettings& settings,
        MoonwalkSettings& moonwalk,
        bool& startRequested,
        bool& stopRequested,
        bool& emergencyStopRequested,
        bool macroRunning,
        bool& captureExcluded,
        bool& captureExclusionChanged,
        bool captureExclusionFailed,
        bool& exitRequested);

    // Rounded "Unload" pill drawn at the current cursor; it asks the
    // application to close itself completely. Lives on the Settings tab.
    void DrawUnloadButton(bool& exitRequested, float width);

    // Footer row under the panel: version label and the update banner.
    void DrawFooter(bool& exitRequested);
    void DrawUpdateBanner(float leftX, float maxRightX, float rowY);

    // Settings tab: Client, Appearance, Menu mode, Performance & Miscellaneous.
    void DrawSettings(
        MacroSettings& settings,
        bool& captureExcluded,
        bool& captureExclusionChanged,
        bool captureExclusionFailed,
        bool& exitRequested);

    // Moonwalk Tech tab: hold keybind plus the fixed A/D tap sequence.
    void DrawMoonwalkPage(MoonwalkSettings& moonwalk);

    // Config tab: two-column Config & Manage layout (plus compact single-card helper).
    void DrawConfigsPage();
    void DrawConfigCard(float colWidth);

    ImVec4 Accent() const { return m_AccentColor; }
    void DrawDimOverlay(float appear);
    void RestoreAppearanceDefaults();

    void UpdateAnimations();
    void DrawLogoGlow(ImVec2 min, ImVec2 max, float rounding);

    // Soft crimson halo around the whole menu panel, drawn behind the window
    // so the surrounding backdrop picks up the glow while the UI is open.
    void DrawWindowGlow(ImVec2 min, ImVec2 max, float appear);

    bool m_Initialized = false;

    bool m_Attached = false;
    DWORD m_Pid = 0;
    bool m_MoonwalkRunning = false;

    CaptureTarget m_CaptureTarget = CaptureTarget::None;
    int m_ToggleKey = VK_OEM_5;

    ID3D11ShaderResourceView* m_MacroIcon = nullptr;

    // Cool glass surfaces and a configurable luminous accent.
    ImVec4 m_AccentColor = ImVec4(0.24f, 0.84f, 0.88f, 1.0f);
    // Base colour of the dark cards behind the features (alpha comes from kCardOpacity).
    ImVec4 m_CardColor = ImVec4(0.055f, 0.075f, 0.095f, 1.0f);
    float m_GlowIntensity = 0.45f;
    float m_WindowGlowIntensity = 0.45f;
    float m_AppearAnim = 0.0f;
    float m_PillIndexAnim = 1.0f;
    float m_ToggleAnim[6] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    float m_PanelOpacity = 0.40f;
    bool m_DarkenBackground = false;
    float m_DimOpacity = 0.45f;

    Page m_ActivePage = Page::General;
    bool m_PortableMode = false;
    bool m_MenuOpen = false;
    bool m_MenuPressStartedInside = false;
    float m_PanelReveal = 0.0f;
    bool m_WasPortableMode = false;
    ImVec2 m_ClassicPosition = ImVec2(18.0f, 18.0f);
    ImVec4 m_BlurRect = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    bool m_BlurActive = false;
    int m_GuiSizePercent = 100;
    bool m_ShowNotifications = true;
    bool m_CollapseCard[12] = {};

    bool m_LimitMenuFps = false;
    int m_MenuFpsCap = 60;

    // Named configs: rendered list plus one pending user action per frame.
    char m_ConfigNameBuffer[64] = "default";
    std::string m_SelectedConfig;
    std::string m_ActiveConfigName = "none";
    std::vector<std::string> m_ConfigList;
    std::string m_ConfigStatus;
    std::string m_PendingConfigSave;
    std::string m_PendingConfigLoad;
    std::string m_PendingConfigDelete;

    // Update banner state.
    bool m_UpdateAvailable = false;
    std::string m_UpdateVersion;
    std::string m_UpdateNotes;
    bool m_UpdateDownloading = false;
    bool m_UpdateReadyToApply = false;
    float m_UpdateProgress = 0.0f;
    std::string m_UpdateErrorText;
    bool m_PendingUpdateNow = false;

    float m_PreviewHorizAnim = 180.0f;
    float m_PreviewVertAnim = 100.0f;

    ImVec4 m_WindowColor = ImVec4(0.035f, 0.055f, 0.075f, 1.0f);
    ImVec4 m_BackgroundColor = ImVec4(0.055f, 0.075f, 0.095f, 1.0f);
    ImVec4 m_FrameColor = ImVec4(0.085f, 0.115f, 0.145f, 1.0f);
    ImVec4 m_ButtonColor = ImVec4(0.11f, 0.15f, 0.18f, 1.0f);
    ImVec4 m_ButtonHoveredColor = ImVec4(0.16f, 0.24f, 0.27f, 1.0f);
    ImVec4 m_ButtonActiveColor = ImVec4(0.24f, 0.84f, 0.88f, 1.0f);
    ImVec4 m_ItemColor = ImVec4(0.24f, 0.84f, 0.88f, 1.0f);
    ImVec4 m_ItemActiveColor = ImVec4(0.40f, 0.93f, 0.96f, 1.0f);
};
