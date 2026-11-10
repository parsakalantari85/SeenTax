#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

namespace seentax
{
    struct Landmark
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    inline constexpr std::size_t kLandmarkCount = 21;
    inline constexpr std::size_t kValuesPerLandmark = 3;

    using HandLandmarks = std::array<Landmark, kLandmarkCount>;

    enum class HandJoint : std::size_t
    {
        Wrist,
        ThumbCmc, ThumbMcp, ThumbIp, ThumbTip,
        IndexMcp, IndexPip, IndexDip, IndexTip,
        MiddleMcp, MiddlePip, MiddleDip, MiddleTip,
        RingMcp, RingPip, RingDip, RingTip,
        PinkyMcp, PinkyPip, PinkyDip, PinkyTip,
    };

    constexpr std::size_t IndexOf(HandJoint joint)
    {
        return static_cast<std::size_t>(joint);
    }

    inline float JointDistance(const HandLandmarks& landmarks, HandJoint from, HandJoint to)
    {
        const auto& a = landmarks[IndexOf(from)];
        const auto& b = landmarks[IndexOf(to)];
        return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
    }

    enum class Hand : std::uint8_t
    {
        Right,
        Left,
    };
    inline constexpr std::array<std::pair<HandJoint, HandJoint>, 21> kHandBones{ {
        { HandJoint::Wrist, HandJoint::ThumbCmc },      { HandJoint::ThumbCmc, HandJoint::ThumbMcp },   { HandJoint::ThumbMcp, HandJoint::ThumbIp },
        { HandJoint::ThumbIp, HandJoint::ThumbTip },    { HandJoint::Wrist, HandJoint::IndexMcp },      { HandJoint::IndexMcp, HandJoint::IndexPip },
        { HandJoint::IndexPip, HandJoint::IndexDip },   { HandJoint::IndexDip, HandJoint::IndexTip },   { HandJoint::IndexMcp, HandJoint::MiddleMcp },
        { HandJoint::MiddleMcp, HandJoint::MiddlePip }, { HandJoint::MiddlePip, HandJoint::MiddleDip }, { HandJoint::MiddleDip, HandJoint::MiddleTip },
        { HandJoint::MiddleMcp, HandJoint::RingMcp },   { HandJoint::RingMcp, HandJoint::RingPip },     { HandJoint::RingPip, HandJoint::RingDip },
        { HandJoint::RingDip, HandJoint::RingTip },     { HandJoint::RingMcp, HandJoint::PinkyMcp },    { HandJoint::Wrist, HandJoint::PinkyMcp },
        { HandJoint::PinkyMcp, HandJoint::PinkyPip },   { HandJoint::PinkyPip, HandJoint::PinkyDip },   { HandJoint::PinkyDip, HandJoint::PinkyTip },
    } };

    inline constexpr std::array<std::string_view, kLandmarkCount> kLandmarkNames = {
        "Wrist",
        "ThumbCmc", "ThumbMcp", "ThumbIp", "ThumbTip",
        "IndexMcp", "IndexPip", "IndexDip", "IndexTip",
        "MiddleMcp", "MiddlePip", "MiddleDip", "MiddleTip",
        "RingMcp", "RingPip", "RingDip", "RingTip",
        "PinkyMcp", "PinkyPip", "PinkyDip", "PinkyTip",
    };

    std::optional<HandLandmarks> DecodeLandmarks(std::span<const float> raw);
}
