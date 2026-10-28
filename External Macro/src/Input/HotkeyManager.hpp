#pragma once

#include <windows.h>

#include <array>
#include <string>

class HotkeyManager
{
public:
    HotkeyManager();

    // Returns a virtual-key code that transitioned to down since
    // the previous poll, or 0 when nothing changed.
    int PollPressedKey();

    static std::string GetKeyName(int virtualKey);
    static bool IsModifier(int virtualKey);
    // True while the key is physically down. Used by hold-triggered macros
    // (e.g. Moonwalk Tech) that run for as long as the keybind is held.
    static bool IsKeyDown(int virtualKey);

private:
    std::array<bool, 256> m_PreviousState{};
};
