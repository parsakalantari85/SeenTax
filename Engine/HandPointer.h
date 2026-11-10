#pragma once

#include <array>
#include <optional>
#include <string_view>

#include "Camera.h"
#include "Cursor.h"
#include "CursorMapping.h"
#include "Fist.h"
#include "GestureLog.h"
#include "HandTracker.h"
#include "ImagePreprocessing.h"
#include "OneEuroFilter.h"
#include "Pinch.h"
#include "PointerController.h"
#include "Settings.h"
#include "Timing.h"

namespace seentax
{
    struct PointerSettings
    {
        ActiveArea area = kDefaultActiveArea;
        std::array<bool, kPinchFingerCount> fingersUsed{};
        bool fistUsed = false;
    };

    PointerSettings PointerSettingsFor(const Settings& settings, Point2 center);

    bool MakesFrame(const TrackedFrame& tracked);

    class HandPointer
    {
    public:
        explicit HandPointer(DesktopSize desktop);

        PointerSample Update(const TrackedFrame& tracked, const CameraFrame& frame, const PointerSettings& settings);
        PointerSample Lose(Clock::time_point at, std::string_view why);

        PinchFinger ShownFinger() const { return m_shownFinger; }
        const PinchDetector& ShownDetector() const { return Detector(m_shownFinger); }
        const FistDetector& Fist() const { return m_fist; }
        bool Framed() const { return m_framed; }

        struct Jitter
        {
            double raw = 0.0;
            double smoothed = 0.0;
            double pointer = 0.0;
        };
        std::optional<Jitter> TakeJitter();

    private:
        struct Positions
        {
            Point2 raw;
            Point2 smoothed;
            Point2 pointer;
        };

        const PinchDetector& Detector(PinchFinger finger) const { return m_pinches[SlotOf(finger)]; }
        PinchFinger ChooseShownFinger(const std::array<bool, kPinchFingerCount>& used) const;
        bool Closer(PinchFinger finger, PinchFinger than) const;
        void MeasureJitter(Point2 raw, Point2 smoothed, Point2 pointer);
        double SquaredPixelDistance(Point2 a, Point2 b) const;

        DesktopSize m_desktop;
        PointFilter m_filter;
        PrecisionPointer m_precision;
        ActiveArea m_area = kDefaultActiveArea;
        std::array<PinchDetector, kPinchFingerCount> m_pinches;
        PinchFinger m_shownFinger = PinchFinger::Index;
        FistDetector m_fist;
        bool m_framed = false;
        GestureLog m_log;
        std::optional<Positions> m_last;
        int m_stillFrames = 0;
        double m_rawSquares = 0.0;
        double m_smoothedSquares = 0.0;
        double m_pointerSquares = 0.0;
    };
}
