#pragma once

#include <chrono>
#include <optional>

#include "ImagePreprocessing.h"

namespace seentax
{
    // The 1€ filter (Casiez, Roussel and Vogel, CHI 2012): a low-pass filter whose cutoff rises with
    // speed, so slow movements are smoothed hard (no jitter) and fast ones barely (no lag).
    struct OneEuroSettings
    {
        float minCutoffHz = 1.0f;      // lower: steadier at rest, but laggier
        float beta = 0.01f;            // higher: less lag when moving fast
        float velocityCutoffHz = 1.0f; // smoothing of the speed estimate
    };

    class PointFilter
    {
    public:
        explicit PointFilter(OneEuroSettings settings);

        // The first value after a reset passes through unchanged.
        Point2 Filter(Point2 value, std::chrono::steady_clock::time_point time);

        // The smoothed speed, in units per second.
        Point2 Velocity() const { return m_velocity; }

        void Reset() { m_lastTime.reset(); }

    private:
        OneEuroSettings m_settings;
        std::optional<std::chrono::steady_clock::time_point> m_lastTime;
        Point2 m_value;
        Point2 m_velocity;
    };
}
