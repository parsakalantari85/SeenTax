#pragma once

#include <chrono>
#include <optional>
#include <span>

#include "HandLandmarks.h"
#include "ImagePreprocessing.h"

namespace seentax
{
    inline constexpr float kLMinAngle = 55.0f;
    inline constexpr float kLMaxAngle = 125.0f;
    inline constexpr float kLMinThumbOut = 0.5f;
    inline constexpr float kLMinIndexReach = 1.5f;
    inline constexpr float kLMaxOthersReach = 1.3f;

    inline constexpr float kFrameMaxOpeningOff = 35.0f;
    inline constexpr float kFrameMinSize = 1.0f;

    struct LShape
    {
        float angleDegrees = 0.0f;
        float thumbOut = 0.0f;
        float indexReach = 0.0f;
        float othersReach = 0.0f;
    };

    LShape MeasureL(const HandLandmarks& hand);
    bool MakesL(const LShape& shape);

    inline bool MakesL(const HandLandmarks& hand)
    {
        return MakesL(MeasureL(hand));
    }

    bool FormFrame(std::span<const Point2, kLandmarkCount> first, std::span<const Point2, kLandmarkCount> second);

    class FrameDetector
    {
    public:
        using Time = std::chrono::steady_clock::time_point;

        static constexpr std::chrono::milliseconds kHoldFor{ 500 };
        static constexpr std::chrono::milliseconds kGapAllowed{ 150 };

        bool Update(bool framed, Time time);
        bool Framing() const { return m_since.has_value(); }

    private:
        std::optional<Time> m_since;
        Time m_lastSeen{};
        bool m_fired = false;
    };
}
