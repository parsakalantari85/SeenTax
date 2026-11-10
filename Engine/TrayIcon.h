#pragma once

#include <functional>
#include <string>
#include <string_view>

#include "WindowsHeaders.h"

namespace seentax
{
    class TrayIcon
    {
    public:
        TrayIcon(std::function<void()> onToggleTracking, std::function<void()> onExit);
        ~TrayIcon();
        TrayIcon(const TrayIcon&) = delete;
        TrayIcon& operator=(const TrayIcon&) = delete;

        void ShowState(bool trackingOn, bool cameraFailed);
        void ShowError(std::wstring_view text);
        void ShowNotice(std::wstring_view title, std::wstring_view text);

    private:        
        static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
        LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
        bool AddIcon();
        void Notify(std::wstring_view title, std::wstring_view text, DWORD flags);
        std::wstring Tooltip() const;
        void ShowMenu(POINT at);

        std::function<void()> m_onToggleTracking;
        std::function<void()> m_onExit;
        UINT m_taskbarCreated;
        HWND m_window = nullptr;
        HICON m_icon = nullptr;
        HICON m_largeIcon = nullptr;
        bool m_trackingOn = true;
        bool m_cameraFailed = false;
    };
}
