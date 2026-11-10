#include "pch.h"
#include "OneEuroFilter.h"

#include <cmath>
#include <numbers>

namespace seentax
{
    namespace
    {
        // A first-order low-pass filter's smoothing factor (from the 1€ paper).
        float Alpha(float cutoffHz, float seconds)
        {
            const float timeConstant = 1.0f / (2.0f * std::numbers::pi_v<float> * cutoffHz);
            return 1.0f / (1.0f + timeConstant / seconds);
        }

        Point2 Lerp(Point2 from, Point2 to, float alpha)
        {
            return { from.x + (to.x - from.x) * alpha, from.y + (to.y - from.y) * alpha };
        }
    }

    PointFilter::PointFilter(OneEuroSettings settings)
        : m_settings(settings)
    {
    }

    Point2 PointFilter::Filter(Point2 value, std::chrono::steady_clock::time_point time)
    {
        if (!m_lastTime)
        {
            m_lastTime = time;
            m_value = value;
            m_velocity = {};
            return value;
        }
        const float seconds = std::chrono::duration<float>(time - *m_lastTime).count();
        if (seconds <= 0.0f)
        {
            return m_value; // same or older timestamp
        }
        m_lastTime = time;

        const Point2 rawVelocity{ (value.x - m_value.x) / seconds, (value.y - m_value.y) / seconds };
        m_velocity = Lerp(m_velocity, rawVelocity, Alpha(m_settings.velocityCutoffHz, seconds));

        // The 2D speed, so a diagonal movement is treated like a straight one.
        const float speed = std::hypot(m_velocity.x, m_velocity.y);
        m_value = Lerp(m_value, value, Alpha(m_settings.minCutoffHz + m_settings.beta * speed, seconds));
        return m_value;
    }
}
