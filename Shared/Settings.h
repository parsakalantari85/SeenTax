#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "HandLandmarks.h"
#include "ImagePreprocessing.h"
#include "Pinch.h"
#include "PointerController.h"

namespace seentax
{
    enum class Pose : std::uint8_t
    {
        Off,
        IndexTap,
        IndexDoubleTap,
        IndexHold,
        MiddleTap,
        MiddleDoubleTap,
        MiddleHold,
        Fist,
    };

    enum class InferenceDevice : std::uint8_t
    {
        Auto,
        Cpu,
        Gpu,
    };

    inline constexpr std::size_t kPoseCount = 8;

    using PoseBindings = std::array<Pose, kPointerActionCount>;

    inline constexpr PoseBindings kDefaultPoses{ Pose::IndexTap, Pose::MiddleTap, Pose::Off, Pose::IndexHold, Pose::Fist, Pose::MiddleHold };

    inline constexpr int kMinSpeedPercent = 50;
    inline constexpr int kMaxSpeedPercent = 200;

    struct Settings
    {
        bool trackingOn = true;
        int speedPercent = 100;
        PoseBindings poses = kDefaultPoses;
        std::wstring cameraId;
        Point2 center{ 0.5f, 0.5f };
        InferenceDevice inferenceDevice = InferenceDevice::Auto;

        bool operator==(const Settings&) const = default;
    };

    inline constexpr std::array<std::string_view, 2> kHandNames{ "right", "left" };
    inline constexpr std::array<std::string_view, 3> kInferenceDeviceNames{ "auto", "cpu", "gpu" };
    inline constexpr std::array<std::string_view, kPoseCount> kPoseNames{
        "off", "index-tap", "index-double-tap", "index-hold", "middle-tap", "middle-double-tap", "middle-hold", "fist",
    };
    inline constexpr std::array<std::string_view, kPointerActionCount> kPointerActionNames{
        "leftClick", "rightClick", "doubleClick", "pressAndHold", "closeWindow", "scroll",
    };

    template <typename Enum, std::size_t N>
    constexpr std::string_view NameOf(Enum value, const std::array<std::string_view, N>& names)
    {
        const auto index = static_cast<std::size_t>(value);
        return index < N ? names[index] : std::string_view();
    }

    template <typename Enum, std::size_t N>
    constexpr std::optional<Enum> FromName(std::string_view name, const std::array<std::string_view, N>& names)
    {
        const auto found = std::ranges::find(names, name);
        return found == names.end() ? std::nullopt : std::optional(static_cast<Enum>(found - names.begin()));
    }

    constexpr Trigger TriggerOf(Pose pose)
    {
        switch (pose)
        {
        case Pose::MiddleTap:
        case Pose::MiddleDoubleTap:
        case Pose::MiddleHold:
            return Trigger::MiddlePinch;
        case Pose::Fist:
            return Trigger::Fist;
        default:
            return Trigger::IndexPinch;
        }
    }

    constexpr Gesture GestureOf(Pose pose)
    {
        switch (pose)
        {
        case Pose::IndexDoubleTap:
        case Pose::MiddleDoubleTap:
            return Gesture::DoubleTap;
        case Pose::IndexHold:
        case Pose::MiddleHold:
            return Gesture::Hold;
        default:
            return Gesture::Tap;
        }
    }

    constexpr bool NeedsHold(PointerAction action)
    {
        return action == PointerAction::PressAndHold || action == PointerAction::Scroll;
    }

    constexpr bool Allows(PointerAction action, Pose pose)
    {
        if (action == PointerAction::CloseWindow)
        {
            return pose == Pose::Fist;
        }
        if (pose == Pose::Off)
        {
            return true;
        }
        if (TriggerOf(pose) == Trigger::Fist)
        {
            return false;
        }
        switch (GestureOf(pose))
        {
        case Gesture::Tap:
            return action == PointerAction::LeftClick || action == PointerAction::RightClick;
        case Gesture::DoubleTap:
            return action == PointerAction::DoubleClick;
        case Gesture::Hold:
            return NeedsHold(action);
        }
        return false;
    }

    inline Settings Normalized(Settings settings)
    {
        settings.speedPercent = std::clamp(settings.speedPercent, kMinSpeedPercent, kMaxSpeedPercent);
        settings.center = { std::clamp(settings.center.x, 0.0f, 1.0f), std::clamp(settings.center.y, 0.0f, 1.0f) };
        settings.poses[static_cast<std::size_t>(PointerAction::CloseWindow)] = Pose::Fist;
        std::array<bool, kPoseCount> taken{};
        for (std::size_t i = 0; i < settings.poses.size(); ++i)
        {
            auto& pose = settings.poses[i];
            const auto slot = static_cast<std::size_t>(pose);
            if (pose == Pose::Off)
            {
                continue;
            }
            if (slot >= kPoseCount || taken[slot] || !Allows(static_cast<PointerAction>(i), pose))
            {
                pose = Pose::Off;
                continue;
            }
            taken[slot] = true;
        }
        return settings;
    }

    inline GestureMap MapGestures(const PoseBindings& poses)
    {
        GestureMap gestures{};
        for (std::size_t i = 0; i < poses.size(); ++i)
        {
            if (poses[i] != Pose::Off)
            {
                gestures[SlotOf(TriggerOf(poses[i]))][static_cast<std::size_t>(GestureOf(poses[i]))] = static_cast<PointerAction>(i);
            }
        }
        return gestures;
    }

    inline std::array<bool, kPinchFingerCount> FingersUsed(const PoseBindings& poses)
    {
        std::array<bool, kPinchFingerCount> used{};
        for (const auto pose : poses)
        {
            if (pose != Pose::Off && TriggerOf(pose) != Trigger::Fist)
            {
                used[SlotOf(FingerOf(TriggerOf(pose)))] = true;
            }
        }
        return used;
    }

    inline bool UsesFist(const PoseBindings& poses)
    {
        return std::ranges::any_of(poses, [](Pose pose) { return pose != Pose::Off && TriggerOf(pose) == Trigger::Fist; });
    }
}
