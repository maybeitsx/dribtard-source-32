#include "Platform/Hotkeys.hpp"

#import <ApplicationServices/ApplicationServices.h>

#include <chrono>

#include <unistd.h>

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
            // The system disables a slow tap; ask it back instead of silently
            // losing every hotkey.
            monitor->PushTapDisabled();
            return event;
        }

        /*
            Ignore events this app posted itself. The macro injects its keys at
            kCGHIDEventTap, which this session tap sees, so without this filter
            a parry arrives a second time as a fresh key press and can fire the
            macro again: that is the double F a curve parry showed.
        */
        const pid_t sourcePid = static_cast<pid_t>(
            CGEventGetIntegerValueField(event, kCGEventSourceUnixProcessID));
        if (sourcePid == getpid())
            return event;

        if (type == kCGEventKeyDown)
        {
            const int keyCode = static_cast<int>(
                CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode));

            // PushKey() queues only the transition, so the auto-repeat that a
            // held key sends does not retrigger the macro.
            monitor->PushKey(keyCode);
        }
        else if (type == kCGEventKeyUp)
        {
            const int keyCode = static_cast<int>(
                CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode));

            monitor->PushKeyUp(keyCode);
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
    if (keyCode < 0 || keyCode > 255)
        return;

    std::lock_guard<std::mutex> lock(m_Mutex);

    const bool wasDown = m_Down[keyCode];
    m_Down[keyCode] = true;

    // The tap's auto-repeat repeats the down event; only the transition is a
    // press, exactly like a physical key going down once.
    if (!wasDown)
        m_Pending.push_back(keyCode);
}

void HotkeyMonitor::PushKeyUp(int keyCode)
{
    if (keyCode < 0 || keyCode > 255)
        return;

    std::lock_guard<std::mutex> lock(m_Mutex);
    m_Down[keyCode] = false;
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

bool HotkeyMonitor::IsKeyDown(int keyCode) const
{
    if (keyCode < 0 || keyCode > 255)
        return false;

    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_Down[keyCode];
}

bool HotkeyMonitor::Start()
{
    if (m_Running.load())
        return true;

    /*
        A previous attempt can leave a thread behind when the tap never
        reported in (the usual case before Accessibility is granted). Retire
        it before replacing the member, or assigning to a joinable
        std::thread ends the process.
    */
    Stop();

    // Key-up is needed as much as key-down: it ends an auto-repeat and
    // releases the clicker when its hold key comes up.
    const CGEventMask mask =
        CGEventMaskBit(kCGEventKeyDown) | CGEventMaskBit(kCGEventKeyUp);

    CFMachPortRef tap = CGEventTapCreate(
        kCGSessionEventTap,
        kCGHeadInsertEventTap,
        kCGEventTapOptionListenOnly,
        mask,
        TapCallback,
        this);

    /*
        Input Monitoring is only what a listen-only tap needs. An active tap is
        covered by Accessibility, which this app already asks for and which
        macro users have usually granted, so fall back to it instead of leaving
        every hotkey dead for somebody who never found the Input Monitoring
        pane. The callback returns each event untouched, so the game and the
        rest of the system see exactly the same keys as before.
    */
    if (tap == nullptr)
    {
        tap = CGEventTapCreate(
            kCGSessionEventTap,
            kCGHeadInsertEventTap,
            kCGEventTapOptionDefault,
            mask,
            TapCallback,
            this);
    }

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

    const bool running = m_Running.load();

    if (!running)
        Stop();

    return running;
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

    std::lock_guard<std::mutex> lock(m_Mutex);
    m_Pending.clear();
    m_Down.fill(false);
}
