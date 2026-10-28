#include "HotkeyManager.hpp"

#include <sstream>

HotkeyManager::HotkeyManager()
{
    for (int key = 0; key < 256; ++key)
    {
        m_PreviousState[key] =
            (GetAsyncKeyState(key) & 0x8000) != 0;
    }
}

int HotkeyManager::PollPressedKey()
{
    for (int key = 1; key < 256; ++key)
    {
        const bool down =
            (GetAsyncKeyState(key) & 0x8000) != 0;

        const bool pressed =
            down && !m_PreviousState[key];

        m_PreviousState[key] = down;

        if (pressed)
            return key;
    }

    return 0;
}

bool HotkeyManager::IsKeyDown(int virtualKey)
{
    if (virtualKey <= 0 || virtualKey > 255)
        return false;

    return (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
}

bool HotkeyManager::IsModifier(int virtualKey)
{
    return
        virtualKey == VK_SHIFT ||
        virtualKey == VK_LSHIFT ||
        virtualKey == VK_RSHIFT ||
        virtualKey == VK_CONTROL ||
        virtualKey == VK_LCONTROL ||
        virtualKey == VK_RCONTROL ||
        virtualKey == VK_MENU ||
        virtualKey == VK_LMENU ||
        virtualKey == VK_RMENU ||
        virtualKey == VK_LWIN ||
        virtualKey == VK_RWIN;
}

std::string HotkeyManager::GetKeyName(int virtualKey)
{
    if (virtualKey == VK_OEM_5)
        return "\\";

    if (virtualKey >= 'A' && virtualKey <= 'Z')
        return std::string(1, static_cast<char>(virtualKey));

    if (virtualKey >= '0' && virtualKey <= '9')
        return std::string(1, static_cast<char>(virtualKey));

    switch (virtualKey)
    {
    case VK_SPACE: return "Space";
    case VK_RETURN: return "Enter";
    case VK_ESCAPE: return "Escape";
    case VK_TAB: return "Tab";
    case VK_BACK: return "Backspace";
    case VK_SHIFT: return "Shift";
    case VK_CONTROL: return "Ctrl";
    case VK_MENU: return "Alt";
    case VK_LEFT: return "Left";
    case VK_RIGHT: return "Right";
    case VK_UP: return "Up";
    case VK_DOWN: return "Down";
    case VK_INSERT: return "Insert";
    case VK_DELETE: return "Delete";
    case VK_HOME: return "Home";
    case VK_END: return "End";
    case VK_PRIOR: return "Page Up";
    case VK_NEXT: return "Page Down";
    case VK_F1: return "F1";
    case VK_F2: return "F2";
    case VK_F3: return "F3";
    case VK_F4: return "F4";
    case VK_F5: return "F5";
    case VK_F6: return "F6";
    case VK_F7: return "F7";
    case VK_F8: return "F8";
    case VK_F9: return "F9";
    case VK_F10: return "F10";
    case VK_F11: return "F11";
    case VK_F12: return "F12";
    case VK_OEM_MINUS: return "-";
    case VK_OEM_PLUS: return "=";
    case VK_OEM_COMMA: return ",";
    case VK_OEM_PERIOD: return ".";
    case VK_OEM_1: return ";";
    case VK_OEM_2: return "/";
    case VK_OEM_3: return "`";
    case VK_OEM_4: return "[";
    case VK_OEM_6: return "]";
    case VK_OEM_7: return "'";
    }

    UINT scanCode =
        MapVirtualKey(
            static_cast<UINT>(virtualKey),
            MAPVK_VK_TO_VSC);

    char name[128]{};

    if (GetKeyNameTextA(
        static_cast<LONG>(scanCode) << 16,
        name,
        static_cast<int>(sizeof(name))))
    {
        return name;
    }

    std::ostringstream out;
    out << "VK " << virtualKey;
    return out.str();
}
