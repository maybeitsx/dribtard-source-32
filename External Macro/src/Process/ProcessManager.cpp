#include "ProcessManager.hpp"

#include <tlhelp32.h>

ProcessManager::ProcessManager()
    : m_hProcess(nullptr),
      m_ProcessId(0),
      m_hTargetWindow(nullptr)
{
}

ProcessManager::~ProcessManager()
{
    Detach();
}

bool ProcessManager::Attach(
    const std::string& processName)
{
    if (m_hProcess)
        Detach();

    m_ProcessId = GetProcessIdByName(processName);

    if (m_ProcessId == 0)
        return false;

    // The app only needs a process handle to detect when the target exits.
    // SYNCHRONIZE is sufficient; it does not grant access to target memory.
    m_hProcess = OpenProcess(SYNCHRONIZE, FALSE, m_ProcessId);
    if (!m_hProcess)
    {
        m_ProcessId = 0;
        return false;
    }

    m_hTargetWindow = nullptr;

    EnumWindows(
        EnumWindowsCallback,
        reinterpret_cast<LPARAM>(this)
    );

    return true;
}

void ProcessManager::Detach()
{
    if (m_hProcess)
    {
        CloseHandle(m_hProcess);
        m_hProcess = nullptr;
    }

    m_ProcessId = 0;
    m_hTargetWindow = nullptr;
}

bool ProcessManager::IsRunning() const
{
    return m_hProcess && WaitForSingleObject(m_hProcess, 0) == WAIT_TIMEOUT;
}

bool ProcessManager::IsTargetFocused() const
{
    if (!IsRunning() || m_ProcessId == 0)
        return false;

    const HWND foreground = GetForegroundWindow();

    if (foreground == nullptr)
        return false;

    DWORD foregroundPid = 0;
    GetWindowThreadProcessId(foreground, &foregroundPid);

    return foregroundPid != 0 && foregroundPid == m_ProcessId;
}

DWORD ProcessManager::GetProcessIdByName(
    const std::string& name)
{
    DWORD pid = 0;

    HANDLE snapshot =
        CreateToolhelp32Snapshot(
            TH32CS_SNAPPROCESS,
            0
        );

    if (snapshot == INVALID_HANDLE_VALUE)
        return 0;

    PROCESSENTRY32 entry{};
    entry.dwSize = sizeof(entry);

    if (Process32First(
        snapshot,
        &entry))
    {
        do
        {
            if (_stricmp(
                entry.szExeFile,
                name.c_str()) == 0)
            {
                pid = entry.th32ProcessID;
                break;
            }

        } while (Process32Next(
            snapshot,
            &entry));
    }

    CloseHandle(snapshot);

    return pid;
}

BOOL CALLBACK
ProcessManager::EnumWindowsCallback(
    HWND hwnd,
    LPARAM lParam)
{
    auto* manager =
        reinterpret_cast<ProcessManager*>(
            lParam
        );

    DWORD windowPid = 0;

    GetWindowThreadProcessId(
        hwnd,
        &windowPid
    );

    if (windowPid != manager->m_ProcessId)
        return TRUE;

    if (!IsWindowVisible(hwnd))
        return TRUE;

    manager->m_hTargetWindow = hwnd;

    return FALSE;
}