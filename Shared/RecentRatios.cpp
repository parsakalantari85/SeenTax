#include "pch.h"
#include "RecentRatios.h"

#include <algorithm>

namespace seentax
{
    // With fewer than three ratios yet, the largest, so a pinch or a fist can't start from too little evidence.
    float RecentRatios::Median(float ratio)
    {
        m_last[m_nextSlot] = ratio;
        m_nextSlot = (m_nextSlot + 1) % m_last.size();
        m_count = std::min(m_count + 1, 3);
        if (m_count < 3)
        {
            return m_count == 1 ? m_last[0] : std::max(m_last[0], m_last[1]);
        }
        auto sorted = m_last;
        std::ranges::sort(sorted);
        return sorted[1];
    }

    void RecentRatios::Remember(Time time, float ratio)
    {
        m_remembered.emplace_back(time, ratio);
        while (m_remembered.front().first < time - m_window)
        {
            m_remembered.pop_front();
        }
    }

    float RecentRatios::Highest(float ratio) const
    {
        float highest = ratio;
        for (const auto& [time, remembered] : m_remembered)
        {
            highest = std::max(highest, remembered);
        }
        return highest;
    }
}
