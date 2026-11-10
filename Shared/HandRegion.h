#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "HandLandmarks.h"
#include "ImagePreprocessing.h"
#include "PalmDetection.h"

namespace seentax
{
    enum class HandDistance : std::uint8_t
    {
        Unknown,
        TooFar,
        Good,
        TooClose,
    };

    inline constexpr float kTooFarBelow = 0.30f;
    inline constexpr float kTooCloseAbove = 0.70f;

    constexpr HandDistance DistanceOf(float squareInPictureHeights)
    {
        if (!(squareInPictureHeights > 0.0f))
        {
            return HandDistance::Unknown;
        }
        if (squareInPictureHeights < kTooFarBelow)
        {
            return HandDistance::TooFar;
        }
        return squareInPictureHeights > kTooCloseAbove ? HandDistance::TooClose : HandDistance::Good;
    }

    constexpr std::string_view Describe(HandDistance distance)
    {
        switch (distance)
        {
        case HandDistance::TooFar:
            return "too far";
        case HandDistance::Good:
            return "good distance";
        case HandDistance::TooClose:
            return "too close";
        case HandDistance::Unknown:
            break;
        }
        return "";
    }

    // Where the landmark model looks: a square around the whole hand, turned so the hand is upright.

    // From a palm detection: the first frame, or after the hand was lost.
    SquareRegion HandRegionFromPalm(const PalmDetection& palm);

    // From this frame's landmarks, for the next frame.
    SquareRegion HandRegionFromLandmarks(std::span<const Point2, kLandmarkCount> landmarks);
}
