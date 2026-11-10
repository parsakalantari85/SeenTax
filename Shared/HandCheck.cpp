#include "pch.h"
#include "HandCheck.h"

#include <cmath>

namespace seentax
{
    namespace
    {
        bool SizeOk(const HandSighting& sighting)
        {
            if (!(sighting.pictureHeight > 0.0f))
            {
                return false;
            }
            const float square = sighting.region.size / sighting.pictureHeight;
            return square >= HandVerifier::kMinSquare && square <= HandVerifier::kMaxSquare;
        }

        bool Steady(const SquareRegion& from, const SquareRegion& to, float maxMove, float maxResize)
        {
            if (!(from.size > 0.0f) || !(to.size > 0.0f))
            {
                return false;
            }
            const float moved = std::hypot(to.centerX - from.centerX, to.centerY - from.centerY);
            const float resize = to.size > from.size ? to.size / from.size : from.size / to.size;
            return moved <= maxMove * from.size && resize <= maxResize;
        }
    }

    HandCheck HandVerifier::Start(const HandSighting& sighting, bool knownHand, Time time)
    {
        Reset();
        if (sighting.presence < kSurePresence)
        {
            return HandCheck::Unsure;
        }
        if (!SizeOk(sighting))
        {
            return HandCheck::WrongSize;
        }
        m_seen = 1;
        m_following = knownHand || kConfirmFrames <= 1;
        m_last = sighting.region;
        m_sureAt = time;
        return m_following ? HandCheck::Hand : HandCheck::Checking;
    }

    HandCheck HandVerifier::Next(const HandSighting& sighting, Time time)
    {
        if (m_seen == 0 || sighting.presence < kKeepPresence)
        {
            return Drop(HandCheck::NoHand);
        }
        if (!SizeOk(sighting))
        {
            return Drop(HandCheck::WrongSize);
        }
        if (!Steady(m_last, sighting.region, m_following ? kMaxMove : kMaxCheckingMove, m_following ? kMaxResize : kMaxCheckingResize))
        {
            return Drop(HandCheck::Jumped);
        }
        m_last = sighting.region;
        if (sighting.presence >= kSurePresence)
        {
            m_sureAt = time;
        }
        if (m_following)
        {
            return time - m_sureAt > kWeakTooLong ? Drop(HandCheck::WeakTooLong) : HandCheck::Hand;
        }
        if (sighting.presence < kSurePresence)
        {
            return Drop(HandCheck::Unsure);
        }
        if (++m_seen < kConfirmFrames)
        {
            return HandCheck::Checking;
        }
        m_following = true;
        return HandCheck::Hand;
    }

    void HandVerifier::Reset()
    {
        *this = HandVerifier{};
    }

    HandCheck HandVerifier::Drop(HandCheck reason)
    {
        Reset();
        return reason;
    }
}
