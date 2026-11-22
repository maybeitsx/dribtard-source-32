#pragma once

#include "Macro/BladeBallMacro.hpp"
#include "Macro/ClickerMacro.hpp"
#include "Macro/MacroEngine.hpp"

#include <string>

struct UiSettings
{
    int menuKey = Keys::Backslash;
    float accentR = 0.45f;
    float accentG = 0.38f;
    float accentB = 1.0f;
    // Panel background opacity, 0.30 (pure glass) .. 1.0 (solid).
    float panelOpacity = 0.88f;
};

/*
    The macOS profile. The JSON keys are prefixed per engine so a flat lookup
    cannot confuse the Structure macro's keys with Blade Ball's.
*/
struct AppConfig
{
    MacroSettings macro{};
    ClickerSettings clicker{};
    BladeBallSettings bladeBall{};
    UiSettings ui{};
};

namespace Settings
{
// ~/Library/Application Support/dribtard
std::string Directory();
// Directory() + "/last.json"
std::string Path();

bool Save(const AppConfig& config);
bool Load(AppConfig& config);
} // namespace Settings
