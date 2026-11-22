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

    void ApplyPalette()
    {
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 14.0f;
        style.ChildRounding = 10.0f;
        style.FrameRounding = 7.0f;
        style.PopupRounding = 8.0f;
        style.ScrollbarRounding = 8.0f;
        style.GrabRounding = 7.0f;
        style.WindowBorderSize = 1.0f;
        style.FramePadding = ImVec2(8.0f, 4.0f);
        style.ItemSpacing = ImVec2(8.0f, 6.0f);
        style.WindowPadding = ImVec2(10.0f, 10.0f);

        ImVec4* colors = style.Colors;
        colors[ImGuiCol_WindowBg] = ImColorOf(8, 15, 23, 225);
        colors[ImGuiCol_ChildBg] = ImColorOf(14, 17, 33, 235);
        colors[ImGuiCol_Border] = ImColorOf(255, 255, 255, 26);
        colors[ImGuiCol_FrameBg] = ImColorOf(19, 19, 43, 255);
        colors[ImGuiCol_FrameBgHovered] = ImColorOf(46, 33, 92, 255);
        colors[ImGuiCol_FrameBgActive] = ImColorOf(63, 48, 122, 255);
        colors[ImGuiCol_Button] = ImColorOf(26, 24, 51, 255);
        colors[ImGuiCol_ButtonHovered] = ImColorOf(46, 33, 92, 255);
        colors[ImGuiCol_ButtonActive] = ImColorOf(115, 97, 255, 255);
        colors[ImGuiCol_Header] = ImColorOf(115, 97, 255, 90);
        colors[ImGuiCol_SliderGrab] = ImColorOf(115, 97, 255, 255);
        colors[ImGuiCol_SliderGrabActive] = ImColorOf(161, 146, 255, 255);
        colors[ImGuiCol_CheckMark] = ImColorOf(161, 146, 255, 255);
        colors[ImGuiCol_Separator] = ImColorOf(255, 255, 255, 22);
        colors[ImGuiCol_Text] = ImColorOf(236, 236, 244, 255);
        colors[ImGuiCol_TextDisabled] = ImColorOf(150, 152, 164, 255);
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
