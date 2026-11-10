#include "Cursor.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "WindowsHeaders.h"

namespace seentax
{
    namespace
    {
        // SendInput's absolute coordinates run 0..65535 across the desktop, whatever its size.
        constexpr float kAbsoluteMax = 65535.0f;

        LONG ToAbsolute(float fraction)
        {
            return static_cast<LONG>(std::lround(std::clamp(fraction, 0.0f, 1.0f) * kAbsoluteMax));
        }

        bool SendMouseInput(DWORD flags, LONG dx, LONG dy)
        {
            INPUT input{};
            input.type = INPUT_MOUSE;
            input.mi.dx = dx;
            input.mi.dy = dy;
            input.mi.dwFlags = flags;
            return SendInput(1, &input, sizeof(input)) == 1;
        }

        // One SendInput call, so no other input can slip in between press and release.
        template <std::size_t N>
        bool SendButtons(const std::array<DWORD, N>& flags)
        {
            std::array<INPUT, N> inputs{};
            for (std::size_t i = 0; i < N; ++i)
            {
                inputs[i].type = INPUT_MOUSE;
                inputs[i].mi.dwFlags = flags[i];
            }
            return SendInput(static_cast<UINT>(N), inputs.data(), sizeof(INPUT)) == N;
        }

        float LastPixel(int size)
        {
            return static_cast<float>(std::max(size - 1, 1));
        }
    }

    DesktopSize VirtualDesktopSize()
    {
        return { GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN) };
    }

    int MainDisplayRefreshRate()
    {
        DEVMODEW mode{};
        mode.dmSize = sizeof(mode);
        // 0 and 1 mean "hardware default", which doesn't say how fast.
        if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode) && mode.dmDisplayFrequency > 1)
        {
            return static_cast<int>(mode.dmDisplayFrequency);
        }
        return 60;
    }

    // Fraction 1 is the last pixel, as 65535 is for SendInput.
    Point2 ToDesktopPixels(Point2 fraction, DesktopSize desktop)
    {
        return { fraction.x * LastPixel(desktop.width), fraction.y * LastPixel(desktop.height) };
    }

    Point2 ToDesktopFraction(Point2 pixels, DesktopSize desktop)
    {
        return { pixels.x / LastPixel(desktop.width), pixels.y / LastPixel(desktop.height) };
    }

    std::optional<Point2> CursorPosition(DesktopSize desktop)
    {
        POINT point{};
        if (!GetCursorPos(&point))
        {
            return std::nullopt;
        }
        const Point2 pixels{ static_cast<float>(point.x - GetSystemMetrics(SM_XVIRTUALSCREEN)),
                             static_cast<float>(point.y - GetSystemMetrics(SM_YVIRTUALSCREEN)) };
        return ToDesktopFraction(pixels, desktop);
    }

    bool MoveCursorTo(Point2 desktopFraction)
    {
        // VIRTUALDESK: 0..65535 spans all monitors, not just the main one.
        return SendMouseInput(MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK, ToAbsolute(desktopFraction.x),
                              ToAbsolute(desktopFraction.y));
    }

    bool ClickLeftButton()
    {
        return SendButtons(std::array<DWORD, 2>{ MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP });
    }

    bool ClickRightButton()
    {
        return SendButtons(std::array<DWORD, 2>{ MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP });
    }

    bool DoubleClickLeftButton()
    {
        return SendButtons(std::array<DWORD, 4>{ MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP, MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP });
    }

    bool PressLeftButton()
    {
        return SendMouseInput(MOUSEEVENTF_LEFTDOWN, 0, 0);
    }

    bool ReleaseLeftButton()
    {
        return SendMouseInput(MOUSEEVENTF_LEFTUP, 0, 0);
    }

    bool TurnWheel(int notches)
    {
        INPUT input{};
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = MOUSEEVENTF_WHEEL;
        input.mi.mouseData = static_cast<DWORD>(notches * WHEEL_DELTA);
        return SendInput(1, &input, sizeof(input)) == 1;
    }
}
