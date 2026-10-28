#pragma once

#include <windows.h>
#include <d3d11.h>

class Renderer
{
public:
    bool Initialize(HWND hwnd);
    void Shutdown();

    // Pass the overlay HWND so the backbuffer can be resized to match the
    // window whenever SyncWithTarget() (or a resolution/monitor change)
    // alters it. Without this the stretched frame desyncs from ImGui's
    // mouse coordinates and buttons stop responding where they appear.
    void BeginFrame(HWND hwnd);
    void EndFrame();

    /*
        True when the swap chain was created with premultiplied alpha, so DWM
        composites the overlay per pixel. The translucent UI depends on this;
        the fallback path keeps the legacy colour-key transparency instead.
    */
    bool UsesPerPixelAlpha() const { return m_PerPixelAlpha; }

private:
    IDXGISwapChain* m_SwapChain = nullptr;
    ID3D11RenderTargetView* m_RenderTargetView = nullptr;
    bool m_PerPixelAlpha = false;
    int m_Width = 0;
    int m_Height = 0;
    void EnsureBackbufferMatchesWindow(HWND hwnd);

    bool CreateDeviceD3D(HWND hwnd);
    bool CreateSwapChain(HWND hwnd, bool perPixelAlpha);
    void CleanupDeviceD3D();
    void CreateRenderTarget();
    void CleanupRenderTarget();
};