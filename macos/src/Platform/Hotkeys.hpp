#pragma once

#include <array>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

/*
    Global key monitor built on a listen-only CGEventTap.

    A listen-only tap never blocks the key, so typing in the game is unaffected.
    The tap requires Accessibility permission; Start() returns false when the
    system refuses to create it, and the app keeps working without hotkeys -
    and keeps retrying, so granting the permission later starts the hotkeys
    without a relaunch.
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

    /*
        True while the key is physically down, from the tap's own key events.

        MacInput::IsKeyDown asks CGEventSourceKeyState the same question, but
        that needs the same Accessibility / Input Monitoring grant the tap
        needs and can answer false for keys aimed at the game. The tap sees
        every key whatever is frontmost, so the clicker's hold key reads
        it from here whenever the tap is running.
    */
    bool IsKeyDown(int keyCode) const;

    // Called from the event tap.
    void PushKey(int keyCode);
    void PushKeyUp(int keyCode);
    void PushTapDisabled();

private:
    mutable std::mutex m_Mutex;
    std::vector<int> m_Pending;
    std::array<bool, 256> m_Down{};

    void* m_Tap = nullptr;      // CFMachPortRef
    void* m_Source = nullptr;   // CFRunLoopSourceRef
    std::thread m_Thread;
    std::atomic<bool> m_Running{ false };
};
