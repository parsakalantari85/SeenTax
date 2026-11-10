#include "MessageWindow.h"

#include <atomic>
#include <stdexcept>

namespace seentax
{
    namespace
    {
        constexpr wchar_t kMessageWindowClass[] = L"SeenTaxEngine";

        std::atomic<HWND> g_messageWindow{ nullptr };
    }

    MessageWindow::MessageWindow()
    {
        const HINSTANCE instance = GetModuleHandleW(nullptr);
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = WindowProc;
        windowClass.hInstance = instance;
        windowClass.lpszClassName = kMessageWindowClass;
        if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        {
            throw std::runtime_error("Couldn't register the Engine's message window class");
        }
        m_window = CreateWindowExW(0, kMessageWindowClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, this);
        if (!m_window)
        {
            throw std::runtime_error("Couldn't create the Engine's message window");
        }
        g_messageWindow = m_window;
    }

    MessageWindow::~MessageWindow()
    {
        g_messageWindow = nullptr;
        DestroyWindow(m_window);
    }

    LRESULT CALLBACK MessageWindow::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_NCCREATE)
        {
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<const CREATESTRUCTW*>(lParam)->lpCreateParams));
        }
        const auto* self = reinterpret_cast<const MessageWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (self && self->m_handler && (message == WM_TIMER || message == WM_HOTKEY || message >= WM_APP))
        {
            self->m_handler(message, wParam);
            return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    void PostToEngine(UINT message)
    {
        PostMessageW(g_messageWindow, message, 0, 0);
    }

    ScopedHotkey::ScopedHotkey(HWND window, int id, UINT modifiers, UINT key)
        : m_window(window), m_id(id), m_registered(RegisterHotKey(window, id, modifiers, key) != FALSE)
    {
    }

    ScopedHotkey::~ScopedHotkey()
    {
        if (m_registered)
        {
            UnregisterHotKey(m_window, m_id);
        }
    }
}
