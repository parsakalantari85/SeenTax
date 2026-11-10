#include "pch.h"
#include "FrameGesture.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <utility>

namespace seentax
{
    namespace
    {
        constexpr float kDegreesPerRadian = 180.0f / std::numbers::pi_v<float>;

        constexpr std::array<std::pair<HandJoint, HandJoint>, 3> kOtherFingers{ {
            { HandJoint::MiddleMcp, HandJoint::MiddleTip },
            { HandJoint::RingMcp, HandJoint::RingTip },
            { HandJoint::PinkyMcp, HandJoint::PinkyTip },
        } };

        struct Vector3
        {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
        };

        Vector3 Between(const Landmark& from, const Landmark& to)
        {
            return { to.x - from.x, to.y - from.y, to.z - from.z };
        }

        float CosineBetween(Vector3 a, Vector3 b)
        {
            const float lengths = std::sqrt((a.x * a.x + a.y * a.y + a.z * a.z) * (b.x * b.x + b.y * b.y + b.z * b.z));
            return lengths > 0.0f ? std::clamp((a.x * b.x + a.y * b.y + a.z * b.z) / lengths, -1.0f, 1.0f) : 0.0f;
        }

        float Reach(const HandLandmarks& hand, HandJoint knuckle, HandJoint tip)
        {
            const float knuckleToWrist = JointDistance(hand, HandJoint::Wrist, knuckle);
            return knuckleToWrist > 0.0f ? JointDistance(hand, HandJoint::Wrist, tip) / knuckleToWrist : 0.0f;
        }

        Point2 Minus(Point2 to, Point2 from)
        {
            return { to.x - from.x, to.y - from.y };
        }

        float Length(Point2 vector)
        {
            return std::hypot(vector.x, vector.y);
        }

        float Dot(Point2 a, Point2 b)
        {
            return a.x * b.x + a.y * b.y;
        }

        std::optional<Point2> Unit(Point2 vector)
        {
            const float length = Length(vector);
            if (!(length > 0.0f))
            {
                return std::nullopt;
            }
            return Point2{ vector.x / length, vector.y / length };
        }

        struct Corner
        {
            Point2 at;
            Point2 opening;
            float palmLength = 0.0f;
        };

        std::optional<Corner> CornerOf(std::span<const Point2, kLandmarkCount> points)
        {
            const auto joint = [points](HandJoint id) { return points[IndexOf(id)]; };
            const auto thumb = Unit(Minus(joint(HandJoint::ThumbTip), joint(HandJoint::ThumbMcp)));
            const auto index = Unit(Minus(joint(HandJoint::IndexTip), joint(HandJoint::IndexMcp)));
            if (!thumb || !index)
            {
                return std::nullopt;
            }
            const auto opening = Unit({ thumb->x + index->x, thumb->y + index->y });
            if (!opening)
            {
                return std::nullopt;
            }
            const Point2 thumbBase = joint(HandJoint::ThumbMcp);
            const Point2 indexBase = joint(HandJoint::IndexMcp);
            return Corner{ { (thumbBase.x + indexBase.x) / 2.0f, (thumbBase.y + indexBase.y) / 2.0f }, *opening,
                           Length(Minus(joint(HandJoint::MiddleMcp), joint(HandJoint::Wrist))) };
        }
    }

    LShape MeasureL(const HandLandmarks& hand)
    {
        const auto joint = [&hand](HandJoint id) -> const Landmark& { return hand[IndexOf(id)]; };
        const Vector3 thumb = Between(joint(HandJoint::ThumbMcp), joint(HandJoint::ThumbTip));
        const Vector3 index = Between(joint(HandJoint::IndexMcp), joint(HandJoint::IndexTip));
        const Vector3 towardThumbSide = Between(joint(HandJoint::PinkyMcp), joint(HandJoint::IndexMcp));

        LShape shape;
        shape.angleDegrees = std::acos(CosineBetween(thumb, index)) * kDegreesPerRadian;
        shape.thumbOut = CosineBetween(thumb, towardThumbSide);
        shape.indexReach = Reach(hand, HandJoint::IndexMcp, HandJoint::IndexTip);
        for (const auto& [knuckle, tip] : kOtherFingers)
        {
            shape.othersReach = std::max(shape.othersReach, Reach(hand, knuckle, tip));
        }
        return shape;
    }

    bool MakesL(const LShape& shape)
    {
        return shape.angleDegrees >= kLMinAngle && shape.angleDegrees <= kLMaxAngle && shape.thumbOut >= kLMinThumbOut
               && shape.indexReach >= kLMinIndexReach && shape.othersReach <= kLMaxOthersReach;
    }

    bool FormFrame(std::span<const Point2, kLandmarkCount> first, std::span<const Point2, kLandmarkCount> second)
    {
        const auto a = CornerOf(first);
        const auto b = CornerOf(second);
        if (!a || !b)
        {
            return false;
        }
        const Point2 between = Minus(b->at, a->at);
        const float size = Length(between);
        if (!(size > 0.0f) || size < kFrameMinSize * (a->palmLength + b->palmLength) / 2.0f)
        {
            return false;
        }
        const Point2 towardSecond{ between.x / size, between.y / size };
        const float minCosine = std::cos(kFrameMaxOpeningOff / kDegreesPerRadian);
        return Dot(a->opening, towardSecond) >= minCosine && -Dot(b->opening, towardSecond) >= minCosine;
    }

    bool FrameDetector::Update(bool framed, Time time)
    {
        if (!framed)
        {
            if (m_since && time - m_lastSeen > kGapAllowed)
            {
                m_since.reset();
                m_fired = false;
            }
            return false;
        }
        if (!m_since)
        {
            m_since = time;
        }
        m_lastSeen = time;
        if (m_fired || time - *m_since < kHoldFor)
        {
            return false;
        }
        m_fired = true;
        return true;
    }
}
