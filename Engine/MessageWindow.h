#pragma once

#include <functional>
#include <utility>

#include "WindowsHeaders.h"

namespace seentax
{
    class MessageWindow
    {
    public:
        using Handler = std::function<void(UINT message, WPARAM wParam)>;

        MessageWindow();
        ~MessageWindow();
        MessageWindow(const MessageWindow&) = delete;
        MessageWindow& operator=(const MessageWindow&) = delete;

        HWND Handle() const { return m_window; }
        void OnMessage(Handler handler) { m_handler = std::move(handler); }

    private:
        static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

        Handler m_handler;
        HWND m_window = nullptr;
    };

    void PostToEngine(UINT message);

    class ScopedHotkey
    {
    public:
        ScopedHotkey(HWND window, int id, UINT modifiers, UINT key);
        ~ScopedHotkey();
        ScopedHotkey(const ScopedHotkey&) = delete;
        ScopedHotkey& operator=(const ScopedHotkey&) = delete;

        bool Registered() const { return m_registered; }

    private:
        HWND m_window;
        int m_id;
        bool m_registered;
    };
}
