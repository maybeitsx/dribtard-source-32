#pragma once

#include "Macro/BladeBallMacro.hpp"
#include "Macro/ClickerMacro.hpp"
#include "Macro/MacroEngine.hpp"

#include <string>
#include <vector>

/*
    The macOS profile now carries the same menu settings as the Windows build:
    menu key, accent and surface colours, glows, opacity, background dimming,
    notifications, chat detection and the menu mode.

    Key codes are still macOS virtual key codes; a Windows config file and a
    macOS config file are not interchangeable.
*/
struct UiSettings
{
    int menuKey = Keys::Backslash;

    // Default look matches the Windows build: pink accent on black glass.
    float accentR = 0.8167f;
    float accentG = 0.3512f;
    float accentB = 0.5799f;

    float cardR = 0.0f;
    float cardG = 0.0f;
    float cardB = 0.0f;

    float panelR = 0.0f;
    float panelG = 0.0f;
    float panelB = 0.0f;

    float glowIntensity = 1.50f;
    float windowGlowIntensity = 0.67f;
    float panelOpacity = 0.56f;

    bool darkenBackground = false;
    float dimOpacity = 0.45f;

    // Top-right notices for curve hotkey switches.
    bool showNotifications = true;

    // Pause every macro keybind while the in-game chat is open: "/" opens it,
    // Enter sends the message and brings the keybinds back.
    bool chatDetection = false;

    // Compact hides the panel behind the rail until the pointer is over it.
    bool portableMode = false;

    // Fade the page when switching tabs.
    bool tabFade = false;
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
// Directory() + "/configs"
std::string ConfigsDirectory();
// Full path of a named config; empty for an invalid name.
std::string ConfigFile(const std::string& name);

// Trimmed name when it is safe to use as a file name, empty otherwise.
std::string SanitizeName(const std::string& name);

bool Save(const AppConfig& config);
bool Load(AppConfig& config);

// Named profiles, saved explicitly from the Config tab.
bool SaveNamed(const std::string& name, const AppConfig& config);
bool LoadNamed(const std::string& name, AppConfig& config);
bool DeleteNamed(const std::string& name);
std::vector<std::string> ListNamed();
} // namespace Settings
