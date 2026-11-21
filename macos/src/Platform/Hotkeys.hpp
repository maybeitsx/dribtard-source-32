#pragma once

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

/*
    Global key monitor built on a listen-only CGEventTap.

    A listen-only tap never blocks the key, so typing in the game is unaffected.
    The tap requires Accessibility permission; Start() returns false when the
    system refuses to create it, and the app keeps working without hotkeys.
*/
class HotkeyMonitor
{
public:
    HotkeyMonitor() = default;
    ~HotkeyMonitor();

    HotkeyMonitor(const HotkeyMonitor&) = delete;
    HotkeyMonitor& operator=(const HotkeyMonitor&) = delete;

    bool Start();
    void Stop();
    bool Running() const { return m_Running.load(); }

    // Takes the keys that went down since the last call.
    std::vector<int> PollPressed();

    // Called from the event tap.
    void PushKey(int keyCode);
    void PushTapDisabled();

private:
    std::mutex m_Mutex;
    std::vector<int> m_Pending;

    void* m_Tap = nullptr;      // CFMachPortRef
    void* m_Source = nullptr;   // CFRunLoopSourceRef
    std::thread m_Thread;
    std::atomic<bool> m_Running{ false };
};
