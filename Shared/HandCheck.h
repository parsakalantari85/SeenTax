#pragma once

#include <chrono>
#include <cstdint>
#include <string_view>

#include "ImagePreprocessing.h"

namespace seentax
{
    struct HandSighting
    {
        float presence = 0.0f;
        SquareRegion region;
        float pictureHeight = 0.0f;
    };

    enum class HandCheck : std::uint8_t
    {
        NoHand,
        Checking,
        Hand,
        Unsure,
        WrongSize,
        Jumped,
        WeakTooLong,
    };

    constexpr std::string_view Describe(HandCheck check)
    {
        switch (check)
        {
        case HandCheck::Checking:
            return "checking a hand";
        case HandCheck::Hand:
            return "hand";
        case HandCheck::Unsure:
            return "not sure it's a hand";
        case HandCheck::WrongSize:
            return "too small or too big for a hand";
        case HandCheck::Jumped:
            return "moved or changed size too fast for a hand";
        case HandCheck::WeakTooLong:
            return "stayed unsure for too long";
        case HandCheck::NoHand:
            break;
        }
        return "no hand";
    }

    constexpr bool Seen(HandCheck check)
    {
        return check == HandCheck::Hand || check == HandCheck::Checking;
    }

    constexpr bool TurnedDown(HandCheck check)
    {
        return check == HandCheck::Unsure || check == HandCheck::WrongSize || check == HandCheck::Jumped || check == HandCheck::WeakTooLong;
    }

    class HandVerifier
    {
    public:
        using Time = std::chrono::steady_clock::time_point;

        static constexpr float kKeepPresence = 0.5f;
        static constexpr float kSurePresence = 0.7f;
        static constexpr int kConfirmFrames = 3;
        static constexpr std::chrono::milliseconds kWeakTooLong{ 1000 };
        static constexpr float kMinSquare = 0.10f;
        static constexpr float kMaxSquare = 3.0f;
        static constexpr float kMaxCheckingMove = 0.25f;
        static constexpr float kMaxCheckingResize = 1.3f;
        static constexpr float kMaxMove = 1.0f;
        static constexpr float kMaxResize = 2.0f;

        HandCheck Start(const HandSighting& sighting, bool knownHand, Time time);
        HandCheck Next(const HandSighting& sighting, Time time);
        void Reset();

        bool Following() const { return m_following; }

    private:
        HandCheck Drop(HandCheck reason);

        int m_seen = 0;
        bool m_following = false;
        SquareRegion m_last;
        Time m_sureAt{};
    };
}
