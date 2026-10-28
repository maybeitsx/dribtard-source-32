#include "Renderer.hpp"

#include <d3d11.h>
#include <dxgi1_2.h>

ID3D11Device* g_pd3dDevice = nullptr;
ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;

static IDXGISwapChain* g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

bool Renderer::Initialize(HWND hwnd)
{
    if (!CreateDeviceD3D(hwnd))
        return false;

    CreateRenderTarget();

    // Remember the size the swap chain was created at so the first
    // BeginFrame() can detect a mismatch and resize immediately.
    if (hwnd)
    {
        RECT rc{};
        if (GetClientRect(hwnd, &rc))
        {
            m_Width = rc.right - rc.left;
            m_Height = rc.bottom - rc.top;
        }
    }

    m_SwapChain = g_pSwapChain;
    m_RenderTargetView =
        g_mainRenderTargetView;

    return true;
}

void Renderer::Shutdown()
{
    CleanupRenderTarget();
    CleanupDeviceD3D();
}

bool Renderer::CreateDeviceD3D(
    HWND hwnd)
{
    UINT createDeviceFlags = 0;

#ifdef _DEBUG
    createDeviceFlags |=
        D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL featureLevel;

    const D3D_FEATURE_LEVEL featureLevels[] =
    {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_0
    };

    const HRESULT res =
        D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            createDeviceFlags,
            featureLevels,
            2,
            D3D11_SDK_VERSION,
            &g_pd3dDevice,
            &featureLevel,
            &g_pd3dDeviceContext
        );

    if (FAILED(res))
        return false;

    /*
        Escape hatch for machines that dislike flip-model overlays: keeping
        DRIBTARD_LEGACY_PRESENT set restores the previous opaque swap chain
        (the overlay then falls back to colour-key transparency).
    */
    wchar_t legacyPresent[8]{};
    const bool forceLegacy =
        GetEnvironmentVariableW(
            L"DRIBTARD_LEGACY_PRESENT",
            legacyPresent,
            8) > 0;

    if (!forceLegacy && CreateSwapChain(hwnd, true))
    {
        m_PerPixelAlpha = true;
        return true;
    }

    return CreateSwapChain(hwnd, false);
}

bool Renderer::CreateSwapChain(
    HWND hwnd,
    bool perPixelAlpha)
{
    /*
        Premultiplied alpha needs a flip-model swap chain, which is created
        through the DXGI factory rather than D3D11CreateDeviceAndSwapChain.
        With it, DWM honours the alpha channel of every rendered pixel, so
        the UI can be translucent.
    */
    IDXGIDevice* dxgiDevice = nullptr;

    if (FAILED(g_pd3dDevice->QueryInterface(
        IID_PPV_ARGS(&dxgiDevice))))
        return false;

    IDXGIAdapter* adapter = nullptr;
    HRESULT res = dxgiDevice->GetAdapter(&adapter);
    dxgiDevice->Release();

    if (FAILED(res))
        return false;

    IDXGIFactory2* factory = nullptr;
    res = adapter->GetParent(IID_PPV_ARGS(&factory));
    adapter->Release();

    if (FAILED(res))
        return false;

    DXGI_SWAP_CHAIN_DESC1 desc{};

    // Zero resolves to the client area of the window.
    desc.Width = 0;
    desc.Height = 0;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.Stereo = FALSE;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = perPixelAlpha
        ? DXGI_SWAP_EFFECT_FLIP_DISCARD
        : DXGI_SWAP_EFFECT_DISCARD;
    desc.AlphaMode = perPixelAlpha
        ? DXGI_ALPHA_MODE_PREMULTIPLIED
        : DXGI_ALPHA_MODE_IGNORE;
    desc.Flags = 0;

    IDXGISwapChain1* swapChain = nullptr;

    res = factory->CreateSwapChainForHwnd(
        g_pd3dDevice,
        hwnd,
        &desc,
        nullptr,
        nullptr,
        &swapChain);

    factory->Release();

    if (FAILED(res))
        return false;

    // IDXGISwapChain1 derives from IDXGISwapChain, so the rest of the renderer
    // keeps working against the base interface.
    g_pSwapChain = swapChain;

    return true;
}

void Renderer::CreateRenderTarget()
{
    ID3D11Texture2D* backBuffer = nullptr;

    if (SUCCEEDED(
        g_pSwapChain->GetBuffer(
            0,
            IID_PPV_ARGS(&backBuffer))))
    {
        g_pd3dDevice->CreateRenderTargetView(
            backBuffer,
            nullptr,
            &g_mainRenderTargetView
        );

        backBuffer->Release();
    }
}

void Renderer::CleanupRenderTarget()
{
    if (g_mainRenderTargetView)
    {
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = nullptr;
    }
}

void Renderer::CleanupDeviceD3D()
{
    if (g_pSwapChain)
    {
        g_pSwapChain->Release();
        g_pSwapChain = nullptr;
    }

    if (g_pd3dDeviceContext)
    {
        g_pd3dDeviceContext->Release();
        g_pd3dDeviceContext = nullptr;
    }

    if (g_pd3dDevice)
    {
        g_pd3dDevice->Release();
        g_pd3dDevice = nullptr;
    }
}

void Renderer::EnsureBackbufferMatchesWindow(HWND hwnd)
{
    if (!hwnd || !g_pSwapChain)
        return;

    RECT rc{};
    if (!GetClientRect(hwnd, &rc))
        return;

    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;

    // Minimized: nothing to render into.
    if (w <= 0 || h <= 0)
        return;

    if (w == m_Width && h == m_Height)
        return;

    CleanupRenderTarget();

    if (SUCCEEDED(g_pSwapChain->ResizeBuffers(0, static_cast<UINT>(w), static_cast<UINT>(h), DXGI_FORMAT_UNKNOWN, 0)))
    {
        m_Width = w;
        m_Height = h;
    }

    CreateRenderTarget();
}

void Renderer::BeginFrame(HWND hwnd)
{
    if (!g_pd3dDeviceContext)
        return;

    EnsureBackbufferMatchesWindow(hwnd);

    if (!g_mainRenderTargetView)
        return;

    const float clearColor[4] =
    {
        0.0f,
        0.0f,
        0.0f,
        0.0f
    };

    g_pd3dDeviceContext->OMSetRenderTargets(
        1,
        &g_mainRenderTargetView,
        nullptr
    );

    g_pd3dDeviceContext->ClearRenderTargetView(
        g_mainRenderTargetView,
        clearColor
    );
}

void Renderer::EndFrame()
{
    if (g_pSwapChain)
    {
        g_pSwapChain->Present(
            1,
            0
        );
    }
}