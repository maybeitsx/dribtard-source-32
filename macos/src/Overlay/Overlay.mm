#include "Overlay/Overlay.hpp"

#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <QuartzCore/QuartzCore.h>

#include "imgui.h"
#include "backends/imgui_impl_metal.h"
#include "backends/imgui_impl_osx.h"

struct Overlay::Impl
{
    void* window = nullptr;
    void* view = nullptr;
    void* device = nullptr;
    void* queue = nullptr;
    void* delegate = nullptr;
    void* mouseMonitor = nullptr;
    Overlay::Contents contents;
    float panel[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    bool visible = true;
    bool clickThrough = true;
    bool passMouse = false;
};

@interface DribtardPanel : NSPanel
@end

@implementation DribtardPanel
- (BOOL)canBecomeKeyWindow { return YES; }
- (BOOL)canBecomeMainWindow { return NO; }
@end

@interface DribtardMetalView : MTKView
@end

@implementation DribtardMetalView
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent*)event { (void)event; return YES; }
@end

static void RenderFrame(void* owner, MTKView* view);

@interface DribtardViewDelegate : NSObject <MTKViewDelegate>
@property (nonatomic, assign) void* owner;
@end

@implementation DribtardViewDelegate
- (void)mtkView:(MTKView*)view drawableSizeWillChange:(CGSize)size
{
    (void)view;
    (void)size;
}

- (void)drawInMTKView:(MTKView*)view
{
    RenderFrame(self.owner, view);
}
@end

namespace
{
    ImVec4 ImColorOf(unsigned r, unsigned g, unsigned b, unsigned a)
    {
        return ImVec4(static_cast<float>(r) / 255.0f, static_cast<float>(g) / 255.0f,
            static_cast<float>(b) / 255.0f, static_cast<float>(a) / 255.0f);
    }

    /*
        The Windows theme, carried over value for value: the same rounding,
        the same spacing, the same dark blue-black surfaces and the same
        text colours. The accent-driven colours (grab, check mark) are pushed
        per frame by the menu, so a custom accent reaches them too.
    */
    void ApplyPalette()
    {
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowPadding = ImVec2(10.0f, 10.0f);
        style.FramePadding = ImVec2(10.0f, 3.0f);
        style.ItemSpacing = ImVec2(8.0f, 4.0f);
        style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
        style.ScrollbarSize = 8.0f;
        style.GrabMinSize = 8.0f;
        style.WindowBorderSize = 1.0f;
        style.ChildBorderSize = 1.0f;
        style.FrameBorderSize = 1.0f;
        style.PopupBorderSize = 1.0f;

        style.AntiAliasedLines = true;
        style.AntiAliasedLinesUseTex = true;
        style.AntiAliasedFill = true;

        style.WindowRounding = 12.0f;
        style.ChildRounding = 10.0f;
        style.FrameRounding = 7.0f;
        style.PopupRounding = 8.0f;
        style.ScrollbarRounding = 6.0f;
        style.GrabRounding = 6.0f;
        style.TabRounding = 6.0f;

        ImVec4* colors = style.Colors;
        colors[ImGuiCol_WindowBg] = ImVec4(0.025f, 0.027f, 0.07f, 1.0f);
        colors[ImGuiCol_ChildBg] = ImVec4(0.04f, 0.045f, 0.11f, 1.0f);
        colors[ImGuiCol_PopupBg] = ImVec4(0.035f, 0.035f, 0.095f, 0.99f);
        colors[ImGuiCol_FrameBg] = ImVec4(0.075f, 0.075f, 0.17f, 1.0f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.14f, 0.11f, 0.28f, 1.0f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.20f, 0.15f, 0.38f, 1.0f);
        colors[ImGuiCol_Button] = ImVec4(0.10f, 0.085f, 0.22f, 1.0f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.18f, 0.13f, 0.36f, 1.0f);
        colors[ImGuiCol_ButtonActive] = ImVec4(0.45f, 0.38f, 1.0f, 1.0f);
        colors[ImGuiCol_Header] = ImVec4(0.45f, 0.38f, 1.0f, 0.34f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(0.54f, 0.46f, 1.0f, 0.58f);
        colors[ImGuiCol_HeaderActive] = ImVec4(0.54f, 0.46f, 1.0f, 1.0f);
        colors[ImGuiCol_CheckMark] = ImVec4(0.54f, 0.46f, 1.0f, 1.0f);
        colors[ImGuiCol_SliderGrab] = ImVec4(0.45f, 0.38f, 1.0f, 1.0f);
        colors[ImGuiCol_SliderGrabActive] = ImVec4(0.54f, 0.46f, 1.0f, 1.0f);
        colors[ImGuiCol_Border] = ImVec4(0.63f, 0.57f, 1.0f, 0.14f);
        colors[ImGuiCol_Separator] = ImVec4(0.63f, 0.57f, 1.0f, 0.10f);
        colors[ImGuiCol_Text] = ImVec4(0.94f, 0.94f, 1.0f, 1.0f);
        colors[ImGuiCol_TextDisabled] = ImVec4(0.58f, 0.60f, 0.76f, 1.0f);
    }

    bool PointerOverPanel(Overlay::Impl* impl)
    {
        DribtardPanel* window = (__bridge DribtardPanel*)impl->window;
        if (window == nil)
            return false;

        const NSPoint mouse = [NSEvent mouseLocation];
        const NSRect frame = window.frame;

        const CGFloat localX = mouse.x - frame.origin.x;
        const CGFloat localY = (frame.origin.y + frame.size.height) - mouse.y;

        return localX >= impl->panel[0] && localX <= impl->panel[0] + impl->panel[2] &&
            localY >= impl->panel[1] && localY <= impl->panel[1] + impl->panel[3];
    }

    void UpdateClickThrough(Overlay::Impl* impl)
    {
        DribtardPanel* window = (__bridge DribtardPanel*)impl->window;
        if (window == nil)
            return;

        const bool ignore = impl->passMouse || !impl->visible || !PointerOverPanel(impl);
        if (ignore != impl->clickThrough)
        {
            window.ignoresMouseEvents = ignore;
            impl->clickThrough = ignore;
        }
    }
}

static void RenderFrame(void* owner, MTKView* view)
{
    auto* impl = static_cast<Overlay::Impl*>(owner);
    if (impl == nullptr)
        return;

    @autoreleasepool
    {
        if (view.drawableSize.width <= 0.0 || view.drawableSize.height <= 0.0)
            return;

        MTLRenderPassDescriptor* pass = view.currentRenderPassDescriptor;
        id<CAMetalDrawable> drawable = view.currentDrawable;
        if (pass == nil || drawable == nil)
            return;

        UpdateClickThrough(impl);

        ImGui_ImplMetal_NewFrame(pass);
        ImGui_ImplOSX_NewFrame(view);
        ImGui::NewFrame();

        if (impl->contents)
            impl->contents();

        ImGui::Render();

        id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)impl->queue;
        id<MTLCommandBuffer> command = [queue commandBuffer];

        pass.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 0.0);
        pass.colorAttachments[0].loadAction = MTLLoadActionClear;

        id<MTLRenderCommandEncoder> encoder =
            [command renderCommandEncoderWithDescriptor:pass];

        ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), command, encoder);

        [encoder endEncoding];
        [command presentDrawable:drawable];
        [command commit];
    }
}

bool Overlay::Init(Contents contents)
{
    if (m_Impl != nullptr)
        return false;

    m_Impl = new Impl();
    m_Impl->contents = std::move(contents);

    @autoreleasepool
    {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (device == nil)
            return false;

        m_Impl->device = (__bridge_retained void*)device;
        m_Impl->queue = (__bridge_retained void*)[device newCommandQueue];

        NSScreen* screen = [NSScreen mainScreen];
        if (screen == nil)
            return false;

        const NSRect frame = screen.frame;

        DribtardPanel* panel = [[DribtardPanel alloc]
            initWithContentRect:frame
                      styleMask:(NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel)
                        backing:NSBackingStoreBuffered
                          defer:NO];

        panel.opaque = NO;
        panel.backgroundColor = [NSColor clearColor];
        panel.hasShadow = NO;
        panel.level = NSFloatingWindowLevel;
        panel.collectionBehavior =
            NSWindowCollectionBehaviorCanJoinAllSpaces |
            NSWindowCollectionBehaviorFullScreenAuxiliary |
            NSWindowCollectionBehaviorStationary |
            NSWindowCollectionBehaviorIgnoresCycle;
        panel.hidesOnDeactivate = NO;
        panel.releasedWhenClosed = NO;
        panel.movable = NO;
        panel.animationBehavior = NSWindowAnimationBehaviorNone;
        panel.ignoresMouseEvents = YES;

        DribtardMetalView* view = [[DribtardMetalView alloc]
            initWithFrame:NSMakeRect(0.0, 0.0, frame.size.width, frame.size.height)
                   device:device];

        view.colorPixelFormat = MTLPixelFormatBGRA8Unorm;
        view.clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 0.0);
        view.paused = NO;
        view.enableSetNeedsDisplay = NO;
        view.autoResizeDrawable = YES;
        view.wantsLayer = YES;
        view.layer.opaque = NO;
        if ([view.layer isKindOfClass:[CAMetalLayer class]])
            ((CAMetalLayer*)view.layer).opaque = NO;

        DribtardViewDelegate* delegate = [[DribtardViewDelegate alloc] init];
        delegate.owner = m_Impl;
        view.delegate = delegate;

        m_Impl->window = (__bridge_retained void*)panel;
        m_Impl->view = (__bridge_retained void*)view;
        m_Impl->delegate = (__bridge_retained void*)delegate;

        panel.contentView = view;

        // The panel only becomes key when it is actually clicked, so the game
        // keeps keyboard focus while the menu is merely visible.
        id monitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskLeftMouseDown
                                                          handler:^NSEvent* (NSEvent* event)
        {
            if (m_Impl != nullptr && m_Impl->passMouse)
                return event;
            [panel makeKeyWindow];
            return event;
        }];
        m_Impl->mouseMonitor = (__bridge_retained void*)monitor;

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.FontGlobalScale = 1.0f;

        /*
            Crisp text: the same 16 px, 3x3-oversampled UI font the Windows
            build rasterizes from Segoe UI. macOS has no Segoe, so the rounded
            system faces are tried first - SF Pro Rounded gives the menu its
            soft, cute look - then the plain system faces, with the built-in
            font as the last resort.
        */
        ImFontConfig fontConfig;
        fontConfig.OversampleH = 3;
        fontConfig.OversampleV = 3;
        fontConfig.PixelSnapH = true;
        fontConfig.RasterizerMultiply = 1.05f;

        const char* fontCandidates[] = {
            "/System/Library/Fonts/SFNSRounded.ttf",
            "/System/Library/Fonts/Supplemental/Arial Rounded Bold.ttf",
            "/System/Library/Fonts/SFNS.ttf",
            "/System/Library/Fonts/Helvetica.ttc",
            "/System/Library/Fonts/Supplemental/Arial.ttf"
        };
        bool fontLoaded = false;
        for (const char* fontPath : fontCandidates)
        {
            if ([[NSFileManager defaultManager] fileExistsAtPath:@(fontPath)] == NO)
                continue;
            if (io.Fonts->AddFontFromFileTTF(fontPath, 16.0f, &fontConfig) != nullptr)
            {
                fontLoaded = true;
                break;
            }
        }
        if (!fontLoaded)
            io.Fonts->AddFontDefault();

        ApplyPalette();

        if (!ImGui_ImplMetal_Init(device))
            return false;
        if (!ImGui_ImplOSX_Init(view))
            return false;

        // orderFrontRegardless does not activate the app, so Roblox stays
        // frontmost until the user clicks the panel.
        [panel orderFrontRegardless];
    }

    return true;
}

void Overlay::SetPanelRect(float x, float y, float width, float height)
{
    if (m_Impl == nullptr)
        return;

    m_Impl->panel[0] = x;
    m_Impl->panel[1] = y;
    m_Impl->panel[2] = width;
    m_Impl->panel[3] = height;
}

void Overlay::SetPassMouseThrough(bool pass)
{
    if (m_Impl != nullptr)
        m_Impl->passMouse = pass;
}

void Overlay::SetVisible(bool visible)
{
    if (m_Impl == nullptr)
        return;

    m_Impl->visible = visible;

    DribtardPanel* window = (__bridge DribtardPanel*)m_Impl->window;
    if (window != nil && !visible)
        window.ignoresMouseEvents = YES;
}

void Overlay::Shutdown()
{
    if (m_Impl == nullptr)
        return;

    @autoreleasepool
    {
        ImGui_ImplOSX_Shutdown();
        ImGui_ImplMetal_Shutdown();
        ImGui::DestroyContext();

        if (m_Impl->mouseMonitor != nullptr)
        {
            [NSEvent removeMonitor:(__bridge id)m_Impl->mouseMonitor];
            CFRelease(m_Impl->mouseMonitor);
            m_Impl->mouseMonitor = nullptr;
        }

        if (m_Impl->view != nullptr)
        {
            DribtardMetalView* view = (__bridge_transfer DribtardMetalView*)m_Impl->view;
            view.delegate = nil;
            view.paused = YES;
            [view removeFromSuperview];
            m_Impl->view = nullptr;
        }

        if (m_Impl->window != nullptr)
        {
            DribtardPanel* window = (__bridge_transfer DribtardPanel*)m_Impl->window;
            [window orderOut:nil];
            [window close];
            m_Impl->window = nullptr;
        }

        if (m_Impl->delegate != nullptr)
        {
            CFRelease(m_Impl->delegate);
            m_Impl->delegate = nullptr;
        }

        if (m_Impl->queue != nullptr)
        {
            CFRelease(m_Impl->queue);
            m_Impl->queue = nullptr;
        }

        if (m_Impl->device != nullptr)
        {
            CFRelease(m_Impl->device);
            m_Impl->device = nullptr;
        }
    }

    delete m_Impl;
    m_Impl = nullptr;
}
