#pragma once

#include <chrono>

namespace seentax
{
    using Clock = std::chrono::steady_clock;

    inline double MillisecondsBetween(Clock::time_point start, Clock::time_point end)
    {
        return std::chrono::duration<double, std::milli>(end - start).count();
    }

    template <typename Work>
    double MillisecondsOf(Work&& work)
    {
        const auto start = Clock::now();
        work();
        return MillisecondsBetween(start, Clock::now());
    }
}
