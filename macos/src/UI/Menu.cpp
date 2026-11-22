#include "UI/Menu.hpp"

#include "Keys.hpp"
#include "Macro/MacroEngine.hpp"

#include <algorithm>
#include <cstdio>
#include <string>

namespace
{
    constexpr float kRailWidth = 56.0f;
    constexpr float kPadding = 10.0f;
    constexpr float kCardOpacity = 0.92f;

    ImVec4 AccentOf(const AppConfig& config)
    {
        return ImVec4(config.ui.accentR, config.ui.accentG, config.ui.accentB, 1.0f);
    }

    ImVec4 CardColour()
    {
        return ImVec4(0.055f, 0.065f, 0.13f, kCardOpacity);
    }

    void ClampSensitivity(double& value)
    {
        value = std::clamp(value, kMinRobloxSensitivity, kMaxRobloxSensitivity);
    }

    void SensitivityFields(const char* id, double& sensitivity, float width)
    {
        float slider = static_cast<float>(RobloxEngineToSlider(sensitivity));
        ImGui::PushID(id);
        ImGui::SetNextItemWidth(width);
        if (ImGui::SliderFloat("Roblox slider", &slider, 0.0f, 10.0f, "%.2f"))
            sensitivity = RobloxSliderToEngine(slider);
        ImGui::SetNextItemWidth(width);
        ImGui::InputDouble("Roblox box value", &sensitivity, 0.01, 0.1, "%.3f");
        ClampSensitivity(sensitivity);
        ImGui::PopID();
    }
}

void Menu::BeginCard(const char* id, float width, float height)
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, CardColour());
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 1.0f, 1.0f, 0.08f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
    ImGui::BeginChild(id, ImVec2(width, height), ImGuiChildFlags_Borders,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
}

void Menu::EndCard()
{
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}

void Menu::CardHeader(const char* title)
{
    ImGui::TextColored(m_Accent, "%s", title);
}

void Menu::PageTitle(const char* title, const char* subtitle)
{
    ImGui::TextUnformatted(title);
    ImGui::TextDisabled("%s", subtitle);
    ImGui::Separator();
    ImGui::Spacing();
}

void Menu::StatusLine()
{
    if (m_Status.gameFocused)
        ImGui::TextColored(ImVec4(0.55f, 0.90f, 0.62f, 1.0f), "Roblox focused");
    else if (m_Status.gameRunning)
        ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.45f, 1.0f), "Roblox running, not focused");
    else
        ImGui::TextColored(ImVec4(0.90f, 0.55f, 0.55f, 1.0f), "Roblox not detected");
}

void Menu::KeyRow(const char* id, const char* title, const char* subtitle,
    int keyCode, Capture target, float width)
{
    ImGui::TextUnformatted(title);
    ImGui::TextDisabled("%s", subtitle);

    ImGui::SameLine(width - 158.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.14f, 0.18f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.16f, 0.24f, 0.27f, 1.0f));

    const std::string keyLabel = std::string(Keys::Name(keyCode)) + "##key_" + id;
    if (ImGui::Button(keyLabel.c_str(), ImVec2(54.0f, 24.0f)))
        m_Capture = target;

    ImGui::PopStyleColor(2);
    ImGui::SameLine(0.0f, 6.0f);

    if (m_Capture == target)
    {
        ImGui::TextColored(m_Accent, "Press a key...");
    }
    else if (ImGui::Button((std::string("Change##change_") + id).c_str(), ImVec2(76.0f, 24.0f)))
    {
        m_Capture = target;
    }
}

void Menu::CaptureKey(int keyCode, AppConfig& config)
{
    const Capture target = m_Capture;
    m_Capture = Capture::None;

    // F12 is the emergency stop and can never be rebound.
    if (target == Capture::None || keyCode == Keys::F12)
        return;

    switch (target)
    {
    case Capture::MenuKey:
        config.ui.menuKey = keyCode;
        break;
    case Capture::TriggerKey:
        config.macro.triggerKey = keyCode;
        break;
    case Capture::ActivationKey:
        config.macro.activationKey = keyCode;
        break;
    case Capture::SecondaryKey:
        config.macro.secondaryKey = keyCode;
        break;
    case Capture::ClickerKey:
        config.clicker.key = keyCode;
        break;
    case Capture::BladeBallTrigger:
        // The trigger must stay a key the macro itself does not press.
        if (keyCode == config.bladeBall.parryKey)
            return;
        config.bladeBall.triggerKey = keyCode;
        break;
    case Capture::BladeBallParry:
        if (keyCode == config.bladeBall.triggerKey)
            return;
        config.bladeBall.parryKey = keyCode;
        break;
    case Capture::BladeBallAbility:
        config.bladeBall.abilityKey = keyCode;
        break;
    case Capture::None:
        break;
    }
}

void Menu::DrawRail(float height)
{
    ImGui::BeginChild("##rail", ImVec2(kRailWidth, height), false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    struct Slot
    {
        const char* label;
        const char* tip;
        Page page;
    };
    const Slot slots[4] = {
        { "GEN", "General: camera curve and block key", Page::General },
        { "CLK", "Clicker: hold or toggle auto click", Page::Clicker },
        { "BB", "Blade Ball: armed curve parry", Page::BladeBall },
        { "SET", "Settings: menu key and appearance", Page::Settings }
    };

    for (const Slot& slot : slots)
    {
        const bool active = m_Page == slot.page;
        if (active)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45f, 0.38f, 1.0f, 0.30f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.82f, 1.0f, 1.0f));
        }

        if (ImGui::Button(slot.label, ImVec2(kRailWidth - 18.0f, 34.0f)))
            m_Page = slot.page;

        if (active)
            ImGui::PopStyleColor(2);

        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", slot.tip);
    }

    ImGui::EndChild();
}

void Menu::DrawGeneralPage(AppConfig& config, MenuActions& actions)
{
    PageTitle("General", "Camera curve, block key and macro timing");

    StatusLine();
    ImGui::Spacing();

    const float width = ImGui::GetContentRegionAvail().x;

    BeginCard("##card_curve", width, 320.0f);
    {
        CardHeader("Curve");

        int curve = static_cast<int>(config.macro.curveType);
        if (ImGui::Combo("Preset", &curve, "Upwards\0Backwards\0Right\0Left\0"))
            config.macro.curveType = static_cast<CurveType>(curve);

        ImGui::Checkbox("Custom angles", &config.macro.customCurveAngle);
        if (config.macro.customCurveAngle)
        {
            ImGui::SetNextItemWidth(width - 210.0f);
            ImGui::InputDouble("Horizontal (deg)", &config.macro.horizontalDegrees, 5.0, 15.0, "%.1f");
            ImGui::SetNextItemWidth(width - 210.0f);
            ImGui::InputDouble("Vertical (deg)", &config.macro.verticalDegrees, 5.0, 15.0, "%.1f");
        }

        SensitivityFields("structure", config.macro.robloxSensitivity, width - 210.0f);

        ImGui::SetNextItemWidth(width - 210.0f);
        ImGui::InputDouble("Counts per degree", &config.macro.mouseCountsPerDegree, 0.1, 1.0, "%.2f");
        config.macro.mouseCountsPerDegree = std::clamp(config.macro.mouseCountsPerDegree, 0.1, 100.0);

        ImGui::SetNextItemWidth(width - 210.0f);
        ImGui::InputDouble("Delay before block (sec)", &config.macro.secondaryDelaySeconds, 0.01, 0.05, "%.3f");
        config.macro.secondaryDelaySeconds = std::clamp(config.macro.secondaryDelaySeconds, 0.0, 30.0);

        ImGui::Checkbox("Return camera", &config.macro.restoreCamera);
        ImGui::SameLine();
        ImGui::Checkbox("Hotkey mode (1-4)", &config.macro.hotkeyMode);
    }
    EndCard();

    ImGui::Dummy(ImVec2(0.0f, 8.0f));

    BeginCard("##card_keys", width, 210.0f);
    {
        CardHeader("Keys");

        KeyRow("trigger", "Trigger key", "You press this in game",
            config.macro.triggerKey, Capture::TriggerKey, width - 24.0f);
        KeyRow("activation", "Ability key", "Pressed once before the camera move",
            config.macro.activationKey, Capture::ActivationKey, width - 24.0f);
        KeyRow("secondary", "Block key", "Tapped twice at the end",
            config.macro.secondaryKey, Capture::SecondaryKey, width - 24.0f);
    }
    EndCard();

    ImGui::Dummy(ImVec2(0.0f, 8.0f));

    if (ImGui::Button("Run curve now", ImVec2(150.0f, 30.0f)))
        actions.startStructure = true;
    ImGui::SameLine();
    if (m_Status.structureRunning)
        ImGui::TextColored(AccentOf(config), "Running");
    else
        ImGui::TextDisabled("Needs Roblox focused and a granted Accessibility permission");
}

void Menu::DrawClickerPage(AppConfig& config)
{
    PageTitle("Clicker", "Hold or toggle a click stream");

    StatusLine();
    ImGui::Spacing();

    const float width = ImGui::GetContentRegionAvail().x;

    BeginCard("##card_clicker", width, 250.0f);
    {
        CardHeader("Clicker");

        int mode = config.clicker.mode == ClickerMode::Toggle ? 1 : 0;
        if (ImGui::Combo("Mode", &mode, "Hold\0Toggle\0"))
            config.clicker.mode = mode == 1 ? ClickerMode::Toggle : ClickerMode::Hold;

        int button = config.clicker.button == ClickerButton::Right ? 1 : 0;
        if (ImGui::Combo("Button", &button, "Left\0Right\0"))
            config.clicker.button = button == 1 ? ClickerButton::Right : ClickerButton::Left;

        ImGui::SetNextItemWidth(width - 210.0f);
        ImGui::SliderInt("CPS", &config.clicker.cps, kClickerMinCps, kClickerMaxCps, "%d");
        config.clicker.cps = ClampClickerCps(config.clicker.cps);

        KeyRow("clicker", config.clicker.mode == ClickerMode::Toggle ? "Toggle key" : "Hold key",
            "Starts and stops the click stream",
            config.clicker.key, Capture::ClickerKey, width - 24.0f);

        ImGui::Spacing();
        if (m_Status.clickerRunning)
        {
            if (m_Status.clickerMeasuredCps >= 0.0f)
                ImGui::TextColored(AccentOf(config), "RUNNING  %d clicks  %.0f cps",
                    m_Status.clickerClicks, m_Status.clickerMeasuredCps);
            else
                ImGui::TextColored(AccentOf(config), "RUNNING  %d clicks", m_Status.clickerClicks);
        }
        else if (m_Status.clickerEngaged && m_Status.clickerPause == 1)
            ImGui::TextDisabled("Paused: Roblox is not focused");
        else if (m_Status.clickerEngaged && m_Status.clickerPause == 2)
            ImGui::TextDisabled("Paused: the curve macro is running");
        else if (config.clicker.mode == ClickerMode::Toggle)
            ImGui::TextDisabled("Press %s to start, press it again to stop",
                Keys::Name(config.clicker.key));
        else
            ImGui::TextDisabled("Hold %s to click", Keys::Name(config.clicker.key));
    }
    EndCard();

    ImGui::Spacing();
    ImGui::TextWrapped(
        "Each click is held for part of the gap so the game can sample it, then "
        "released for the rest of 1/CPS. The Structure curve pauses it. Blade Ball "
        "does not. F12 stops it.");
}

void Menu::DrawBladeBallPage(AppConfig& config)
{
    PageTitle("Blade Ball", "Armed curve parry");

    const float width = ImGui::GetContentRegionAvail().x;

    BeginCard("##card_bb", width, 210.0f);
    {
        CardHeader("Blade Ball");

        ImGui::Checkbox("Armed", &config.bladeBall.armed);
        ImGui::SameLine();
        ImGui::TextDisabled(config.bladeBall.armed
            ? "Armed: the structure curve is refused"
            : "Off: Blade Ball keys do nothing");

        KeyRow("bb_trigger", "Trigger key", "Runs one curve parry",
            config.bladeBall.triggerKey, Capture::BladeBallTrigger, width - 24.0f);
        KeyRow("bb_parry", "Parry key", "Pressed by the macro",
            config.bladeBall.parryKey, Capture::BladeBallParry, width - 24.0f);
        KeyRow("bb_ability", "Ability key", "Optional follow-up",
            config.bladeBall.abilityKey, Capture::BladeBallAbility, width - 24.0f);
    }
    EndCard();

    ImGui::Dummy(ImVec2(0.0f, 8.0f));

    BeginCard("##card_bb_curve", width, 360.0f);
    {
        CardHeader("Curve Parry");

        ImGui::Checkbox("Hotkey Mode", &config.bladeBall.hotkeyMode);
        ImGui::SameLine();
        ImGui::TextDisabled("1 Up  2 Back  3 Left  4 Right");

        int curve = static_cast<int>(config.bladeBall.curveType);
        if (ImGui::Combo("Curve Type", &curve, "Upwards\0Backwards\0Right\0Left\0"))
        {
            config.bladeBall.curveType = static_cast<CurveType>(curve);
            if (!config.bladeBall.customCurveAngle)
            {
                const ParryCurvePreset& preset = ParryCurvePresetFor(config.bladeBall.curveType);
                config.bladeBall.horizontalDegrees = preset.horizontalDegrees;
                config.bladeBall.verticalDegrees = preset.verticalDegrees;
            }
        }

        ImGui::Checkbox("Custom angle", &config.bladeBall.customCurveAngle);
        if (config.bladeBall.customCurveAngle)
        {
            ImGui::SetNextItemWidth(width - 210.0f);
            ImGui::InputDouble("Flick horizontal (deg)", &config.bladeBall.horizontalDegrees, 5.0, 15.0, "%.1f");
            ImGui::SetNextItemWidth(width - 210.0f);
            ImGui::InputDouble("Flick vertical (deg)", &config.bladeBall.verticalDegrees, 5.0, 15.0, "%.1f");
        }

        ImGui::SetNextItemWidth(width - 210.0f);
        ImGui::InputDouble("Flick duration (sec)", &config.bladeBall.flickDurationSeconds, 0.01, 0.05, "%.3f");
        config.bladeBall.flickDurationSeconds =
            std::clamp(config.bladeBall.flickDurationSeconds, 0.0, 10.0);

        SensitivityFields("bladeball", config.bladeBall.robloxSensitivity, width - 210.0f);

        ImGui::Checkbox("Return camera", &config.bladeBall.restoreCamera);
        ImGui::SameLine();
        ImGui::Checkbox("Tap ability after the parry", &config.bladeBall.abilityEnabled);

        ImGui::SetNextItemWidth(width - 210.0f);
        ImGui::InputDouble("Ability delay (sec)", &config.bladeBall.abilityDelaySeconds, 0.01, 0.05, "%.3f");
        config.bladeBall.abilityDelaySeconds =
            std::clamp(config.bladeBall.abilityDelaySeconds, 0.0, 30.0);
    }
    EndCard();

    ImGui::Dummy(ImVec2(0.0f, 8.0f));

    BeginCard("##card_bb_status", width, 100.0f);
    {
        CardHeader("Status");
        ImGui::TextDisabled("Parries sent: %d", m_Status.parries);

        if (m_Status.bladeBallRunning)
            ImGui::TextColored(AccentOf(config), "Curve parry running");
        else if (!config.bladeBall.armed)
            ImGui::TextDisabled("Arm Blade Ball to make these keys live");
        else
            ImGui::TextDisabled("Armed - waiting for %s",
                Keys::Name(config.bladeBall.triggerKey));

        ImGui::TextDisabled("Window 500 ms  |  Whiff 2.0 s");
    }
    EndCard();
}

void Menu::DrawSettingsPage(AppConfig& config, MenuActions& actions)
{
    PageTitle("Settings", "Menu key, appearance and exit");

    const float width = ImGui::GetContentRegionAvail().x;

    BeginCard("##card_settings", width, 170.0f);
    {
        CardHeader("Menu");

        KeyRow("menu", "Menu key", "Shows or hides this panel",
            config.ui.menuKey, Capture::MenuKey, width - 24.0f);

        ImGui::Spacing();
        ImGui::ColorEdit3("Accent", &config.ui.accentR,
            ImGuiColorEditFlags_NoInputs);

        ImGui::SetNextItemWidth(width - 210.0f);
        ImGui::SliderFloat("Panel opacity", &config.ui.panelOpacity, 0.30f, 1.0f, "%.2f");
    }
    EndCard();

    ImGui::Spacing();
    ImGui::TextWrapped(
        "This build stores its settings in ~/Library/Application Support/dribtard. "
        "It is separate from the Windows build and its settings are not shared.");

    ImGui::Spacing();
    if (ImGui::Button("Quit dribtard", ImVec2(150.0f, 30.0f)))
        actions.exitRequested = true;
}

void Menu::Draw(AppConfig& config, MenuActions& actions)
{
    if (!m_Visible)
        return;

    const ImGuiIO& io = ImGui::GetIO();
    m_Accent = AccentOf(config);

    // Keep the panel on screen.
    m_PanelPos.x = std::clamp(m_PanelPos.x, 0.0f, std::max(0.0f, io.DisplaySize.x - kPanelWidth));
    m_PanelPos.y = std::clamp(m_PanelPos.y, 0.0f, std::max(0.0f, io.DisplaySize.y - kPanelHeight));

    ImGui::SetNextWindowPos(m_PanelPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(kPanelWidth, kPanelHeight), ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg,
        ImVec4(0.031f, 0.059f, 0.090f, config.ui.panelOpacity));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 1.0f, 1.0f, 0.10f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 14.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(kPadding, kPadding));

    ImGui::Begin("##dribtard_panel", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoScrollbar);

    // Drag the panel by its top strip; the strip is empty on purpose so a
    // drag can never start on a control.
    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    const ImVec2 panelOrigin = ImGui::GetWindowPos();
    const bool overStrip = io.MousePos.y >= panelOrigin.y &&
        io.MousePos.y <= panelOrigin.y + 16.0f &&
        io.MousePos.x >= panelOrigin.x &&
        io.MousePos.x <= panelOrigin.x + kPanelWidth;

    if (overStrip && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered())
        m_Dragging = true;

    if (m_Dragging)
    {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            m_PanelPos.x += io.MouseDelta.x;
            m_PanelPos.y += io.MouseDelta.y;
        }
        else
        {
            m_Dragging = false;
        }
    }

    const float contentHeight = ImGui::GetContentRegionAvail().y;
    DrawRail(contentHeight);
    ImGui::SameLine(0.0f, kPadding);

    ImGui::BeginChild("##page", ImVec2(0.0f, contentHeight), false);
    switch (m_Page)
    {
    case Page::General:
        DrawGeneralPage(config, actions);
        break;
    case Page::Clicker:
        DrawClickerPage(config);
        break;
    case Page::BladeBall:
        DrawBladeBallPage(config);
        break;
    case Page::Settings:
        DrawSettingsPage(config, actions);
        break;
    }
    ImGui::EndChild();

    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}
