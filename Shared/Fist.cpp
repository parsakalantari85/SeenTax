#include "pch.h"
#include "Fist.h"

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

namespace seentax
{
    namespace
    {
        constexpr std::array<std::pair<HandJoint, HandJoint>, 4> kFingers{ {
            { HandJoint::IndexMcp, HandJoint::IndexTip },
            { HandJoint::MiddleMcp, HandJoint::MiddleTip },
            { HandJoint::RingMcp, HandJoint::RingTip },
            { HandJoint::PinkyMcp, HandJoint::PinkyTip },
        } };
    }

    float FistRatio(const HandLandmarks& landmarks)
    {
        float loosestFinger = 0.0f;
        for (const auto& [knuckle, tip] : kFingers)
        {
            const float knuckleToWrist = JointDistance(landmarks, HandJoint::Wrist, knuckle);
            if (!(knuckleToWrist > 0.0f))
            {
                return std::numeric_limits<float>::infinity();
            }
            loosestFinger = std::max(loosestFinger, JointDistance(landmarks, HandJoint::Wrist, tip) / knuckleToWrist);
        }
        return loosestFinger;
    }

    bool FistDetector::Update(float ratio, float handPresence, Time time)
    {
        m_ratio = m_ratios.Median(ratio);
        if (m_closed)
        {
            if (m_ratio >= kOpenAbove)
            {
                m_closed = false;
                m_ratios.Forget();
            }
            m_ratios.Remember(time, m_ratio);
            return m_closed;
        }
        m_ratios.Remember(time, m_ratio);
        if (m_ratio >= kCloseBelow || handPresence < kMinPresence || m_ratios.Highest(m_ratio) - m_ratio < kMinClosingDrop)
        {
            return false;
        }
        m_closed = true;
        return true;
    }

    void FistDetector::Reset()
    {
        *this = FistDetector{};
    }
}
