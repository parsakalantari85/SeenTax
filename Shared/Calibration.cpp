#include "pch.h"
#include "Calibration.h"

#include <algorithm>

namespace seentax
{
    void Calibration::Start(Time now)
    {
        m_endsAt = now + kCountdown;
        m_recent.clear();
    }

    void Calibration::Cancel()
    {
        m_endsAt.reset();
        m_recent.clear();
    }

    int Calibration::SecondsLeft(Time now) const
    {
        if (!m_endsAt)
        {
            return 0;
        }
        const auto left = std::chrono::ceil<std::chrono::seconds>(*m_endsAt - now).count();
        return static_cast<int>(std::max<decltype(left)>(left, 1));
    }

    std::optional<Calibration::Result> Calibration::Update(std::optional<Point2> handPoint, Time now)
    {
        if (!m_endsAt)
        {
            return std::nullopt;
        }
        if (handPoint)
        {
            m_recent.emplace_back(now, *handPoint);
        }
        while (!m_recent.empty() && m_recent.front().first < now - kAveraged)
        {
            m_recent.pop_front();
        }
        if (now < *m_endsAt)
        {
            return std::nullopt;
        }

        Result result;
        result.handSeen = m_recent.size() >= kMinSamples;
        if (result.handSeen)
        {
            Point2 sum;
            for (const auto& [time, point] : m_recent)
            {
                sum.x += point.x;
                sum.y += point.y;
            }
            const auto count = static_cast<float>(m_recent.size());
            result.center = { sum.x / count, sum.y / count };
        }
        Cancel();
        return result;
    }
}
