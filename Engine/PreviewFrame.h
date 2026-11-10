#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "Camera.h"
#include "CursorMapping.h"
#include "HandPointer.h"
#include "HandRegion.h"
#include "HandTracker.h"
#include "PointerController.h"
#include "PreviewMessage.h"
#include "Timing.h"

namespace seentax
{
    struct TimedEvent
    {
        GestureEvent event = GestureEvent::None;
        Clock::time_point at;
    };

    struct PreviewStatus
    {
        double fps = 0.0;
        CursorMode cursor = CursorMode::On;
        bool tooDark = false;
        TimedEvent lastEvent;
        ActiveArea area = kDefaultActiveArea;
        int calibrationSecondsLeft = 0;
        bool fistUsed = false;
        HandDistance distance = HandDistance::Unknown;
    };

    std::optional<std::vector<std::uint8_t>> MakePreview(const CameraFrame& frame, const TrackedFrame& tracked, const HandPointer& pointer,
                                                         const PreviewStatus& status);
}
