#pragma once

#include "Configuration/Settings.hpp"
#include "imgui.h"

#include <string>
#include <vector>

struct AppStatus
{
    bool gameRunning = false;
    bool gameFocused = false;
    bool structureRunning = false;
    bool clickerRunning = false;
    bool clickerEngaged = false;
    int clickerClicks = 0;
    float clickerMeasuredCps = -1.0f;
    int clickerPause = 0;
    bool bladeBallRunning = false;
    int parries = 0;
    // True while chat detection has the keybinds suspended ("/" until Enter).
    bool chatActive = false;
    // False when the global key tap could not be created (no Input
    // Monitoring), so the menu key and every macro hotkey are unavailable.
    bool hotkeysActive = true;
};

struct MenuActions
{
    // "Run curve now" on the General page; the app starts the structure macro.
    bool startStructure = false;
    bool exitRequested = false;
};

/*
    The macOS menu. It mirrors the Windows overlay: the same 900x580 glass
    panel, icon rail, feature cards, animated switches, trajectory preview and
    five tabs (General, Clicker, Blade Ball, Config, Settings).

    Everything is drawn with ImDrawList so the Metal backend and the Windows
    DirectX backend render the same design from the same measurements.
*/
class Menu
{
public:
    // Windows menu size. Blade Ball carries the same trajectory preview as
    // the Camera Movement page, so that tab is taller.
    static constexpr float kPanelWidth = 900.0f;
    static constexpr float kPanelHeight = 580.0f;
    static constexpr float kBladeBallPanelHeight = 750.0f;

    void SetStatus(const AppStatus& status) { m_Status = status; }

    // Top-right notice, e.g. the curve a hotkey just selected.
    void ShowNotice(const std::string& text);

    // Draws the panel. Key capture is fed by the app from the global hotkey
    // monitor, not by ImGui, because the menu never owns keyboard focus.
    void Draw(AppConfig& config, MenuActions& actions);

    bool Visible() const { return m_Visible; }
    void SetVisible(bool visible) { m_Visible = visible; }

    bool IsCapturingKey() const { return m_Capture != Capture::None; }
    void CaptureKey(int keyCode, AppConfig& config);

    float PanelX() const { return m_PanelPos.x; }
    float PanelY() const { return m_PanelPos.y; }
    float PanelWidth() const;
    float PanelHeight() const;

private:
    enum class Page
    {
        General,
        Clicker,
        BladeBall,
        Configs,
        Settings
    };

    enum class Capture
    {
        None,
        MenuKey,
        TriggerKey,
        ActivationKey,
        SecondaryKey,
        ClickerKey,
        ClickerOutputKey,
        BladeBallTriggerKey,
        BladeBallParryKey,
        BladeBallAbilityKey
    };

    void DrawRail(float height);
    void DrawGeneralPage(AppConfig& config, MenuActions& actions);
    void DrawClickerPage(AppConfig& config);
    void DrawBladeBallPage(AppConfig& config);
    void DrawConfigsPage(AppConfig& config);
    void DrawSettingsPage(AppConfig& config, MenuActions& actions);
    void DrawNoticeWindow();

    void BeginCard(const char* id, float width, float height);
    void EndCard();
    void CardHeader(const char* title, float cardWidth);
    void Divider(float width);
    void StatusPill(const AppConfig& config);
    bool Toggle(const char* label, bool* value, int animIndex, const char* tooltip);
    bool Chip(const char* label, bool selected, float width);
    void KeyRow(const char* id, const char* title, const char* subtitle,
        int keyCode, Capture target, float width);
    void CurvePreview(const char* id, float width, float height,
        double& horizontalDegrees, double& verticalDegrees,
        bool& customCurveAngle, double presetHorizontal, double presetVertical,
        bool showReturn);
    void RefreshConfigList();

    AppStatus m_Status{};
    Page m_Page = Page::General;
    Capture m_Capture = Capture::None;

    ImVec2 m_PanelPos{ 40.0f, 40.0f };
    bool m_Dragging = false;
    bool m_Visible = true;
    bool m_PortableOpen = false;

    ImVec4 m_Accent{ 0.8167f, 0.3512f, 0.5799f, 1.0f };
    ImVec4 m_CardColor{ 0.0f, 0.0f, 0.0f, 1.0f };
    ImVec4 m_PanelColor{ 0.0f, 0.0f, 0.0f, 1.0f };
    float m_PanelOpacity = 0.56f;

    // Per-switch animation progress, same slots the Windows menu uses.
    float m_ToggleAnim[12] = {};

    // Page fade when the tab changes.
    Page m_FadePage = Page::General;
    float m_PageFade = 1.0f;

    // Top-right notice.
    std::string m_NoticeText;
    double m_NoticeUntil = 0.0;

    // Config tab.
    char m_ConfigName[64] = "default";
    std::string m_SelectedConfig;
    std::string m_ConfigStatus;
    std::vector<std::string> m_ConfigList;
    bool m_ConfigListLoaded = false;
};
