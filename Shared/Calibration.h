#pragma once

#include <chrono>
#include <cstddef>
#include <deque>
#include <optional>
#include <utility>

#include "ImagePreprocessing.h"

namespace seentax
{
    class Calibration
    {
    public:
        using Time = std::chrono::steady_clock::time_point;

        static constexpr std::chrono::seconds kCountdown{ 3 };
        static constexpr std::chrono::milliseconds kAveraged{ 1000 };
        static constexpr std::size_t kMinSamples = 3;

        struct Result
        {
            bool handSeen = false;
            Point2 center;
        };

        void Start(Time now);
        void Cancel();
        bool Running() const { return m_endsAt.has_value(); }
        int SecondsLeft(Time now) const;
        std::optional<Result> Update(std::optional<Point2> handPoint, Time now);

    private:
        std::optional<Time> m_endsAt;
        std::deque<std::pair<Time, Point2>> m_recent;
    };
}
