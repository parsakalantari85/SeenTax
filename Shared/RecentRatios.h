#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <deque>
#include <utility>

namespace seentax
{
    class RecentRatios
    {
    public:
        using Time = std::chrono::steady_clock::time_point;

        explicit RecentRatios(std::chrono::milliseconds window) : m_window(window) {}

        float Median(float ratio);
        void Remember(Time time, float ratio);
        float Highest(float ratio) const;
        void Forget() { m_remembered.clear(); }

    private:
        std::chrono::milliseconds m_window;
        std::array<float, 3> m_last{}; // raw, for the median
        std::size_t m_nextSlot = 0;
        int m_count = 0;
        std::deque<std::pair<Time, float>> m_remembered; // smoothed ratios of the last m_window
    };
}
