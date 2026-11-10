#include "SystemActions.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>

#include "WindowsHeaders.h"

namespace seentax
{
    namespace
    {
        constexpr std::array<std::wstring_view, 4> kShellWindowClasses{ L"Progman", L"WorkerW", L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd" };

        INPUT KeyEvent(WORD key, bool release)
        {
            INPUT input{};
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = key;
            input.ki.dwFlags = KEYEVENTF_EXTENDEDKEY | (release ? KEYEVENTF_KEYUP : 0);
            return input;
        }

        bool IsShellWindow(HWND window)
        {
            if (window == GetShellWindow())
            {
                return true;
            }
            std::array<wchar_t, 64> className{};
            const int length = GetClassNameW(window, className.data(), static_cast<int>(className.size()));
            const std::wstring_view name(className.data(), static_cast<std::size_t>(std::max(length, 0)));
            return std::ranges::find(kShellWindowClasses, name) != kShellWindowClasses.end();
        }

        bool IsOwnWindow(HWND window)
        {
            DWORD process = 0;
            GetWindowThreadProcessId(window, &process);
            return process == GetCurrentProcessId();
        }
    }

    CloseResult CloseFocusedWindow()
    {
        const HWND window = GetForegroundWindow();
        if (!window)
        {
            return CloseResult::NoWindow;
        }
        if (IsShellWindow(window))
        {
            return CloseResult::ShellWindow;
        }
        if (IsOwnWindow(window))
        {
            return CloseResult::OwnWindow;
        }
        return PostMessageW(window, WM_SYSCOMMAND, SC_CLOSE, 0) ? CloseResult::Sent : CloseResult::Refused;
    }

    bool TakeScreenshot()
    {
        std::array inputs{ KeyEvent(VK_LWIN, false), KeyEvent(VK_SNAPSHOT, false), KeyEvent(VK_SNAPSHOT, true), KeyEvent(VK_LWIN, true) };
        return SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT)) == inputs.size();
    }
}
