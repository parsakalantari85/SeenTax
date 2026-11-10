#pragma once

#include <array>
#include <optional>
#include <string_view>

#include "Fist.h"
#include "Pinch.h"
#include "Timing.h"

namespace seentax
{
    class GestureLog
    {
    public:
        void Pinch(PinchFinger finger, const PinchDetector& detector, Clock::time_point at);
        void Fist(const FistDetector& detector, Clock::time_point at);
        void Frame(bool framed, Clock::time_point at);
        void End(Clock::time_point at, std::string_view why);

    private:
        struct PinchInProgress
        {
            Clock::time_point started;
            float lowestRatio = 0.0f;
        };

        struct NearMiss
        {
            float lowestRatio = 0.0f;
            PinchBlocker blocker = PinchBlocker::None;
        };

        struct FingerLog
        {
            std::optional<PinchInProgress> inProgress;
            std::optional<NearMiss> nearMiss;
            bool nearMissArmed = true;
        };

        void NotePinch(PinchFinger finger, const PinchDetector& detector, Clock::time_point at);
        void NoteNearMiss(PinchFinger finger, const PinchDetector& detector);
        void EndPinch(PinchFinger finger, Clock::time_point at, std::string_view how);
        void EndFist(Clock::time_point at, std::string_view how);
        void EndFrame(Clock::time_point at, std::string_view how);

        std::array<FingerLog, kPinchFingerCount> m_fingers;
        std::optional<Clock::time_point> m_fistClosedAt;
        float m_lowestFistRatio = 0.0f;
        std::optional<Clock::time_point> m_framedAt;
    };
}
