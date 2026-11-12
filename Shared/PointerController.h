#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string_view>

#include "CursorMapping.h"
#include "FrameGesture.h"
#include "ImagePreprocessing.h"
#include "Pinch.h"

namespace seentax
{
    using SteadyTime = std::chrono::steady_clock::time_point;

    enum class Trigger : std::uint8_t
    {
        IndexPinch,
        MiddlePinch,
        Fist,
    };

    inline constexpr std::size_t kTriggerCount = 3;

    constexpr std::size_t SlotOf(Trigger trigger)
    {
        return static_cast<std::size_t>(trigger);
    }

    constexpr Trigger TriggerOf(PinchFinger finger)
    {
        return finger == PinchFinger::Middle ? Trigger::MiddlePinch : Trigger::IndexPinch;
    }

    constexpr PinchFinger FingerOf(Trigger trigger)
    {
        return trigger == Trigger::MiddlePinch ? PinchFinger::Middle : PinchFinger::Index;
    }

    // One camera frame's hand; positions are fractions 0..1 of the desktop.
    struct PointerSample
    {
        SteadyTime capturedAt;
        bool handPresent = false; // also false while cursor control is off
        Point2 position;          // smoothed
        Point2 velocity;          // desktop widths and heights per second
        std::array<bool, kPinchFingerCount> pinched{};
        std::array<float, kPinchFingerCount> pinchRatio{};
        bool fist = false;
        bool nearlyFist = false;
        bool framed = false;
        float handHeight = 0.0f;
        float activeAreaSize = kDefaultActiveArea.right - kDefaultActiveArea.left;
    };

    enum class ButtonAction
    {
        None,
        LeftClick,
        RightClick,
        DoubleClick,
        LeftDown,
        LeftUp,
    };

    enum class SystemAction
    {
        None,
        CloseWindow,
        Screenshot,
    };

    // What became of a pinch, for the log and the preview.
    enum class GestureEvent
    {
        None,
        LeftClick,
        RightClick,
        DoubleClick,
        CloseWindow,
        Screenshot,
        Scroll,
        Hold,
        LetGo,
        LetGoHandLost,
        HeldTooLong,
        HandMoving,
        LooksLikeFist,
        TooSoon, // right after the hand appeared
        HandLost,
        NoAction,
    };

    constexpr std::string_view Describe(GestureEvent event)
    {
        switch (event)
        {
        case GestureEvent::LeftClick:
            return "left click";
        case GestureEvent::RightClick:
            return "right click";
        case GestureEvent::DoubleClick:
            return "double click";
        case GestureEvent::CloseWindow:
            return "close window";
        case GestureEvent::Screenshot:
            return "screenshot";
        case GestureEvent::Scroll:
            return "scroll";
        case GestureEvent::Hold:
            return "press and hold";
        case GestureEvent::LetGo:
            return "let go";
        case GestureEvent::LetGoHandLost:
            return "let go: hand lost";
        case GestureEvent::HeldTooLong:
            return "no click: second tap held too long";
        case GestureEvent::HandMoving:
            return "no click: hand was moving";
        case GestureEvent::LooksLikeFist:
            return "no click: hand looks like a fist";
        case GestureEvent::TooSoon:
            return "no click: hand just appeared";
        case GestureEvent::HandLost:
            return "no click: hand lost";
        case GestureEvent::NoAction:
            return "no action for this pose";
        case GestureEvent::None:
            break;
        }
        return "";
    }

    constexpr bool IsClick(GestureEvent event)
    {
        return event == GestureEvent::LeftClick || event == GestureEvent::RightClick || event == GestureEvent::DoubleClick;
    }

    enum class Gesture : std::uint8_t
    {
        Tap,
        DoubleTap,
        Hold,
    };

    inline constexpr std::size_t kGestureCount = 3;

    enum class PointerAction : std::uint8_t
    {
        LeftClick,
        RightClick,
        DoubleClick,
        PressAndHold,
        CloseWindow,
        Scroll,
    };

    inline constexpr std::size_t kPointerActionCount = 6;

    using TriggerGestures = std::array<std::optional<PointerAction>, kGestureCount>;
    using GestureMap = std::array<TriggerGestures, kTriggerCount>;

    // What to do with the real cursor now: move first, then the button.
    struct PointerCommand
    {
        std::optional<Point2> moveTo;
        ButtonAction button = ButtonAction::None;
        GestureEvent event = GestureEvent::None;
        SystemAction system = SystemAction::None;
        int wheelNotches = 0;
    };

    // Moves the cursor once per display refresh and turns pinches and the fist into the actions bound to them.
    // The cursor stays frozen from the start of a pinch until the gesture is decided. When a double tap is
    // bound, a tap waits out the double-tap window, so a double tap doesn't also tap.
    class PointerController
    {
    public:
        static constexpr std::chrono::milliseconds kMaxTapDuration{ 600 }; // a first pinch held longer holds the button
        static constexpr std::chrono::milliseconds kDoubleTapWindow{ 300 }; // from letting go to the second pinch
        static constexpr std::chrono::milliseconds kPinchLookBack{ 100 };   // clicks land where the hand was just before the pinch
        static constexpr std::chrono::milliseconds kSettleTime{ 300 };      // the first frames of a hand are the least reliable
        static constexpr float kMaxPinchStartSpeed = 0.35f;                 // camera picture widths per second
        static constexpr float kDragDistance = 0.014f;                      // well above how far the knuckles shift in a pinch
        static constexpr std::chrono::milliseconds kSampleTimeout{ 500 };   // older than this, the camera stopped: no hand
        static constexpr std::chrono::milliseconds kMaxPrediction{ 40 };
        // Prediction fades in between these speeds; below them the speed is mostly tracking noise.
        static constexpr float kPredictionStartSpeed = 0.21f;
        static constexpr float kPredictionFullSpeed = 0.7f;
        static constexpr std::chrono::milliseconds kMaxFistTapDuration{ 500 };
        static constexpr float kScrollNotchesPerCameraHeight = 40.0f;
        static constexpr std::chrono::milliseconds kLostHandGrace{ 200 };

        void SetGestures(const GestureMap& gestures) { m_gestures = gestures; }

        // Call once per display refresh, with the newest camera sample if one arrived since.
        PointerCommand Update(std::optional<PointerSample> sample, SteadyTime now);

        // A hand in view or a gesture in progress: there's work on every refresh.
        bool Active() const;

    private:
        enum class State
        {
            Following,
            FirstPinch,            // frozen; a tap if let go in time
            WaitingForSecondPinch, // frozen; a tap unless a second pinch follows
            SecondPinch,           // frozen; a double tap if let go in time
            Holding,               // left button down, frozen until the hand moves
            Dragging,              // left button down, following
            IgnoringPinch,         // following; this pinch can't click
            Scrolling,
        };

        std::optional<Trigger> StartingTrigger(const PointerSample& sample) const;
        bool Uses(Trigger trigger) const;
        bool Closed(const PointerSample& sample) const;
        bool FistTakesOver(const PointerSample& sample) const;
        static bool HandOpen(const PointerSample& sample);
        std::chrono::milliseconds MaxTapDuration() const;
        State AfterRelease() const;
        PointerCommand Start(Trigger trigger, const PointerSample& latest, SteadyTime now);
        std::optional<PointerAction> ActionFor(Gesture gesture) const;
        PointerCommand Perform(Gesture gesture, SteadyTime now);
        PointerCommand StartHold(bool moved, const PointerSample& latest, SteadyTime now);
        PointerCommand Click(PointerAction action) const;
        PointerCommand WhileHeld(const PointerSample& latest, SteadyTime now);
        PointerCommand StartScroll(const PointerSample& latest, SteadyTime now);
        PointerCommand WhileScrolling(const PointerSample& latest, SteadyTime now);
        bool StillHeld(const PointerSample& sample) const;
        bool HandGone(const PointerSample& latest, SteadyTime now);
        PointerCommand Ignore(GestureEvent reason, const PointerSample& latest, SteadyTime now);
        PointerCommand Cancel(SteadyTime now);
        bool MovedAway(const PointerSample& latest) const;
        void Remember(const PointerSample& sample);
        void Enter(State state, SteadyTime now);
        Point2 PositionBefore(SteadyTime time) const;

        std::deque<PointerSample> m_history; // the last half second
        SteadyTime m_presentSince{};
        State m_state = State::Following;
        SteadyTime m_stateSince{};
        GestureMap m_gestures{};
        Trigger m_trigger = Trigger::IndexPinch;
        Point2 m_spot;       // where the gesture clicks or presses
        Point2 m_pinchStart; // the hand when the pinch began
        float m_scrollFrom = 0.0f;
        int m_notchesSent = 0;
        std::optional<SteadyTime> m_handMissingSince;
        FrameDetector m_frame;
    };
}
