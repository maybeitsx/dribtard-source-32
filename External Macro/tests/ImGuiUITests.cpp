#include "Overlay/ImGuiUI.hpp"
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

namespace
{
    struct Item
    {
        ImRect rect;
        bool disabled = false;
    };
    std::map<ImGuiID, Item> g_ItemsById;
    std::map<std::string, Item> g_ItemsByLabel;
    bool g_TooltipRendered = false;

    void Check(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }
}

// Observe the built-in ImGui test hooks; no third-party test engine needed.
void ImGuiTestEngineHook_ItemAdd(ImGuiContext* ctx, ImGuiID id, const ImRect& bb, const ImGuiLastItemData*)
{
    const Item item{ bb, (ctx->CurrentItemFlags & ImGuiItemFlags_Disabled) != 0 };
    g_ItemsById[id] = item;
    if (ctx->CurrentWindow && ctx->CurrentWindow->IDStack.Size > 0 &&
        ctx->CurrentWindow->GetID("Curve Type") == id)
        g_ItemsByLabel["Curve Type"] = item;
}

void ImGuiTestEngineHook_ItemInfo(ImGuiContext*, ImGuiID id, const char* label, ImGuiItemStatusFlags)
{
    if (g_ItemsById.count(id))
        g_ItemsByLabel[label] = g_ItemsById.at(id);
}

void ImGuiTestEngineHook_Log(ImGuiContext*, const char*, ...) {}
const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*, ImGuiID) { return nullptr; }struct ImGuiUITestAccess
{
    static void Theme(ImGuiUI& ui) { ui.ApplyTheme(); }
    static void Draw(ImGuiUI& ui, MacroSettings& settings, MoonwalkSettings& moonwalk,
        bool& start, bool& stop, bool& emergency, bool& captureExcluded,
        bool& captureExclusionChanged, bool& exitRequested)
    {
        ui.DrawWindow(settings, moonwalk, start, stop, emergency, false,
            captureExcluded, captureExclusionChanged, false, exitRequested);
    }
    static void Toast(ImGuiUI& ui, const char* presetName, char digit)
    {
        ui.DrawToast(presetName, digit);
    }
    static float AppearProgress(ImGuiUI& ui) { return ui.m_AppearAnim; }
    static float PanelReveal(ImGuiUI& ui) { return ui.m_PanelReveal; }
    static void FinishAppear(ImGuiUI& ui) { ui.m_AppearAnim = 1.0f; }
};

class MenuHarness
{
public:
    MacroSettings settings;
    MoonwalkSettings moonwalk;
    bool start = false;
    bool stop = false;
    bool emergency = false;
    bool captureExcluded = false;
    bool captureExclusionChanged = false;
    bool exitRequested = false;

    MenuHarness()
    {
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(1000, 800);
        io.DeltaTime = 1.0f / 60.0f;
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        ImGuiUITestAccess::Theme(ui);
        GImGui->TestEngineHookItems = true;
        Frame();
        for (int i = 0; i < 40; ++i) Frame();
    }

    ~MenuHarness() { ImGui::DestroyContext(); }

    void Frame()
    {
        g_ItemsById.clear();
        g_ItemsByLabel.clear();
        g_TooltipRendered = false;
        start = stop = emergency = captureExclusionChanged = false;
        exitRequested = false;
        ImGui::NewFrame();
        ImGuiUITestAccess::Draw(ui, settings, moonwalk, start, stop,
            emergency, captureExcluded, captureExclusionChanged, exitRequested);
        ImGui::Render();
        if (ImGuiWindow* tooltip = ImGui::FindWindowByName("##Tooltip_00"))
            g_TooltipRendered = tooltip->Active && tooltip->DrawList->VtxBuffer.Size > 0;
        Check(ImGui::GetDrawData()->TotalVtxCount > 0, "Menu did not render");
    }

    bool Has(const char* label) const { return g_ItemsByLabel.count(label) != 0; }

    ImRect Rect(const char* label) const
    {
        Check(Has(label), label);
        return g_ItemsByLabel.at(label).rect;
    }

    // Child window (card) whose name contains the given fragment, used to prove
    // the unload button has room around it and that the menu cannot scroll.
    static ImGuiWindow* CardWindow(const char* nameFragment)
    {
        ImGuiContext* ctx = ImGui::GetCurrentContext();
        for (int i = 0; i < ctx->Windows.Size; ++i)
        {
            ImGuiWindow* window = ctx->Windows[i];
            if (!window || !window->Name)
                continue;
            if (std::string(window->Name).find(nameFragment) == std::string::npos)
                continue;
            return window;
        }
        throw std::runtime_error("Card window not found");
    }

    static ImRect CardRect(const char* nameFragment)
    {
        ImGuiWindow* window = CardWindow(nameFragment);
        return ImRect(window->Pos, ImVec2(window->Pos.x + window->Size.x,
            window->Pos.y + window->Size.y));
    }

    // Bounding box of everything drawn in the last frame, used to prove the
    // glow halo really is painted outside the menu rectangle.
    static ImRect DrawnBounds()
    {
        ImDrawData* data = ImGui::GetDrawData();
        ImVec2 min(1e30f, 1e30f);
        ImVec2 max(-1e30f, -1e30f);
        for (int list = 0; list < data->CmdLists.Size; ++list)
        {
            const ImDrawList* drawList = data->CmdLists[list];
            for (int v = 0; v < drawList->VtxBuffer.Size; ++v)
            {
                const ImVec2 p = drawList->VtxBuffer[v].pos;
                min.x = std::min(min.x, p.x);
                min.y = std::min(min.y, p.y);
                max.x = std::max(max.x, p.x);
                max.y = std::max(max.y, p.y);
            }
        }
        return ImRect(min, max);
    }

    /*
        Highest alpha byte among the vertices sitting on a rectangle's left
        edge. A filled rounded rectangle only emits vertices on its boundary,
        so its own background colour shows up here: this is how the test proves
        the panel and cards are drawn translucent rather than opaque.
    */
    static int EdgeVertexAlpha(const ImRect& rect)
    {
        ImDrawData* data = ImGui::GetDrawData();
        int maxAlpha = 0;
        for (int list = 0; list < data->CmdLists.Size; ++list)
        {
            const ImDrawList* drawList = data->CmdLists[list];
            for (int v = 0; v < drawList->VtxBuffer.Size; ++v)
            {
                const ImDrawVert& vertex = drawList->VtxBuffer[v];
                if (vertex.pos.x < rect.Min.x - 0.6f || vertex.pos.x > rect.Min.x + 0.6f)
                    continue;
                if (vertex.pos.y < rect.Min.y || vertex.pos.y > rect.Max.y)
                    continue;
                // Only neutral grey fills count: the accent-red divider lines
                // sit on the same edge but are opaque by design.
                const int red = static_cast<int>((vertex.col >> IM_COL32_R_SHIFT) & 0xFF);
                const int green = static_cast<int>((vertex.col >> IM_COL32_G_SHIFT) & 0xFF);
                if ((red > green ? red - green : green - red) > 40)
                    continue;
                const int alpha = static_cast<int>((vertex.col >> IM_COL32_A_SHIFT) & 0xFF);
                maxAlpha = std::max(maxAlpha, alpha);
            }
        }
        return maxAlpha;
    }

    /*
        Lowest non-zero alpha among the neutral grey vertices on a rectangle's
        left edge. The card background is the translucent fill on that edge, so
        this reads its alpha even though text, dividers and anti-aliasing
        fringe also sit there.
    */
    static int EdgeFillAlpha(const ImRect& rect)
    {
        ImDrawData* data = ImGui::GetDrawData();
        int minAlpha = 256;
        for (int list = 0; list < data->CmdLists.Size; ++list)
        {
            const ImDrawList* drawList = data->CmdLists[list];
            for (int v = 0; v < drawList->VtxBuffer.Size; ++v)
            {
                const ImDrawVert& vertex = drawList->VtxBuffer[v];
                if (vertex.pos.x < rect.Min.x - 0.6f || vertex.pos.x > rect.Min.x + 0.6f)
                    continue;
                if (vertex.pos.y < rect.Min.y || vertex.pos.y > rect.Max.y)
                    continue;
                const int red = static_cast<int>((vertex.col >> IM_COL32_R_SHIFT) & 0xFF);
                const int green = static_cast<int>((vertex.col >> IM_COL32_G_SHIFT) & 0xFF);
                const int alpha = static_cast<int>((vertex.col >> IM_COL32_A_SHIFT) & 0xFF);
                if ((red > green ? red - green : green - red) > 40 || alpha == 0)
                    continue;
                minAlpha = std::min(minAlpha, alpha);
            }
        }
        return minAlpha == 256 ? 0 : minAlpha;
    }

    static ImRect PanelRect()
    {
        ImGuiContext* ctx = ImGui::GetCurrentContext();
        for (int i = 0; i < ctx->Windows.Size; ++i)
        {
            ImGuiWindow* window = ctx->Windows[i];
            if (!window || !window->Name || std::string(window->Name) != "##dribtard_main")
                continue;
            return ImRect(window->Pos, ImVec2(window->Pos.x + window->Size.x,
                window->Pos.y + window->Size.y));
        }
        throw std::runtime_error("Menu panel window not found");
    }

    // The overlay fades in over a fraction of a second, and the glow is tied to
    // that fade. Test frames render far faster than real time, so settle the
    // animation before asserting on glow output.
    void SettleAppearAnimation()
    {
        ImGuiUITestAccess::FinishAppear(ui);
        for (int i = 0; i < 40; ++i)
            Frame();
    }

    float AppearProgress() { return ImGuiUITestAccess::AppearProgress(ui); }
    float PanelReveal() { return ImGuiUITestAccess::PanelReveal(ui); }

    void Wheel(float amount, float x = 400.0f, float y = 300.0f)
    {
        auto& io = ImGui::GetIO();
        io.AddMousePosEvent(x, y);
        Frame();
        io.AddMouseWheelEvent(0.0f, amount);
        Frame();
    }

    void ToastFrame(const char* presetName, char digit)
    {
        // A new auto-fit window is hidden for one frame while ImGui
        // measures it, so draw twice before asserting output.
        for (int frame = 0; frame < 2; ++frame)
        {
            g_ItemsById.clear();
            g_ItemsByLabel.clear();
            ImGui::NewFrame();
            ImGuiUITestAccess::Toast(ui, presetName, digit);
            ImGui::Render();
        }
        Check(ImGui::GetDrawData()->TotalVtxCount > 0, "Toast did not render");
        Check(ImGui::FindWindowByName("##curve_toast") != nullptr,
            "Toast window missing");
    }
    void CaptureKey(int key) { ui.SetCapturedKey(key, settings, moonwalk); }
    int MenuFpsCap() { return ui.MenuFpsCap(); }
    bool IsDisabled(const char* label) const
    {
        Check(Has(label), label);
        return g_ItemsByLabel.at(label).disabled;
    }
    int MenuKey() const { return ui.GetToggleKey(); }
    bool CapturingKey() const { return ui.IsCapturingKey(); }

    void SetUpdateBanner(
        bool available,
        const std::string& version,
        const std::string& notes,
        bool downloading,
        bool readyToApply,
        float progress)
    {
        ui.SetUpdateBanner(available, version, notes, downloading, readyToApply, progress);
    }

    bool ConsumeUpdateNow() { return ui.ConsumeUpdateNowRequest(); }
    bool IsPortableMode() const { return ui.IsPortableMode(); }
    bool IsMenuOpen() const { return ui.IsMenuOpen(); }
    bool BlurActive() const { return ui.BlurActive(); }
    ImVec4 BlurRect() const { return ui.BlurRect(); }

    // The menu panel only opens while the pointer is on the icon rail, so hover
    // the rail and let a frame pass for the panel to appear.
    void OpenMenu()
    {
        Frame();
        Hover("##rail_general");
        Frame();
    }

    // Move the pointer and let two frames pass: one for the panel to notice the
    // pointer moved and one for the window to resize to its new size.
    void MovePointer(float x, float y)
    {
        ImGui::GetIO().AddMousePosEvent(x, y);
        Frame();
        Frame();
    }
    std::string ConsumeConfigSave() { return ui.ConsumeConfigSaveRequest(); }

    void Hover(const char* label)
    {
        Check(Has(label), label);
        const auto item = g_ItemsByLabel.at(label);
        auto& io = ImGui::GetIO();
        io.AddMousePosEvent(item.rect.Min.x + 8,
            (item.rect.Min.y + item.rect.Max.y) / 2);
        Frame();
    }

    void Click(const char* label)
    {
        Check(Has(label), label);
        const auto item = g_ItemsByLabel.at(label);
        if (item.disabled)
            throw std::runtime_error(std::string("Cannot click disabled item: ") + label);
        const ImVec2 pos(item.rect.Min.x + 8, (item.rect.Min.y + item.rect.Max.y) / 2);
        auto& io = ImGui::GetIO();
        io.AddMousePosEvent(pos.x, pos.y);
        Frame();
        io.AddMouseButtonEvent(0, true);
        Frame();
        io.AddMouseButtonEvent(0, false);
        Frame();
    }

    void Drag(const char* label, float fromRatioX, float fromRatioY, float toRatioX, float toRatioY)
    {
        Check(Has(label), label);
        const auto item = g_ItemsByLabel.at(label);
        Check(!item.disabled, "Cannot drag disabled item");
        const float w = item.rect.Max.x - item.rect.Min.x;
        const float h = item.rect.Max.y - item.rect.Min.y;
        const ImVec2 startPos(item.rect.Min.x + w * fromRatioX, item.rect.Min.y + h * fromRatioY);
        const ImVec2 endPos(item.rect.Min.x + w * toRatioX, item.rect.Min.y + h * toRatioY);
        auto& io = ImGui::GetIO();
        io.AddMousePosEvent(startPos.x, startPos.y);
        Frame();
        io.AddMouseButtonEvent(0, true);
        Frame();
        io.AddMousePosEvent(endPos.x, endPos.y);
        Frame();
        io.AddMouseButtonEvent(0, false);
        Frame();
    }

    void Edit(const char* label, const char* value)
    {
        Click(label);
        auto& io = ImGui::GetIO();
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        io.AddKeyEvent(ImGuiKey_A, true);
        Frame();
        io.AddKeyEvent(ImGuiKey_A, false);
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        Frame();
        io.AddInputCharactersUTF8(value);
        Frame();
        io.AddKeyEvent(ImGuiKey_Enter, true);
        Frame();
        io.AddKeyEvent(ImGuiKey_Enter, false);
        Frame();
    }

    void CheckHidden() const
    {
        Check(!Has("Rotation Duration (sec)"), "Rotation Duration still visible");
        Check(!Has("Key Tap Hold (sec)"), "Key Tap Hold still visible");
        Check(!Has("Vertical Return Limit (deg)"), "Vertical Return Limit still visible");
        Check(settings.rotationDurationSeconds == 0.10 &&
            settings.secondaryHoldSeconds == 0.0, "UI changed hidden values");
    }

private:
    ImGuiUI ui;
};

int main()
{
    try
    {
        MenuHarness menu;
        menu.OpenMenu();
        Check(menu.Has("Curve Type") && menu.Has("Custom Curve Angle"), "Curve controls missing");

        // The redesigned edge light remains inside the menu rather than
        // drawing an oversized rectangular halo over the desktop.
        {
            menu.SettleAppearAnimation();
            Check(menu.AppearProgress() == 1.0f, "Menu fade-in did not settle");
            const ImRect drawn = MenuHarness::DrawnBounds();
            const ImRect panel = MenuHarness::PanelRect();
            Check(drawn.Min.x >= panel.Min.x - 1.0f && drawn.Min.y >= panel.Min.y - 1.0f &&
                drawn.Max.x <= panel.Max.x + 1.0f && drawn.Max.y <= panel.Max.y + 1.0f,
                "Edge lighting must not paint a halo outside the menu");

            // The panel is clear: the frosted blur behind the menu supplies
            // the glass. Its outline is a faint light line (alpha 22), so the
            // fill must not be a solid colour, and the blur must cover it.
            const int panelAlpha = MenuHarness::EdgeVertexAlpha(panel);
            Check(panelAlpha < 60,
                "Menu panel must not paint a solid background over the blur");
            Check(menu.BlurActive(), "Frosted blur must be active while the menu is open");
            // PanelRect() is the whole window, margins included, so the blur is
            // checked against the cards it has to frost.
            const ImVec4 blur = menu.BlurRect();
            const ImRect keybindsCard = MenuHarness::CardRect("##card_keybinds");
            const ImRect cameraCard = MenuHarness::CardRect("##card_camera");
            Check(blur.x <= keybindsCard.Min.x && blur.y <= keybindsCard.Min.y &&
                blur.z >= cameraCard.Max.x && blur.w >= cameraCard.Max.y,
                "Frosted blur must cover the menu cards");
            const int cardFill = MenuHarness::EdgeFillAlpha(MenuHarness::CardRect("##card_keybinds"));
            Check(cardFill > 0, "Card background was not found on its edge");
            Check(cardFill < 250,
                "Menu cards must stay translucent");
        }

        Check(menu.Has("Hotkey Mode"), "Hotkey Mode toggle missing");
        Check(!menu.settings.hotkeyMode, "Hotkey mode must be off by default");
        menu.Click("Hotkey Mode");
        menu.Frame();
        Check(menu.settings.hotkeyMode, "Hotkey Mode checkbox did not enable hotkey mode");
        menu.Click("Hotkey Mode");
        menu.Frame();
        Check(!menu.settings.hotkeyMode, "Hotkey Mode checkbox did not disable hotkey mode");
        Check(!menu.Has("Horizontal Rotation (deg)") && !menu.Has("Vertical / Pitch (deg)"),
            "Angles should be hidden in preset mode");
        menu.CheckHidden();
        Check(menu.Has("Exclude UI from screen captures"), "Capture exclusion control missing");
        Check(!menu.captureExcluded && !menu.captureExclusionChanged,
            "Capture exclusion should be disabled by default");
        menu.Hover("Exclude UI from screen captures");
        Check(g_TooltipRendered, "Hovering a feature should display its explanation");
        menu.Click("Exclude UI from screen captures");
        Check(menu.captureExcluded && menu.captureExclusionChanged,
            "Capture exclusion toggle did not request enabling");
        menu.captureExcluded = false;
        menu.Frame();
        Check(!menu.captureExclusionChanged, "Capture exclusion generated a change without user input");
        Check(menu.Has("\\##menu_keybind") && menu.MenuKey() == VK_OEM_5, "Menu keybind control missing");
        menu.Click("Change##menu_keybind_change");
        Check(menu.CapturingKey(), "Menu change button did not request key capture");
        menu.CaptureKey(VK_F12);
        Check(menu.CapturingKey() && menu.MenuKey() == VK_OEM_5, "F12 must stay reserved");
        menu.CaptureKey(VK_LBUTTON);
        Check(menu.CapturingKey(), "Menu click must not become a keybind");
        menu.CaptureKey(VK_F8);
        menu.Frame();
        Check(!menu.CapturingKey() && menu.MenuKey() == VK_F8 && menu.Has("F8##menu_keybind"),
            "Menu keybind was not changed or displayed");
        menu.Click("F8##menu_keybind");
        menu.CaptureKey(VK_F9);
        menu.Frame();
        Check(menu.MenuKey() == VK_F9 && !menu.CapturingKey(), "Current key button cannot rebind menu");

        for (size_t i = 0; i < kCurvePresets.size(); ++i)
        {
            menu.Click("Curve Type");
            menu.Frame();
            for (const auto& preset : kCurvePresets)
                Check(menu.Has(preset.name), "Dropdown option missing");
            menu.Click(kCurvePresets[i].name);
            menu.Frame();
            Check(static_cast<size_t>(menu.settings.curveType) == i, "Dropdown selection not applied");
            const auto effective = GetEffectiveCurveSettings(menu.settings);
            Check(effective.horizontalDegrees == kCurvePresets[i].horizontalDegrees &&
                effective.verticalDegrees == kCurvePresets[i].verticalDegrees &&
                effective.secondaryDelaySeconds == 0.350, "Dropdown selected wrong curve values");
            // Old custom edits must be replaced on every enable, for all four curves.
            menu.settings.horizontalDegrees = 7;
            menu.settings.verticalDegrees = 11;
            menu.settings.secondaryDelaySeconds = 0.987;
            menu.Click("Custom Curve Angle");
            menu.Frame();
            Check(menu.settings.customCurveAngle &&
                menu.settings.horizontalDegrees == kCurvePresets[i].horizontalDegrees &&
                menu.settings.verticalDegrees == kCurvePresets[i].verticalDegrees &&
                menu.settings.secondaryDelaySeconds == 0.350, "Custom mode did not copy selected preset");
            menu.CheckHidden();
            menu.Click("Custom Curve Angle");
            menu.Frame();
        }

        menu.Click("Custom Curve Angle");
        menu.Frame();
        Check(menu.settings.customCurveAngle, "Checkbox did not enable custom mode");
        Check(menu.Has("Horizontal Rotation (deg)") && menu.Has("Vertical / Pitch (deg)"),
            "Checkbox did not restore angle editors");
        Check(g_ItemsByLabel.at("Curve Type").disabled, "Preset dropdown should be disabled in custom mode");
        menu.Edit("Horizontal Rotation (deg)", "-42.5");
        menu.Edit("Vertical / Pitch (deg)", "155.25");
        menu.Edit("Delay Before Pressing (sec)", "0.617");
        Check(menu.settings.horizontalDegrees == -42.5 && menu.settings.verticalDegrees == 155.25 &&
            menu.settings.secondaryDelaySeconds == 0.617, "Custom editors did not update settings");
        menu.CheckHidden();

        Check(menu.Has("Mouse Counts Per Degree"), "Mouse Counts Per Degree editor missing");
        menu.Edit("Mouse Counts Per Degree", "7.5");
        Check(menu.settings.mouseCountsPerDegree == 7.5, "Mouse Counts editor did not update settings");

        menu.Click("Custom Curve Angle");
        menu.Frame();
        Check(!menu.settings.customCurveAngle && !menu.Has("Horizontal Rotation (deg)"),
            "Unticking checkbox did not restore preset mode");
        menu.Click("Curve Type");
        menu.Frame();
        menu.Click("Upwards");
        menu.Frame();
        menu.Click("Custom Curve Angle");
        menu.Frame();
        Check(menu.settings.horizontalDegrees == 30.0 && menu.settings.verticalDegrees == 170.0 &&
            menu.settings.secondaryDelaySeconds == 0.350, "Re-enabling custom must copy Upwards settings");
        menu.Click("Custom Curve Angle");
        menu.Frame();
        Check(!menu.settings.customCurveAngle, "Unticking checkbox did not restore preset mode");

        // Dragging inside the 2D Trajectory Preview canvas must enable custom mode
        // and update horizontal/vertical curve angles interactively.
        Check(menu.Has("##trajectory_canvas"), "Trajectory preview canvas missing");
        menu.Drag("##trajectory_canvas", 0.50f, 0.50f, 0.75f, 0.25f);
        Check(menu.settings.customCurveAngle, "Dragging trajectory canvas must enable custom curve mode");
        Check(menu.settings.horizontalDegrees > 50.0 && menu.settings.verticalDegrees > 50.0,
            "Dragging trajectory canvas to upper-right quadrant must set positive H and V angles");
        menu.Drag("##trajectory_canvas", 0.75f, 0.25f, 0.25f, 0.80f);
        Check(menu.settings.horizontalDegrees < -50.0 && menu.settings.verticalDegrees < -40.0,
            "Dragging trajectory canvas to lower-left quadrant must set negative H and V angles");

        // The menu must never scroll: the fixed header bar (brand box, preset
        // pills, status pill) is drawn from the window position and does not
        // move, so any scroll range would slide the cards and section headers
        // under it and clip their text.
        {
            const ImRect keybindsBefore = MenuHarness::CardRect("##card_keybinds");
            menu.Wheel(-6.0f);
            const ImRect menuContent = MenuHarness::CardRect("##general");
            const ImRect keybindsCard = MenuHarness::CardRect("##card_keybinds");
            const ImRect privacyCard = MenuHarness::CardRect("##card_privacy");
            Check(MenuHarness::CardWindow("##general")->ScrollMax.y == 0.0f,
                "Menu layout is taller than the window, so it can be scrolled");
            Check(MenuHarness::CardWindow("##general")->Scroll.y == 0.0f,
                "Mouse wheel scrolled the menu under the header bar");
            Check(keybindsCard.Min.y == keybindsBefore.Min.y,
                "Scrolling moved the menu content");
            Check(privacyCard.Max.y <= menuContent.Max.y,
                "Bottom row of the menu is clipped by the window");
            Check(keybindsCard.Min.y - menuContent.Min.y < 40.0f,
                "First card must sit at the top of the panel");
        }


        menu.Click("Custom Curve Angle");
        menu.Frame();

        // Settings tab: the menu frame-rate cap.
        {
            Check(menu.Has("##rail_general") && menu.Has("##rail_settings"), "Icon rail tabs missing");
            Check(menu.MenuFpsCap() == 0, "Menu frame rate should be uncapped by default");
            Check(!menu.Has("Menu FPS Cap"),
                "Settings controls must not appear on the main tab");

            menu.Click("##rail_settings");
            menu.Frame();
            Check(menu.Has("Menu FPS Cap"),
                "Settings controls missing from the Settings tab");
            Check(!menu.Has("Curve Type") && !menu.Has("Hotkey Mode"),
                "Main tab controls must not appear on the Settings tab");
            Check(MenuHarness::CardWindow("##general")->ScrollMax.y == 0.0f,
                "Settings tab is taller than the menu window");

            Check(menu.IsDisabled("Menu FPS Cap"),
                "FPS cap field must be disabled while the limit is off");
            menu.Click("Limit Menu FPS");
            Check(menu.MenuFpsCap() == 60, "Enabling the limit should cap the menu at 60 FPS");
            Check(!menu.IsDisabled("Menu FPS Cap"), "FPS cap field stayed disabled");
            menu.Edit("Menu FPS Cap", "30");
            Check(menu.MenuFpsCap() == 30, "FPS cap did not accept a new value");
            menu.Edit("Menu FPS Cap", "2");
            Check(menu.MenuFpsCap() == ImGuiUI::kMinMenuFpsCap, "FPS cap must clamp to its minimum");
            menu.Edit("Menu FPS Cap", "9999");
            Check(menu.MenuFpsCap() == ImGuiUI::kMaxMenuFpsCap, "FPS cap must clamp to its maximum");
            menu.Click("Limit Menu FPS");
            Check(menu.MenuFpsCap() == 0, "Switching the limit off must remove the cap");

            menu.Click("##rail_general");
            menu.Frame();
            Check(menu.Has("Curve Type"),
                "Main tab did not come back");
        }

        // Moonwalk Tech is its own page with a hold keybind and the fixed
        // A/D tap sequence; it must not expose the main macro controls.
        {
            Check(menu.Has("##rail_moonwalk"), "Moonwalk Tech tab missing");

            menu.Click("##rail_moonwalk");
            menu.Frame();
            Check(menu.Has("X##moonwalk_hold"),
                "Moonwalk hold key must default to X");
            Check(menu.Has("Change##moonwalk_hold_change"),
                "Moonwalk hold key control missing");
            Check(!menu.Has("Curve Type") && !menu.Has("Hotkey Mode") &&
                !menu.Has("Menu FPS Cap"),
                "Moonwalk tab must not show main macro or Settings controls");
            Check(MenuHarness::CardWindow("##general")->ScrollMax.y == 0.0f,
                "Moonwalk tab is taller than the menu window");
            (void)MenuHarness::CardWindow("##card_moonwalk_sequence");

            menu.Click("Change##moonwalk_hold_change");
            Check(menu.CapturingKey(),
                "Moonwalk change button did not request key capture");
            menu.CaptureKey(VK_F12);
            Check(menu.CapturingKey() && menu.moonwalk.holdKey == 'X',
                "F12 must stay reserved for emergency stop");
            menu.CaptureKey('G');
            menu.Frame();
            Check(menu.moonwalk.holdKey == 'G' && menu.Has("G##moonwalk_hold"),
                "Moonwalk hold key was not rebound");

            menu.Click("##rail_general");
            menu.Frame();
            Check(menu.Has("Curve Type"),
                "General tab did not come back from Moonwalk Tech");
        }

        // Ocho-style floating vertical icon rail & Config / Menu mode tabs:
        {
            Check(menu.Has("##rail_general") && menu.Has("##rail_moonwalk") &&
                menu.Has("##rail_configs") && menu.Has("##rail_settings") &&
                menu.Has("##rail_mode_toggle"),
                "Floating vertical icon rail controls missing");
            Check(!menu.Has("##rail_collapse_toggle"),
                "The collapse button must be gone; the panel opens on hover");

            // Navigate to Config page via the vertical icon rail
            menu.Click("##rail_configs");
            menu.Frame();
            Check(menu.Has("Save") && menu.Has("Load") && menu.Has("Delete") && menu.Has("Open folder"),
                "Ocho Config & Manage controls missing on Config tab");
            (void)MenuHarness::CardWindow("##card_config_list_box");
            (void)MenuHarness::CardWindow("##card_config_manage");
            menu.Click("Save");
            Check(!menu.ConsumeConfigSave().empty(), "Clicking Save on Manage card must queue config save");

            // Navigate to Settings via the vertical icon rail and verify Portable / Classic Menu mode
            menu.Click("##rail_settings");
            menu.Frame();
            // The Client card holds the UI color and Card color rows. Content
            // taller than the card would make it scroll, so no scroll range.
            Check(MenuHarness::CardWindow("##card_client")->ScrollMax.y == 0.0f,
                "Client card rows overflow the card height");
            (void)MenuHarness::CardWindow("##card_menu_mode");
            (void)MenuHarness::CardWindow("##card_misc");
            // Unload lives at the bottom of the Miscellaneous card: a full exit.
            Check(menu.Has("Unload"), "Unload button missing from the Settings tab");
            menu.Hover("Unload");
            Check(g_TooltipRendered, "Hovering Unload should explain what it does");
            const ImRect unload = menu.Rect("Unload");
            const ImRect miscCard = MenuHarness::CardRect("##card_misc");
            Check(unload.Min.x >= miscCard.Min.x && unload.Max.x <= miscCard.Max.x &&
                unload.Min.y >= miscCard.Min.y && unload.Max.y <= miscCard.Max.y,
                "Unload button must sit inside the Miscellaneous card");
            Check(!menu.exitRequested, "Unload must not request an exit before it is clicked");
            menu.Click("Unload");
            Check(menu.exitRequested, "Clicking Unload must request a full application exit");
            Check(!menu.IsPortableMode(), "Menu mode should default to Classic");
            menu.Click("##mode_portable");
            menu.Frame();
            Check(menu.IsPortableMode(), "Clicking Portable preview did not switch to Portable mode");
            menu.MovePointer(900.0f, 780.0f);
            menu.Hover("##rail_settings");
            for (int i = 0; i < 40; ++i) menu.Frame();
            menu.Click("##mode_classic");
            menu.Frame();
            Check(!menu.IsPortableMode(), "Clicking Classic preview did not switch back to Classic mode");

            // Classic keeps the rail and the menu open, even with the pointer away.
            Check(menu.IsMenuOpen(), "Menu should stay open in Classic mode");
            menu.MovePointer(900.0f, 780.0f);
            Check(menu.IsMenuOpen() && menu.Has("##mode_classic"),
                "Classic menu must stay open when the pointer leaves the sidebar");

            // Compact: the panel only pops up while the pointer is on the rail.
            menu.Click("##mode_portable");
            menu.Frame();
            Check(menu.IsPortableMode(), "Clicking Portable preview did not switch to Portable mode");
            menu.MovePointer(900.0f, 780.0f);
            Check(!menu.IsMenuOpen(),
                "Compact menu must start closing when the pointer leaves the sidebar");
            Check(menu.PanelReveal() > 0.0f && menu.PanelReveal() < 1.0f,
                "Compact panel must animate during collapse");
            for (int i = 0; i < 40; ++i) menu.Frame();
            Check(!menu.Has("##mode_classic") && menu.PanelReveal() == 0.0f,
                "Compact panel must finish closing");
            menu.Hover("##rail_settings");
            menu.Frame();
            Check(menu.PanelReveal() > 0.0f && menu.PanelReveal() < 1.0f,
                "Compact panel must animate during expansion");
            for (int i = 0; i < 40; ++i) menu.Frame();
            Check(menu.IsMenuOpen() && menu.Has("##mode_classic"),
                "Hovering the sidebar must pop the compact menu up");
            menu.Click("##rail_general");
            for (int i = 0; i < 40; ++i) menu.Frame();
            Check(menu.Has("Curve Type"), "General controls must appear in compact mode");
            {
                const ImRect compact = MenuHarness::PanelRect();
                const ImRect drawn = MenuHarness::DrawnBounds();
                const ImVec4 blur = menu.BlurRect();
                Check(compact.GetWidth() == 560.0f,
                    "Compact General must use its wider window");
                Check(drawn.Max.x < compact.Max.x + 24.0f,
                    "Compact glow must not extend to the classic window width");
                Check(blur.z <= compact.Max.x && blur.x >= compact.Min.x,
                    "Compact blur must stay inside the compact window");
                Check(MenuHarness::CardWindow("##compact_general_scroll")->ScrollMax.y > 0.0f,
                    "Compact content must remain reachable by scrolling");
            }
            {
                const ImRect compact = MenuHarness::PanelRect();
                menu.MovePointer(compact.Min.x + 5.0f, compact.Min.y + 5.0f);
                Check(menu.IsMenuOpen(), "Transparent top margin must keep compact menu open");
                menu.MovePointer(compact.Max.x - 3.0f, compact.Max.y - 3.0f);
                Check(menu.IsMenuOpen(), "Transparent bottom margin must keep compact menu open");
                auto& io = ImGui::GetIO();
                io.AddMouseButtonEvent(0, true);
                menu.Frame();
                io.AddMouseButtonEvent(0, false);
                menu.Frame();
                Check(menu.IsMenuOpen(), "Clicking a transparent margin must not collapse the menu");
                menu.Hover("##rail_general");
                menu.Frame();
            }
            menu.Click("##rail_settings");
            for (int i = 0; i < 40; ++i) menu.Frame();
            Check(menu.Has("##mode_classic"), "Settings must remain available in compact mode");

            // Dragging the transparent margin of the open menu moves the window
            // and keeps it open. The margin sits outside the rail and panel, so
            // the menu must not collapse to the rail while the drag is running.
            {
                const ImRect before = MenuHarness::PanelRect();
                const ImVec2 grip(before.Min.x + 300.0f, before.Min.y + 6.0f);
                auto& io = ImGui::GetIO();
                io.AddMousePosEvent(grip.x, grip.y);
                menu.Frame();
                menu.Frame();
                Check(menu.IsMenuOpen(),
                    "Pointer on the menu's transparent margin must keep the compact menu open");
                io.AddMouseButtonEvent(0, true);
                menu.Frame();
                for (int step = 1; step <= 4; ++step)
                {
                    io.AddMousePosEvent(grip.x + 30.0f * step, grip.y + 20.0f * step);
                    menu.Frame();
                    const ImVec4 blur = menu.BlurRect();
                    Check(blur.z - blur.x <= 820.0f && blur.w - blur.y <= 524.0f,
                        "Dragging must not expand the blur beyond the menu");
                }
                io.AddMouseButtonEvent(0, false);
                menu.Frame();
                const ImRect after = MenuHarness::PanelRect();
                const float movedX = after.Min.x - before.Min.x;
                const float movedY = after.Min.y - before.Min.y;
                Check(std::fabs(movedX) < 1.0f && std::fabs(movedY) < 1.0f,
                    "Compact menu must remain anchored while dragging");
                Check(std::fabs(after.Min.x) < 1.0f && std::fabs(after.Min.y - 138.0f) < 1.0f,
                    "Compact menu must be centered on the left edge");
            }

            // Dragging far past the screen edge must clamp the window on screen,
            // so the frosted blur cannot stretch over the whole display.
            {
                const ImRect before = MenuHarness::PanelRect();
                auto& io = ImGui::GetIO();
                io.AddMousePosEvent(before.Min.x + 300.0f, before.Min.y + 6.0f);
                menu.Frame();
                menu.Frame();
                io.AddMouseButtonEvent(0, true);
                menu.Frame();
                io.AddMousePosEvent(before.Min.x + 3000.0f, before.Min.y + 3000.0f);
                menu.Frame();
                // Step the pointer back inside the menu so the following
                // Classic-mode interaction still has its button.
                io.AddMousePosEvent(before.Min.x + 300.0f, before.Min.y + 6.0f);
                menu.Frame();
                io.AddMouseButtonEvent(0, false);
                menu.Frame();
                const ImRect after = MenuHarness::PanelRect();
                Check(after.Min.x >= -1.0f && after.Min.y >= -1.0f,
                    "The menu window must not be dragged off the top-left of the screen");
                Check(after.Max.x <= 1001.0f && after.Max.y <= 801.0f,
                    "The menu window must not be dragged off the bottom-right of the screen");
            }

            menu.Click("##mode_classic");
            menu.Frame();
            Check(!menu.IsPortableMode(), "Clicking Classic preview did not switch back to Classic mode");
        }

        // A click on the transparent game area outside the menu must not open
        // the compact menu or widen the frosted blur to cover the screen.
        {
            menu.Click("##mode_portable");
            menu.Frame();
            Check(menu.IsPortableMode(), "Clicking Portable preview did not switch to Portable mode");
            menu.MovePointer(900.0f, 780.0f);
            menu.Frame();
            Check(!menu.IsMenuOpen(), "Compact menu must close when the pointer leaves the sidebar");
            const ImRect before = MenuHarness::PanelRect();
            auto& io = ImGui::GetIO();
            io.AddMousePosEvent(5.0f, 5.0f);
            menu.Frame();
            io.AddMouseButtonEvent(0, true);
            menu.Frame();
            io.AddMousePosEvent(40.0f, 40.0f);
            menu.Frame();
            io.AddMouseButtonEvent(0, false);
            menu.Frame();
            Check(!menu.IsMenuOpen(),
                "Clicking the transparent game area must not open the menu");
            const ImRect after = MenuHarness::PanelRect();
            const ImVec4 blurAfter = menu.BlurRect();
            Check(after.GetWidth() == before.GetWidth() && after.GetHeight() == before.GetHeight(),
                "Clicking the transparent game area must not resize the menu window");
            Check((blurAfter.z - blurAfter.x) <= (after.Max.x - after.Min.x) + 2.0f,
                "Clicking the transparent game area must not stretch the blur across the screen");
        }

        // Execution section and the old sidebar/hidden settings controls stay gone.
        Check(!menu.Has("START MACRO") && !menu.Has("STOP") && !menu.Has("EMERGENCY STOP"),
            "Execution section buttons must not be present");
        Check(!menu.Has("##macro_nav") && !menu.Has("Macro##nav_0") &&
            !menu.Has("Logo glow intensity") && !menu.Has("Reset UI defaults"),
            "Removed sidebar and hidden settings controls must not be present");
        menu.ToastFrame("Upwards", '1');
        menu.ToastFrame("Backwards", '2');
        menu.ToastFrame("Left", '3');
        menu.ToastFrame("Right", '4');

        // Update banner: hidden when no update is available, shows "Update now"
        // when an update is available, and triggers ConsumeUpdateNowRequest() on click.
        menu.OpenMenu();
        Check(!menu.Has("Update now"), "Update now button must be hidden when no update is available");
        menu.SetUpdateBanner(true, "1.3.0", "Faster curve presets", false, false, 0.0f);
        menu.Frame();
        Check(menu.Has("Update now"), "Update now button must appear when an update is available");
        Check(!menu.ConsumeUpdateNow(), "Update now should not trigger before click");
        menu.Click("Update now");
        Check(menu.ConsumeUpdateNow(), "Clicking Update now must request an update");
        menu.SetUpdateBanner(true, "1.3.0", "Faster curve presets", true, false, 0.45f);
        menu.Frame();
        Check(!menu.Has("Update now"), "Update now button is replaced by progress bar while downloading");
        menu.SetUpdateBanner(false, "", "", false, false, 0.0f);
        menu.Frame();

        menu.CheckHidden();
        std::cout << "ImGui menu keybind, preset-to-custom, hidden control, unload button and start/stop button tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
