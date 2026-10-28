#include "Configuration/SettingsManager.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <stdexcept>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace
{

void Check(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

bool Near(double a, double b, double eps = 1e-4)
{
    return std::fabs(a - b) <= eps;
}

void CheckEqual(const AppConfig& a, const AppConfig& b)
{
    const MacroSettings& m1 = a.macro;
    const MacroSettings& m2 = b.macro;
    Check(m1.triggerKey == m2.triggerKey, "triggerKey mismatch");
    Check(m1.activationKey == m2.activationKey, "activationKey mismatch");
    Check(m1.secondaryKey == m2.secondaryKey, "secondaryKey mismatch");
    Check(m1.curveType == m2.curveType, "curveType mismatch");
    Check(m1.customCurveAngle == m2.customCurveAngle, "customCurveAngle mismatch");
    Check(m1.hotkeyMode == m2.hotkeyMode, "hotkeyMode mismatch");
    Check(Near(m1.horizontalDegrees, m2.horizontalDegrees), "horizontalDegrees mismatch");
    Check(Near(m1.verticalDegrees, m2.verticalDegrees), "verticalDegrees mismatch");
    Check(Near(m1.rotationDurationSeconds, m2.rotationDurationSeconds), "rotationDuration mismatch");
    Check(Near(m1.robloxSensitivity, m2.robloxSensitivity), "robloxSensitivity mismatch");
    Check(Near(m1.mouseCountsPerDegree, m2.mouseCountsPerDegree), "mouseCountsPerDegree mismatch");
    Check(m1.qBeforeCamera == m2.qBeforeCamera, "qBeforeCamera mismatch");
    Check(Near(m1.activationHoldSeconds, m2.activationHoldSeconds), "activationHold mismatch");
    Check(Near(m1.activationLeadSeconds, m2.activationLeadSeconds), "activationLead mismatch");
    Check(Near(m1.initialDelaySeconds, m2.initialDelaySeconds), "initialDelay mismatch");
    Check(Near(m1.secondaryDelaySeconds, m2.secondaryDelaySeconds), "secondaryDelay mismatch");
    Check(Near(m1.secondaryHoldSeconds, m2.secondaryHoldSeconds), "secondaryHold mismatch");
    Check(m1.instantMode == m2.instantMode, "instantMode mismatch");
    Check(m1.restoreCamera == m2.restoreCamera, "restoreCamera mismatch");
    Check(a.moonwalk.holdKey == b.moonwalk.holdKey, "moonwalk holdKey mismatch");

    const UiSettings& u1 = a.ui;
    const UiSettings& u2 = b.ui;
    Check(u1.toggleKey == u2.toggleKey, "toggleKey mismatch");
    Check(Near(u1.accentR, u2.accentR, 1e-3), "accentR mismatch");
    Check(Near(u1.accentG, u2.accentG, 1e-3), "accentG mismatch");
    Check(Near(u1.accentB, u2.accentB, 1e-3), "accentB mismatch");
    Check(Near(u1.cardR, u2.cardR, 1e-3), "cardR mismatch");
    Check(Near(u1.cardG, u2.cardG, 1e-3), "cardG mismatch");
    Check(Near(u1.cardB, u2.cardB, 1e-3), "cardB mismatch");
    Check(Near(u1.glowIntensity, u2.glowIntensity, 1e-3), "glow mismatch");
    Check(Near(u1.windowGlowIntensity, u2.windowGlowIntensity, 1e-3), "windowGlow mismatch");
    Check(Near(u1.panelOpacity, u2.panelOpacity, 1e-3), "panelOpacity mismatch");
    Check(u1.darkenBackground == u2.darkenBackground, "darkenBackground mismatch");
    Check(Near(u1.dimOpacity, u2.dimOpacity, 1e-3), "dimOpacity mismatch");
    Check(u1.limitMenuFps == u2.limitMenuFps, "limitMenuFps mismatch");
    Check(u1.menuFpsCap == u2.menuFpsCap, "menuFpsCap mismatch");

    Check(a.targetProcessName == b.targetProcessName, "targetProcessName mismatch");
}

AppConfig MakeCustom()
{
    AppConfig config;
    config.macro.triggerKey = 'X';
    config.macro.activationKey = 'E';
    config.macro.secondaryKey = 'G';
    config.macro.curveType = CurveType::Left;
    config.macro.customCurveAngle = true;
    config.macro.hotkeyMode = true;
    config.macro.horizontalDegrees = -42.5;
    config.macro.verticalDegrees = 155.25;
    config.macro.rotationDurationSeconds = 0.25;
    config.macro.robloxSensitivity = 1.5;
    config.macro.mouseCountsPerDegree = 7.5;
    config.macro.qBeforeCamera = true;
    config.macro.activationHoldSeconds = 0.05;
    config.macro.activationLeadSeconds = 0.02;
    config.macro.initialDelaySeconds = 0.2;
    config.macro.secondaryDelaySeconds = 0.617;
    config.macro.secondaryHoldSeconds = 0.043;
    config.macro.instantMode = false;
    config.macro.restoreCamera = false;
    config.moonwalk.holdKey = 'V';
    config.ui.toggleKey = 0x70;
    config.ui.accentR = 0.1f;
    config.ui.accentG = 0.5f;
    config.ui.accentB = 0.9f;
    config.ui.cardR = 0.20f;
    config.ui.cardG = 0.35f;
    config.ui.cardB = 0.10f;
    config.ui.glowIntensity = 1.2f;
    config.ui.windowGlowIntensity = 0.3f;
    config.ui.panelOpacity = 0.85f;
    config.ui.darkenBackground = true;
    config.ui.dimOpacity = 0.6f;
    config.ui.limitMenuFps = true;
    config.ui.menuFpsCap = 144;
    config.targetProcessName = "TestGame.exe";
    return config;
}

void UseTempDir(std::string& dir)
{
#ifdef _WIN32
    char tmp[MAX_PATH]{};
    DWORD len = GetTempPathA(static_cast<DWORD>(sizeof(tmp)), tmp);
    Check(len > 0 && len < sizeof(tmp), "GetTempPathA failed");
    dir = std::string(tmp) + "dribtard_config_test";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    Check(SetEnvironmentVariableA("DRIBTARD_CONFIG_DIR", dir.c_str()) != 0,
        "Could not set DRIBTARD_CONFIG_DIR");
#else
    dir = "/tmp/dribtard_config_test";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    Check(setenv("DRIBTARD_CONFIG_DIR", dir.c_str(), 1) == 0,
        "Could not set DRIBTARD_CONFIG_DIR");
#endif
}

void ClearTempDir(const std::string& dir)
{
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
#ifdef _WIN32
    SetEnvironmentVariableA("DRIBTARD_CONFIG_DIR", nullptr);
#else
    unsetenv("DRIBTARD_CONFIG_DIR");
#endif
}

} // namespace
int main()
{
    try
    {
        // 1. Default round-trip: every field survives JSON.
        {
            const AppConfig defaults;
            AppConfig parsed;
            Check(SettingsManager::FromJson(SettingsManager::ToJson(defaults), parsed),
                "Default round-trip failed");
            CheckEqual(defaults, parsed);
        }

        // 2. Custom round-trip: non-default values in every field survive.
        {
            const AppConfig custom = MakeCustom();
            AppConfig parsed;
            Check(SettingsManager::FromJson(SettingsManager::ToJson(custom), parsed),
                "Custom round-trip failed");
            CheckEqual(custom, parsed);
        }

        // 3. Garbage and foreign documents are rejected, defaults kept.
        {
            AppConfig parsed;
            Check(!SettingsManager::FromJson("", parsed), "Empty doc accepted");
            Check(!SettingsManager::FromJson("not json at all", parsed), "Garbage accepted");
            Check(!SettingsManager::FromJson("{\"foo\": 1}", parsed), "Foreign doc accepted");
        }

        // 4. Out-of-range values are clamped to safe ranges.
        {
            AppConfig parsed;
            Check(SettingsManager::FromJson(
                "{\"version\": 1, \"menuFpsCap\": 9999, \"panelOpacity\": 5.0, "
                "\"robloxSensitivity\": 0.0, \"curveType\": 7, \"triggerKey\": 999, "
                "\"holdKey\": 999}",
                parsed), "Clamp doc rejected");
            Check(parsed.ui.menuFpsCap == 480, "FPS cap not clamped");
            Check(parsed.ui.panelOpacity <= 1.0f, "Panel opacity not clamped");
            Check(parsed.macro.robloxSensitivity >= 0.01, "Sensitivity not clamped");
            Check(parsed.macro.triggerKey <= 255, "Key code not clamped");
            Check(parsed.moonwalk.holdKey == 255, "Moonwalk key code not clamped");
        }

        // 5. Name validation rejects path traversal and junk.
        Check(SettingsManager::SanitizeName("My Setup 1").size() > 0, "Valid name rejected");
        Check(SettingsManager::SanitizeName("").empty(), "Empty name accepted");
        Check(SettingsManager::SanitizeName("../evil").empty(), "Traversal accepted");
        Check(SettingsManager::SanitizeName("a/b").empty(), "Slash accepted");
        Check(SettingsManager::SanitizeName("..").empty(), "Dot-dot accepted");
        Check(SettingsManager::SanitizeName("x\"y").empty(), "Quote accepted");
        Check(SettingsManager::SanitizeName(std::string(64, 'a')).empty(), "Long name accepted");
        Check(SettingsManager::SanitizeName("  padded  ") == "padded", "Trim broken");

        // 6. last.json save/load (the autosave path) in an isolated temp dir.
        std::string tempDir;
        UseTempDir(tempDir);
        try
        {
            const AppConfig custom = MakeCustom();
            Check(SettingsManager::SaveLast(custom), "SaveLast failed");
            AppConfig loaded;
            Check(SettingsManager::LoadLast(loaded), "LoadLast failed");
            CheckEqual(custom, loaded);

            // 7. Named configs: save two, list both, load one, delete one.
            Check(SettingsManager::SaveNamed("alpha", custom), "SaveNamed alpha failed");
            AppConfig defaults;
            Check(SettingsManager::SaveNamed("beta", defaults), "SaveNamed beta failed");
            Check(!SettingsManager::SaveNamed("../evil", custom), "Evil name saved");

            auto names = SettingsManager::ListConfigs();
            Check(names.size() == 2 && names[0] == "alpha" && names[1] == "beta",
                "ListConfigs wrong");

            AppConfig back;
            Check(SettingsManager::LoadNamed("alpha", back), "LoadNamed alpha failed");
            CheckEqual(custom, back);
            Check(!SettingsManager::LoadNamed("missing", back), "Missing load succeeded");

            Check(SettingsManager::DeleteNamed("alpha"), "DeleteNamed failed");
            Check(!SettingsManager::DeleteNamed("alpha"), "Double delete succeeded");
            names = SettingsManager::ListConfigs();
            Check(names.size() == 1 && names[0] == "beta", "List after delete wrong");

            // 8. last.json still round-trips after named-config traffic.
            Check(SettingsManager::SaveLast(defaults), "SaveLast overwrite failed");
            Check(SettingsManager::LoadLast(loaded), "LoadLast re-read failed");
            CheckEqual(defaults, loaded);
        }
        catch (...)
        {
            ClearTempDir(tempDir);
            throw;
        }
        ClearTempDir(tempDir);

        std::cout << "Config JSON round-trip, clamp, sanitize and file tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
