#pragma once

#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <string>

class ProcessManager
{
public:
    ProcessManager();
    ~ProcessManager();

    bool Attach(const std::string& processName);
    void Detach();

    bool IsRunning() const;

    // True only when the foreground window belongs to the attached target
    // process. Used to gate the macro trigger key so typing in other
    // windows cannot start the macro.
    bool IsTargetFocused() const;

    HANDLE GetHandle() const
    {
        return m_hProcess;
    }

    DWORD GetProcessId() const
    {
        return m_ProcessId;
    }

    HWND GetTargetWindow() const
    {
        return m_hTargetWindow;
    }

private:
    HANDLE m_hProcess;
    DWORD m_ProcessId;
    HWND m_hTargetWindow;

    DWORD GetProcessIdByName(
        const std::string& name);

    static BOOL CALLBACK EnumWindowsCallback(
        HWND hwnd,
        LPARAM lParam);
};