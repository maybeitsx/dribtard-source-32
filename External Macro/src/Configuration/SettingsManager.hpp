#pragma once

#include <string>
#include <vector>

#include "Macro/MacroEngine.hpp"
#include "Macro/MoonwalkMacro.hpp"

// UI appearance + menu behaviour edited on the menu's Settings tab,
// plus the menu toggle key. Plain floats so this header stays free
// of ImGui/DirectX dependencies.
struct UiSettings
{
    int toggleKey = 220; // VK_OEM_5, the default menu toggle

    float accentR = 0.24f;
    float accentG = 0.84f;
    float accentB = 0.88f;
    float accentA = 1.0f;

    // Base colour of the dark cards behind the menu features.
    float cardR = 0.055f;
    float cardG = 0.075f;
    float cardB = 0.095f;

    float glowIntensity = 0.45f;
    float windowGlowIntensity = 0.45f;
    float panelOpacity = 0.40f;

    bool darkenBackground = false;
    float dimOpacity = 0.45f;

    bool limitMenuFps = false;
    int menuFpsCap = 60;
};

// Every user-adjustable setting in one place: macro engine values,
// menu appearance, and the target process name.
struct AppConfig
{
    MacroSettings macro{};
    MoonwalkSettings moonwalk{};
    UiSettings ui{};
    std::string targetProcessName = "RobloxPlayerBeta.exe";
};

class SettingsManager
{
public:
    static SettingsManager& Get();

    std::string TargetProcessName =
        "RobloxPlayerBeta.exe";

    // --- Config system --------------------------------------------------
    // Pure JSON (no filesystem): unit tested.
    static std::string ToJson(const AppConfig& config);
    // Parses a document written by ToJson. Starts from defaults, overrides
    // every field it finds, clamps to safe ranges. Returns false when the
    // document is not one of ours (missing "version").
    static bool FromJson(const std::string& json, AppConfig& out);

    // Last-used settings: auto-saved while the app runs, auto-loaded at
    // startup so the menu reopens exactly where the user left it.
    static bool SaveLast(const AppConfig& config);
    static bool LoadLast(AppConfig& out);

    // Named configs the user saves explicitly from the Settings tab.
    static bool SaveNamed(const std::string& name, const AppConfig& config);
    static bool LoadNamed(const std::string& name, AppConfig& out);
    static bool DeleteNamed(const std::string& name);
    static std::vector<std::string> ListConfigs();

    // Filesystem locations. DRIBTARD_CONFIG_DIR overrides the directory
    // (unit tests point it at a temp dir so they never touch %APPDATA%).
    static std::string ConfigDir();
    static std::string LastConfigPath();
    static std::string ConfigsDir();
    static std::string ConfigFilePath(const std::string& name);

    // Validates a user-typed config name. Returns the trimmed name, or an
    // empty string when invalid (empty, too long, path separators, "."/"..",
    // or characters outside [A-Za-z0-9 _-]).
    static std::string SanitizeName(const std::string& name);

    static std::string JsonEscape(const std::string& value);

private:
    SettingsManager() = default;
};
