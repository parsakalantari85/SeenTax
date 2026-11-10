#pragma once

#include <atomic>
#include <functional>
#include <optional>
#include <stop_token>
#include <thread>

#include "Cursor.h"
#include "LatestValue.h"
#include "PointerController.h"

namespace seentax
{
    // Moves the real cursor on its own thread, once per display refresh, and turns pinches into mouse
    // buttons (see PointerController). Sleeps while there's no hand to follow.
    class CursorDriver
    {
    public:
        // With `dryRun` it decides everything but never touches the mouse (--no-cursor).
        CursorDriver(bool dryRun, std::function<void()> onScreenshot);

        // Called by the tracking thread for every camera frame.
        void Publish(const PointerSample& sample) { m_samples.Put(sample); }

        void SetGestures(const GestureMap& gestures) { m_gestures.Put(gestures); }

        // The newest gesture decided since the last call, for the preview.
        std::optional<GestureEvent> TakeEvent() { return m_events.Take(); }

        DesktopSize Desktop() const { return m_desktop; }
        int RefreshRateHz() const { return m_refreshRateHz; }

        // Counts since the last call, for the status line.
        struct Activity
        {
            int updates = 0;
            int leftClicks = 0;
            int rightClicks = 0;
            int doubleClicks = 0;
            int holds = 0;
            int windowsClosed = 0;
            int screenshots = 0;
            int wheelNotches = 0;
        };
        Activity TakeActivity()
        {
            return { m_updates.exchange(0), m_leftClicks.exchange(0), m_rightClicks.exchange(0), m_doubleClicks.exchange(0),
                     m_holds.exchange(0), m_windowsClosed.exchange(0), m_screenshots.exchange(0), m_wheelNotches.exchange(0) };
        }

    private:
        void Run(std::stop_token stop);
        void Execute(const PointerCommand& command);
        void Move(Point2 desktopFraction, bool evenIfSamePixel);
        void Apply(ButtonAction button);
        void Scroll(int wheelNotches);
        void Perform(SystemAction action);
        void CloseFocused();
        void Screenshot();
        void Report(const PointerCommand& command);

        const bool m_dryRun;
        const std::function<void()> m_onScreenshot;
        const DesktopSize m_desktop;
        const int m_refreshRateHz;
        LatestValue<PointerSample> m_samples;
        LatestValue<GestureMap> m_gestures;
        LatestValue<GestureEvent> m_events;
        PointerController m_controller; // driver thread only
        std::optional<Point2> m_lastMovePixel;
        bool m_leftDown = false;
        std::atomic<int> m_updates{ 0 };
        std::atomic<int> m_leftClicks{ 0 };
        std::atomic<int> m_rightClicks{ 0 };
        std::atomic<int> m_doubleClicks{ 0 };
        std::atomic<int> m_holds{ 0 };
        std::atomic<int> m_windowsClosed{ 0 };
        std::atomic<int> m_screenshots{ 0 };
        std::atomic<int> m_wheelNotches{ 0 };
        std::jthread m_thread; // last: it starts in the constructor and uses everything above
    };
}
