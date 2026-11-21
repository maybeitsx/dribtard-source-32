#include "Keys.hpp"

#include <cstdio>

namespace
{
    const char* LetterOrDigitName(int key)
    {
        switch (key)
        {
        case Keys::A: return "A";
        case Keys::B: return "B";
        case Keys::C: return "C";
        case Keys::D: return "D";
        case Keys::E: return "E";
        case Keys::F: return "F";
        case Keys::G: return "G";
        case Keys::H: return "H";
        case Keys::I: return "I";
        case Keys::J: return "J";
        case Keys::K: return "K";
        case Keys::L: return "L";
        case Keys::M: return "M";
        case Keys::N: return "N";
        case Keys::O: return "O";
        case Keys::P: return "P";
        case Keys::Q: return "Q";
        case Keys::R: return "R";
        case Keys::S: return "S";
        case Keys::T: return "T";
        case Keys::U: return "U";
        case Keys::V: return "V";
        case Keys::W: return "W";
        case Keys::X: return "X";
        case Keys::Y: return "Y";
        case Keys::Z: return "Z";
        case Keys::Zero: return "0";
        case Keys::One: return "1";
        case Keys::Two: return "2";
        case Keys::Three: return "3";
        case Keys::Four: return "4";
        case Keys::Five: return "5";
        case Keys::Six: return "6";
        case Keys::Seven: return "7";
        case Keys::Eight: return "8";
        case Keys::Nine: return "9";
        default: return nullptr;
        }
    }

    const char* SymbolName(int key)
    {
        switch (key)
        {
        case Keys::Minus: return "-";
        case Keys::Equal: return "=";
        case Keys::LeftBracket: return "[";
        case Keys::RightBracket: return "]";
        case Keys::Backslash: return "\\";
        case Keys::Semicolon: return ";";
        case Keys::Quote: return "'";
        case Keys::Comma: return ",";
        case Keys::Period: return ".";
        case Keys::Slash: return "/";
        case Keys::Grave: return "`";
        case Keys::Space: return "Space";
        case Keys::Tab: return "Tab";
        case Keys::Return: return "Return";
        case Keys::Escape: return "Escape";
        case Keys::Backspace: return "Backspace";
        case Keys::Delete: return "Delete";
        case Keys::Home: return "Home";
        case Keys::End: return "End";
        case Keys::PageUp: return "Page Up";
        case Keys::PageDown: return "Page Down";
        case Keys::Left: return "Left";
        case Keys::Right: return "Right";
        case Keys::Up: return "Up";
        case Keys::Down: return "Down";
        case Keys::F1: return "F1";
        case Keys::F2: return "F2";
        case Keys::F3: return "F3";
        case Keys::F4: return "F4";
        case Keys::F5: return "F5";
        case Keys::F6: return "F6";
        case Keys::F7: return "F7";
        case Keys::F8: return "F8";
        case Keys::F9: return "F9";
        case Keys::F10: return "F10";
        case Keys::F11: return "F11";
        case Keys::F12: return "F12";
        case Keys::Keypad1: return "Keypad 1";
        case Keys::Keypad2: return "Keypad 2";
        case Keys::Keypad3: return "Keypad 3";
        case Keys::Keypad4: return "Keypad 4";
        default: return nullptr;
        }
    }
}

const char* Keys::Name(int key)
{
    if (const char* name = LetterOrDigitName(key))
        return name;
    if (const char* name = SymbolName(key))
        return name;

    // Unknown key: a stable label beats an empty button. One buffer per thread
    // is enough because the menu renders on the main thread.
    static thread_local char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "Key %d", key);
    return buffer;
}
