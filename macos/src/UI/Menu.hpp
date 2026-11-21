#pragma once

#include "Configuration/Settings.hpp"
#include "imgui.h"

struct AppStatus
{
    bool gameRunning = false;
    bool gameFocused = false;
    bool structureRunning = false;
    bool moonwalkRunning = false;
    bool bladeBallRunning = false;
    int parries = 0;
};

struct MenuActions
{
    // "Run curve now" on the General page; the app starts the structure macro.
    bool startStructure = false;
    bool exitRequested = false;
};

class Menu
{
public:
    static constexpr float kPanelWidth = 760.0f;
    static constexpr float kPanelHeight = 600.0f;

    void SetStatus(const AppStatus& status) { m_Status = status; }

    // Draws the panel. Key capture is fed by the app from the global hotkey
    // monitor, not by ImGui, because the menu never owns keyboard focus.
    void Draw(AppConfig& config, MenuActions& actions);

    bool Visible() const { return m_Visible; }
    void SetVisible(bool visible) { m_Visible = visible; }

    bool IsCapturingKey() const { return m_Capture != Capture::None; }
    void CaptureKey(int keyCode, AppConfig& config);

    float PanelX() const { return m_PanelPos.x; }
    float PanelY() const { return m_PanelPos.y; }
    float PanelWidth() const { return kPanelWidth; }
    float PanelHeight() const { return kPanelHeight; }

private:
    enum class Page
    {
        General,
        Moonwalk,
        BladeBall,
        Settings
    };

    enum class Capture
    {
        None,
        MenuKey,
        TriggerKey,
        ActivationKey,
        SecondaryKey,
        MoonwalkHold,
        BladeBallTrigger,
        BladeBallParry,
        BladeBallAbility
    };

    void DrawRail(float height);
    void DrawGeneralPage(AppConfig& config, MenuActions& actions);
    void DrawMoonwalkPage(AppConfig& config);
    void DrawBladeBallPage(AppConfig& config);
    void DrawSettingsPage(AppConfig& config, MenuActions& actions);

    void BeginCard(const char* id, float width, float height);
    void EndCard();
    void CardHeader(const char* title);
    void PageTitle(const char* title, const char* subtitle);
    void KeyRow(const char* id, const char* title, const char* subtitle,
        int keyCode, Capture target, float width);
    void StatusLine();

    AppStatus m_Status{};
    ImVec4 m_Accent{ 0.45f, 0.38f, 1.0f, 1.0f };
    Page m_Page = Page::General;
    Capture m_Capture = Capture::None;
    ImVec2 m_PanelPos{ 48.0f, 48.0f };
    bool m_Dragging = false;
    bool m_Visible = true;
};
