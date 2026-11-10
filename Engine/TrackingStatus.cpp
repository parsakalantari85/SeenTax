#include "TrackingStatus.h"

#include <format>

#include "FrameGesture.h"
#include "HandRegion.h"
#include "Log.h"
#include "Settings.h"

namespace seentax
{
    namespace
    {
        constexpr double kSlowCameraFps = 20.0;

        std::string FrameStatus(const TrackedFrame& last)
        {
            const auto shape = MeasureL(last.model->landmarks);
            std::string text = std::format("L shape: thumb-index {:.0f} deg, thumb out {:.2f}, index {:.2f}, others {:.2f}{}", shape.angleDegrees,
                                           shape.thumbOut, shape.indexReach, shape.othersReach, MakesL(shape) ? " (L)" : "");
            if (last.otherHand)
            {
                text += std::format(", other hand {}{}", MakesL(last.otherHand->shape) ? "(L)" : "(not L)", MakesFrame(last) ? ", frame" : "");
            }
            return text;
        }
    }

    std::string HandStatus(const TrackingInterval& interval, const TrackedFrame& last, const HandPointer& pointer, bool calibrating, float handSquare)
    {
        if (interval.frames == 0)
        {
            return "no camera frames";
        }
        if (last.handFound)
        {
            const auto& detector = pointer.ShownDetector();
            const auto& fist = pointer.Fist();
            return std::format("tracking: {} hand (presence {:.2f}){}, {} pinch {:.2f}{}, fist {:.2f}{}, hand square {:.2f} of the picture ({}), {}",
                               NameOf(HandOf(*last.model), kHandNames), last.model->handPresence, calibrating ? ", recalibrating" : "",
                               Describe(pointer.ShownFinger()), detector.Ratio(), detector.Pinched() ? " (pinched)" : "", fist.Ratio(),
                               fist.Closed() ? " (closed)" : "", handSquare, Describe(DistanceOf(handSquare)), FrameStatus(last));
        }
        if (last.switchingHands)
        {
            return "switching hands";
        }
        if (last.check == HandCheck::Checking || TurnedDown(last.check))
        {
            return std::format("{} (presence {:.2f})", Describe(last.check), last.model->handPresence);
        }
        if (last.model)
        {
            return std::format("lost (presence {:.2f})", last.model->handPresence);
        }
        return interval.brightness < kDarkPicture ? "searching (no palm in view; the picture is very dark)" : "searching (no palm in view)";
    }

    std::string JitterStatus(const std::optional<HandPointer::Jitter>& jitter)
    {
        return jitter ? std::format("{:.1f} px raw -> {:.1f} px smoothed -> {:.1f} px pointer", jitter->raw, jitter->smoothed, jitter->pointer)
                      : "- (hand not held still)";
    }

    std::string CursorStatus(bool dryRun, const CursorDriver::Activity& activity, double seconds)
    {
        return std::format(
            "{}, {:.0f} updates/s, {} left clicks, {} right clicks, {} double clicks, {} holds, {} windows closed, {} screenshots, {} wheel notches",
            dryRun ? "dry run (--no-cursor)" : "on", activity.updates / seconds, activity.leftClicks, activity.rightClicks, activity.doubleClicks,
            activity.holds, activity.windowsClosed, activity.screenshots, activity.wheelNotches);
    }

    void LogStatus(Clock::time_point started, const TrackingInterval& interval, int droppedFrames, const std::string& hand, const std::string& jitter,
                   const std::string& cursor)
    {
        const auto now = Clock::now();
        const double seconds = MillisecondsBetween(interval.start, now) / 1000.0;
        const double fps = interval.frames / seconds;
        const double msPerFrame = interval.frames > 0 ? interval.processingMs / interval.frames : 0.0;
        const double latencyMs = interval.frames > 0 ? interval.latencyMs / interval.frames : 0.0;
        const bool cameraIsBottleneck = interval.frames > 0 && fps < kSlowCameraFps && droppedFrames == 0;
        const std::string turnedDown = interval.handsTurnedDown > 0 ? std::format(", {} hand checks failed (most sure {:.2f})", interval.handsTurnedDown,
                                                                                  interval.mostSureTurnedDown)
                                                                    : std::string();
        Log("[{:4.0f} s] {:4.1f} fps{}, {:4.1f} ms per frame, latency {:3.0f} ms, palm detector ran {}x{}, {} dropped, light {:3.0f}%\n"
            "         hand: {} | jitter while still: {} | cursor: {}",
            MillisecondsBetween(started, now) / 1000.0, fps, cameraIsBottleneck ? " (camera is slow: more light usually helps)" : "", msPerFrame,
            latencyMs, interval.palmDetectorRuns, turnedDown, droppedFrames, interval.brightness * 100.0f, hand, jitter, cursor);
    }
}
