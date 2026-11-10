#pragma once

#include <chrono>
#include <optional>
#include <span>

#include "HandLandmarks.h"
#include "ImagePreprocessing.h"

namespace seentax
{
    struct ActiveArea
    {
        float left = 0.0f;
        float top = 0.0f;
        float right = 1.0f;
        float bottom = 1.0f;

        bool operator==(const ActiveArea&) const = default;
    };

    inline constexpr ActiveArea kDefaultActiveArea{ 0.15f, 0.15f, 0.85f, 0.85f };

    ActiveArea ActiveAreaFor(Point2 center, int speedPercent);

    Point2 PointerAnchor(std::span<const Point2, kLandmarkCount> landmarks);

    Point2 ImageToAreaFraction(Point2 imagePoint, int imageWidth, int imageHeight, const ActiveArea& area, bool mirror);

    Point2 ImageToScreenFraction(Point2 imagePoint, int imageWidth, int imageHeight, const ActiveArea& area, bool mirror);

    Point2 AreaCenterFor(Point2 pictureFraction, Point2 screenFraction, const ActiveArea& area, bool mirror);

    class PrecisionPointer
    {
    public:
        using Time = std::chrono::steady_clock::time_point;

        static constexpr float kPreciseBelow = 0.2f;
        static constexpr float kCatchUpDistance = 0.1f;

        Point2 Update(Point2 target, Time time);
        void Pause() { m_last.reset(); }
        void Reset() { *this = PrecisionPointer{}; }

    private:
        struct Step
        {
            Point2 target;
            Time time;
        };

        std::optional<Step> m_last;
        Point2 m_offset;
    };
}
