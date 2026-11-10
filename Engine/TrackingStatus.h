#pragma once

#include <optional>
#include <string>

#include "CursorDriver.h"
#include "HandPointer.h"
#include "HandTracker.h"
#include "Timing.h"

namespace seentax
{
    inline constexpr float kDarkPicture = 0.15f;

    struct TrackingInterval
    {
        Clock::time_point start = Clock::now();
        int frames = 0;
        int palmDetectorRuns = 0;
        int handsTurnedDown = 0;
        float mostSureTurnedDown = 0.0f;
        double processingMs = 0.0;
        double latencyMs = 0.0;
        float brightness = 0.0f;
    };

    std::string HandStatus(const TrackingInterval& interval, const TrackedFrame& last, const HandPointer& pointer, bool calibrating, float handSquare);

    std::string JitterStatus(const std::optional<HandPointer::Jitter>& jitter);

    std::string CursorStatus(bool dryRun, const CursorDriver::Activity& activity, double seconds);

    void LogStatus(Clock::time_point started, const TrackingInterval& interval, int droppedFrames, const std::string& hand, const std::string& jitter,
                   const std::string& cursor);
}
