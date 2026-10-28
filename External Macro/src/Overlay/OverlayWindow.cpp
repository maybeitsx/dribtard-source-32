#include "OverlayWindow.hpp"
#include "../resource.hpp"

#include <dwmapi.h>
#include <algorithm>

namespace
{
    constexpr wchar_t kBlurClassName[] = L"DribtardBlur";

    /*
        Undocumented but long-standing user32 entry point that applies the
        acrylic (frosted blur) accent to a window. It is looked up at run time
        so a missing export simply leaves the menu without the blur.
    */
    struct AccentPolicy
    {
        int AccentState;
        int AccentFlags;
        unsigned int GradientColor;
        int AnimationId;
    };

    struct WindowCompositionAttribData
    {
        int Attribute;
        void* Data;
        SIZE_T SizeOfData;
    };

    using SetWindowCompositionAttributeFn =
        BOOL(WINAPI*)(HWND, WindowCompositionAttribData*);

    constexpr int kWcaAccentPolicy = 19;
    constexpr int kAccentEnableAcrylicBlurBehind = 4;

    bool ApplyAcrylic(HWND hwnd, unsigned tintAlpha)
    {
        static const auto setAttribute = reinterpret_cast<SetWindowCompositionAttributeFn>(
            GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowCompositionAttribute"));
        if (!setAttribute)
            return false;

        // Colour is AABBGGRR: a neutral, dark-grey tint at the requested alpha.
        AccentPolicy policy{};
        policy.AccentState = kAccentEnableAcrylicBlurBehind;
        policy.AccentFlags = 0;
        policy.GradientColor = (tintAlpha << 24) | (0x14u << 16) | (0x14u << 8) | 0x10u;
        policy.AnimationId = 0;

        WindowCompositionAttribData data{};
        data.Attribute = kWcaAccentPolicy;
        data.Data = &policy;
        data.SizeOfData = sizeof(policy);

        return setAttribute(hwnd, &data) != FALSE;
    }
}

// Provided by imgui_impl_win32.cpp. Without forwarding window messages
// to it, ImGui never receives mouse clicks or keyboard text.
extern LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hWnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam);

bool OverlayWindow::Initialize(
    HINSTANCE hInstance,
    const wchar_t* title)
{
    m_hInstance = hInstance;

    m_IconBig = static_cast<HICON>(LoadImageW(
        hInstance,
        MAKEINTRESOURCEW(IDI_APP_ICON),
        IMAGE_ICON,
        GetSystemMetrics(SM_CXICON),
        GetSystemMetrics(SM_CYICON),
        LR_DEFAULTCOLOR));
    if (!m_IconBig)
    {
        m_IconBig = static_cast<HICON>(LoadImageW(
            nullptr,
            L"assets\\macro_icon.ico",
            IMAGE_ICON,
            GetSystemMetrics(SM_CXICON),
            GetSystemMetrics(SM_CYICON),
            LR_LOADFROMFILE | LR_DEFAULTCOLOR));
    }

    m_IconSmall = static_cast<HICON>(LoadImageW(
        hInstance,
        MAKEINTRESOURCEW(IDI_APP_ICON),
        IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON),
        GetSystemMetrics(SM_CYSMICON),
        LR_DEFAULTCOLOR));
    if (!m_IconSmall)
    {
        m_IconSmall = static_cast<HICON>(LoadImageW(
            nullptr,
            L"assets\\macro_icon.ico",
            IMAGE_ICON,
            GetSystemMetrics(SM_CXSMICON),
            GetSystemMetrics(SM_CYSMICON),
            LR_LOADFROMFILE | LR_DEFAULTCOLOR));
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = m_IconBig;
    wc.hIconSm = m_IconSmall;
    wc.lpszClassName = L"DribtardOverlay";

    RegisterClassExW(&wc);

    // Cover the actual display instead of assuming 1920x1080. The old
    // hardcoded size left the overlay smaller/larger than the game window on
    // any other resolution, so the stretched frame no longer lined up with
    // ImGui's click coordinates. SyncWithTarget() snaps to the game every
    // frame after this; the initial size just needs to be sane.
    const int screenW = GetSystemMetrics(SM_CXSCREEN) > 0 ? GetSystemMetrics(SM_CXSCREEN) : 1920;
    const int screenH = GetSystemMetrics(SM_CYSCREEN) > 0 ? GetSystemMetrics(SM_CYSCREEN) : 1080;

    /*
        Created without WS_EX_LAYERED on purpose. A layered window is
        composited through the legacy layered-window path, which ignores the
        flip-model swap chain's per-pixel alpha: ImGui output then looks
        solid and a fullscreen dim quad (pure black) can never darken the
        game. SetPerPixelAlpha() adds the layered style back only when the
        renderer reports the colour-key fallback.
    */
    hwnd = CreateWindowExW(
        WS_EX_TOPMOST,

        wc.lpszClassName,
        title,

        WS_POPUP,

        0,
        0,
        screenW,
        screenH,

        nullptr,
        nullptr,
        hInstance,
        this
    );

    if (!hwnd)
        return false;

    // Blur backdrop for the menu. It is created topmost and hidden; it is only
    // shown under the overlay once SetBlurRect() is given a menu rectangle.
    WNDCLASSEXW blurClass{};
    blurClass.cbSize = sizeof(WNDCLASSEXW);
    blurClass.lpfnWndProc = DefWindowProcW;
    blurClass.hInstance = hInstance;
    blurClass.lpszClassName = kBlurClassName;
    RegisterClassExW(&blurClass);

    m_BlurHwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
        kBlurClassName,
        L"",
        WS_POPUP,
        0, 0, 1, 1,
        nullptr,
        nullptr,
        hInstance,
        nullptr);

    if (m_IconBig)
        SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(m_IconBig));
    if (m_IconSmall)
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(m_IconSmall));

    m_Visible = true;
    m_Interactive = true;

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    return true;
}

void OverlayWindow::Shutdown()
{
    if (hwnd)
    {
        DestroyWindow(hwnd);
        hwnd = nullptr;
    }

    if (m_BlurHwnd)
    {
        DestroyWindow(m_BlurHwnd);
        m_BlurHwnd = nullptr;
    }
    UnregisterClassW(kBlurClassName, m_hInstance);

    UnregisterClassW(
        L"DribtardOverlay",
        m_hInstance
    );

    if (m_IconBig)
    {
        DestroyIcon(m_IconBig);
        m_IconBig = nullptr;
    }
    if (m_IconSmall)
    {
        DestroyIcon(m_IconSmall);
        m_IconSmall = nullptr;
    }
}

void OverlayWindow::SetPerPixelAlpha(bool perPixelAlpha)
{
    m_PerPixelAlpha = perPixelAlpha;

    if (!hwnd)
        return;

    LONG_PTR exStyle =
        GetWindowLongPtrW(
            hwnd,
            GWL_EXSTYLE);

    if (perPixelAlpha)
    {
        /*
            Per-pixel path: the window must NOT be layered, otherwise DWM
            composites it through the legacy layered-window path and the
            swap chain's alpha channel is ignored (solid-looking panels,
            no background dimming). Strip the style if a previous run left
            it set, then extend the DWM glass over the whole window so the
            premultiplied flip-model swap chain is blended per pixel.
        */
        exStyle &= ~static_cast<LONG_PTR>(WS_EX_LAYERED);
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle);
        SetWindowPos(
            hwnd,
            nullptr,
            0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
            SWP_NOACTIVATE | SWP_FRAMECHANGED);

        MARGINS glass{ -1, -1, -1, -1 };
        DwmExtendFrameIntoClientArea(hwnd, &glass);
        return;
    }

    exStyle |= WS_EX_LAYERED;
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle);
    SetWindowPos(
        hwnd,
        nullptr,
        0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
        SWP_NOACTIVATE | SWP_FRAMECHANGED);
    SetLayeredWindowAttributes(
        hwnd,
        RGB(0, 0, 0),
        0,
        LWA_COLORKEY);
}

bool OverlayWindow::SetCaptureExcluded(bool excluded)
{
    if (!hwnd)
        return false;

    const DWORD affinity = excluded ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE;
    if (!SetWindowDisplayAffinity(hwnd, affinity))
        return false;

    // The blur backdrop is part of the menu, so it must be hidden from
    // capture too.
    if (m_BlurHwnd)
        SetWindowDisplayAffinity(m_BlurHwnd, affinity);

    m_CaptureExcluded = excluded;
    return true;
}

void OverlayWindow::SetVisible(bool visible)
{
    if (!hwnd)
        return;

    m_Visible = visible;

    LONG_PTR exStyle =
        GetWindowLongPtrW(
            hwnd,
            GWL_EXSTYLE);

    if (visible)
    {
        // Visible UI should receive mouse/keyboard focus.
        exStyle &= ~static_cast<LONG_PTR>(
            WS_EX_TRANSPARENT | WS_EX_NOACTIVATE);

        m_Interactive = true;

        SetWindowLongPtrW(
            hwnd,
            GWL_EXSTYLE,
            exStyle);

        ShowWindow(hwnd, SW_SHOW);
        SetWindowPos(
            hwnd,
            HWND_TOPMOST,
            0,
            0,
            0,
            0,
            SWP_NOMOVE |
            SWP_NOSIZE |
            SWP_NOACTIVATE |
            SWP_SHOWWINDOW |
            SWP_FRAMECHANGED);
    }
    else
    {
        // Hidden overlay becomes click-through/non-activating.
        exStyle |=
            WS_EX_TRANSPARENT |
            WS_EX_NOACTIVATE;

        m_Interactive = false;

        SetWindowLongPtrW(
            hwnd,
            GWL_EXSTYLE,
            exStyle);

        ShowWindow(hwnd, SW_HIDE);
    }
}

void OverlayWindow::ShowToast()
{
    if (!hwnd)
        return;

    m_Visible = true;
    m_Interactive = false;

    LONG_PTR exStyle =
        GetWindowLongPtrW(
            hwnd,
            GWL_EXSTYLE);

    exStyle |=
        WS_EX_TRANSPARENT |
        WS_EX_NOACTIVATE;

    SetWindowLongPtrW(
        hwnd,
        GWL_EXSTYLE,
        exStyle);

    ShowWindow(hwnd, SW_SHOW);
    SetWindowPos(
        hwnd,
        HWND_TOPMOST,
        0,
        0,
        0,
        0,
        SWP_NOMOVE |
        SWP_NOSIZE |
        SWP_NOACTIVATE |
        SWP_SHOWWINDOW |
        SWP_FRAMECHANGED);
}

void OverlayWindow::SetBlurRect(
    int left,
    int top,
    int right,
    int bottom,
    unsigned tintAlpha,
    bool visible)
{
    if (!m_BlurHwnd || !hwnd)
        return;

    // Reject malformed or unexpectedly large rectangles instead of letting a
    // transient layout error spread the acrylic backdrop across the display.
    RECT client{};
    const bool haveClient = GetClientRect(hwnd, &client) != FALSE;
    const long long rawWidth = static_cast<long long>(right) - left;
    const long long rawHeight = static_cast<long long>(bottom) - top;
    if (!visible || !haveClient || rawWidth <= 0 || rawHeight <= 0 ||
        rawWidth > 820 || rawHeight > 524)
    {
        if (m_BlurVisible)
        {
            ShowWindow(m_BlurHwnd, SW_HIDE);
            m_BlurVisible = false;
        }
        return;
    }

    left = (std::max)(left, static_cast<int>(client.left));
    top = (std::max)(top, static_cast<int>(client.top));
    right = (std::min)(right, static_cast<int>(client.right));
    bottom = (std::min)(bottom, static_cast<int>(client.bottom));
    const int width = right - left;
    const int height = bottom - top;
    if (width <= 0 || height <= 0)
    {
        if (m_BlurVisible)
        {
            ShowWindow(m_BlurHwnd, SW_HIDE);
            m_BlurVisible = false;
        }
        return;
    }

    if (tintAlpha != m_BlurTint)
    {
        ApplyAcrylic(m_BlurHwnd, tintAlpha);
        m_BlurTint = tintAlpha;
    }

    // Only move the blur when its rectangle changes. Always restore its place
    // beneath the overlay as well: the cache must not preserve a bad z-order
    // after a drag, focus change, or target-window restack.
    POINT origin{ left, top };
    ClientToScreen(hwnd, &origin);

    RECT next{ origin.x, origin.y, origin.x + width, origin.y + height };
    if (m_BlurVisible &&
        next.left == m_BlurRect.left && next.top == m_BlurRect.top &&
        next.right == m_BlurRect.right && next.bottom == m_BlurRect.bottom &&
        GetWindow(hwnd, GW_HWNDNEXT) == m_BlurHwnd)
    {
        return;
    }

    SetWindowPos(
        m_BlurHwnd,
        hwnd,
        next.left,
        next.top,
        width,
        height,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);

    m_BlurRect = next;
    m_BlurVisible = true;
}

void OverlayWindow::SyncWithTarget(
    HWND targetWindow)
{
    if (!hwnd || !targetWindow)
        return;

    RECT rect{};

    if (!GetWindowRect(
        targetWindow,
        &rect))
    {
        return;
    }

    const int width =
        rect.right - rect.left;

    const int height =
        rect.bottom - rect.top;

    SetWindowPos(
        hwnd,
        HWND_TOPMOST,
        rect.left,
        rect.top,
        width,
        height,
        SWP_NOACTIVATE |
        (m_Visible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
}

LRESULT CALLBACK OverlayWindow::WndProc(
    HWND hwnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam)
{
    OverlayWindow* window = nullptr;

    if (msg == WM_NCCREATE)
    {
        const auto* createStruct =
            reinterpret_cast<CREATESTRUCTW*>(lParam);

        window =
            static_cast<OverlayWindow*>(
                createStruct->lpCreateParams);

        SetWindowLongPtrW(
            hwnd,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(window));
    }
    else
    {
        window =
            reinterpret_cast<OverlayWindow*>(
                GetWindowLongPtrW(
                    hwnd,
                    GWLP_USERDATA));
    }

    if (ImGui_ImplWin32_WndProcHandler(
        hwnd,
        msg,
        wParam,
        lParam))
    {
        return true;
    }

    switch (msg)
    {
    case WM_NCHITTEST:
        if (window && window->m_Interactive)
            return HTCLIENT;

        return HTTRANSPARENT;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(
        hwnd,
        msg,
        wParam,
        lParam);
}
