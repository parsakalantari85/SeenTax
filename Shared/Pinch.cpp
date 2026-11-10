#include "pch.h"
#include "Pinch.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace seentax
{
    float FingerGap(const HandLandmarks& landmarks, PinchFinger finger)
    {
        return JointDistance(landmarks, HandJoint::ThumbTip, TipOf(finger));
    }

    float HandSize(const HandLandmarks& landmarks)
    {
        return JointDistance(landmarks, HandJoint::Wrist, HandJoint::MiddleMcp);
    }

    float PinchRatio(const HandLandmarks& landmarks, PinchFinger finger)
    {
        const float handSize = HandSize(landmarks);
        return handSize > 0.0f ? FingerGap(landmarks, finger) / handSize : std::numeric_limits<float>::infinity();
    }

    bool PinchDetector::Update(float fingerGap, float handSize, float handPresence, Time time)
    {
        const float smoothedSize = SmoothHandSize(handSize, time);
        if (!(smoothedSize > 0.0f))
        {
            Reset();
            return false;
        }
        m_ratio = m_ratios.Median(fingerGap / smoothedSize);

        if (m_pinched)
        {
            m_lowestWhilePinched = std::min(m_lowestWhilePinched, m_ratio);
            if (m_ratio >= Threshold())
            {
                m_pinched = false;
                m_ratios.Forget(); // the next pinch must close quickly from here
            }
            m_ratios.Remember(time, m_ratio);
            return m_pinched;
        }

        m_ratios.Remember(time, m_ratio);
        m_blocker = PinchBlocker::None;
        if (m_ratio >= kPressBelow)
        {
            return false;
        }
        if (handPresence < kMinPresence)
        {
            m_blocker = PinchBlocker::UnsureTracking;
            return false;
        }
        if (m_ratios.Highest(m_ratio) - m_ratio < kMinClosingDrop)
        {
            m_blocker = PinchBlocker::ClosedTooSlowly;
            return false;
        }
        m_pinched = true;
        m_lowestWhilePinched = m_ratio;
        return true;
    }

    float PinchDetector::Threshold() const
    {
        return m_pinched ? std::min(kReleaseAbove, m_lowestWhilePinched + kReleaseRise) : kPressBelow;
    }

    void PinchDetector::Reset()
    {
        *this = PinchDetector{};
    }

    // An exponential moving average with a time constant, so it behaves the same at any frame rate.
    float PinchDetector::SmoothHandSize(float handSize, Time time)
    {
        if (!m_handSize || !m_lastTime || time <= *m_lastTime)
        {
            m_handSize = m_handSize.value_or(handSize);
        }
        else
        {
            const float seconds = std::chrono::duration<float>(time - *m_lastTime).count();
            const float timeConstant = std::chrono::duration<float>(kHandSizeTimeConstant).count();
            *m_handSize += (handSize - *m_handSize) * (1.0f - std::exp(-seconds / timeConstant));
        }
        m_lastTime = time;
        return *m_handSize;
    }
}
