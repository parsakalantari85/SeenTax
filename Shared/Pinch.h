#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "HandLandmarks.h"
#include "RecentRatios.h"

namespace seentax
{
    enum class PinchFinger : std::uint8_t
    {
        Index,
        Middle,
    };

    inline constexpr std::size_t kPinchFingerCount = 2;
    inline constexpr std::array<PinchFinger, kPinchFingerCount> kPinchFingers{ PinchFinger::Index, PinchFinger::Middle };

    constexpr std::size_t SlotOf(PinchFinger finger)
    {
        return static_cast<std::size_t>(finger);
    }

    constexpr HandJoint TipOf(PinchFinger finger)
    {
        return finger == PinchFinger::Middle ? HandJoint::MiddleTip : HandJoint::IndexTip;
    }

    constexpr std::string_view Describe(PinchFinger finger)
    {
        return finger == PinchFinger::Middle ? "middle" : "index";
    }

    float FingerGap(const HandLandmarks& landmarks, PinchFinger finger = PinchFinger::Index);

    // Wrist to the middle finger's knuckle, in the landmarks' own units.
    float HandSize(const HandLandmarks& landmarks);

    // FingerGap / HandSize for one frame: about 0.15 with the fingertips touching, 0.4 and up apart.
    float PinchRatio(const HandLandmarks& landmarks, PinchFinger finger = PinchFinger::Index);

    // Why the fingers came close but no pinch started.
    enum class PinchBlocker
    {
        None,
        UnsureTracking,
        ClosedTooSlowly, // drifted together instead of tapping
    };

    constexpr std::string_view Describe(PinchBlocker blocker)
    {
        switch (blocker)
        {
        case PinchBlocker::UnsureTracking:
            return "unsure it sees a hand";
        case PinchBlocker::ClosedTooSlowly:
            return "fingers closed too slowly";
        case PinchBlocker::None:
            break;
        }
        return "";
    }

    // Decides frame by frame whether the hand pinches. The ratio is the median of the last three frames
    // over a hand size averaged across half a second, so one bad frame can't start or end a pinch. A pinch
    // must close quickly (a tap, not fingers drifting together) and ends once the fingers clearly open.
    class PinchDetector
    {
    public:
        static constexpr float kPressBelow = 0.30f;
        static constexpr float kReleaseAbove = 0.40f;
        static constexpr float kReleaseRise = 0.12f;    // opening this far above the pinch's lowest ratio also ends it
        static constexpr float kMinClosingDrop = 0.10f; // the fall needed within kClosingTime to start a pinch
        static constexpr std::chrono::milliseconds kClosingTime{ 400 };
        static constexpr float kMinPresence = 0.8f; // as in the model's reference code (OpenCV Model Zoo)
        static constexpr std::chrono::milliseconds kHandSizeTimeConstant{ 500 };

        // Feeds one frame's FingerGap and HandSize (same units); returns whether the hand pinches.
        bool Update(float fingerGap, float handSize, float handPresence, std::chrono::steady_clock::time_point time);

        bool Pinched() const { return m_pinched; }
        float Ratio() const { return m_ratio; } // the smoothed ratio the decisions use
        // The ratio that flips the state next: below it a pinch can start; while pinched, above it the pinch ends.
        float Threshold() const;
        PinchBlocker Blocker() const { return m_blocker; }

        void Reset();

    private:
        using Time = std::chrono::steady_clock::time_point;

        float SmoothHandSize(float handSize, Time time);

        bool m_pinched = false;
        float m_ratio = 0.0f;
        float m_lowestWhilePinched = 0.0f;
        PinchBlocker m_blocker = PinchBlocker::None;
        std::optional<float> m_handSize;
        std::optional<Time> m_lastTime;
        RecentRatios m_ratios{ kClosingTime };
    };
}
