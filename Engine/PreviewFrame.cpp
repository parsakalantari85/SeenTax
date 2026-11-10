#include "PreviewFrame.h"

#include <algorithm>
#include <cstring>
#include <span>

#include "HandLandmarkModel.h"
#include "ImagePreprocessing.h"

namespace seentax
{
    std::optional<std::vector<std::uint8_t>> MakePreview(const CameraFrame& frame, const TrackedFrame& tracked, const HandPointer& pointer,
                                                         const PreviewStatus& status)
    {
        const int factor = PreviewScale(frame.width, frame.height);
        const auto toPicture = [factor](Point2 point) { return Point2{ point.x / factor, point.y / factor }; };
        const auto& detector = pointer.ShownDetector();
        PreviewHeader header{
            .width = static_cast<std::uint32_t>(frame.width / factor),
            .height = static_cast<std::uint32_t>(frame.height / factor),
            .fps = static_cast<float>(status.fps),
            .handPresence = tracked.model ? tracked.model->handPresence : 0.0f,
            .pinchRatio = detector.Ratio(),
            .pinchThreshold = detector.Threshold(),
            .fistRatio = pointer.Fist().Ratio(),
            .lastEvent = status.lastEvent.event,
            .lastEventAgeMs = static_cast<float>(MillisecondsBetween(status.lastEvent.at, Clock::now())),
            .blocker = detector.Blocker(),
            .pinchFinger = pointer.ShownFinger(),
            .cursor = status.cursor,
            .hand = tracked.model ? HandOf(*tracked.model) : Hand::Right,
            .distance = status.distance,
            .handCheck = tracked.check,
            .handFound = tracked.handFound,
            .otherHandFound = tracked.otherHand.has_value(),
            .framed = pointer.Framed(),
            .switchingHands = tracked.switchingHands,
            .pinched = detector.Pinched(),
            .fist = pointer.Fist().Closed(),
            .fistUsed = status.fistUsed,
            .tooDark = status.tooDark,
            .calibrationSecondsLeft = static_cast<std::uint8_t>(std::clamp(status.calibrationSecondsLeft, 0, 255)),
            .activeArea = status.area,
        };
        if (tracked.handFound)
        {
            header.anchor = toPicture(PointerAnchor(tracked.landmarks));
            std::ranges::transform(tracked.landmarks, header.landmarks.begin(), toPicture);
        }
        if (tracked.otherHand)
        {
            std::ranges::transform(tracked.otherHand->landmarks, header.otherLandmarks.begin(), toPicture);
        }

        std::vector<std::uint8_t> message(sizeof(header) + PreviewPixelBytes(header));
        std::memcpy(message.data(), &header, sizeof(header));
        if (!DownscaleToBgra(frame.View(), factor, std::span(message).subspan(sizeof(header))))
        {
            return std::nullopt;
        }
        return message;
    }
}
