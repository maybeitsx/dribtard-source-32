#pragma once

#include <windows.h>

class OverlayWindow
{
public:
    HWND hwnd = nullptr;

    bool Initialize(
        HINSTANCE hInstance,
        const wchar_t* title);

    void Shutdown();

    void SyncWithTarget(
        HWND targetWindow);

    void SetVisible(bool visible);
    bool IsVisible() const { return m_Visible; }

    /*
        Chooses how the overlay's transparency is produced. With per-pixel
        alpha the window stays non-layered with full DWM glass so the flip-
        model swap chain's alpha channel is blended per pixel (translucent
        panels and background dimming work). Without it the window becomes
        layered with a black colour key: pixels are then either fully
        transparent or fully opaque, so translucency and dimming cannot
        apply on that path.
    */
    void SetPerPixelAlpha(bool perPixelAlpha);

    /*
        Frosted-glass backdrop for the menu: an acrylic blur window placed just
        under the overlay and sized to the menu rectangle (client coordinates),
        so only the menu area is blurred and the rest of the game stays sharp.
        tintAlpha is the strength of the light tint over the blur (0 = pure
        blur, 255 = solid tint).
    */
    void SetBlurRect(
        int left,
        int top,
        int right,
        int bottom,
        unsigned tintAlpha,
        bool visible);


    // Show the window for notifications only: visible on screen but
    // click-through and non-activating, so the game keeps mouse/keyboard.
    void ShowToast();

    bool SetCaptureExcluded(bool excluded);
    bool IsCaptureExcluded() const { return m_CaptureExcluded; }

private:
    HINSTANCE m_hInstance = nullptr;
    HICON m_IconBig = nullptr;
    HICON m_IconSmall = nullptr;
    bool m_Visible = true;
    bool m_Interactive = true;
    bool m_CaptureExcluded = false;
    bool m_PerPixelAlpha = false;

    HWND m_BlurHwnd = nullptr;
    RECT m_BlurRect{};
    unsigned m_BlurTint = 0xFFFFFFFFu;
    bool m_BlurVisible = false;

    static LRESULT CALLBACK WndProc(
        HWND hwnd,
        UINT msg,
        WPARAM wParam,
        LPARAM lParam);
};
