#pragma once

#include <functional>

/*
    A transparent, always-on-top panel that hosts the ImGui menu.

    The window covers the main screen but only the menu panel takes mouse
    input: everywhere else the click goes straight to the game, so the overlay
    never steals a click. Anything drawn outside the panel is click-through.
*/
class Overlay
{
public:
    // Opaque implementation detail, defined in Overlay.mm. Public only so the
    // Objective-C++ helpers in that file can name it.
    struct Impl;

    using Contents = std::function<void()>;

    // Creates the window, the Metal view and the ImGui context. Must run on
    // the main thread. `contents` draws the UI every frame.
    bool Init(Contents contents);
    void Shutdown();

    // Panel rectangle in view coordinates (top-left origin, points). Used for
    // the click-through test.
    void SetPanelRect(float x, float y, float width, float height);
    void SetVisible(bool visible);

    // While the clicker is posting mouse buttons, the panel must not receive
    // them. Otherwise every injected click lands on the menu.
    void SetPassMouseThrough(bool pass);

private:
    Impl* m_Impl = nullptr;
};
