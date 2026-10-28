#include <windows.h>
#include "Application.hpp"
#include "imgui_impl_win32.h"

int WINAPI WinMain(
    HINSTANCE hInstance,
    HINSTANCE,
    LPSTR,
    int
)
{
    // Without this, Windows virtualizes mouse/window coordinates on any
    // display scale other than 100% (125%, 150%, 4K, etc.). ImGui then gets
    // scaled cursor positions while DirectX renders in physical pixels, so
    // every button hitbox is offset and footer pills like Settings become
    // unclickable. Per-monitor V2 keeps coordinates in physical pixels on
    // all resolutions, which is also why this can break "out of nowhere"
    // (a Windows update, driver change, or moving the game to another
    // monitor can change the effective scale with no settings touched).
    ImGui_ImplWin32_EnableDpiAwareness();

    Application application;
    return application.Run(hInstance) ? 0 : 1;
}