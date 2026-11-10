#include "pch.h"
#include "CursorMapping.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>

namespace seentax
{
    ActiveArea ActiveAreaFor(Point2 center, int speedPercent)
    {
        const float defaultSize = kDefaultActiveArea.right - kDefaultActiveArea.left;
        const float size = std::min(defaultSize * 100.0f / static_cast<float>(std::max(speedPercent, 1)), 1.0f);
        const float half = size / 2.0f;
        const float x = std::clamp(center.x, half, 1.0f - half);
        const float y = std::clamp(center.y, half, 1.0f - half);
        return { x - half, y - half, x + half, y + half };
    }

    Point2 PointerAnchor(std::span<const Point2, kLandmarkCount> landmarks)
    {
        constexpr std::array kKnuckles{ HandJoint::IndexMcp, HandJoint::MiddleMcp, HandJoint::RingMcp, HandJoint::PinkyMcp };
        Point2 sum;
        for (const auto joint : kKnuckles)
        {
            sum.x += landmarks[IndexOf(joint)].x;
            sum.y += landmarks[IndexOf(joint)].y;
        }
        const auto count = static_cast<float>(kKnuckles.size());
        return { sum.x / count, sum.y / count };
    }

    Point2 ImageToAreaFraction(Point2 imagePoint, int imageWidth, int imageHeight, const ActiveArea& area, bool mirror)
    {
        const float x = (imagePoint.x / static_cast<float>(imageWidth) - area.left) / (area.right - area.left);
        const float y = (imagePoint.y / static_cast<float>(imageHeight) - area.top) / (area.bottom - area.top);
        return { mirror ? 1.0f - x : x, y };
    }

    Point2 ImageToScreenFraction(Point2 imagePoint, int imageWidth, int imageHeight, const ActiveArea& area, bool mirror)
    {
        const auto point = ImageToAreaFraction(imagePoint, imageWidth, imageHeight, area, mirror);
        return { std::clamp(point.x, 0.0f, 1.0f), std::clamp(point.y, 0.0f, 1.0f) };
    }

    Point2 AreaCenterFor(Point2 pictureFraction, Point2 screenFraction, const ActiveArea& area, bool mirror)
    {
        const float shiftX = (area.right - area.left) * (screenFraction.x - 0.5f);
        const float shiftY = (area.bottom - area.top) * (screenFraction.y - 0.5f);
        return { mirror ? pictureFraction.x + shiftX : pictureFraction.x - shiftX, pictureFraction.y - shiftY };
    }

    Point2 PrecisionPointer::Update(Point2 target, Time time)
    {
        if (m_last)
        {
            const Point2 delta{ target.x - m_last->target.x, target.y - m_last->target.y };
            const float distance = std::hypot(delta.x, delta.y);
            const float seconds = std::chrono::duration<float>(time - m_last->time).count();
            const float speed = seconds > 0.0f ? distance / seconds : 0.0f;
            if (speed < kPreciseBelow)
            {
                const float heldBack = 1.0f - speed / kPreciseBelow;
                m_offset = { m_offset.x - heldBack * delta.x, m_offset.y - heldBack * delta.y };
            }
            else
            {
                const float kept = std::exp(-distance / kCatchUpDistance);
                m_offset = { m_offset.x * kept, m_offset.y * kept };
            }
        }
        m_last = Step{ target, time };
        const Point2 pointer{ std::clamp(target.x + m_offset.x, 0.0f, 1.0f), std::clamp(target.y + m_offset.y, 0.0f, 1.0f) };
        m_offset = { pointer.x - target.x, pointer.y - target.y };
        return pointer;
    }
}
