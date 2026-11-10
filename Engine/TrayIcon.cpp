#include "TrayIcon.h"

#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include <commctrl.h>
#include <shellapi.h>
#include <windowsx.h>

#include "Log.h"
#include "resource.h"

#pragma comment(lib, "comctl32")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace seentax
{
    namespace
    {
        constexpr wchar_t kClassName[] = L"SeenTaxTray";
        constexpr wchar_t kTitle[] = L"SeenTax Engine";
        constexpr UINT kIconId = 1;
        constexpr UINT kIconMessage = WM_APP + 2;
        constexpr UINT kExitCommand = 1;
        constexpr UINT kTrackingCommand = 2;

        NOTIFYICONDATAW IconData(HWND window)
        {
            NOTIFYICONDATAW data{};
            data.cbSize = sizeof(data);
            data.hWnd = window;
            data.uID = kIconId;
            return data;
        }

        template <std::size_t N>
        void CopyText(wchar_t (&destination)[N], std::wstring_view text)
        {
            const std::wstring truncated(text.substr(0, N - 1));
            wcscpy_s(destination, truncated.c_str());
        }

        struct MenuDeleter
        {
            void operator()(HMENU menu) const { DestroyMenu(menu); }
        };

        void LogNotificationEvent(UINT event)
        {
            switch (event)
            {
            case NIN_BALLOONSHOW:
                Log("         notification: Windows is showing it");
                break;
            case NIN_BALLOONHIDE:
                Log("         notification: hidden before it timed out");
                break;
            case NIN_BALLOONTIMEOUT:
                Log("         notification: timed out or closed");
                break;
            case NIN_BALLOONUSERCLICK:
                Log("         notification: clicked");
                break;
            default:
                break;
            }
        }
    }

    TrayIcon::TrayIcon(std::function<void()> onToggleTracking, std::function<void()> onExit)
        : m_onToggleTracking(std::move(onToggleTracking)), m_onExit(std::move(onExit)), m_taskbarCreated(RegisterWindowMessageW(L"TaskbarCreated"))
    {
        const HINSTANCE instance = GetModuleHandleW(nullptr);
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = WindowProc;
        windowClass.hInstance = instance;
        windowClass.lpszClassName = kClassName;
        if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        {
            throw std::runtime_error("Couldn't register the tray icon's window class");
        }

        // Never shown. A normal top-level window rather than a message-only one, because only top-level
        // windows get the TaskbarCreated broadcast.
        m_window = CreateWindowExW(0, kClassName, kTitle, WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr, instance, this);
        if (!m_window)
        {
            throw std::runtime_error("Couldn't create the tray icon's window");
        }
        if (FAILED(LoadIconMetric(instance, MAKEINTRESOURCEW(IDI_ICON1), LIM_SMALL, &m_icon)))
        {
            Log("Couldn't load the SeenTax icon, so the tray shows the standard app icon");
        }
        if (FAILED(LoadIconMetric(instance, MAKEINTRESOURCEW(IDI_ICON1), LIM_LARGE, &m_largeIcon)))
        {
            m_largeIcon = nullptr;
        }
        if (!AddIcon())
        {
            Log("No tray icon yet: the taskbar isn't running. It's added when the taskbar starts.");
        }
    }

    TrayIcon::~TrayIcon()
    {
        auto data = IconData(m_window);
        Shell_NotifyIconW(NIM_DELETE, &data);
        DestroyWindow(m_window);
        DestroyIcon(m_icon);
        DestroyIcon(m_largeIcon);
    }

    void TrayIcon::ShowState(bool trackingOn, bool cameraFailed)
    {
        m_trackingOn = trackingOn;
        m_cameraFailed = cameraFailed;
        auto data = IconData(m_window);
        data.uFlags = NIF_TIP | NIF_SHOWTIP;
        CopyText(data.szTip, Tooltip());
        Shell_NotifyIconW(NIM_MODIFY, &data);
    }

    void TrayIcon::ShowError(std::wstring_view text)
    {
        Notify(kTitle, text, NIIF_ERROR);
    }

    void TrayIcon::ShowNotice(std::wstring_view title, std::wstring_view text)
    {
        Notify(title, text, (m_largeIcon ? NIIF_USER | NIIF_LARGE_ICON : NIIF_INFO) | NIIF_NOSOUND);
    }

    void TrayIcon::Notify(std::wstring_view title, std::wstring_view text, DWORD flags)
    {
        auto data = IconData(m_window);
        data.uFlags = NIF_INFO;
        data.dwInfoFlags = flags;
        data.hBalloonIcon = m_largeIcon;
        CopyText(data.szInfoTitle, title);
        CopyText(data.szInfo, text);
        const bool accepted = Shell_NotifyIconW(NIM_MODIFY, &data) != FALSE;
        Log("         notification \"{}\": {}", winrt::to_string(title), accepted ? "sent to Windows" : "Windows didn't accept it");
    }

    bool TrayIcon::AddIcon()
    {
        auto data = IconData(m_window);
        data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
        data.uCallbackMessage = kIconMessage;
        data.hIcon = m_icon ? m_icon : LoadIconW(nullptr, IDI_APPLICATION);
        CopyText(data.szTip, Tooltip());
        if (!Shell_NotifyIconW(NIM_ADD, &data))
        {
            return false;
        }
        // Version 4 reports clicks as NIN_SELECT and WM_CONTEXTMENU, with the position in wParam.
        data.uVersion = NOTIFYICON_VERSION_4;
        return Shell_NotifyIconW(NIM_SETVERSION, &data) != FALSE;
    }

    std::wstring TrayIcon::Tooltip() const
    {
        if (!m_trackingOn)
        {
            return L"SeenTax Engine: hand tracking off";
        }
        return m_cameraFailed ? L"SeenTax Engine: the camera didn't start" : L"SeenTax Engine: hand tracking on";
    }

    void TrayIcon::ShowMenu(POINT at)
    {
        const std::unique_ptr<std::remove_pointer_t<HMENU>, MenuDeleter> menu(CreatePopupMenu());
        AppendMenuW(menu.get(), MF_STRING | (m_trackingOn ? MF_CHECKED : MF_UNCHECKED), kTrackingCommand, L"Hand tracking");
        AppendMenuW(menu.get(), MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu.get(), MF_STRING, kExitCommand, L"Exit");
        // Without these two the menu doesn't close when you click somewhere else (documented for TrackPopupMenu).
        SetForegroundWindow(m_window);
        TrackPopupMenuEx(menu.get(), TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, at.x, at.y, m_window, nullptr);
        PostMessageW(m_window, WM_NULL, 0, 0);
    }

    LRESULT CALLBACK TrayIcon::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_NCCREATE)
        {
            auto* creator = static_cast<TrayIcon*>(reinterpret_cast<const CREATESTRUCTW*>(lParam)->lpCreateParams);
            creator->m_window = window;
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(creator));
        }
        auto* self = reinterpret_cast<TrayIcon*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        return self ? self->HandleMessage(message, wParam, lParam) : DefWindowProcW(window, message, wParam, lParam);
    }

    LRESULT TrayIcon::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == kIconMessage)
        {
            const UINT event = LOWORD(lParam);
            if (event == NIN_SELECT || event == NIN_KEYSELECT || event == WM_CONTEXTMENU)
            {
                ShowMenu({ GET_X_LPARAM(wParam), GET_Y_LPARAM(wParam) });
            }
            LogNotificationEvent(event);
            return 0;
        }
        if (message == m_taskbarCreated)
        {
            AddIcon();
            return 0;
        }
        if (message == WM_COMMAND && LOWORD(wParam) == kExitCommand)
        {
            m_onExit();
            return 0;
        }
        if (message == WM_COMMAND && LOWORD(wParam) == kTrackingCommand)
        {
            m_onToggleTracking();
            return 0;
        }
        return DefWindowProcW(m_window, message, wParam, lParam);
    }
}
