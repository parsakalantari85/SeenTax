#pragma once

#include <chrono>

#include "HandLandmarks.h"
#include "RecentRatios.h"

namespace seentax
{
    float FistRatio(const HandLandmarks& landmarks);

    class FistDetector
    {
    public:
        static constexpr float kCloseBelow = 1.0f;
        static constexpr float kOpenAbove = 1.2f;
        static constexpr float kNearlyClosedBelow = 1.25f;
        static constexpr float kMinClosingDrop = 0.4f;
        static constexpr std::chrono::milliseconds kClosingTime{ 500 };
        static constexpr float kMinPresence = 0.8f;

        bool Update(float ratio, float handPresence, std::chrono::steady_clock::time_point time);

        bool Closed() const { return m_closed; }
        bool NearlyClosed() const { return m_closed || m_ratio < kNearlyClosedBelow; }
        float Ratio() const { return m_ratio; }

        void Reset();

    private:
        using Time = std::chrono::steady_clock::time_point;

        bool m_closed = false;
        float m_ratio = 0.0f;
        RecentRatios m_ratios{ kClosingTime };
    };
}
