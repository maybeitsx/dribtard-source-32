#include "UI/Menu.hpp"

#include "Keys.hpp"
#include "Macro/BladeBallMacro.hpp"
#include "Macro/MacroEngine.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace
{
    /*
        Shared metrics, copied from the Windows overlay so both builds draw
        the same panel: 900x580 full size, a 46 px icon rail inset 10 px, the
        page starting at x = 58 and y = 16.
    */
    constexpr float kRailWidth = 46.0f;
    constexpr float kRailMargin = 10.0f;
    constexpr float kMainLeft = 58.0f;
    constexpr float kPanelTop = 16.0f;
    constexpr float kCollapsedWidth = 68.0f;
    constexpr float kCardOpacity = 0.82f;
    constexpr float kColumnGap = 12.0f;

    // SliderScalar needs stable doubles to point at.
    constexpr double kMinus180 = -180.0;
    constexpr double kPlus180 = 180.0;
    constexpr double kMinus80 = -80.0;
    constexpr double kPlus80 = 80.0;
    constexpr double kZero = 0.0;
    constexpr double kOne = 1.0;

    ImU32 Faded(ImU32 color)
    {
        const float alpha = ImGui::GetStyle().Alpha;
        if (alpha >= 0.999f)
            return color;

        const ImU32 scaled = static_cast<ImU32>(
            static_cast<float>((color >> IM_COL32_A_SHIFT) & 0xFF) * alpha);
        return (color & ~IM_COL32_A_MASK) | (scaled << IM_COL32_A_SHIFT);
    }

    void AddTooltip(const char* text)
    {
        if (text == nullptr || !ImGui::IsItemHovered())
            return;

        if (ImGui::BeginTooltip())
        {
            ImGui::PushTextWrapPos(320.0f);
            ImGui::TextUnformatted(text);
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    }

    float LabeledItemWidth(const char* label)
    {
        const float labelWidth = ImGui::CalcTextSize(label).x;
        const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
        const float avail = ImGui::GetContentRegionAvail().x;
        return std::max(48.0f, avail - labelWidth - spacing);
    }

    ImVec4 WithAlpha(const ImVec4& color, float alpha)
    {
        return ImVec4(color.x, color.y, color.z, alpha);
    }

    ImVec4 Mix(const ImVec4& a, const ImVec4& b, float t)
    {
        return ImVec4(
            a.x + (b.x - a.x) * t,
            a.y + (b.y - a.y) * t,
            a.z + (b.z - a.z) * t,
            a.w + (b.w - a.w) * t);
    }
}

void Menu::ShowNotice(const std::string& text)
{
    if (text.empty())
        return;

    m_NoticeText = text;
    m_NoticeUntil = ImGui::GetTime() + 2.5;
}

bool Menu::Toggle(const char* label, bool* value, int animIndex, const char* tooltip)
{
    ImGuiIO& io = ImGui::GetIO();
    const float dt = io.DeltaTime > 0.0f ? io.DeltaTime : (1.0f / 60.0f);

    float& anim = m_ToggleAnim[animIndex % 12];
    const float target = *value ? 1.0f : 0.0f;
    anim += (target - anim) * std::min(1.0f, dt * 16.0f);

    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    constexpr float width = 40.0f;
    constexpr float height = 22.0f;

    char hiddenId[128];
    std::snprintf(hiddenId, sizeof(hiddenId), "##%s", label);

    // A real checkbox keeps keyboard navigation working; the switch is drawn
    // over it at almost zero opacity.
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.001f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 3.0f));
    const bool changed = ImGui::Checkbox(hiddenId, value);
    ImGui::PopStyleVar(2);

    const bool hovered = ImGui::IsItemHovered();
    AddTooltip(tooltip);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float t = std::clamp(anim, 0.0f, 1.0f);
    const ImVec2 trackMin(cursor.x + 2.0f, cursor.y + 2.0f);
    const ImVec2 trackMax(cursor.x + width - 2.0f, cursor.y + height - 2.0f);
    const ImVec4 off = hovered ? ImVec4(0.19f, 0.23f, 0.28f, 1.0f)
        : ImVec4(0.11f, 0.14f, 0.18f, 1.0f);
    const ImVec4 track(
        off.x + (m_Accent.x - off.x) * t * 0.75f,
        off.y + (m_Accent.y - off.y) * t * 0.75f,
        off.z + (m_Accent.z - off.z) * t * 0.75f, 1.0f);

    draw->AddRectFilled(trackMin, trackMax, ImGui::GetColorU32(track), 9.0f);
    draw->AddRect(trackMin, trackMax,
        ImGui::GetColorU32(WithAlpha(m_Accent, 0.18f + 0.32f * t)), 9.0f);

    const ImVec2 knob(
        trackMin.x + 9.0f + t * (trackMax.x - trackMin.x - 18.0f),
        (trackMin.y + trackMax.y) * 0.5f);
    if (t > 0.05f)
    {
        draw->AddCircleFilled(knob, 8.0f,
            ImGui::GetColorU32(WithAlpha(m_Accent, 0.20f * t)), 20);
    }
    draw->AddCircleFilled(knob, 6.0f, Faded(IM_COL32(239, 247, 250, 255)), 20);

    return changed;
}

bool Menu::Chip(const char* label, bool selected, float width)
{
    ImGui::PushStyleColor(ImGuiCol_Button,
        selected ? ImVec4(m_Accent.x, m_Accent.y, m_Accent.z, 0.26f)
                 : ImVec4(0.10f, 0.12f, 0.16f, 0.92f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
        ImVec4(m_Accent.x, m_Accent.y, m_Accent.z, 0.36f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
        ImVec4(m_Accent.x, m_Accent.y, m_Accent.z, 0.44f));
    ImGui::PushStyleColor(ImGuiCol_Text,
        selected ? ImVec4(1.0f, 0.96f, 0.99f, 1.0f) : ImVec4(0.68f, 0.70f, 0.76f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border,
        selected ? WithAlpha(m_Accent, 0.65f) : ImVec4(1.0f, 1.0f, 1.0f, 0.08f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);

    const bool clicked = ImGui::Button(label, ImVec2(width, 24.0f));

    ImGui::PopStyleVar();
    ImGui::PopStyleColor(5);
    return clicked;
}

void Menu::BeginCard(const char* id, float width, float height)
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg,
        ImVec4(m_CardColor.x, m_CardColor.y, m_CardColor.z, kCardOpacity));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 1.0f, 1.0f, 0.08f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 8.0f));
    ImGui::BeginChild(id, ImVec2(width, height),
        ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
}

void Menu::EndCard()
{
    /*
        Several cards end by moving the cursor below their last row to leave
        breathing room. ImGui reads a moved cursor with no item after it as an
        attempt to grow the child past its content and raises "Code uses
        SetCursorPos()/SetCursorScreenPos() to extend window/parent boundaries"
        (the Calibration card did exactly this). A zero-height Dummy records
        that space properly, so every card closes cleanly.
    */
    ImGui::Dummy(ImVec2(0.0f, 0.0f));

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}

void Menu::CardHeader(const char* title, float cardWidth)
{
    // The same instrument-panel header as the Windows cards: a dark strip, a
    // ringed accent dot, the title, three status pips and an accent underline.
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 window = ImGui::GetWindowPos();
    constexpr float headerHeight = 25.0f;
    const ImVec2 min = window;
    const ImVec2 max(window.x + cardWidth, window.y + headerHeight);

    draw->AddRectFilled(min, max, Faded(IM_COL32(17, 25, 35, 205)),
        10.0f, ImDrawFlags_RoundCornersTop);

    const ImVec2 dot(window.x + 13.0f, window.y + headerHeight * 0.5f);
    const ImU32 accent = ImGui::GetColorU32(m_Accent);
    draw->AddCircle(dot, 4.0f, accent, 16, 1.2f);
    draw->AddCircleFilled(dot, 1.5f, accent, 12);

    draw->AddText(ImVec2(window.x + 23.0f, window.y + 5.0f),
        Faded(IM_COL32(240, 241, 246, 255)), title);

    const float pipY = window.y + headerHeight * 0.5f;
    for (int i = 0; i < 3; ++i)
    {
        draw->AddCircleFilled(ImVec2(window.x + cardWidth - 20.0f + i * 5.0f, pipY),
            1.0f, Faded(IM_COL32(120, 151, 166, 150)), 8);
    }

    const float lineY = window.y + headerHeight;
    draw->AddLine(ImVec2(window.x + 1.0f, lineY),
        ImVec2(window.x + cardWidth - 1.0f, lineY),
        Faded(IM_COL32(255, 255, 255, 18)), 1.0f);
    draw->AddLine(ImVec2(window.x + 12.0f, lineY),
        ImVec2(window.x + 48.0f, lineY), accent, 1.5f);

    ImGui::SetCursorPosY(headerHeight + 8.0f);
}

void Menu::Divider(float width)
{
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    draw->AddLine(ImVec2(p.x + 12.0f, p.y), ImVec2(p.x + width - 12.0f, p.y),
        Faded(IM_COL32(255, 255, 255, 13)), 1.0f);
    ImGui::Dummy(ImVec2(0.0f, 6.0f));
}

void Menu::StatusPill(const AppConfig& config)
{
    (void)config;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 size(ImGui::GetContentRegionAvail().x, 24.0f);

    ImVec4 dotColor;
    const char* text = "Roblox not detected";
    if (!m_Status.hotkeysActive)
    {
        // Every hotkey, including the menu key that closes this panel, needs
        // the listen-only event tap.
        dotColor = ImVec4(0.95f, 0.62f, 0.40f, 1.0f);
        text = "Hotkeys off: allow Input Monitoring";
    }
    else if (m_Status.chatActive)
    {
        dotColor = ImVec4(0.95f, 0.78f, 0.45f, 1.0f);
        text = "Chat open - keybinds paused";
    }
    else if (m_Status.gameFocused)
    {
        dotColor = ImVec4(0.55f, 0.90f, 0.62f, 1.0f);
        text = "Roblox focused";
    }
    else if (m_Status.gameRunning)
    {
        dotColor = ImVec4(0.95f, 0.78f, 0.45f, 1.0f);
        text = "Roblox running, not focused";
    }
    else
    {
        dotColor = ImVec4(0.90f, 0.55f, 0.55f, 1.0f);
    }

    draw->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
        Faded(IM_COL32(10, 16, 24, 210)), 6.0f);
    draw->AddCircleFilled(ImVec2(pos.x + 12.0f, pos.y + size.y * 0.5f), 3.5f,
        ImGui::GetColorU32(dotColor), 12);
    draw->AddText(ImVec2(pos.x + 22.0f, pos.y + 4.0f),
        Faded(IM_COL32(205, 208, 218, 255)), text);

    ImGui::Dummy(size);
}

void Menu::KeyRow(const char* id, const char* title, const char* subtitle,
    int keyCode, Capture target, float width)
{
    ImGui::TextUnformatted(title);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.50f, 0.51f, 0.56f, 1.0f));
    ImGui::TextUnformatted(subtitle);
    ImGui::PopStyleColor();

    const float rowY = ImGui::GetCursorPosY();
    ImGui::SetCursorPosY(rowY - ImGui::GetTextLineHeightWithSpacing() * 2.0f);
    ImGui::SetCursorPosX(width - 138.0f);

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.14f, 0.18f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.16f, 0.24f, 0.27f, 1.0f));
    const std::string keyLabel = std::string(Keys::Name(keyCode)) + "##key_" + id;
    if (ImGui::Button(keyLabel.c_str(), ImVec2(58.0f, 22.0f)))
        m_Capture = target;
    ImGui::PopStyleColor(2);

    ImGui::SameLine(0.0f, 5.0f);
    if (m_Capture == target)
    {
        ImGui::TextColored(m_Accent, "Press...");
    }
    else if (ImGui::Button((std::string("Change##change_") + id).c_str(),
        ImVec2(62.0f, 22.0f)))
    {
        m_Capture = target;
    }

    ImGui::SetCursorPosY(rowY + 4.0f);
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

void Menu::CurvePreview(const char* id, float width, float height,
    double& horizontalDegrees, double& verticalDegrees,
    bool& customCurveAngle, double presetHorizontal, double presetVertical,
    bool showReturn)
{
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 max(min.x + width, min.y + height);
    ImDrawList* draw = ImGui::GetWindowDrawList();

    draw->AddRectFilled(min, max, Faded(IM_COL32(8, 15, 23, 245)), 8.0f);
    draw->AddRect(min, max, Faded(IM_COL32(255, 255, 255, 20)), 8.0f);
    draw->AddText(ImVec2(min.x + 10.0f, min.y + 7.0f),
        Faded(IM_COL32(140, 146, 160, 255)), "Trajectory");

    ImGui::InvisibleButton(id, ImVec2(width, height));
    const bool active = ImGui::IsItemActive();

    const ImVec2 center(min.x + width * 0.5f, min.y + height * 0.74f);
    const float halfW = width * 0.38f;
    const float halfH = height * 0.44f;

    const double horiz = customCurveAngle ? horizontalDegrees : presetHorizontal;
    const double vert = customCurveAngle ? verticalDegrees : presetVertical;
    const ImVec2 target(
        center.x + static_cast<float>(horiz / 180.0) * halfW,
        center.y - static_cast<float>(vert / 80.0) * halfH);

    // Grid and axes.
    for (int i = -2; i <= 2; ++i)
    {
        if (i == 0)
            continue;
        const float x = center.x + halfW * (static_cast<float>(i) / 2.0f);
        const float y = center.y + halfH * (static_cast<float>(i) / 2.0f);
        draw->AddLine(ImVec2(x, min.y + 24.0f), ImVec2(x, max.y - 8.0f),
            Faded(IM_COL32(255, 255, 255, 10)));
        draw->AddLine(ImVec2(min.x + 10.0f, y), ImVec2(max.x - 10.0f, y),
            Faded(IM_COL32(255, 255, 255, 10)));
    }
    draw->AddLine(ImVec2(min.x + 10.0f, center.y), ImVec2(max.x - 10.0f, center.y),
        Faded(IM_COL32(255, 255, 255, 26)));
    draw->AddLine(ImVec2(center.x, min.y + 24.0f), ImVec2(center.x, max.y - 8.0f),
        Faded(IM_COL32(255, 255, 255, 26)));

    const ImU32 accent = ImGui::GetColorU32(m_Accent);

    // Return path, dashed, when the macro puts the camera back.
    if (showReturn)
    {
        const float dx = target.x - center.x;
        const float dy = target.y - center.y;
        const int segments = 14;
        for (int i = 0; i < segments; i += 2)
        {
            const float t0 = static_cast<float>(i) / segments;
            const float t1 = static_cast<float>(i + 1) / segments;
            draw->AddLine(
                ImVec2(target.x - dx * t0, target.y - dy * t0),
                ImVec2(target.x - dx * t1, target.y - dy * t1),
                ImGui::GetColorU32(WithAlpha(m_Accent, 0.35f)), 1.0f);
        }
    }

    // Sweep, target and a soft end flash.
    draw->AddLine(center, target, accent, 2.0f);

    const float pulse = 1.0f + 0.25f * std::sin(static_cast<float>(ImGui::GetTime()) * 4.0f);
    draw->AddCircle(target, 10.0f * pulse,
        ImGui::GetColorU32(WithAlpha(m_Accent, 0.30f)), 24, 1.2f);
    draw->AddCircleFilled(target, 4.0f, accent, 16);

    const float angle = std::atan2(target.y - center.y, target.x - center.x);
    const float head = 9.0f;
    draw->AddTriangleFilled(
        target,
        ImVec2(target.x - std::cos(angle - 0.45f) * head,
            target.y - std::sin(angle - 0.45f) * head),
        ImVec2(target.x - std::cos(angle + 0.45f) * head,
            target.y - std::sin(angle + 0.45f) * head),
        accent);

    char label[64];
    std::snprintf(label, sizeof(label), "%.0f / %.0f deg", horiz, vert);
    const ImVec2 labelSize = ImGui::CalcTextSize(label);
    draw->AddText(ImVec2(max.x - 10.0f - labelSize.x, min.y + 7.0f),
        Faded(IM_COL32(170, 175, 190, 255)), label);

    if (active)
    {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        horizontalDegrees = std::clamp(
            (static_cast<double>(mouse.x - center.x) / halfW) * 180.0, -180.0, 180.0);
        verticalDegrees = std::clamp(
            (static_cast<double>(center.y - mouse.y) / halfH) * 80.0, -80.0, 80.0);
        customCurveAngle = true;
    }
}

void Menu::CaptureKey(int keyCode, AppConfig& config)
{
    const Capture target = m_Capture;
    m_Capture = Capture::None;

    // F12 is the emergency stop and can never be rebound.
    if (target == Capture::None || keyCode == 0 || keyCode == Keys::F12)
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
        if (keyCode == config.clicker.outputKey)
            return;
        config.clicker.key = keyCode;
        break;
    case Capture::ClickerOutputKey:
        // The output can never be the activation key or the emergency stop.
        if (keyCode == config.clicker.key)
            return;
        config.clicker.outputKey = keyCode;
        break;
    case Capture::BladeBallTriggerKey:
        if (keyCode == config.bladeBall.parryKey)
            return;
        config.bladeBall.triggerKey = keyCode;
        break;
    case Capture::BladeBallParryKey:
        if (keyCode == config.bladeBall.triggerKey)
            return;
        config.bladeBall.parryKey = keyCode;
        break;
    case Capture::BladeBallAbilityKey:
        config.bladeBall.abilityKey = keyCode;
        break;
    case Capture::None:
        break;
    }
}

float Menu::PanelWidth() const
{
    if (m_Visible)
    {
        // PanelWidth is read right after Draw, so m_PortableOpen is current.
        return m_PortableOpen ? kPanelWidth : kCollapsedWidth;
    }
    return kPanelWidth;
}

float Menu::PanelHeight() const
{
    return m_Page == Page::BladeBall ? kBladeBallPanelHeight : kPanelHeight;
}

void Menu::RefreshConfigList()
{
    m_ConfigList = Settings::ListNamed();
    m_ConfigListLoaded = true;

    if (!m_SelectedConfig.empty() &&
        std::find(m_ConfigList.begin(), m_ConfigList.end(), m_SelectedConfig) == m_ConfigList.end())
    {
        m_SelectedConfig.clear();
    }
}

void Menu::DrawRail(float height)
{
    ImGui::SetCursorPos(ImVec2(kRailMargin, kPanelTop - 6.0f));
    ImGui::BeginChild("##rail", ImVec2(kRailWidth, height), false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    // Emblem: a small ringed accent dot, the same mark the cards use.
    {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const ImVec2 center(pos.x + kRailWidth * 0.5f - 6.0f, pos.y + 12.0f);
        const ImU32 accent = ImGui::GetColorU32(m_Accent);
        draw->AddCircle(center, 9.0f, accent, 24, 1.5f);
        draw->AddCircleFilled(center, 3.0f, accent, 16);
        draw->AddCircle(center, 14.0f,
            ImGui::GetColorU32(WithAlpha(m_Accent, 0.16f)), 24, 1.0f);
        ImGui::Dummy(ImVec2(0.0f, 30.0f));
    }

    struct Slot
    {
        const char* label;
        const char* tip;
        Page page;
    };
    const Slot slots[5] = {
        { "GEN", "General: camera curve and timing", Page::General },
        { "CLK", "Clicker: hold or toggle auto click", Page::Clicker },
        { "BB", "Blade Ball: armed curve parry", Page::BladeBall },
        { "CFG", "Config: save and load profiles", Page::Configs },
        { "SET", "Settings: keys, appearance, chat detection", Page::Settings }
    };

    for (const Slot& slot : slots)
    {
        const bool active = m_Page == slot.page;

        if (active)
        {
            ImGui::PushStyleColor(ImGuiCol_Button,
                ImVec4(m_Accent.x, m_Accent.y, m_Accent.z, 0.26f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.96f, 0.94f, 1.0f, 1.0f));
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.62f, 0.64f, 0.71f, 1.0f));
        }

        if (ImGui::Button(slot.label, ImVec2(kRailWidth - 12.0f, 30.0f)))
        {
            if (m_Page != slot.page)
            {
                m_Page = slot.page;
                m_PageFade = 0.0f;
                m_FadePage = slot.page;
            }
        }
        AddTooltip(slot.tip);

        if (active)
        {
            // A short accent pip marks the open tab, like the Windows rail.
            ImDrawList* draw = ImGui::GetWindowDrawList();
            const ImVec2 min = ImGui::GetItemRectMin();
            const ImVec2 max = ImGui::GetItemRectMax();
            draw->AddRectFilled(ImVec2(min.x - 6.0f, min.y + 6.0f),
                ImVec2(min.x - 3.0f, max.y - 6.0f),
                ImGui::GetColorU32(m_Accent), 1.5f);
        }

        ImGui::PopStyleColor(active ? 2 : 1);
        ImGui::Dummy(ImVec2(0.0f, 2.0f));
    }

    ImGui::EndChild();
}

void Menu::DrawGeneralPage(AppConfig& config, MenuActions& actions)
{
    const float avail = ImGui::GetContentRegionAvail().x;
    const float colWidth = (avail - kColumnGap) * 0.5f;
    const float rightX = colWidth + kColumnGap;
    const float topY = ImGui::GetCursorPosY();

    // --- Left: camera movement -------------------------------------------
    BeginCard("##card_curve", colWidth, 320.0f);
    {
        CardHeader("Camera Movement", colWidth);

        const char* presetNames[4] = { "Upwards", "Backwards", "Right", "Left" };
        const float chipWidth = (colWidth - 24.0f - 3.0f * 6.0f) / 4.0f;
        for (int i = 0; i < 4; ++i)
        {
            if (i > 0)
                ImGui::SameLine(0.0f, 6.0f);
            const bool selected = !config.macro.customCurveAngle &&
                static_cast<int>(config.macro.curveType) == i;
            if (Chip(presetNames[i], selected, chipWidth))
            {
                config.macro.curveType = static_cast<CurveType>(i);
                config.macro.customCurveAngle = false;
            }
        }

        ImGui::Dummy(ImVec2(0.0f, 4.0f));

        const CurvePreset& preset = CurvePresetFor(config.macro.curveType);
        CurvePreview("##gen_preview", colWidth - 24.0f, 130.0f,
            config.macro.horizontalDegrees, config.macro.verticalDegrees,
            config.macro.customCurveAngle,
            preset.horizontalDegrees, preset.verticalDegrees,
            config.macro.restoreCamera);

        ImGui::Dummy(ImVec2(0.0f, 2.0f));

        const float toggleY = ImGui::GetCursorPosY();
        ImGui::TextUnformatted("Custom angles");
        ImGui::SetCursorPos(ImVec2(colWidth - 54.0f, toggleY - 2.0f));
        Toggle("Custom angles", &config.macro.customCurveAngle, 0,
            "Use your own horizontal and vertical sweep instead of a preset.");
        ImGui::SetCursorPosY(toggleY + 24.0f);

        if (config.macro.customCurveAngle)
        {
            ImGui::SetNextItemWidth(LabeledItemWidth("Horizontal"));
            ImGui::SliderScalar("Horizontal", ImGuiDataType_Double,
                &config.macro.horizontalDegrees, &kMinus180, &kPlus180, "%.0f deg");
            ImGui::SetNextItemWidth(LabeledItemWidth("Vertical"));
            ImGui::SliderScalar("Vertical", ImGuiDataType_Double,
                &config.macro.verticalDegrees, &kMinus80, &kPlus80, "%.0f deg");
        }
        else
        {
            ImGui::TextDisabled("Preset angles: %.0f / %.0f deg",
                preset.horizontalDegrees, preset.verticalDegrees);
        }
    }
    EndCard();

    // --- Right: calibration ----------------------------------------------
    ImGui::SetCursorPos(ImVec2(rightX, topY));
    BeginCard("##card_calibration", colWidth, 320.0f);
    {
        CardHeader("Calibration", colWidth);

        ImGui::SetNextItemWidth(LabeledItemWidth("Rotate over"));
        ImGui::SliderScalar("Rotate over", ImGuiDataType_Double,
            &config.macro.rotationDurationSeconds, &kZero, &kOne, "%.2f s");
        config.macro.rotationDurationSeconds =
            std::clamp(config.macro.rotationDurationSeconds, 0.0, 1.0);

        float slider = static_cast<float>(RobloxEngineToSlider(config.macro.robloxSensitivity));
        ImGui::SetNextItemWidth(LabeledItemWidth("Roblox slider"));
        if (ImGui::SliderFloat("Roblox slider", &slider, 0.0f, 10.0f, "%.2f"))
            config.macro.robloxSensitivity = RobloxSliderToEngine(slider);
        ImGui::SetNextItemWidth(LabeledItemWidth("Roblox box value"));
        ImGui::InputDouble("Roblox box value", &config.macro.robloxSensitivity, 0.01, 0.1, "%.3f");
        config.macro.robloxSensitivity =
            std::clamp(config.macro.robloxSensitivity, kMinRobloxSensitivity, kMaxRobloxSensitivity);

        ImGui::SetNextItemWidth(LabeledItemWidth("Counts per degree"));
        ImGui::InputDouble("Counts per degree", &config.macro.mouseCountsPerDegree, 0.1, 1.0, "%.2f");
        config.macro.mouseCountsPerDegree =
            std::clamp(config.macro.mouseCountsPerDegree, 0.1, 100.0);

        ImGui::SetNextItemWidth(LabeledItemWidth("Delay before block"));
        ImGui::SliderScalar("Delay before block", ImGuiDataType_Double,
            &config.macro.secondaryDelaySeconds, &kZero, &kOne, "%.2f s");
        config.macro.secondaryDelaySeconds =
            std::clamp(config.macro.secondaryDelaySeconds, 0.0, 30.0);

        const float instantY = ImGui::GetCursorPosY();
        ImGui::TextUnformatted("Instant curve");
        ImGui::SetCursorPos(ImVec2(colWidth - 54.0f, instantY - 2.0f));
        Toggle("Instant curve", &config.macro.instantMode, 1,
            "Send the whole sweep at once instead of over the rotation time.");
        ImGui::SetCursorPosY(instantY + 24.0f);

        const float returnY = ImGui::GetCursorPosY();
        ImGui::TextUnformatted("Return camera");
        ImGui::SetCursorPos(ImVec2(colWidth - 54.0f, returnY - 2.0f));
        Toggle("Return camera", &config.macro.restoreCamera, 2,
            "Undo the sweep so the camera ends where it started.");
        ImGui::SetCursorPosY(returnY + 24.0f);

        const float hotkeyY = ImGui::GetCursorPosY();
        ImGui::TextUnformatted("Hotkey mode (1-4)");
        ImGui::SetCursorPos(ImVec2(colWidth - 54.0f, hotkeyY - 2.0f));
        Toggle("Hotkey mode", &config.macro.hotkeyMode, 3,
            "Switch curves with 1-4 (Upwards, Backwards, Left, Right) while the menu is closed.");
        ImGui::SetCursorPosY(hotkeyY + 24.0f);
    }
    EndCard();

    // --- Keybinds --------------------------------------------------------
    ImGui::SetCursorPos(ImVec2(0.0f, topY + 328.0f));
    BeginCard("##card_general_keys", colWidth, 150.0f);
    {
        CardHeader("Keybinds", colWidth);

        KeyRow("trigger", "Trigger key", "You press this in game",
            config.macro.triggerKey, Capture::TriggerKey, colWidth - 24.0f);
        Divider(colWidth - 24.0f);
        KeyRow("activation", "Ability key", "Tapped before the camera move",
            config.macro.activationKey, Capture::ActivationKey, colWidth - 24.0f);
        Divider(colWidth - 24.0f);
        KeyRow("secondary", "Block key", "Tapped twice at the end",
            config.macro.secondaryKey, Capture::SecondaryKey, colWidth - 24.0f);
    }
    EndCard();

    // --- Session ---------------------------------------------------------
    ImGui::SetCursorPos(ImVec2(rightX, topY + 328.0f));
    BeginCard("##card_general_session", colWidth, 150.0f);
    {
        CardHeader("Session", colWidth);
        StatusPill(config);
        ImGui::Dummy(ImVec2(0.0f, 2.0f));

        if (ImGui::Button("Run curve now", ImVec2(colWidth - 24.0f, 28.0f)))
            actions.startStructure = true;
        AddTooltip("Starts the Structure curve once. Roblox must be focused.");

        ImGui::Dummy(ImVec2(0.0f, 2.0f));
        if (m_Status.structureRunning)
            ImGui::TextColored(m_Accent, "Running");
        else
            ImGui::TextDisabled("Trigger %s in game, or run it here.",
                Keys::Name(config.macro.triggerKey));
    }
    EndCard();
}

void Menu::DrawClickerPage(AppConfig& config)
{
    const float avail = ImGui::GetContentRegionAvail().x;
    const float colWidth = (avail - kColumnGap) * 0.5f;
    const float rightX = colWidth + kColumnGap;
    const float topY = ImGui::GetCursorPosY();

    BeginCard("##card_clicker", colWidth, 320.0f);
    {
        CardHeader("Clicker", colWidth);

        const float chipWidth = (colWidth - 24.0f - 6.0f) * 0.5f;
        if (Chip("Hold", config.clicker.mode == ClickerMode::Hold, chipWidth))
            config.clicker.mode = ClickerMode::Hold;
        ImGui::SameLine(0.0f, 6.0f);
        if (Chip("Toggle", config.clicker.mode == ClickerMode::Toggle, chipWidth))
            config.clicker.mode = ClickerMode::Toggle;

        ImGui::Dummy(ImVec2(0.0f, 4.0f));

        KeyRow("clicker_key",
            config.clicker.mode == ClickerMode::Toggle ? "Toggle key" : "Hold key",
            "Starts and stops the stream",
            config.clicker.key, Capture::ClickerKey, colWidth - 24.0f);

        Divider(colWidth - 24.0f);

        ImGui::TextUnformatted("Output");
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.50f, 0.51f, 0.56f, 1.0f));
        ImGui::TextUnformatted("What each click presses");
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0.0f, 2.0f));

        const bool mouseLeft = config.clicker.outputKey == ClickerMouseLeft;
        const bool mouseRight = config.clicker.outputKey == ClickerMouseRight;
        const bool keyOutput = config.clicker.outputKey >= 0;
        const float outputWidth = (colWidth - 24.0f - 12.0f) / 3.0f;

        if (Chip("Left click", mouseLeft, outputWidth))
            config.clicker.outputKey = ClickerMouseLeft;
        ImGui::SameLine(0.0f, 6.0f);
        if (Chip("Right click", mouseRight, outputWidth))
            config.clicker.outputKey = ClickerMouseRight;
        ImGui::SameLine(0.0f, 6.0f);
        if (Chip("Key", keyOutput, outputWidth))
        {
            if (!keyOutput)
                config.clicker.outputKey = Keys::F;
        }

        if (keyOutput)
        {
            ImGui::Dummy(ImVec2(0.0f, 2.0f));
            KeyRow("clicker_output", "Output key", "Pressed by the clicker",
                config.clicker.outputKey, Capture::ClickerOutputKey, colWidth - 24.0f);
        }

        Divider(colWidth - 24.0f);

        ImGui::SetNextItemWidth(LabeledItemWidth("CPS"));
        ImGui::SliderInt("CPS", &config.clicker.cps, kClickerMinCps, kClickerMaxCps, "%d");
        config.clicker.cps = ClampClickerCps(config.clicker.cps);
    }
    EndCard();

    ImGui::SetCursorPos(ImVec2(rightX, topY));
    BeginCard("##card_clicker_session", colWidth, 320.0f);
    {
        CardHeader("Session", colWidth);
        StatusPill(config);
        ImGui::Dummy(ImVec2(0.0f, 4.0f));

        if (m_Status.clickerRunning)
        {
            ImGui::TextColored(m_Accent, "RUNNING");
            ImGui::TextDisabled("%d clicks", m_Status.clickerClicks);
            if (m_Status.clickerMeasuredCps >= 0.0f)
                ImGui::TextDisabled("%.0f clicks per second", m_Status.clickerMeasuredCps);
        }
        else if (m_Status.clickerEngaged && m_Status.clickerPause == 1)
        {
            ImGui::TextDisabled("Paused: Roblox is not focused");
        }
        else if (m_Status.clickerEngaged && m_Status.clickerPause == 2)
        {
            ImGui::TextDisabled("Paused: the Structure curve is running");
        }
        else if (config.clicker.mode == ClickerMode::Toggle)
        {
            ImGui::TextDisabled("Press %s to start, press it again to stop.",
                Keys::Name(config.clicker.key));
        }
        else
        {
            ImGui::TextDisabled("Hold %s to click.", Keys::Name(config.clicker.key));
        }

        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ImGui::TextWrapped(
            "Each click is held for part of the gap so the game can sample it, "
            "then released for the rest of 1/CPS. The Structure curve pauses "
            "the stream; Blade Ball does not. F12 stops it.");
    }
    EndCard();

    ImGui::SetCursorPos(ImVec2(0.0f, topY + 328.0f));
    BeginCard("##card_clicker_notes", avail, 120.0f);
    {
        CardHeader("Behaviour", avail);
        ImGui::TextDisabled(
            "F12 is the emergency stop in every mode and cannot be rebound. "
            "Hold mode runs while the key is down; Toggle mode keeps running "
            "until the key is pressed again, F12 or a lost licence.");
        ImGui::TextDisabled(
            "A one-sample focus blip does not stop a running stream, and a "
            "hold-key blip is bridged for 40 ms so a clash hitch cannot kill it.");
    }
    EndCard();
}

void Menu::DrawBladeBallPage(AppConfig& config)
{
    const float avail = ImGui::GetContentRegionAvail().x;
    const float colWidth = (avail - kColumnGap) * 0.5f;
    const float rightX = colWidth + kColumnGap;
    const float topY = ImGui::GetCursorPosY();

    BeginCard("##card_bb", colWidth, 300.0f);
    {
        CardHeader("Blade Ball", colWidth);

        const float armedY = ImGui::GetCursorPosY();
        ImGui::TextUnformatted("Armed");
        ImGui::SetCursorPos(ImVec2(colWidth - 54.0f, armedY - 2.0f));
        Toggle("Armed", &config.bladeBall.armed, 6,
            "While armed, Blade Ball owns the parry trigger and the Structure curve is refused.");
        ImGui::SetCursorPosY(armedY + 24.0f);

        ImGui::TextDisabled(config.bladeBall.armed
            ? "Armed: the Structure trigger is refused."
            : "Off: the Blade Ball keys do nothing.");
        ImGui::Dummy(ImVec2(0.0f, 4.0f));

        KeyRow("bb_trigger", "Trigger key", "Runs one curve parry",
            config.bladeBall.triggerKey, Capture::BladeBallTriggerKey, colWidth - 24.0f);
        Divider(colWidth - 24.0f);
        KeyRow("bb_parry", "Parry key", "Pressed by the macro",
            config.bladeBall.parryKey, Capture::BladeBallParryKey, colWidth - 24.0f);
        Divider(colWidth - 24.0f);
        KeyRow("bb_ability", "Ability key", "Optional follow-up",
            config.bladeBall.abilityKey, Capture::BladeBallAbilityKey, colWidth - 24.0f);

        const float hotkeyY = ImGui::GetCursorPosY();
        ImGui::TextUnformatted("Hotkey mode (1-4)");
        ImGui::SetCursorPos(ImVec2(colWidth - 54.0f, hotkeyY - 2.0f));
        Toggle("Parry hotkeys", &config.bladeBall.hotkeyMode, 9,
            "While armed, 1-4 pick Up, Back, Left and Right.");
        ImGui::SetCursorPosY(hotkeyY + 24.0f);
    }
    EndCard();

    ImGui::SetCursorPos(ImVec2(rightX, topY));
    BeginCard("##card_bb_curve", colWidth, 300.0f);
    {
        CardHeader("Curve Parry", colWidth);

        const char* presetNames[4] = { "Up", "Back", "Right", "Left" };
        const float chipWidth = (colWidth - 24.0f - 3.0f * 6.0f) / 4.0f;
        for (int i = 0; i < 4; ++i)
        {
            if (i > 0)
                ImGui::SameLine(0.0f, 6.0f);
            const bool selected = !config.bladeBall.customCurveAngle &&
                static_cast<int>(config.bladeBall.curveType) == i;
            if (Chip(presetNames[i], selected, chipWidth))
            {
                config.bladeBall.curveType = static_cast<CurveType>(i);
                config.bladeBall.customCurveAngle = false;
            }
        }

        ImGui::Dummy(ImVec2(0.0f, 4.0f));

        const float customY = ImGui::GetCursorPosY();
        ImGui::TextUnformatted("Custom angle");
        ImGui::SetCursorPos(ImVec2(colWidth - 54.0f, customY - 2.0f));
        Toggle("Custom angle", &config.bladeBall.customCurveAngle, 10,
            "Use your own flick instead of a preset angle.");
        ImGui::SetCursorPosY(customY + 24.0f);

        if (config.bladeBall.customCurveAngle)
        {
            ImGui::SetNextItemWidth(LabeledItemWidth("Flick horizontal"));
            ImGui::SliderScalar("Flick horizontal", ImGuiDataType_Double,
                &config.bladeBall.horizontalDegrees, &kMinus180, &kPlus180, "%.0f deg");
            ImGui::SetNextItemWidth(LabeledItemWidth("Flick vertical"));
            ImGui::SliderScalar("Flick vertical", ImGuiDataType_Double,
                &config.bladeBall.verticalDegrees, &kMinus80, &kPlus80, "%.0f deg");
        }

        ImGui::SetNextItemWidth(LabeledItemWidth("Flick duration"));
        ImGui::SliderScalar("Flick duration", ImGuiDataType_Double,
            &config.bladeBall.flickDurationSeconds, &kZero, &kOne, "%.2f s");
        config.bladeBall.flickDurationSeconds =
            std::clamp(config.bladeBall.flickDurationSeconds, 0.0, 10.0);

        float slider = static_cast<float>(RobloxEngineToSlider(config.bladeBall.robloxSensitivity));
        ImGui::SetNextItemWidth(LabeledItemWidth("Roblox slider"));
        if (ImGui::SliderFloat("Roblox slider", &slider, 0.0f, 10.0f, "%.2f"))
            config.bladeBall.robloxSensitivity = RobloxSliderToEngine(slider);
        ImGui::SetNextItemWidth(LabeledItemWidth("Roblox box value"));
        ImGui::InputDouble("Roblox box value", &config.bladeBall.robloxSensitivity, 0.01, 0.1, "%.3f");
        config.bladeBall.robloxSensitivity =
            std::clamp(config.bladeBall.robloxSensitivity, kMinRobloxSensitivity, kMaxRobloxSensitivity);
    }
    EndCard();

    // --- Preview ---------------------------------------------------------
    ImGui::SetCursorPos(ImVec2(0.0f, topY + 308.0f));
    BeginCard("##card_bb_preview", avail, 230.0f);
    {
        CardHeader("Curve Parry Trajectory", avail);

        const ParryCurvePreset& preset = ParryCurvePresetFor(config.bladeBall.curveType);
        CurvePreview("##bb_preview", avail - 24.0f, 170.0f,
            config.bladeBall.horizontalDegrees, config.bladeBall.verticalDegrees,
            config.bladeBall.customCurveAngle,
            preset.horizontalDegrees, preset.verticalDegrees,
            config.bladeBall.restoreCamera);

        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Drag the target to set a custom flick angle.");
    }
    EndCard();

    // --- Status + ability ------------------------------------------------
    ImGui::SetCursorPos(ImVec2(0.0f, topY + 546.0f));
    BeginCard("##card_bb_status", avail, 140.0f);
    {
        CardHeader("Status", avail);

        ImGui::TextDisabled("Parries sent: %d", m_Status.parries);

        if (m_Status.bladeBallRunning)
            ImGui::TextColored(m_Accent, "Curve parry running");
        else if (!config.bladeBall.armed)
            ImGui::TextDisabled("Arm Blade Ball to make these keys live");
        else
            ImGui::TextDisabled("Armed - waiting for %s",
                Keys::Name(config.bladeBall.triggerKey));

        ImGui::Dummy(ImVec2(0.0f, 4.0f));

        const float abilityY = ImGui::GetCursorPosY();
        ImGui::TextUnformatted("Tap ability after the parry");
        ImGui::SetCursorPos(ImVec2(avail - 54.0f, abilityY - 2.0f));
        Toggle("Tap ability", &config.bladeBall.abilityEnabled, 8,
            "Press the ability key once the camera has returned.");
        ImGui::SetCursorPosY(abilityY + 24.0f);

        ImGui::SetNextItemWidth(LabeledItemWidth("Ability delay"));
        ImGui::SliderScalar("Ability delay", ImGuiDataType_Double,
            &config.bladeBall.abilityDelaySeconds, &kZero, &kOne, "%.2f s");
        config.bladeBall.abilityDelaySeconds =
            std::clamp(config.bladeBall.abilityDelaySeconds, 0.0, 30.0);
    }
    EndCard();
}

void Menu::DrawConfigsPage(AppConfig& config)
{
    if (!m_ConfigListLoaded)
        RefreshConfigList();

    const float avail = ImGui::GetContentRegionAvail().x;
    const float leftWidth = avail * 0.4f - kColumnGap * 0.5f;
    const float rightWidth = avail - leftWidth - kColumnGap;
    const float rightX = leftWidth + kColumnGap;
    const float topY = ImGui::GetCursorPosY();

    BeginCard("##card_config_save", leftWidth, 220.0f);
    {
        CardHeader("Save current", leftWidth);

        ImGui::TextDisabled("Name the profile, then save every setting in it.");
        ImGui::Dummy(ImVec2(0.0f, 4.0f));

        ImGui::SetNextItemWidth(leftWidth - 24.0f);
        ImGui::InputText("##config_name", m_ConfigName, sizeof(m_ConfigName));

        ImGui::Dummy(ImVec2(0.0f, 4.0f));

        if (ImGui::Button("Save config", ImVec2(leftWidth - 24.0f, 28.0f)))
        {
            const std::string name = Settings::SanitizeName(m_ConfigName);
            if (name.empty())
            {
                m_ConfigStatus = "Use letters, numbers, spaces, _ or -.";
            }
            else if (Settings::SaveNamed(name, config))
            {
                m_ConfigStatus = "Saved " + name + ".";
                m_SelectedConfig = name;
                RefreshConfigList();
            }
            else
            {
                m_ConfigStatus = "Could not save that config.";
            }
        }

        if (!m_ConfigStatus.empty())
            ImGui::TextWrapped("%s", m_ConfigStatus.c_str());
    }
    EndCard();

    ImGui::SetCursorPos(ImVec2(rightX, topY));
    BeginCard("##card_config_list", rightWidth, 420.0f);
    {
        CardHeader("Saved configs", rightWidth);

        if (m_ConfigList.empty())
        {
            ImGui::TextDisabled("No saved configs yet.");
        }
        else
        {
            for (const std::string& name : m_ConfigList)
            {
                const bool selected = name == m_SelectedConfig;
                if (ImGui::Selectable((name + "##cfg").c_str(), selected,
                    ImGuiSelectableFlags_None, ImVec2(rightWidth - 24.0f, 22.0f)))
                {
                    m_SelectedConfig = name;
                }
            }
        }

        ImGui::Dummy(ImVec2(0.0f, 8.0f));

        const bool hasSelection = !m_SelectedConfig.empty();
        ImGui::BeginDisabled(!hasSelection);
        if (ImGui::Button("Load", ImVec2(90.0f, 26.0f)))
        {
            AppConfig loaded;
            if (Settings::LoadNamed(m_SelectedConfig, loaded))
            {
                config = loaded;
                m_ConfigStatus = "Loaded " + m_SelectedConfig + ".";
                m_ConfigListLoaded = true;
            }
            else
            {
                m_ConfigStatus = "Could not load " + m_SelectedConfig + ".";
            }
        }
        ImGui::SameLine(0.0f, 6.0f);
        if (ImGui::Button("Delete", ImVec2(90.0f, 26.0f)))
        {
            if (Settings::DeleteNamed(m_SelectedConfig))
                m_ConfigStatus = "Deleted " + m_SelectedConfig + ".";
            m_SelectedConfig.clear();
            RefreshConfigList();
        }
        ImGui::EndDisabled();

        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        if (ImGui::Button("Refresh list", ImVec2(110.0f, 24.0f)))
            RefreshConfigList();

        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        ImGui::TextDisabled(
            "Profiles live in ~/Library/Application Support/dribtard/configs. "
            "The app also saves your last-used settings automatically.");
    }
    EndCard();
}

void Menu::DrawSettingsPage(AppConfig& config, MenuActions& actions)
{
    const float avail = ImGui::GetContentRegionAvail().x;
    const float colWidth = (avail - kColumnGap) * 0.5f;
    const float rightX = colWidth + kColumnGap;
    const float topY = ImGui::GetCursorPosY();

    // --- Left: Client ----------------------------------------------------
    BeginCard("##card_client", colWidth, 190.0f);
    {
        CardHeader("Client", colWidth);

        const float accentY = ImGui::GetCursorPosY();
        ImGui::TextUnformatted("UI color");
        ImGui::SetCursorPos(ImVec2(colWidth - 154.0f, accentY - 2.0f));
        ImGui::SetNextItemWidth(140.0f);
        ImGui::ColorEdit3("Accent color", &config.ui.accentR, ImGuiColorEditFlags_NoInputs);
        AddTooltip("Main highlight color used for headers, switches, sliders and glows.");
        ImGui::SetCursorPosY(accentY + 24.0f);

        const float notifY = ImGui::GetCursorPosY();
        ImGui::TextUnformatted("Notifications");
        ImGui::SetCursorPos(ImVec2(colWidth - 54.0f, notifY - 2.0f));
        Toggle("Notifications", &config.ui.showNotifications, 4,
            "Show the top-right notice when a hotkey switches the curve.");
        ImGui::SetCursorPosY(notifY + 24.0f);

        const float cardY = ImGui::GetCursorPosY();
        ImGui::TextUnformatted("Card color");
        ImGui::SetCursorPos(ImVec2(colWidth - 154.0f, cardY - 2.0f));
        ImGui::SetNextItemWidth(140.0f);
        ImGui::ColorEdit3("Card color", &config.ui.cardR, ImGuiColorEditFlags_NoInputs);
        ImGui::SetCursorPosY(cardY + 24.0f);

        const float panelY = ImGui::GetCursorPosY();
        ImGui::TextUnformatted("Panel color");
        ImGui::SetCursorPos(ImVec2(colWidth - 154.0f, panelY - 2.0f));
        ImGui::SetNextItemWidth(140.0f);
        ImGui::ColorEdit3("Panel color", &config.ui.panelR, ImGuiColorEditFlags_NoInputs);
    }
    EndCard();

    // --- Left: Keybinds --------------------------------------------------
    ImGui::SetCursorPos(ImVec2(0.0f, topY + 198.0f));
    BeginCard("##card_settings_keys", colWidth, 170.0f);
    {
        CardHeader("Keybinds", colWidth);

        KeyRow("menu", "Menu key", "Show or hide this panel",
            config.ui.menuKey, Capture::MenuKey, colWidth - 24.0f);
        Divider(colWidth - 24.0f);
        KeyRow("settings_trigger", "Trigger key", "Starts the curve",
            config.macro.triggerKey, Capture::TriggerKey, colWidth - 24.0f);
        Divider(colWidth - 24.0f);
        KeyRow("settings_ability", "Ability key", "Tapped by the macro",
            config.macro.activationKey, Capture::ActivationKey, colWidth - 24.0f);
    }
    EndCard();

    // --- Left: Appearance ------------------------------------------------
    ImGui::SetCursorPos(ImVec2(0.0f, topY + 376.0f));
    BeginCard("##card_appearance", colWidth, 150.0f);
    {
        CardHeader("Appearance", colWidth);

        ImGui::SetNextItemWidth(LabeledItemWidth("Icon glow"));
        ImGui::SliderFloat("Icon glow", &config.ui.glowIntensity, 0.0f, 1.5f, "%.2f");
        ImGui::SetNextItemWidth(LabeledItemWidth("Window halo"));
        ImGui::SliderFloat("Window halo", &config.ui.windowGlowIntensity, 0.0f, 1.5f, "%.2f");
        ImGui::SetNextItemWidth(LabeledItemWidth("Menu opacity"));
        ImGui::SliderFloat("Menu opacity", &config.ui.panelOpacity, 0.30f, 1.0f, "%.2f");
        config.ui.panelOpacity = std::clamp(config.ui.panelOpacity, 0.30f, 1.0f);

        const float fadeY = ImGui::GetCursorPosY();
        ImGui::TextUnformatted("Tab fade");
        ImGui::SetCursorPos(ImVec2(colWidth - 54.0f, fadeY - 2.0f));
        Toggle("Tab fade", &config.ui.tabFade, 5,
            "Fade the page when switching tabs.");
        ImGui::SetCursorPosY(fadeY + 24.0f);
    }
    EndCard();

    // --- Right: Menu mode ------------------------------------------------
    ImGui::SetCursorPos(ImVec2(rightX, topY));
    BeginCard("##card_menu_mode", colWidth, 130.0f);
    {
        CardHeader("Menu mode", colWidth);

        ImGui::TextDisabled(
            "Portable hides the panel behind the rail until the pointer is over it.");
        ImGui::Dummy(ImVec2(0.0f, 4.0f));

        const float modeWidth = (colWidth - 24.0f - 6.0f) * 0.5f;
        if (Chip("Portable", config.ui.portableMode, modeWidth))
            config.ui.portableMode = true;
        ImGui::SameLine(0.0f, 6.0f);
        if (Chip("Classic", !config.ui.portableMode, modeWidth))
            config.ui.portableMode = false;
    }
    EndCard();

    // --- Right: Miscellaneous --------------------------------------------
    ImGui::SetCursorPos(ImVec2(rightX, topY + 138.0f));
    BeginCard("##card_misc", colWidth, 300.0f);
    {
        CardHeader("Miscellaneous", colWidth);

        const float chatY = ImGui::GetCursorPosY();
        ImGui::TextUnformatted("Chat detection");
        ImGui::SetCursorPos(ImVec2(colWidth - 54.0f, chatY - 2.0f));
        Toggle("Chat detection", &config.ui.chatDetection, 7,
            "Pause every macro keybind while the in-game chat is open: / opens it, Enter sends the message and re-enables the keys.");
        ImGui::SetCursorPosY(chatY + 24.0f);
        ImGui::TextDisabled(config.ui.chatDetection
            ? (m_Status.chatActive ? "Chat open - keybinds paused" : "Waiting for / in the game")
            : "Off: keybinds stay live while you type");

        ImGui::Dummy(ImVec2(0.0f, 6.0f));

        const float darkenY = ImGui::GetCursorPosY();
        ImGui::TextUnformatted("Darken background");
        ImGui::SetCursorPos(ImVec2(colWidth - 54.0f, darkenY - 2.0f));
        Toggle("Darken background", &config.ui.darkenBackground, 0,
            "Dim the game behind the menu so the panel stands out.");
        ImGui::SetCursorPosY(darkenY + 24.0f);

        if (config.ui.darkenBackground)
        {
            ImGui::SetNextItemWidth(LabeledItemWidth("Dim opacity"));
            ImGui::SliderFloat("Dim opacity", &config.ui.dimOpacity, 0.05f, 0.85f, "%.2f");
            config.ui.dimOpacity = std::clamp(config.ui.dimOpacity, 0.05f, 0.85f);
        }

        ImGui::Dummy(ImVec2(0.0f, 6.0f));

        const float panicY = ImGui::GetCursorPosY();
        ImGui::TextUnformatted("Panic key");
        const char* panicLabel = "F12 (Reserved)";
        const ImVec2 panicSize = ImGui::CalcTextSize(panicLabel);
        ImGui::SetCursorPos(ImVec2(colWidth - 14.0f - panicSize.x, panicY));
        ImGui::TextDisabled("%s", panicLabel);

        ImGui::SetCursorPosY(panicY + 26.0f);

        if (ImGui::Button("Restore defaults", ImVec2(colWidth - 24.0f, 24.0f)))
        {
            config = AppConfig{};
            m_Capture = Capture::None;
            m_ConfigStatus.clear();
        }
        AddTooltip("Reset every setting to its original value.");

        ImGui::Dummy(ImVec2(0.0f, 4.0f));

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.32f, 0.10f, 0.16f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.46f, 0.14f, 0.22f, 1.0f));
        if (ImGui::Button("Unload dribtard", ImVec2(colWidth - 24.0f, 28.0f)))
            actions.exitRequested = true;
        ImGui::PopStyleColor(2);
    }
    EndCard();
}

void Menu::DrawNoticeWindow()
{
    if (m_NoticeText.empty())
        return;

    if (ImGui::GetTime() >= m_NoticeUntil)
    {
        m_NoticeText.clear();
        return;
    }

    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 18.0f, 18.0f),
        ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.94f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.02f, 0.04f, 0.07f, 0.94f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(m_Accent.x, m_Accent.y, m_Accent.z, 0.55f));

    ImGui::Begin("##dribtard_notice", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_NoFocusOnAppearing);

    ImGui::TextColored(m_Accent, "Curve");
    ImGui::SameLine();
    ImGui::TextUnformatted(m_NoticeText.c_str());

    ImGui::End();
    ImGui::PopStyleColor(2);
}

void Menu::Draw(AppConfig& config, MenuActions& actions)
{
    if (!m_Visible)
        return;

    m_Accent = ImVec4(config.ui.accentR, config.ui.accentG, config.ui.accentB, 1.0f);
    m_CardColor = ImVec4(config.ui.cardR, config.ui.cardG, config.ui.cardB, 1.0f);
    m_PanelColor = ImVec4(config.ui.panelR, config.ui.panelG, config.ui.panelB, 1.0f);
    m_PanelOpacity = std::clamp(config.ui.panelOpacity, 0.30f, 1.0f);

    ImGuiIO& io = ImGui::GetIO();
    const float height = PanelHeight();

    // Portable mode: the panel is only the rail until the pointer reaches it.
    if (config.ui.portableMode)
    {
        const ImVec2 mouse = io.MousePos;
        const bool overRail = mouse.x >= m_PanelPos.x &&
            mouse.x <= m_PanelPos.x + kCollapsedWidth &&
            mouse.y >= m_PanelPos.y && mouse.y <= m_PanelPos.y + height;
        const bool overPanel = mouse.x >= m_PanelPos.x &&
            mouse.x <= m_PanelPos.x + kPanelWidth &&
            mouse.y >= m_PanelPos.y && mouse.y <= m_PanelPos.y + height;

        if (overRail)
            m_PortableOpen = true;
        else if (!overPanel)
            m_PortableOpen = false;
    }
    else
    {
        m_PortableOpen = true;
    }

    const float width = PanelWidth();

    m_PanelPos.x = std::clamp(m_PanelPos.x, 0.0f, std::max(0.0f, io.DisplaySize.x - width));
    m_PanelPos.y = std::clamp(m_PanelPos.y, 0.0f, std::max(0.0f, io.DisplaySize.y - height));

    // The optional dim sits behind the whole menu, on the desktop layer.
    if (config.ui.darkenBackground)
    {
        ImGui::GetBackgroundDrawList()->AddRectFilled(
            ImVec2(0.0f, 0.0f), io.DisplaySize,
            IM_COL32(0, 0, 0, static_cast<int>(
                std::clamp(config.ui.dimOpacity, 0.05f, 0.85f) * 255.0f)));
    }

    ImGui::SetNextWindowPos(m_PanelPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg,
        ImVec4(m_PanelColor.x, m_PanelColor.y, m_PanelColor.z, m_PanelOpacity));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 1.0f, 1.0f, 0.10f));
    // Accent-driven controls follow the user's UI colour, like the Windows menu.
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, m_Accent);
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(
        std::min(1.0f, m_Accent.x + 0.09f),
        std::min(1.0f, m_Accent.y + 0.08f),
        std::min(1.0f, m_Accent.z + 0.08f), 1.0f));
    ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(
        std::min(1.0f, m_Accent.x + 0.09f),
        std::min(1.0f, m_Accent.y + 0.08f),
        std::min(1.0f, m_Accent.z + 0.08f), 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 14.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGui::Begin("##dribtard_panel", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse);

    // Drag the panel by its top strip; the strip is empty on purpose so a
    // drag can never start on a control.
    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    const ImVec2 panelOrigin = ImGui::GetWindowPos();
    const bool overStrip = io.MousePos.y >= panelOrigin.y &&
        io.MousePos.y <= panelOrigin.y + 16.0f &&
        io.MousePos.x >= panelOrigin.x &&
        io.MousePos.x <= panelOrigin.x + width;

    if (overStrip && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !ImGui::IsAnyItemHovered())
    {
        m_Dragging = true;
    }

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

    // Window halo and accent edge, drawn with the configured glow strengths.
    {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 max(panelOrigin.x + width, panelOrigin.y + height);
        const float glow = std::clamp(config.ui.windowGlowIntensity, 0.0f, 1.5f);
        const float iconGlow = std::clamp(config.ui.glowIntensity, 0.0f, 1.5f);

        draw->AddRect(panelOrigin, max,
            ImGui::GetColorU32(WithAlpha(m_Accent, 0.14f + glow * 0.16f)),
            14.0f, 0, 1.5f);
        draw->AddRectFilledMultiColor(
            ImVec2(panelOrigin.x + 14.0f, panelOrigin.y),
            ImVec2(max.x - 14.0f, panelOrigin.y + 2.0f),
            IM_COL32(0, 0, 0, 0),
            ImGui::GetColorU32(WithAlpha(m_Accent, 0.35f + iconGlow * 0.16f)),
            ImGui::GetColorU32(WithAlpha(m_Accent, 0.35f + iconGlow * 0.16f)),
            IM_COL32(0, 0, 0, 0));
    }

    DrawRail(height - kPanelTop);

    if (width > kCollapsedWidth)
    {
        // Page fade when a tab is switched.
        if (config.ui.tabFade)
        {
            if (m_FadePage != m_Page)
            {
                m_FadePage = m_Page;
                m_PageFade = 0.0f;
            }
            m_PageFade = std::min(1.0f, m_PageFade + io.DeltaTime * 7.0f);
        }
        else
        {
            m_PageFade = 1.0f;
        }

        const float pageHeight = height - kPanelTop - 14.0f;
        ImGui::SetCursorPos(ImVec2(kMainLeft, kPanelTop));
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha,
            std::clamp(m_PageFade, 0.0f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 6.0f));
        ImGui::BeginChild("##page",
            ImVec2(width - kMainLeft - 10.0f, pageHeight), false,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

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
        case Page::Configs:
            DrawConfigsPage(config);
            break;
        case Page::Settings:
            DrawSettingsPage(config, actions);
            break;
        }

        ImGui::EndChild();
        ImGui::PopStyleVar(2);
    }

    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(5);

    if (config.ui.showNotifications)
        DrawNoticeWindow();
}
