#include "pch.h"
#include "HandRegion.h"

#include <algorithm>
#include <array>
#include <limits>

namespace seentax
{
    namespace
    {
        // From the reference code (OpenCV Model Zoo, mp_handpose.py).
        constexpr float kPalmShiftTowardFingers = 0.4f;
        constexpr float kPalmEnlarge = 3.0f;

        // From MediaPipe (hand_landmark_landmarks_to_roi.pbtxt). No fingertips, so a curling finger
        // doesn't make the square jump.
        constexpr std::array kStableJoints{
            HandJoint::Wrist,     HandJoint::ThumbCmc,  HandJoint::ThumbMcp, HandJoint::ThumbIp,
            HandJoint::IndexMcp,  HandJoint::IndexPip,  HandJoint::MiddleMcp, HandJoint::MiddlePip,
            HandJoint::RingMcp,   HandJoint::RingPip,   HandJoint::PinkyMcp, HandJoint::PinkyPip,
        };
        constexpr float kTrackingShiftTowardFingers = 0.1f;
        constexpr float kTrackingEnlarge = 2.0f;

        // The box around `points` with the hand upright, shifted toward the fingers by a fraction of its
        // height, enlarged to `enlarge` times its longer side, and turned back.
        SquareRegion UprightSquareAround(std::span<const Point2> points, float rotation, float shiftTowardFingers, float enlarge)
        {
            const Point2 pivot = points.front();

            Point2 low{ std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
            Point2 high{ std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest() };
            for (const auto& point : points)
            {
                const Point2 upright = Rotate({ point.x - pivot.x, point.y - pivot.y }, -rotation);
                low = { std::min(low.x, upright.x), std::min(low.y, upright.y) };
                high = { std::max(high.x, upright.x), std::max(high.y, upright.y) };
            }
            const float width = high.x - low.x;
            const float height = high.y - low.y;

            // Upright, the fingers point to negative y.
            const Point2 uprightCenter{ (low.x + high.x) / 2.0f, (low.y + high.y) / 2.0f - shiftTowardFingers * height };
            const Point2 center = Rotate(uprightCenter, rotation);
            return { pivot.x + center.x, pivot.y + center.y, std::max(width, height) * enlarge, rotation };
        }

        Point2 Midpoint(Point2 a, Point2 b)
        {
            return { (a.x + b.x) / 2.0f, (a.y + b.y) / 2.0f };
        }
    }

    SquareRegion HandRegionFromPalm(const PalmDetection& palm)
    {
        const float rotation = RotationFacing(palm.Keypoint(PalmKeypoint::Wrist), palm.Keypoint(PalmKeypoint::MiddleBase));
        return UprightSquareAround(palm.keypoints, rotation, kPalmShiftTowardFingers, kPalmEnlarge);
    }

    SquareRegion HandRegionFromLandmarks(std::span<const Point2, kLandmarkCount> landmarks)
    {
        const auto joint = [&](HandJoint id) { return landmarks[IndexOf(id)]; };

        // Like MediaPipe: from the wrist toward the knuckles.
        const Point2 knuckles = Midpoint(Midpoint(joint(HandJoint::IndexMcp), joint(HandJoint::RingMcp)), joint(HandJoint::MiddleMcp));
        const float rotation = RotationFacing(joint(HandJoint::Wrist), knuckles);

        std::array<Point2, kStableJoints.size()> stablePoints{};
        std::ranges::transform(kStableJoints, stablePoints.begin(), joint);
        return UprightSquareAround(stablePoints, rotation, kTrackingShiftTowardFingers, kTrackingEnlarge);
    }
}
