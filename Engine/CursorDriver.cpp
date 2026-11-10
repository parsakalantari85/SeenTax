#include "CursorDriver.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <format>
#include <ratio>
#include <stdexcept>
#include <string>
#include <utility>

#include <winrt/base.h>

#include "Log.h"
#include "SystemActions.h"
#include "Timing.h"
#include "WindowsHeaders.h"

namespace seentax
{
    namespace
    {
        constexpr auto kIdleWait = std::chrono::milliseconds(500);

        winrt::handle CreateHighResolutionTimer()
        {
            winrt::handle timer{ CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS) };
            if (!timer)
            {
                throw std::runtime_error("Couldn't create a high-resolution timer");
            }
            return timer;
        }

        void SleepUntil(const winrt::handle& timer, Clock::time_point wakeAt)
        {
            const auto wait = wakeAt - Clock::now();
            if (wait <= Clock::duration::zero())
            {
                return;
            }
            LARGE_INTEGER dueTime{};
            // Negative means relative to now, in 100 ns units.
            dueTime.QuadPart = -std::chrono::duration_cast<std::chrono::duration<long long, std::ratio<1, 10'000'000>>>(wait).count();
            SetWaitableTimer(timer.get(), &dueTime, 0, nullptr, nullptr, FALSE);
            WaitForSingleObject(timer.get(), INFINITE);
        }

        bool SamePixel(Point2 a, Point2 b)
        {
            return std::lround(a.x) == std::lround(b.x) && std::lround(a.y) == std::lround(b.y);
        }
    }

    CursorDriver::CursorDriver(bool dryRun, std::function<void()> onScreenshot)
        : m_dryRun(dryRun),
          m_onScreenshot(std::move(onScreenshot)),
          m_desktop(VirtualDesktopSize()),
          m_refreshRateHz(MainDisplayRefreshRate()),
          m_thread([this](std::stop_token stop) { Run(stop); })
    {
    }

    void CursorDriver::Run(std::stop_token stop)
    {
        try
        {
            const auto timer = CreateHighResolutionTimer();
            const auto period = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / m_refreshRateHz));
            auto nextTick = Clock::now();
            while (!stop.stop_requested())
            {
                if (const auto gestures = m_gestures.Take())
                {
                    m_controller.SetGestures(*gestures);
                }
                std::optional<PointerSample> sample;
                if (m_controller.Active())
                {
                    nextTick = std::max(nextTick + period, Clock::now());
                    SleepUntil(timer, nextTick);
                    sample = m_samples.Take();
                    ++m_updates;
                }
                else
                {
                    // Nothing to move: wait for the tracker instead of ticking idly.
                    sample = m_samples.WaitAndTake(stop, kIdleWait);
                    nextTick = Clock::now();
                }
                Execute(m_controller.Update(sample, Clock::now()));
            }
        }
        catch (const std::exception& error)
        {
            Log("Error in the cursor thread: {}", error.what());
        }
        if (m_leftDown)
        {
            Apply(ButtonAction::LeftUp); // never leave the button down
        }
    }

    void CursorDriver::Execute(const PointerCommand& command)
    {
        const bool usesButton = command.button != ButtonAction::None;
        if (command.moveTo)
        {
            // Before a button action, move even to the same pixel: the real mouse may have been nudged since.
            Move(*command.moveTo, usesButton);
        }
        if (usesButton)
        {
            Apply(command.button);
        }
        if (command.wheelNotches != 0)
        {
            Scroll(command.wheelNotches);
        }
        if (command.event != GestureEvent::None)
        {
            Report(command);
        }
        if (command.system != SystemAction::None)
        {
            Perform(command.system);
        }
    }

    void CursorDriver::Apply(ButtonAction button)
    {
        bool (*send)() = nullptr;
        switch (button)
        {
        case ButtonAction::LeftClick:
            ++m_leftClicks;
            send = ClickLeftButton;
            break;
        case ButtonAction::RightClick:
            ++m_rightClicks;
            send = ClickRightButton;
            break;
        case ButtonAction::DoubleClick:
            ++m_doubleClicks;
            send = DoubleClickLeftButton;
            break;
        case ButtonAction::LeftDown:
            ++m_holds;
            m_leftDown = true;
            send = PressLeftButton;
            break;
        case ButtonAction::LeftUp:
            m_leftDown = false;
            send = ReleaseLeftButton;
            break;
        case ButtonAction::None:
            break;
        }
        if (send && !m_dryRun)
        {
            send();
        }
    }

    void CursorDriver::Scroll(int wheelNotches)
    {
        m_wheelNotches += std::abs(wheelNotches);
        if (!m_dryRun && !TurnWheel(wheelNotches))
        {
            Log("         scroll: Windows didn't accept the wheel turn");
        }
    }

    void CursorDriver::Perform(SystemAction action)
    {
        switch (action)
        {
        case SystemAction::CloseWindow:
            CloseFocused();
            break;
        case SystemAction::Screenshot:
            Screenshot();
            break;
        case SystemAction::None:
            break;
        }
    }

    void CursorDriver::Screenshot()
    {
        ++m_screenshots;
        if (m_dryRun)
        {
            return;
        }
        if (!TakeScreenshot())
        {
            Log("         screenshot: Windows didn't accept the key presses");
            return;
        }
        m_onScreenshot();
    }

    void CursorDriver::CloseFocused()
    {
        ++m_windowsClosed;
        if (m_dryRun)
        {
            return;
        }
        switch (CloseFocusedWindow())
        {
        case CloseResult::NoWindow:
            Log("         close window: no window has the focus");
            break;
        case CloseResult::ShellWindow:
            Log("         close window: skipped, the desktop or the taskbar has the focus");
            break;
        case CloseResult::OwnWindow:
            Log("         close window: skipped, the Engine's own window has the focus");
            break;
        case CloseResult::Refused:
            Log("         close window: Windows refused, the app may run as administrator");
            break;
        case CloseResult::Sent:
            break;
        }
    }

    void CursorDriver::Report(const PointerCommand& command)
    {
        m_events.Put(command.event);
        std::string line = std::format("         -> {}", Describe(command.event));
        if (command.moveTo && command.button != ButtonAction::None)
        {
            const auto pixel = ToDesktopPixels(*command.moveTo, m_desktop);
            line += std::format(" at ({:.0f}, {:.0f}){}", pixel.x, pixel.y, m_dryRun ? " (dry run: not sent)" : "");
        }
        else if (command.system != SystemAction::None && m_dryRun)
        {
            line += " (dry run: not sent)";
        }
        Log("{}", line);
    }

    void CursorDriver::Move(Point2 desktopFraction, bool evenIfSamePixel)
    {
        // A still hand shouldn't flood Windows with moves to the same pixel.
        const auto pixel = ToDesktopPixels(desktopFraction, m_desktop);
        if (!evenIfSamePixel && m_lastMovePixel && SamePixel(*m_lastMovePixel, pixel))
        {
            return;
        }
        m_lastMovePixel = pixel;
        if (!m_dryRun)
        {
            MoveCursorTo(desktopFraction);
        }
    }
}
