#include "GestureLog.h"

#include <algorithm>
#include <format>
#include <string>

#include "Log.h"

namespace seentax
{
    namespace
    {
        constexpr float kNearPinch = PinchDetector::kPressBelow + 0.05f;

        std::string NearMissReason(PinchBlocker blocker)
        {
            if (blocker != PinchBlocker::None)
            {
                return std::string(Describe(blocker));
            }
            return std::format("Value is above {:.2f}", PinchDetector::kPressBelow);
        }
    }

    void GestureLog::Pinch(PinchFinger finger, const PinchDetector& detector, Clock::time_point at)
    {
        NotePinch(finger, detector, at);
        NoteNearMiss(finger, detector);
    }

    void GestureLog::NotePinch(PinchFinger finger, const PinchDetector& detector, Clock::time_point at)
    {
        auto& pinch = m_fingers[SlotOf(finger)];
        const float ratio = detector.Ratio();
        if (detector.Pinched() && !pinch.inProgress)
        {
            pinch.inProgress = PinchInProgress{ at, ratio };
            Log("         {} pinch started (pinch value {:.2f})", Describe(finger), ratio);
        }
        else if (detector.Pinched())
        {
            pinch.inProgress->lowestRatio = std::min(pinch.inProgress->lowestRatio, ratio);
        }
        else
        {
            EndPinch(finger, at, "let go");
        }
    }

    void GestureLog::NoteNearMiss(PinchFinger finger, const PinchDetector& detector)
    {
        auto& pinch = m_fingers[SlotOf(finger)];
        const float ratio = detector.Ratio();
        const PinchBlocker blocker = detector.Blocker();
        if (detector.Pinched())
        {
            pinch.nearMiss.reset();
            pinch.nearMissArmed = false;
            return;
        }
        if (ratio >= kNearPinch)
        {
            if (pinch.nearMiss)
            {
                Log("         {}: fingers closed (lowest pinch value {:.2f}) no pinch detected: {}", Describe(finger), pinch.nearMiss->lowestRatio,
                    NearMissReason(pinch.nearMiss->blocker));
                pinch.nearMiss.reset();
            }
            pinch.nearMissArmed = true;
            return;
        }
        if (!pinch.nearMissArmed)
        {
            return;
        }
        if (!pinch.nearMiss)
        {
            pinch.nearMiss = NearMiss{ ratio, blocker };
        }
        pinch.nearMiss->lowestRatio = std::min(pinch.nearMiss->lowestRatio, ratio);
        if (blocker != PinchBlocker::None)
        {
            pinch.nearMiss->blocker = blocker;
        }
    }

    void GestureLog::Fist(const FistDetector& detector, Clock::time_point at)
    {
        const float ratio = detector.Ratio();
        if (detector.Closed() && !m_fistClosedAt)
        {
            m_fistClosedAt = at;
            m_lowestFistRatio = ratio;
            Log("         fist closed (fist value {:.2f})", ratio);
        }
        else if (detector.Closed())
        {
            m_lowestFistRatio = std::min(m_lowestFistRatio, ratio);
        }
        else
        {
            EndFist(at, "opened");
        }
    }

    void GestureLog::Frame(bool framed, Clock::time_point at)
    {
        if (framed && !m_framedAt)
        {
            m_framedAt = at;
            Log("         frame: both hands in place");
        }
        else if (!framed)
        {
            EndFrame(at, "let go");
        }
    }

    void GestureLog::End(Clock::time_point at, std::string_view why)
    {
        for (const auto finger : kPinchFingers)
        {
            EndPinch(finger, at, why);
            auto& pinch = m_fingers[SlotOf(finger)];
            pinch.nearMiss.reset();
            pinch.nearMissArmed = true;
        }
        EndFist(at, why);
        EndFrame(at, why);
    }

    void GestureLog::EndPinch(PinchFinger finger, Clock::time_point at, std::string_view how)
    {
        auto& pinch = m_fingers[SlotOf(finger)];
        if (pinch.inProgress)
        {
            Log("         {} pinch ended ({}) after {:.0f} ms, lowest pinch value {:.2f}", Describe(finger), how,
                MillisecondsBetween(pinch.inProgress->started, at), pinch.inProgress->lowestRatio);
            pinch.inProgress.reset();
        }
    }

    void GestureLog::EndFist(Clock::time_point at, std::string_view how)
    {
        if (m_fistClosedAt)
        {
            Log("         fist opened ({}) after {:.0f} ms, lowest fist value {:.2f}", how, MillisecondsBetween(*m_fistClosedAt, at), m_lowestFistRatio);
            m_fistClosedAt.reset();
        }
    }

    void GestureLog::EndFrame(Clock::time_point at, std::string_view how)
    {
        if (m_framedAt)
        {
            Log("         frame ended ({}) after {:.0f} ms", how, MillisecondsBetween(*m_framedAt, at));
            m_framedAt.reset();
        }
    }
}
