#include "pch.h"
#include "PointerController.h"

#include <algorithm>
#include <cmath>
#include <ranges>

namespace seentax
{
    namespace
    {
        constexpr std::chrono::milliseconds kHistoryLength{ 500 };

        float HandSpeed(const PointerSample& sample)
        {
            return std::hypot(sample.velocity.x, sample.velocity.y) * sample.activeAreaSize;
        }

        // The last position moved on along the hand's speed, for at most kMaxPrediction.
        Point2 Predict(const PointerSample& sample, SteadyTime now)
        {
            using Controller = PointerController;
            const float fadeIn = std::clamp((HandSpeed(sample) - Controller::kPredictionStartSpeed) /
                                                (Controller::kPredictionFullSpeed - Controller::kPredictionStartSpeed),
                                            0.0f, 1.0f);
            const auto ahead = std::clamp<SteadyTime::duration>(now - sample.capturedAt, SteadyTime::duration::zero(), Controller::kMaxPrediction);
            const float seconds = std::chrono::duration<float>(ahead).count() * fadeIn;
            return { sample.position.x + sample.velocity.x * seconds, sample.position.y + sample.velocity.y * seconds };
        }

        PointerCommand Follow(const PointerSample& latest, SteadyTime now)
        {
            return latest.handPresent ? PointerCommand{ Predict(latest, now) } : PointerCommand{};
        }
    }

    PointerCommand PointerController::Update(std::optional<PointerSample> sample, SteadyTime now)
    {
        if (sample)
        {
            Remember(*sample);
            if (m_frame.Update(sample->framed && m_state == State::Following, sample->capturedAt))
            {
                return { .event = GestureEvent::Screenshot, .system = SystemAction::Screenshot };
            }
        }
        if (m_history.empty())
        {
            return {};
        }
        if (now - m_history.back().capturedAt > kSampleTimeout)
        {
            m_history.back().handPresent = false; // also lets go of a held button
            m_history.back().pinched = {};
            m_history.back().fist = false;
            m_history.back().nearlyFist = false;
        }

        const PointerSample& latest = m_history.back();
        if (FistTakesOver(latest))
        {
            return Start(Trigger::Fist, latest, now);
        }
        const auto elapsed = now - m_stateSince;

        switch (m_state)
        {
        case State::Following:
        {
            if (m_frame.Framing())
            {
                return {};
            }
            const auto trigger = StartingTrigger(latest);
            return trigger ? Start(*trigger, latest, now) : Follow(latest, now);
        }

        case State::FirstPinch:
            if (!latest.handPresent)
            {
                return Cancel(now);
            }
            if (!Closed(latest))
            {
                if (ActionFor(Gesture::DoubleTap))
                {
                    Enter(State::WaitingForSecondPinch, now);
                    return {};
                }
                return Perform(Gesture::Tap, now);
            }
            if (m_trigger != Trigger::Fist && MovedAway(latest))
            {
                return StartHold(true, latest, now);
            }
            return elapsed > MaxTapDuration() ? StartHold(false, latest, now) : PointerCommand{};

        case State::WaitingForSecondPinch:
            if (Closed(latest))
            {
                Enter(State::SecondPinch, now);
                return {};
            }
            if (!latest.handPresent || elapsed >= kDoubleTapWindow)
            {
                return Perform(Gesture::Tap, now); // the tap was complete, so it clicks even if the hand left
            }
            return {};

        case State::SecondPinch:
            if (!latest.handPresent)
            {
                return Cancel(now);
            }
            if (!Closed(latest))
            {
                return Perform(Gesture::DoubleTap, now);
            }
            return elapsed > MaxTapDuration() ? Ignore(GestureEvent::HeldTooLong, latest, now) : PointerCommand{};

        case State::Holding:
        case State::Dragging:
            return WhileHeld(latest, now);

        case State::IgnoringPinch:
            if (m_trigger == Trigger::Fist ? HandOpen(latest) : !Closed(latest))
            {
                Enter(State::Following, now);
            }
            return Follow(latest, now);

        case State::Scrolling:
            return WhileScrolling(latest, now);
        }
        return {};
    }

    bool PointerController::Active() const
    {
        return m_state != State::Following || (!m_history.empty() && m_history.back().handPresent);
    }

    std::optional<Trigger> PointerController::StartingTrigger(const PointerSample& sample) const
    {
        if (sample.fist && Uses(Trigger::Fist))
        {
            return Trigger::Fist;
        }
        std::optional<PinchFinger> closest;
        for (const auto finger : kPinchFingers)
        {
            const auto slot = SlotOf(finger);
            if (sample.pinched[slot] && Uses(TriggerOf(finger)) && (!closest || sample.pinchRatio[slot] < sample.pinchRatio[SlotOf(*closest)]))
            {
                closest = finger;
            }
        }
        if (!closest)
        {
            return std::nullopt;
        }
        return TriggerOf(*closest);
    }

    bool PointerController::Uses(Trigger trigger) const
    {
        return std::ranges::any_of(m_gestures[SlotOf(trigger)], [](const std::optional<PointerAction>& action) { return action.has_value(); });
    }

    bool PointerController::Closed(const PointerSample& sample) const
    {
        return m_trigger == Trigger::Fist ? sample.fist : sample.pinched[SlotOf(FingerOf(m_trigger))];
    }

    bool PointerController::FistTakesOver(const PointerSample& sample) const
    {
        const bool holding = m_state == State::Holding || m_state == State::Dragging || m_state == State::Scrolling;
        return !holding && m_trigger != Trigger::Fist && sample.fist && Uses(Trigger::Fist);
    }

    bool PointerController::HandOpen(const PointerSample& sample)
    {
        return !sample.fist && std::ranges::none_of(sample.pinched, [](bool pinched) { return pinched; });
    }

    std::chrono::milliseconds PointerController::MaxTapDuration() const
    {
        return m_trigger == Trigger::Fist ? kMaxFistTapDuration : kMaxTapDuration;
    }

    PointerController::State PointerController::AfterRelease() const
    {
        return m_trigger == Trigger::Fist ? State::IgnoringPinch : State::Following;
    }

    PointerCommand PointerController::Start(Trigger trigger, const PointerSample& latest, SteadyTime now)
    {
        m_trigger = trigger;
        if (trigger != Trigger::Fist && latest.nearlyFist && Uses(Trigger::Fist))
        {
            return Ignore(GestureEvent::LooksLikeFist, latest, now);
        }
        if (latest.capturedAt - m_presentSince < kSettleTime)
        {
            return Ignore(GestureEvent::TooSoon, latest, now);
        }
        if (HandSpeed(latest) > kMaxPinchStartSpeed)
        {
            return Ignore(GestureEvent::HandMoving, latest, now);
        }
        m_spot = PositionBefore(latest.capturedAt - kPinchLookBack);
        m_pinchStart = latest.position;
        Enter(State::FirstPinch, now);
        return { m_spot };
    }

    std::optional<PointerAction> PointerController::ActionFor(Gesture gesture) const
    {
        return m_gestures[SlotOf(m_trigger)][static_cast<std::size_t>(gesture)];
    }

    PointerCommand PointerController::Perform(Gesture gesture, SteadyTime now)
    {
        Enter(AfterRelease(), now);
        const auto action = ActionFor(gesture);
        return action ? Click(*action) : PointerCommand{ .event = GestureEvent::NoAction };
    }

    PointerCommand PointerController::StartHold(bool moved, const PointerSample& latest, SteadyTime now)
    {
        const auto action = ActionFor(Gesture::Hold);
        if (action == PointerAction::Scroll)
        {
            return StartScroll(latest, now);
        }
        if (action == PointerAction::PressAndHold)
        {
            Enter(moved ? State::Dragging : State::Holding, now);
            return { m_spot, ButtonAction::LeftDown, GestureEvent::Hold };
        }
        if (!action || moved)
        {
            return Ignore(moved ? GestureEvent::HandMoving : GestureEvent::NoAction, latest, now);
        }
        Enter(State::IgnoringPinch, now);
        return Click(*action);
    }

    PointerCommand PointerController::Click(PointerAction action) const
    {
        switch (action)
        {
        case PointerAction::LeftClick:
            return { m_spot, ButtonAction::LeftClick, GestureEvent::LeftClick };
        case PointerAction::RightClick:
            return { m_spot, ButtonAction::RightClick, GestureEvent::RightClick };
        case PointerAction::DoubleClick:
            return { m_spot, ButtonAction::DoubleClick, GestureEvent::DoubleClick };
        case PointerAction::CloseWindow:
            return { .event = GestureEvent::CloseWindow, .system = SystemAction::CloseWindow };
        case PointerAction::PressAndHold:
        case PointerAction::Scroll:
            break;
        }
        return { .event = GestureEvent::NoAction };
    }

    PointerCommand PointerController::WhileHeld(const PointerSample& latest, SteadyTime now)
    {
        if (HandGone(latest, now))
        {
            Enter(State::Following, now);
            return { .button = ButtonAction::LeftUp, .event = GestureEvent::LetGoHandLost };
        }
        if (!latest.handPresent)
        {
            return {};
        }
        if (m_state == State::Holding && MovedAway(latest))
        {
            Enter(State::Dragging, now);
        }
        const Point2 at = m_state == State::Dragging ? Predict(latest, now) : m_spot;
        if (!StillHeld(latest))
        {
            Enter(AfterRelease(), now);
            return { at, ButtonAction::LeftUp, GestureEvent::LetGo };
        }
        return { at };
    }

    PointerCommand PointerController::StartScroll(const PointerSample& latest, SteadyTime now)
    {
        Enter(State::Scrolling, now);
        m_scrollFrom = latest.handHeight;
        m_notchesSent = 0;
        return { .moveTo = m_spot, .event = GestureEvent::Scroll };
    }

    PointerCommand PointerController::WhileScrolling(const PointerSample& latest, SteadyTime now)
    {
        if (HandGone(latest, now))
        {
            Enter(State::Following, now);
            return { .event = GestureEvent::LetGoHandLost };
        }
        if (!latest.handPresent)
        {
            return { .moveTo = m_spot };
        }
        if (!StillHeld(latest))
        {
            Enter(AfterRelease(), now);
            return { .event = GestureEvent::LetGo };
        }
        const float wanted = (m_scrollFrom - latest.handHeight) * kScrollNotchesPerCameraHeight;
        const int step = static_cast<int>(std::trunc(wanted - static_cast<float>(m_notchesSent)));
        m_notchesSent += step;
        return { .moveTo = m_spot, .wheelNotches = step };
    }

    bool PointerController::StillHeld(const PointerSample& sample) const
    {
        return sample.pinchRatio[SlotOf(FingerOf(m_trigger))] < PinchDetector::kReleaseAbove;
    }

    bool PointerController::HandGone(const PointerSample& latest, SteadyTime now)
    {
        if (latest.handPresent)
        {
            m_handMissingSince.reset();
            return false;
        }
        if (!m_handMissingSince)
        {
            m_handMissingSince = now;
        }
        return now - *m_handMissingSince > kLostHandGrace;
    }

    // Lets the cursor follow the hand again; this pinch can no longer click.
    PointerCommand PointerController::Ignore(GestureEvent reason, const PointerSample& latest, SteadyTime now)
    {
        Enter(State::IgnoringPinch, now);
        auto command = Follow(latest, now);
        command.event = reason;
        return command;
    }

    PointerCommand PointerController::Cancel(SteadyTime now)
    {
        Enter(State::Following, now);
        return { .event = GestureEvent::HandLost };
    }

    bool PointerController::MovedAway(const PointerSample& latest) const
    {
        return std::hypot(latest.position.x - m_pinchStart.x, latest.position.y - m_pinchStart.y) * latest.activeAreaSize > kDragDistance;
    }

    void PointerController::Remember(const PointerSample& sample)
    {
        if (sample.handPresent && (m_history.empty() || !m_history.back().handPresent))
        {
            m_presentSince = sample.capturedAt;
        }
        m_history.push_back(sample);
        while (m_history.front().capturedAt < sample.capturedAt - kHistoryLength)
        {
            m_history.pop_front();
        }
    }

    void PointerController::Enter(State state, SteadyTime now)
    {
        m_handMissingSince.reset();
        m_state = state;
        m_stateSince = now;
    }

    // The newest position from before `time`, never looking past a moment the hand was gone.
    Point2 PointerController::PositionBefore(SteadyTime time) const
    {
        Point2 position = m_history.back().position;
        for (const auto& sample : m_history | std::views::reverse)
        {
            if (!sample.handPresent)
            {
                break;
            }
            position = sample.position;
            if (sample.capturedAt <= time)
            {
                break;
            }
        }
        return position;
    }
}
