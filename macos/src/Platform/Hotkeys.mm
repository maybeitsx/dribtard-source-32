#include "Platform/Hotkeys.hpp"

#import <ApplicationServices/ApplicationServices.h>

#include <chrono>

namespace
{
    CGEventRef TapCallback(
        CGEventTapProxy proxy,
        CGEventType type,
        CGEventRef event,
        void* refcon)
    {
        (void)proxy;

        auto* monitor = static_cast<HotkeyMonitor*>(refcon);
        if (monitor == nullptr)
            return event;

        if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput)
        {
            // The system disables a slow tap; ask for it back instead of
            // silently losing every hotkey.
            monitor->PushTapDisabled();
            return event;
        }

        if (type == kCGEventKeyDown)
        {
            const int keyCode = static_cast<int>(
                CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode));
            const int repeat = static_cast<int>(
                CGEventGetIntegerValueField(event, kCGKeyboardEventAutorepeat));

            // Auto-repeat must not retrigger the macro while a key is held.
            if (repeat == 0)
                monitor->PushKey(keyCode);
        }

        return event;
    }
}

HotkeyMonitor::~HotkeyMonitor()
{
    Stop();
}

void HotkeyMonitor::PushKey(int keyCode)
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_Pending.push_back(keyCode);
}

void HotkeyMonitor::PushTapDisabled()
{
    if (m_Tap != nullptr)
        CGEventTapEnable(static_cast<CFMachPortRef>(m_Tap), true);
}

std::vector<int> HotkeyMonitor::PollPressed()
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    std::vector<int> pressed;
    pressed.swap(m_Pending);
    return pressed;
}

bool HotkeyMonitor::Start()
{
    if (m_Running.load())
        return true;

    const CGEventMask mask = CGEventMaskBit(kCGEventKeyDown);
    CFMachPortRef tap = CGEventTapCreate(
        kCGSessionEventTap,
        kCGHeadInsertEventTap,
        kCGEventTapOptionListenOnly,
        mask,
        TapCallback,
        this);

    if (tap == nullptr)
        return false;

    CFRunLoopSourceRef source = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, tap, 0);
    if (source == nullptr)
    {
        CFRelease(tap);
        return false;
    }

    m_Tap = tap;
    m_Source = source;

    m_Thread = std::thread([this]()
    {
        CFRunLoopAddSource(
            CFRunLoopGetCurrent(),
            static_cast<CFRunLoopSourceRef>(m_Source),
            kCFRunLoopCommonModes);
        CGEventTapEnable(static_cast<CFMachPortRef>(m_Tap), true);

        m_Running.store(true);

        // Bounded runs instead of CFRunLoopRun() so Stop() cannot hang on a
        // run loop that has not been entered yet.
        while (m_Running.load())
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, false);
    });

    for (int i = 0; i < 500 && !m_Running.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    return m_Running.load();
}

void HotkeyMonitor::Stop()
{
    m_Running.store(false);

    if (m_Thread.joinable())
        m_Thread.join();

    if (m_Source != nullptr)
    {
        CFRelease(static_cast<CFRunLoopSourceRef>(m_Source));
        m_Source = nullptr;
    }

    if (m_Tap != nullptr)
    {
        CFRelease(static_cast<CFMachPortRef>(m_Tap));
        m_Tap = nullptr;
    }
}
