// The shell's taskbar window. With the tray host on (shell/tray_host.h) Atrium has
// a window of the same class too, above Explorer's so that apps' tray calls reach
// it: every lookup of Explorer's taskbar goes through here, never FindWindow.
#pragma once

#include <windows.h>
#include <shellapi.h>

#include <atomic>

namespace atrium {

// The Shell_TrayWnd of the shell's process (the desktop's), else the first one of
// another process than `skip` (default: this one). No allocation: the crash
// handler and the guard use it.
inline HWND ShellTaskbar(DWORD skip = GetCurrentProcessId())
{
    DWORD shell = 0;
    if (HWND desktop = GetShellWindow())
        GetWindowThreadProcessId(desktop, &shell);
    HWND other = nullptr;
    for (HWND h = nullptr; (h = FindWindowExW(nullptr, h, L"Shell_TrayWnd", nullptr)) != nullptr;) {
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        if (shell && pid == shell)
            return h;
        if (pid != skip && !other)
            other = h;
    }
    return other;
}

// Set while Atrium itself asks the shell where a tray icon is (the tray reader):
// the tray host passes such questions to Explorer instead of answering them.
inline std::atomic_int g_shellQuestions{0};
struct ShellQuestion {
    ShellQuestion() { ++g_shellQuestions; }
    ~ShellQuestion() { --g_shellQuestions; }
};

// Above 0 while Atrium's tray host steps behind Explorer's taskbar for one of Atrium's own
// app-bar calls (AppBarMessage): it does not move itself back in front meanwhile.
inline std::atomic_int g_hostAside{0};

// SHAppBarMessage for Atrium's own calls. With Atrium's tray host first among the
// Shell_TrayWnd windows, Windows hands the call to a window of the caller's own
// process with an in-process pointer, which Explorer cannot read. The state calls
// carry everything in the message itself: they go to Explorer directly then, in
// the shell's wire format (APPBARMSGDATAV3, handles as 32-bit values).
inline UINT_PTR AppBarMessage(DWORD message, APPBARDATA& abd)
{
    DWORD first = 0;
    GetWindowThreadProcessId(FindWindowW(L"Shell_TrayWnd", nullptr), &first);
    HWND shell = ShellTaskbar();
    if (first != GetCurrentProcessId() || !shell)
        return SHAppBarMessage(message, &abd);
    if (message != ABM_GETSTATE && message != ABM_SETSTATE) {
        // Calls that answer through shared memory (an app bar's place): the host steps behind
        // Explorer's taskbar for the moment of the call, which then goes to Explorer as usual.
        HWND host = FindWindowW(L"Shell_TrayWnd", nullptr);
        ++g_hostAside;
        SetWindowPos(host, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        const UINT_PTR result = SHAppBarMessage(message, &abd);
        SetWindowPos(host, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        --g_hostAside;
        return result;
    }
    struct AppBarData32 {
        DWORD cbSize;
        DWORD hWnd;
        UINT uCallbackMessage;
        UINT uEdge;
        RECT rc;
        INT64 lParam;
    };
    struct AppBarMessage32 {
        AppBarData32 abd;
        DWORD dwMessage;
        DWORD padding1;
        UINT64 hSharedMemory;
        DWORD dwSourceProcessId;
        DWORD padding2;
    };
    static_assert(sizeof(AppBarData32) == 40 && sizeof(AppBarMessage32) == 64);
    AppBarMessage32 data{};
    data.abd.cbSize = sizeof(AppBarData32);
    data.abd.hWnd = DWORD(reinterpret_cast<ULONG_PTR>(abd.hWnd));
    data.abd.uCallbackMessage = abd.uCallbackMessage;
    data.abd.uEdge = abd.uEdge;
    data.abd.rc = abd.rc;
    data.abd.lParam = INT64(abd.lParam);
    data.dwMessage = message;
    data.dwSourceProcessId = GetCurrentProcessId();
    COPYDATASTRUCT cds{0, sizeof(data), &data};
    DWORD_PTR result = 0;
    if (!SendMessageTimeoutW(shell, WM_COPYDATA, reinterpret_cast<WPARAM>(abd.hWnd), reinterpret_cast<LPARAM>(&cds),
                             SMTO_ABORTIFHUNG, 4000, &result))
        return 0;
    return result;
}

} // namespace atrium
