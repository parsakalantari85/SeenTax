#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <type_traits>

#include "CursorMapping.h"
#include "HandCheck.h"
#include "HandLandmarks.h"
#include "HandRegion.h"
#include "ImagePreprocessing.h"
#include "Pinch.h"
#include "PointerController.h"

namespace seentax
{
    // The Control Panel connects to this pipe only while it shows the camera; the Engine then sends one
    // message per camera frame: a PreviewHeader followed by the picture.
    inline std::wstring PreviewPipeName(std::uint32_t sessionId)
    {
        return std::format(L"\\\\.\\pipe\\SeenTax.Preview.{}", sessionId);
    }

    inline constexpr int kPreviewMaxWidth = 320;
    inline constexpr int kPreviewMaxHeight = 240;

    // The whole-number factor that shrinks a camera picture to fit kPreviewMaxWidth x kPreviewMaxHeight.
    constexpr int PreviewScale(int width, int height)
    {
        const int byWidth = (width + kPreviewMaxWidth - 1) / kPreviewMaxWidth;
        const int byHeight = (height + kPreviewMaxHeight - 1) / kPreviewMaxHeight;
        const int factor = byWidth > byHeight ? byWidth : byHeight;
        return factor > 1 ? factor : 1;
    }

    enum class CursorMode : std::uint8_t
    {
        On,
        DryRun,
    };

    // Sent as raw bytes: both apps build it from this header with the same compiler, so the layout matches.
    // Points are in picture pixels, as the camera sees them (not mirrored).
    struct PreviewHeader
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        float fps = 0.0f;
        float handPresence = 0.0f;
        float pinchRatio = 0.0f;
        float pinchThreshold = 0.0f;
        float fistRatio = 0.0f;
        GestureEvent lastEvent = GestureEvent::None;
        float lastEventAgeMs = 0.0f;
        PinchBlocker blocker = PinchBlocker::None;
        PinchFinger pinchFinger = PinchFinger::Index;
        CursorMode cursor = CursorMode::On;
        Hand hand = Hand::Right;
        HandDistance distance = HandDistance::Unknown;
        HandCheck handCheck = HandCheck::NoHand;
        bool handFound = false;
        bool otherHandFound = false;
        bool framed = false;
        bool switchingHands = false;
        bool pinched = false;
        bool fist = false;
        bool fistUsed = false;
        bool tooDark = false;
        std::uint8_t calibrationSecondsLeft = 0;
        ActiveArea activeArea;
        Point2 anchor; // the point the cursor follows
        std::array<Point2, kLandmarkCount> landmarks{};
        std::array<Point2, kLandmarkCount> otherLandmarks{};
    };
    static_assert(std::is_trivially_copyable_v<PreviewHeader>);

    // The picture after the header: width x height opaque BGRA pixels, rows top to bottom.
    constexpr std::size_t PreviewPixelBytes(const PreviewHeader& header)
    {
        return std::size_t{ header.width } * header.height * 4;
    }

    inline constexpr std::size_t kMaxPreviewMessageBytes = sizeof(PreviewHeader) + std::size_t{ kPreviewMaxWidth } * kPreviewMaxHeight * 4;
}
