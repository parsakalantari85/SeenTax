#include "HandPointer.h"

#include <cmath>
#include <utility>

#include "FrameGesture.h"
#include "HandLandmarkModel.h"

namespace seentax
{
    namespace
    {
        constexpr OneEuroSettings kCursorSmoothing{ .minCutoffHz = 0.3f, .beta = 10.0f, .velocityCutoffHz = 1.0f };

        constexpr float kStillSpeed = 0.05f;
    }

    PointerSettings PointerSettingsFor(const Settings& settings, Point2 center)
    {
        return { ActiveAreaFor(center, settings.speedPercent), FingersUsed(settings.poses), UsesFist(settings.poses) };
    }

    bool MakesFrame(const TrackedFrame& tracked)
    {
        return tracked.handFound && tracked.otherHand && MakesL(tracked.model->landmarks) && MakesL(tracked.otherHand->shape)
               && FormFrame(tracked.landmarks, tracked.otherHand->landmarks);
    }

    HandPointer::HandPointer(DesktopSize desktop)
        : m_desktop(desktop), m_filter(kCursorSmoothing)
    {
    }

    PointerSample HandPointer::Update(const TrackedFrame& tracked, const CameraFrame& frame, const PointerSettings& settings)
    {
        if (!tracked.handFound)
        {
            return Lose(frame.capturedAt, "hand lost");
        }
        if (m_area != settings.area)
        {
            m_filter.Reset();
            m_precision.Reset();
            m_last.reset();
            m_area = settings.area;
        }

        const auto anchor = PointerAnchor(tracked.landmarks);
        const auto target = ImageToScreenFraction(anchor, frame.width, frame.height, settings.area, true);
        const auto smoothed = m_filter.Filter(target, frame.capturedAt);
        const auto pointer = m_precision.Update(smoothed, frame.capturedAt);
        MeasureJitter(target, smoothed, pointer);

        PointerSample sample{ frame.capturedAt, true, pointer, m_filter.Velocity() };
        sample.handHeight = anchor.y / static_cast<float>(frame.height);
        sample.activeAreaSize = settings.area.right - settings.area.left;
        const auto& points = tracked.model->landmarks;
        const float toCameraPixels = tracked.handRegion->size / HandLandmarkModel::kInputSize;
        const float handSize = HandSize(points) * toCameraPixels;
        for (const auto finger : kPinchFingers)
        {
            auto& detector = m_pinches[SlotOf(finger)];
            sample.pinched[SlotOf(finger)] = detector.Update(FingerGap(points, finger) * toCameraPixels, handSize, tracked.model->handPresence,
                                                             frame.capturedAt);
            sample.pinchRatio[SlotOf(finger)] = detector.Ratio();
            if (settings.fingersUsed[SlotOf(finger)])
            {
                m_log.Pinch(finger, detector, frame.capturedAt);
            }
        }
        m_shownFinger = ChooseShownFinger(settings.fingersUsed);
        sample.fist = m_fist.Update(FistRatio(points), tracked.model->handPresence, frame.capturedAt);
        sample.nearlyFist = m_fist.NearlyClosed();
        if (settings.fistUsed)
        {
            m_log.Fist(m_fist, frame.capturedAt);
        }
        m_framed = MakesFrame(tracked);
        sample.framed = m_framed;
        m_log.Frame(m_framed, frame.capturedAt);
        return sample;
    }

    PointerSample HandPointer::Lose(Clock::time_point at, std::string_view why)
    {
        m_log.End(at, why);
        for (auto& detector : m_pinches)
        {
            detector.Reset();
        }
        m_fist.Reset();
        m_framed = false;
        m_filter.Reset();
        m_precision.Pause();
        m_last.reset();
        return { .capturedAt = at };
    }

    std::optional<HandPointer::Jitter> HandPointer::TakeJitter()
    {
        const int frames = std::exchange(m_stillFrames, 0);
        const double rawSquares = std::exchange(m_rawSquares, 0.0);
        const double smoothedSquares = std::exchange(m_smoothedSquares, 0.0);
        const double pointerSquares = std::exchange(m_pointerSquares, 0.0);
        if (frames == 0)
        {
            return std::nullopt;
        }
        return Jitter{ std::sqrt(rawSquares / frames), std::sqrt(smoothedSquares / frames), std::sqrt(pointerSquares / frames) };
    }

    PinchFinger HandPointer::ChooseShownFinger(const std::array<bool, kPinchFingerCount>& used) const
    {
        std::optional<PinchFinger> shown;
        for (const auto finger : kPinchFingers)
        {
            if (used[SlotOf(finger)] && (!shown || Closer(finger, *shown)))
            {
                shown = finger;
            }
        }
        return shown.value_or(PinchFinger::Index);
    }

    bool HandPointer::Closer(PinchFinger finger, PinchFinger than) const
    {
        const auto& candidate = Detector(finger);
        const auto& current = Detector(than);
        return candidate.Pinched() != current.Pinched() ? candidate.Pinched() : candidate.Ratio() < current.Ratio();
    }

    void HandPointer::MeasureJitter(Point2 raw, Point2 smoothed, Point2 pointer)
    {
        const auto velocity = m_filter.Velocity();
        if (m_last && std::hypot(velocity.x, velocity.y) < kStillSpeed)
        {
            m_rawSquares += SquaredPixelDistance(m_last->raw, raw);
            m_smoothedSquares += SquaredPixelDistance(m_last->smoothed, smoothed);
            m_pointerSquares += SquaredPixelDistance(m_last->pointer, pointer);
            ++m_stillFrames;
        }
        m_last = Positions{ raw, smoothed, pointer };
    }

    double HandPointer::SquaredPixelDistance(Point2 a, Point2 b) const
    {
        const auto pixelA = ToDesktopPixels(a, m_desktop);
        const auto pixelB = ToDesktopPixels(b, m_desktop);
        const double dx = pixelA.x - pixelB.x;
        const double dy = pixelA.y - pixelB.y;
        return dx * dx + dy * dy;
    }
}
